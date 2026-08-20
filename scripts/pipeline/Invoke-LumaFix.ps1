#Requires -Version 7.0

<#
.SYNOPSIS
    Run the Luma mutating fix pipeline via an agent CLI (Copilot or Claude),
    gated on build + test.

.DESCRIPTION
    Executes each fixer prompt (bug fixes, refactor, optimize, cleanup,
    iterative improvement, release verification, learnings capture) in the
    agent's mode. Every mutating phase is protected:

      * a clean working tree is required before starting (unless -AllowDirty);
      * a green build + test baseline is established first (unless -SkipBaseline);
      * work happens on a dedicated pipeline/fix-<timestamp> branch so your
        current branch is untouched (unless -NoBranch);
      * after each phase the build + test gate runs, then formatters are applied
        and the lint checks (including clang-tidy) must pass, and only a green,
        lint-clean tree is committed (unless -NoCommit / -SkipLintFormat);
      * the run stops at the first red gate (unless -ContinueOnFailure).

    Feed this the reports produced by Invoke-LumaAudit.ps1 (auto-detected from
    the newest audit-* run, or pass -ReportDir explicitly).

.PARAMETER Phase
    One or more phase filters (Id or Name substring, case-insensitive).
    Omit to run every fix phase in order.

.PARAMETER ReportDir
    Directory holding the audit reports (the reports/ folder of an audit run).
    Defaults to the newest <ArtifactRoot>/audit-*/reports directory.

.PARAMETER ArtifactRoot
    Root directory for run artifacts. Defaults to <repo>/pipeline-artifacts.

.PARAMETER Preset
    CMake preset used for the build + test gate. Defaults to 'default'.

.PARAMETER ConvergenceMaxPasses
    Maximum passes for the iterative-improvement phase. Defaults to 3.

.PARAMETER Agent
    Agent CLI backend: 'copilot' (default) or 'claude'.

.PARAMETER Model
    Optional model for the chosen agent (e.g. 'gpt-5.4' for copilot, 'sonnet'
    for claude). Omit to let the agent choose.

.PARAMETER Effort
    Optional reasoning effort. For the copilot agent this is validated against
    low|medium|high|xhigh|max; for claude the value is passed
    through and validated by the CLI itself.

.PARAMETER AllowDirty
    Do not require a clean working tree before starting.

.PARAMETER SkipBaseline
    Skip the initial green build + test baseline check.

.PARAMETER SkipBuild
    Skip the build step of every gate (test only).

.PARAMETER SkipTest
    Skip the test step of every gate (build only).

.PARAMETER SkipLintFormat
    Skip the lint/format gate (scripts/format.py apply + scripts/lint.py verify)
    at the baseline and after every phase. By default the gate runs the full
    lint checks, including clang-tidy; pass this to opt out entirely.

.PARAMETER NoBranch
    Do not create a dedicated branch; run on the current branch.

.PARAMETER NoCommit
    Do not commit after each green phase.

.PARAMETER ContinueOnFailure
    Keep going after a red gate instead of stopping.

.PARAMETER RevertOnFailure
    On a red gate, hard-reset the working tree to the last checkpoint before
    continuing or stopping. This discards tracked changes only; untracked files
    a failed phase created are left in place (run git clean yourself to remove
    them). With -NoCommit there are no per-phase checkpoints, so the reset falls
    back to the branch's starting point and discards every phase's work so far.

.PARAMETER List
    List the fix phases in order and exit without running anything.

.PARAMETER DryRun
    Print every agent / cmake / ctest / git command without executing it.

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaFix.ps1 -List

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaFix.ps1 -DryRun

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaFix.ps1 -Phase bug-fix
#>

[CmdletBinding()]
param(
    [string[]]$Phase,

    [string]$ReportDir,

    [string]$ArtifactRoot,

    [string]$Preset = 'default',

    [ValidateRange(1, 10)]
    [int]$ConvergenceMaxPasses = 3,

    [ValidateSet('copilot', 'claude')]
    [string]$Agent = 'copilot',

    [string]$Model,

    [string]$Effort,

    [switch]$AllowDirty,

    [switch]$SkipBaseline,

    [switch]$SkipBuild,

    [switch]$SkipTest,

    [switch]$SkipLintFormat,

    [switch]$NoBranch,

    [switch]$NoCommit,

    [switch]$ContinueOnFailure,

    [switch]$RevertOnFailure,

    [switch]$List,

    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Report native command failures via $LASTEXITCODE, not thrown errors, regardless
# of a user profile that flips this on (see the note in LumaPipeline.psm1).
$PSNativeCommandUseErrorActionPreference = $false

Import-Module -Name (Join-Path -Path $PSScriptRoot -ChildPath 'LumaPipeline.psm1') -Force

$RepoRoot = Get-LumaRepoRoot
$AllPhases = Get-FixPhase

# Validate -Effort against Copilot's documented set only. Claude accepts a
# different set (low..max plus ultracode), so for claude we defer to the CLI's
# own validation rather than maintain a second allow-list.
$CopilotEffortValues = @('low', 'medium', 'high', 'xhigh', 'max')
if ($Effort -and $Agent -eq 'copilot' -and $Effort -notin $CopilotEffortValues) {
    throw "Invalid -Effort '$Effort' for copilot (use $($CopilotEffortValues -join ', '))."
}

if ($List) {
    $Index = 0
    $AllPhases | ForEach-Object {
        $Index++
        [pscustomobject]@{
            Order       = $Index
            Id          = $_.Id
            Name        = $_.Name
            Prompt      = $_.Prompt
            Gate        = $_.Gate
            Commit      = $_.Commit
            SelfVerifies = $_.SelfVerifies
        }
    }
    return
}

# Select the phases to run.
if ($Phase) {
    $Phase = @($Phase | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $Selected = [System.Collections.Generic.List[psobject]]::new()
    foreach ($Item in $AllPhases) {
        foreach ($Filter in $Phase) {
            # Case-insensitive literal-substring match over "<id> <name>",
            # matching the shell runner exactly (it tests the same concatenated,
            # lower-cased haystack). Using -like here would let wildcard
            # metacharacters in a filter select phases differently.
            if ("$($Item.Id) $($Item.Name)".Contains($Filter, [System.StringComparison]::OrdinalIgnoreCase)) {
                $Selected.Add($Item)
                break
            }
        }
    }
    if ($Selected.Count -eq 0) {
        throw "No fix phase matched: $($Phase -join ', '). Use -List to see the available phases."
    }
}
else {
    $Selected = $AllPhases
}

# Fall back to 'auto' when the CLI cannot select the requested model, so a
# mis-entitled or unknown model name never aborts every phase.
$Model = Resolve-CopilotModel -Agent $Agent -Model $Model -DryRun:$DryRun -RepoRoot $RepoRoot

# Resolve the artifact root and audit reports directory.
if (-not $ArtifactRoot) {
    $ArtifactRoot = Join-Path -Path $RepoRoot -ChildPath 'pipeline-artifacts'
}
# Make the artifact root absolute so it can be reliably excluded from commits
# regardless of the caller's working directory.
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot, (Get-Location).ProviderPath)

if (-not $ReportDir) {
    if (Test-Path -LiteralPath $ArtifactRoot) {
        $LatestAudit = Get-ChildItem -LiteralPath $ArtifactRoot -Directory -Filter 'audit-*' -ErrorAction SilentlyContinue |
            Sort-Object -Property Name -Descending |
            Select-Object -First 1
        if ($LatestAudit) {
            $Candidate = Join-Path -Path $LatestAudit.FullName -ChildPath 'reports'
            if (Test-Path -LiteralPath $Candidate) {
                $ReportDir = $Candidate
            }
        }
    }
}

if ($ReportDir) {
    Write-Host "Audit reports: $ReportDir"
}
else {
    Write-Host 'Audit reports: none found - phases will self-discover findings.' -ForegroundColor Yellow
}

$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunDir = Join-Path -Path $ArtifactRoot -ChildPath "fix-$Timestamp"
$LogDir = Join-Path -Path $RunDir -ChildPath 'logs'
if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
}

Write-PhaseBanner -Title 'Luma fix pipeline (mutating, gated)' -Subtitle "Repo: $RepoRoot"
Write-Host "  Phases  : $($Selected.Count)"
Write-Host "  Preset  : $Preset"
Write-Host "  Agent   : $Agent"
Write-Host "  Output  : $RunDir"
if ($DryRun) { Write-Host '  Mode    : DRY RUN (nothing invoked, built, or committed)' -ForegroundColor Yellow }

# --- Safety: clean tree, dedicated branch, green baseline ---------------------

if (-not $AllowDirty) {
    if (-not (Test-CleanWorkingTree -RepoRoot $RepoRoot -DryRun:$DryRun)) {
        throw 'Working tree is not clean. Commit or stash your changes, or pass -AllowDirty.'
    }
}

$BranchName = "pipeline/fix-$Timestamp"
if (-not $NoBranch) {
    Write-Host "Creating branch: $BranchName"
    if ($DryRun) {
        Write-Host "  DRY RUN > git -C `"$RepoRoot`" checkout -b $BranchName" -ForegroundColor DarkGray
    }
    else {
        & git -C $RepoRoot checkout -b $BranchName
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to create branch '$BranchName'."
        }
    }
}

if (-not $SkipBaseline) {
    Write-PhaseBanner -Title 'Baseline gate' -Subtitle 'Verifying a green build + test before making changes'
    $BaselineGreen = Test-BuildAndTest -RepoRoot $RepoRoot -Preset $Preset `
        -SkipBuild:$SkipBuild -SkipTest:$SkipTest -DryRun:$DryRun
    if (-not $BaselineGreen -and -not $DryRun) {
        throw 'Baseline build + test is not green. Fix the baseline first, or pass -SkipBaseline.'
    }
    if (-not $SkipLintFormat) {
        # Check-only: verify the starting tree is already lint-clean without
        # mutating it, so a per-phase lint failure is attributable to that phase
        # rather than to pre-existing drift.
        $BaselineClean = Test-LintAndFormat -RepoRoot $RepoRoot -CheckOnly -DryRun:$DryRun
        if (-not $BaselineClean -and -not $DryRun) {
            throw 'Baseline lint is not clean. Run "python scripts/format.py" and fix any remaining lint issues, or pass -SkipLintFormat.'
        }
    }
}

# --- Run phases ---------------------------------------------------------------

$Results = [System.Collections.Generic.List[psobject]]::new()
$Position = 0
$Aborted = $false

foreach ($Current in $Selected) {
    $Position++
    Write-PhaseBanner -Title "[$Position/$($Selected.Count)] $($Current.Name)" -Subtitle "Prompt: $($Current.Prompt)"

    # Resolve the input report, if the audit produced one.
    $ReportClause = $null
    if ($ReportDir -and $Current.InputReport) {
        $ReportPath = Join-Path -Path $ReportDir -ChildPath $Current.InputReport
        if (Test-Path -LiteralPath $ReportPath) {
            $ReportClause = $ReportPath
        }
    }

    $Lines = [System.Collections.Generic.List[string]]::new()
    $Lines.Add("You are running the Luma project fix phase: `"$($Current.Name)`".")
    $Lines.Add('')
    if ($ReportClause) {
        $Lines.Add("A prioritised audit report for this phase is available at:")
        $Lines.Add("    $ReportClause")
        $Lines.Add('Read it first and work through its findings in priority order (highest severity times confidence first). Skip any finding you cannot confirm against the current source.')
        $Lines.Add('')
    }
    $Lines.Add("Follow the workflow in the prompt file `".github/prompts/$($Current.Prompt)`" exactly - its ground rules, procedure, verification, and Output Format sections all apply.")
    $Lines.Add('')
    $Lines.Add('Obey every relevant guide under instructions/ for the languages you touch. Make the smallest correct change for each item, add or update the matching regression test, and keep the full test suite green.')
    # Instruct the agent to fix lint findings (incl. clang-tidy) whenever this
    # phase will be lint-gated below, so the deterministic gate only has to
    # verify. Mirrors the gate condition: gated, not self-verifying, not skipped.
    if ($Current.Gate -and -not $Current.SelfVerifies -and -not $SkipLintFormat) {
        $Lines.Add('')
        $Lines.Add('This phase is gated on a clean lint result. After your primary changes, run `python scripts/format.py` to apply formatting and the safe auto-fixes, then run `python scripts/lint.py` and fix every issue it reports - including clang-tidy, shellcheck, cmakelint, tsc, and clippy findings - repeating until it passes. Correct the underlying code rather than suppressing the warnings. The pipeline re-runs format.py then lint.py as a gate and will not commit this phase unless both pass.')
    }
    if ($Current.Id -like '*iterative-improvement*') {
        $Lines.Add('')
        $Lines.Add("Perform at most $ConvergenceMaxPasses full improvement passes. After the final pass, stop and summarise any issues that remain.")
    }
    $Lines.Add('')
    $Lines.Add('Do not push to any remote. Do not amend, rebase, or rewrite existing commits. Leave staging to the pipeline unless the prompt says otherwise.')
    $Instruction = $Lines -join "`n"

    # The release-verification prompt wipes the workspace with `git clean -Xdf`,
    # which would delete the git-ignored pipeline-artifacts/ directory holding
    # this run's logs. Protect it deterministically: stream this phase's
    # transcript to a temp dir *outside* the artifact tree (on Windows a directory
    # cannot be moved while a file inside it is open, which would otherwise defeat
    # the set-aside), and move the artifact root into .git/ for the phase's
    # duration. The set-aside no-ops for a dry run or an out-of-tree artifact root.
    $ProtectClean = -not $DryRun -and ($Current.Id -like '*release-verification*')
    if ($ProtectClean) {
        $PhaseLogDir = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath "luma-relverify-$Timestamp"
        New-Item -ItemType Directory -Force -Path $PhaseLogDir | Out-Null
    }
    else {
        $PhaseLogDir = $LogDir
    }
    $PhaseLog = Join-Path -Path $PhaseLogDir -ChildPath "$($Current.Id).log"

    # A thrown error here (missing agent/cmake/ctest/git executable) must not
    # unwind the whole run: catch it, record the phase as errored, and fall
    # through to the failure handling below so -RevertOnFailure / -ContinueOnFailure
    # and the summary still apply. This mirrors the guarded phase call in
    # Invoke-LumaAudit.ps1. A rejected checkpoint commit is handled more
    # specifically as commit-failed at the checkpoint call below.
    $CommitSha = $null
    $Status = 'ok'
    $ArtifactBackup = $null
    try {
        if ($ProtectClean) {
            $ArtifactBackup = Save-PipelineArtifact -ArtifactRoot $ArtifactRoot -RepoRoot $RepoRoot
        }

        $Outcome = Invoke-AgentPhase -Instruction $Instruction -Mode 'agent' -Agent $Agent -RepoRoot $RepoRoot `
            -LogFile $PhaseLog -LogDir $PhaseLogDir -Model $Model -Effort $Effort -DryRun:$DryRun

        if (-not $Outcome.Success) {
            Write-Warning "Agent reported a non-zero exit for '$($Current.Id)'."
            $Status = 'agent-failed'
        }

        # Build + test gate (unless the prompt self-verifies or the phase opts out).
        if ($Status -eq 'ok' -and $Current.Gate -and -not $Current.SelfVerifies) {
            $Green = Test-BuildAndTest -RepoRoot $RepoRoot -Preset $Preset `
                -SkipBuild:$SkipBuild -SkipTest:$SkipTest -DryRun:$DryRun
            if (-not $Green -and -not $DryRun) {
                Write-Warning "Gate failed after '$($Current.Id)'."
                $Status = 'gate-failed'
            }

            # Lint/format gate: apply formatters, then verify lint (incl.
            # clang-tidy). Runs after build + test so clang-tidy sees the freshest
            # compile database and a broken build fails fast first. Any formatting
            # changes fold into this phase's commit below.
            if ($Status -eq 'ok' -and -not $SkipLintFormat) {
                $LintClean = Test-LintAndFormat -RepoRoot $RepoRoot -DryRun:$DryRun
                if (-not $LintClean -and -not $DryRun) {
                    Write-Warning "Lint/format gate failed after '$($Current.Id)'."
                    $Status = 'gate-failed'
                }
            }
        }

        # Commit a green phase.
        if ($Status -eq 'ok' -and $Current.Commit -and -not $NoCommit) {
            try {
                $CommitSha = Invoke-GitCheckpoint -RepoRoot $RepoRoot `
                    -Subject "chore(pipeline): $($Current.Id)" -Agent $Agent `
                    -ArtifactRoot $ArtifactRoot -DryRun:$DryRun
            }
            catch {
                # A rejected commit (e.g. the pre-commit hook) must not look like
                # success. Mark it commit-failed so the revert/stop path runs,
                # instead of re-staging the un-committable change into every later
                # checkpoint (git add -A) and piling up uncommitted edits.
                Write-Warning "Checkpoint commit failed for '$($Current.Id)': $($_.Exception.Message)"
                $Status = 'commit-failed'
                $CommitSha = $null
            }
        }
    }
    catch {
        Write-Warning "Phase '$($Current.Id)' errored: $($_.Exception.Message)"
        $Status = 'error'
    }
    finally {
        if ($ProtectClean) {
            # Put the artifact root back, then fold this phase's temp transcript(s)
            # into the run's log dir so SUMMARY.md links them exactly as for any
            # other phase. Runs even on an error/abort so nothing is stranded.
            Restore-PipelineArtifact -BackupPath $ArtifactBackup -ArtifactRoot $ArtifactRoot
            if (-not (Test-Path -LiteralPath $LogDir)) {
                New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
            }
            if (Test-Path -LiteralPath $PhaseLogDir) {
                Get-ChildItem -LiteralPath $PhaseLogDir -Force | ForEach-Object {
                    $Dest = Join-Path -Path $LogDir -ChildPath $_.Name
                    if (Test-Path -LiteralPath $Dest) {
                        Remove-Item -LiteralPath $Dest -Recurse -Force -ErrorAction SilentlyContinue
                    }
                    Move-Item -LiteralPath $_.FullName -Destination $Dest
                }
                Remove-Item -LiteralPath $PhaseLogDir -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }

    # Handle failure: optional revert, optional stop.
    if ($Status -ne 'ok') {
        if ($RevertOnFailure) {
            Write-Host 'Reverting working tree to last checkpoint...' -ForegroundColor Yellow
            if ($DryRun) {
                Write-Host "  DRY RUN > git -C `"$RepoRoot`" reset --hard HEAD" -ForegroundColor DarkGray
            }
            else {
                # Best-effort: warn on a failed reset rather than aborting, so
                # the summary and any remaining phases still run. Mirrors the
                # `|| luma_warn` on the shell revert.
                & git -C $RepoRoot reset --hard HEAD
                if ($LASTEXITCODE -ne 0) { Write-Warning 'Revert failed.' }
            }
        }
        if (Test-ShouldAbort -Status $Status -ContinueOnFailure:$ContinueOnFailure -RevertOnFailure:$RevertOnFailure) {
            $Aborted = $true
        }
    }

    $Results.Add([pscustomobject]@{
            Order  = $Position
            Id     = $Current.Id
            Name   = $Current.Name
            Status = $Status
            Commit = if ($CommitSha) { $CommitSha } else { '-' }
        })

    if ($Aborted) {
        if ($Status -eq 'commit-failed' -and $ContinueOnFailure) {
            Write-Warning "Stopping after '$($Current.Id)': the commit was rejected and cannot be skipped (git add -A would re-stage it). Fix the blocker (e.g. the pre-commit hook), or pass -RevertOnFailure to discard failed phases and keep going."
        }
        else {
            Write-Warning "Stopping after '$($Current.Id)' (pass -ContinueOnFailure to keep going)."
        }
        break
    }
}

# --- Summary ------------------------------------------------------------------

# Persist a run summary (mirrors the audit runner's INDEX.md) so a completed run
# is auditable from disk even when the console scrollback is lost. A phase whose
# Commit is '-' recorded no checkpoint of its own; if the tree was dirty that
# work can land in a later phase's commit, so a '-' next to an ok status is worth
# a look.
if (-not $DryRun) {
    $SummaryLines = [System.Collections.Generic.List[string]]::new()
    $SummaryLines.Add('# Luma fix run')
    $SummaryLines.Add('')
    $SummaryLines.Add("- Generated: $Timestamp")
    $SummaryLines.Add("- Repository: $RepoRoot")
    if (-not $NoBranch) {
        $SummaryLines.Add("- Branch: $BranchName")
    }
    $SummaryLines.Add('')
    $SummaryLines.Add('| Order | Id | Phase | Status | Commit | Log |')
    $SummaryLines.Add('| ----- | -- | ----- | ------ | ------ | --- |')
    foreach ($Row in $Results) {
        # Link the phase's transcript when it exists so the summary doubles as a
        # navigation index; a phase that failed before the agent ran (e.g. a
        # missing CLI) records no log, so guard the link on the file.
        $LogName = "$($Row.Id).log"
        $LogCell = if (Test-Path -LiteralPath (Join-Path -Path $LogDir -ChildPath $LogName)) { "[$LogName](logs/$LogName)" } else { '-' }
        $SummaryLines.Add("| $($Row.Order) | $($Row.Id) | $($Row.Name) | $($Row.Status) | $($Row.Commit) | $LogCell |")
    }
    $SummaryLines.Add('')
    if ($Aborted) {
        $SummaryLines.Add('The pipeline stopped early on a failed phase.')
    }
    else {
        $SummaryLines.Add('All selected phases completed.')
    }
    $SummaryLines.Add('')
    $SummaryLines.Add('A `-` in the Commit column means the phase created no commit of its own; a `-` in the Log column means no transcript was captured.')
    $SummaryPath = Join-Path -Path $RunDir -ChildPath 'SUMMARY.md'
    Set-Content -LiteralPath $SummaryPath -Value ($SummaryLines -join "`n") -Encoding utf8
    Write-Host ''
    Write-Host "Summary written: $SummaryPath" -ForegroundColor Green
}

Write-PhaseBanner -Title 'Fix summary'
if (-not $NoBranch -and -not $DryRun) {
    Write-Host "Branch: $BranchName"
}
$Results | Format-Table -AutoSize Order, Id, Status, Commit

Write-Output $Results

# Exit non-zero when the pipeline aborted or any phase failed - including a
# rejected checkpoint commit (commit-failed) - so Invoke-LumaAll.ps1 and CI see
# the failure instead of reading an aborted run as a clean pass.
$RunFailures = @($Results | Where-Object { $_.Status -ne 'ok' }).Count
if ($Aborted) {
    Write-Host 'Pipeline stopped early on a failed phase.' -ForegroundColor Yellow
    exit 1
}
if ($RunFailures -gt 0) {
    Write-Host "Completed with $RunFailures failed phase(s)." -ForegroundColor Yellow
    exit 1
}
Write-Host 'All selected phases completed.' -ForegroundColor Green
exit 0
