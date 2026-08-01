#Requires -Version 7.0

<#
.SYNOPSIS
    Run the Luma mutating language-evolution pipeline via an agent CLI (Copilot
    or Claude), gated on build + test.

.DESCRIPTION
    Implements new language capabilities end to end. Each candidate is a
    (kind, goal) pair; the kind routes to the implementer prompt that builds it:

        function -> new-stdlib-function.prompt.md
        type     -> new-stdlib-type.prompt.md
        module   -> new-stdlib-module.prompt.md
        feature  -> new-language-feature.prompt.md

    Candidates come from -Goal + -Kind (a single candidate) or from a -GoalsFile
    queue (one 'kind|goal' line per candidate). Routing is deterministic - the
    kind selects the prompt - so a candidate is never misrouted by parsing agent
    output. This is the evolve half of the discover -> evolve pipeline; triage
    Invoke-LumaDiscover.ps1's report, then feed chosen candidates here.

    This is a mutating, gated pass built on the same safety model as
    Invoke-LumaFix.ps1, and it reuses that runner's shared helpers
    (LumaPipeline.psm1):

      * a clean working tree is required before starting (unless -AllowDirty);
      * work happens on a dedicated pipeline/evolve-<timestamp> branch so your
        current branch is untouched (unless -NoBranch);
      * a green build + test baseline is established first (unless -SkipBaseline);
      * after each candidate the build + test gate runs, then formatters are
        applied and the lint checks (including clang-tidy) must pass, and only a
        green, lint-clean tree is committed (unless -NoCommit / -SkipLintFormat);
      * the run stops at the first red gate (unless -ContinueOnFailure).

    Nothing is pushed: `git push` is denied to the agent and the runner never
    merges or pushes the branch. Reviewing and merging the branch is left to you.

.PARAMETER Goal
    The candidate to implement, in prose (e.g. 'Add String.center for padding a
    string to a width'). Requires -Kind. Mutually exclusive with -GoalsFile.

.PARAMETER Kind
    The kind of the -Goal candidate: function, type, module, or feature.
    Use -ListKinds to see the kind -> prompt routing.

.PARAMETER GoalsFile
    Path to a queue of candidates, one per line as 'kind|goal'. Blank lines and
    lines beginning with '#' are ignored. Mutually exclusive with -Goal/-Kind.

.PARAMETER ArtifactRoot
    Root directory for run artifacts. Defaults to <repo>/pipeline-artifacts.

.PARAMETER Preset
    CMake preset used for the build + test gate. Defaults to 'default'.

.PARAMETER Agent
    Agent CLI backend: 'copilot' (default) or 'claude'.

.PARAMETER Model
    Model for the chosen agent. Defaults to 'claude-opus-4.6' (Claude Opus 4.6,
    selectable through the Copilot CLI) because implementing a whole feature or
    module is the pipeline's heaviest work. Pass an empty string to let the
    agent choose its own default.

.PARAMETER Effort
    Reasoning effort. Defaults to 'medium'. For the copilot agent this is validated
    against low|medium|high|xhigh|max; for claude the value is passed through and
    validated by the CLI itself. Pass an empty string to omit it.

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
    at the baseline and after every candidate. By default the gate runs the full
    lint checks, including clang-tidy; pass this to opt out entirely.

.PARAMETER NoBranch
    Do not create a dedicated branch; run on the current branch.

.PARAMETER NoCommit
    Do not commit after each green candidate.

.PARAMETER ContinueOnFailure
    Keep going after a red gate instead of stopping.

.PARAMETER RevertOnFailure
    On a red gate, hard-reset the working tree to the last checkpoint before
    continuing or stopping. This discards tracked changes only; untracked files
    a failed candidate created are left in place (run git clean yourself to
    remove them). With -NoCommit there are no per-candidate checkpoints, so the
    reset falls back to the branch's starting point and discards every
    candidate's work so far.

.PARAMETER ListKinds
    List the evolution kinds and their implementer prompts, then exit.

.PARAMETER DryRun
    Print every agent / cmake / ctest / git command without executing it.

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaEvolve.ps1 -ListKinds

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaEvolve.ps1 -Goal 'Add String.center' -Kind function -DryRun

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaEvolve.ps1 -GoalsFile candidates.txt
#>

[CmdletBinding()]
param(
    [string]$Goal,

    [ValidateSet('function', 'type', 'module', 'feature')]
    [string]$Kind,

    [string]$GoalsFile,

    [string]$ArtifactRoot,

    [string]$Preset = 'default',

    [ValidateSet('copilot', 'claude')]
    [string]$Agent = 'copilot',

    [string]$Model = 'claude-opus-4.6',

    [string]$Effort = 'medium',

    [switch]$AllowDirty,

    [switch]$SkipBaseline,

    [switch]$SkipBuild,

    [switch]$SkipTest,

    [switch]$SkipLintFormat,

    [switch]$NoBranch,

    [switch]$NoCommit,

    [switch]$ContinueOnFailure,

    [switch]$RevertOnFailure,

    [switch]$ListKinds,

    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Report native command failures via $LASTEXITCODE, not thrown errors, regardless
# of a user profile that flips this on (see the note in LumaPipeline.psm1).
$PSNativeCommandUseErrorActionPreference = $false

Import-Module -Name (Join-Path -Path $PSScriptRoot -ChildPath 'LumaPipeline.psm1') -Force

$RepoRoot = Get-LumaRepoRoot
$EvolveKinds = Get-EvolveKind

# Index the kinds for O(1), case-insensitive routing.
$KindIndex = @{}
foreach ($EvolveKind in $EvolveKinds) { $KindIndex[$EvolveKind.Kind] = $EvolveKind }

if ($ListKinds) {
    $EvolveKinds | ForEach-Object {
        [pscustomobject]@{
            Kind   = $_.Kind
            Name   = $_.Name
            Prompt = $_.Prompt
        }
    }
    return
}

# Validate -Effort against Copilot's documented set only. Claude accepts a
# different set, so for claude we defer to the CLI's own validation rather than
# maintain a second allow-list.
$CopilotEffortValues = @('low', 'medium', 'high', 'xhigh', 'max')
if ($Effort -and $Agent -eq 'copilot' -and $Effort -notin $CopilotEffortValues) {
    throw "Invalid -Effort '$Effort' for copilot (use $($CopilotEffortValues -join ', '))."
}

# --- Build the candidate list (deterministic kind -> prompt routing) ----------

function Resolve-EvolveKind {
    param([string]$RawKind, [string]$Context)
    $Key = $RawKind.Trim().ToLowerInvariant()
    if (-not $KindIndex.ContainsKey($Key)) {
        throw "Unknown kind '$RawKind'$Context. Valid kinds: $($EvolveKinds.Kind -join ', ') (see -ListKinds)."
    }
    return $KindIndex[$Key]
}

$UsingFile = [bool]$GoalsFile
$UsingGoal = [bool]$Goal -or [bool]$Kind
if ($UsingFile -and $UsingGoal) {
    throw 'Use either -GoalsFile or -Goal/-Kind, not both.'
}

$Candidates = [System.Collections.Generic.List[psobject]]::new()

if ($UsingFile) {
    if (-not (Test-Path -LiteralPath $GoalsFile)) {
        throw "Goals file not found: $GoalsFile"
    }
    $LineNo = 0
    foreach ($Raw in (Get-Content -LiteralPath $GoalsFile)) {
        $LineNo++
        $Line = $Raw.Trim()
        if (-not $Line -or $Line.StartsWith('#')) { continue }
        $SplitAt = $Line.IndexOf('|')
        if ($SplitAt -lt 1) {
            throw "Malformed line $LineNo in ${GoalsFile}: expected 'kind|goal', got '$Raw'."
        }
        $RawKind = $Line.Substring(0, $SplitAt).Trim()
        $LineGoal = $Line.Substring($SplitAt + 1).Trim()
        if (-not $LineGoal) {
            throw "Malformed line $LineNo in ${GoalsFile}: empty goal."
        }
        $Resolved = Resolve-EvolveKind -RawKind $RawKind -Context " on line $LineNo of $GoalsFile"
        $Candidates.Add([pscustomobject]@{ Kind = $Resolved.Kind; Name = $Resolved.Name; Prompt = $Resolved.Prompt; Goal = $LineGoal })
    }
    if ($Candidates.Count -eq 0) {
        throw "No candidates found in $GoalsFile (expected 'kind|goal' lines)."
    }
}
elseif ($Goal) {
    if (-not $Kind) {
        throw 'Provide -Kind with -Goal (function, type, module, or feature). Use -ListKinds to see the kinds.'
    }
    $Resolved = Resolve-EvolveKind -RawKind $Kind -Context ''
    $Candidates.Add([pscustomobject]@{ Kind = $Resolved.Kind; Name = $Resolved.Name; Prompt = $Resolved.Prompt; Goal = $Goal.Trim() })
}
else {
    throw 'Nothing to do. Provide -Goal + -Kind, or -GoalsFile. Use -ListKinds to see the kinds.'
}

# Assign a stable per-candidate id (used for the log file name and the summary).
$Position = 0
foreach ($Candidate in $Candidates) {
    $Position++
    Add-Member -InputObject $Candidate -NotePropertyName 'Order' -NotePropertyValue $Position
    Add-Member -InputObject $Candidate -NotePropertyName 'Id' -NotePropertyValue ('{0:D2}-{1}' -f $Position, $Candidate.Kind)
}

# --- Resolve the artifact root and run directory ------------------------------

if (-not $ArtifactRoot) {
    $ArtifactRoot = Join-Path -Path $RepoRoot -ChildPath 'pipeline-artifacts'
}
# Make the artifact root absolute so it can be reliably excluded from commits
# regardless of the caller's working directory.
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot, (Get-Location).ProviderPath)

$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunDir = Join-Path -Path $ArtifactRoot -ChildPath "evolve-$Timestamp"
$LogDir = Join-Path -Path $RunDir -ChildPath 'logs'
if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
}

Write-PhaseBanner -Title 'Luma evolve pipeline (mutating, gated)' -Subtitle "Repo: $RepoRoot"
Write-Host "  Candidates : $($Candidates.Count)"
Write-Host "  Preset     : $Preset"
Write-Host "  Agent      : $Agent"
Write-Host "  Model      : $(if ($Model) { $Model } else { '(agent default)' })"
Write-Host "  Effort     : $(if ($Effort) { $Effort } else { '(agent default)' })"
Write-Host "  Output     : $RunDir"
if ($DryRun) { Write-Host '  Mode       : DRY RUN (nothing invoked, built, or committed)' -ForegroundColor Yellow }

# --- Safety: clean tree, dedicated branch, green baseline ---------------------

if (-not $AllowDirty) {
    if (-not (Test-CleanWorkingTree -RepoRoot $RepoRoot -DryRun:$DryRun)) {
        throw 'Working tree is not clean. Commit or stash your changes, or pass -AllowDirty.'
    }
}

$BranchName = "pipeline/evolve-$Timestamp"
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
        # mutating it, so a per-candidate lint failure is attributable to that
        # candidate rather than to pre-existing drift.
        $BaselineClean = Test-LintAndFormat -RepoRoot $RepoRoot -CheckOnly -DryRun:$DryRun
        if (-not $BaselineClean -and -not $DryRun) {
            throw 'Baseline lint is not clean. Run "python scripts/format.py" and fix any remaining lint issues, or pass -SkipLintFormat.'
        }
    }
}

# --- Run candidates -----------------------------------------------------------

$Results = [System.Collections.Generic.List[psobject]]::new()
$Aborted = $false

foreach ($Current in $Candidates) {
    # A subject-safe, single-line, length-capped goal for the checkpoint commit.
    $ShortGoal = ($Current.Goal -replace '\s+', ' ').Trim()
    if ($ShortGoal.Length -gt 60) { $ShortGoal = $ShortGoal.Substring(0, 57) + '...' }

    Write-PhaseBanner -Title "[$($Current.Order)/$($Candidates.Count)] $($Current.Name)" -Subtitle "Prompt: $($Current.Prompt)"
    Write-Host "  Goal : $($Current.Goal)"

    $Lines = [System.Collections.Generic.List[string]]::new()
    $Lines.Add("You are evolving the Luma language by implementing a new $($Current.Name).")
    $Lines.Add('')
    $Lines.Add('Goal:')
    $Lines.Add("    $($Current.Goal)")
    $Lines.Add('')
    $Lines.Add("Follow the workflow in the prompt file `".github/prompts/$($Current.Prompt)`" exactly - its ground rules, procedure across every interpreter phase, verification, and Output Format sections all apply.")
    $Lines.Add('')
    $Lines.Add('Obey every relevant guide under instructions/ (and documents/ for Luma) for the languages you touch. Implement the goal completely, add or update the matching tests and documentation, and keep the full build + test suite green.')
    # Instruct the agent to fix lint findings (incl. clang-tidy) whenever this
    # candidate will be lint-gated below, so the deterministic gate only has to
    # verify. Every candidate is build-and-lint gated, so mirror only -SkipLintFormat.
    if (-not $SkipLintFormat) {
        $Lines.Add('')
        $Lines.Add('This work is gated on a clean lint result. After your primary changes, run `python scripts/format.py` to apply formatting and the safe auto-fixes, then run `python scripts/lint.py` and fix every issue it reports - including clang-tidy, shellcheck, cmakelint, tsc, and clippy findings - repeating until it passes. Correct the underlying code rather than suppressing the warnings. The pipeline re-runs format.py then lint.py as a gate and will not commit this candidate unless both pass.')
    }
    $Lines.Add('')
    $Lines.Add('Do not push to any remote. Do not amend, rebase, or rewrite existing commits. Leave staging to the pipeline unless the prompt says otherwise.')
    $Instruction = $Lines -join "`n"

    $PhaseLog = Join-Path -Path $LogDir -ChildPath "$($Current.Id).log"

    # A thrown error here (missing agent/cmake/ctest/git executable) must not
    # unwind the whole run: catch it, record the candidate as errored, and fall
    # through to the failure handling below so -RevertOnFailure /
    # -ContinueOnFailure and the summary still apply. Mirrors Invoke-LumaFix.ps1.
    $CommitSha = $null
    $Status = 'ok'
    try {
        $Outcome = Invoke-AgentPhase -Instruction $Instruction -Mode 'agent' -Agent $Agent -RepoRoot $RepoRoot `
            -LogFile $PhaseLog -LogDir $LogDir -Model $Model -Effort $Effort -DryRun:$DryRun

        if (-not $Outcome.Success) {
            Write-Warning "Agent reported a non-zero exit for '$($Current.Id)'."
            $Status = 'agent-failed'
        }

        # Build + test gate.
        if ($Status -eq 'ok') {
            $Green = Test-BuildAndTest -RepoRoot $RepoRoot -Preset $Preset `
                -SkipBuild:$SkipBuild -SkipTest:$SkipTest -DryRun:$DryRun
            if (-not $Green -and -not $DryRun) {
                Write-Warning "Gate failed after '$($Current.Id)'."
                $Status = 'gate-failed'
            }

            # Lint/format gate: apply formatters, then verify lint (incl.
            # clang-tidy). Runs after build + test so clang-tidy sees the freshest
            # compile database and a broken build fails fast first. Any formatting
            # changes fold into this candidate's commit below.
            if ($Status -eq 'ok' -and -not $SkipLintFormat) {
                $LintClean = Test-LintAndFormat -RepoRoot $RepoRoot -DryRun:$DryRun
                if (-not $LintClean -and -not $DryRun) {
                    Write-Warning "Lint/format gate failed after '$($Current.Id)'."
                    $Status = 'gate-failed'
                }
            }
        }

        # Commit a green candidate.
        if ($Status -eq 'ok' -and -not $NoCommit) {
            try {
                $CommitSha = Invoke-GitCheckpoint -RepoRoot $RepoRoot `
                    -Subject "chore(evolve): $($Current.Kind) - $ShortGoal" -Agent $Agent `
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
        Write-Warning "Candidate '$($Current.Id)' errored: $($_.Exception.Message)"
        $Status = 'error'
    }

    # Handle failure: optional revert, optional stop.
    if ($Status -ne 'ok') {
        if ($RevertOnFailure) {
            Write-Host 'Reverting working tree to last checkpoint...' -ForegroundColor Yellow
            if ($DryRun) {
                Write-Host "  DRY RUN > git -C `"$RepoRoot`" reset --hard HEAD" -ForegroundColor DarkGray
            }
            else {
                # Best-effort: warn on a failed reset rather than aborting, so the
                # summary and any remaining candidates still run.
                & git -C $RepoRoot reset --hard HEAD
                if ($LASTEXITCODE -ne 0) { Write-Warning 'Revert failed.' }
            }
        }
        if (Test-ShouldAbort -Status $Status -ContinueOnFailure:$ContinueOnFailure -RevertOnFailure:$RevertOnFailure) {
            $Aborted = $true
        }
    }

    $Results.Add([pscustomobject]@{
            Order  = $Current.Order
            Id     = $Current.Id
            Kind   = $Current.Kind
            Goal   = $Current.Goal
            Status = $Status
            Commit = if ($CommitSha) { $CommitSha } else { '-' }
        })

    if ($Aborted) {
        if ($Status -eq 'commit-failed' -and $ContinueOnFailure) {
            Write-Warning "Stopping after '$($Current.Id)': the commit was rejected and cannot be skipped (git add -A would re-stage it). Fix the blocker (e.g. the pre-commit hook), or pass -RevertOnFailure to discard failed candidates and keep going."
        }
        else {
            Write-Warning "Stopping after '$($Current.Id)' (pass -ContinueOnFailure to keep going)."
        }
        break
    }
}

# --- Summary ------------------------------------------------------------------

# Persist a run summary so a completed run is auditable from disk even when the
# console scrollback is lost. A candidate whose Commit is '-' recorded no
# checkpoint of its own; if the tree was dirty that work can land in a later
# candidate's commit, so a '-' next to an ok status is worth a look.
if (-not $DryRun) {
    $SummaryLines = [System.Collections.Generic.List[string]]::new()
    $SummaryLines.Add('# Luma evolve run')
    $SummaryLines.Add('')
    $SummaryLines.Add("- Generated: $Timestamp")
    $SummaryLines.Add("- Repository: $RepoRoot")
    if (-not $NoBranch) {
        $SummaryLines.Add("- Branch: $BranchName")
    }
    $SummaryLines.Add('')
    $SummaryLines.Add('| Order | Id | Kind | Goal | Status | Commit | Log |')
    $SummaryLines.Add('| ----- | -- | ---- | ---- | ------ | ------ | --- |')
    foreach ($Row in $Results) {
        # Link the candidate's transcript when it exists so the summary doubles
        # as a navigation index; a candidate that failed before the agent ran
        # (e.g. a missing CLI) records no log, so guard the link on the file.
        $LogName = "$($Row.Id).log"
        $LogCell = if (Test-Path -LiteralPath (Join-Path -Path $LogDir -ChildPath $LogName)) { "[$LogName](logs/$LogName)" } else { '-' }
        # Keep the goal from breaking the table: collapse whitespace and escape pipes.
        $GoalCell = (($Row.Goal -replace '\s+', ' ').Trim() -replace '\|', '\|')
        $SummaryLines.Add("| $($Row.Order) | $($Row.Id) | $($Row.Kind) | $GoalCell | $($Row.Status) | $($Row.Commit) | $LogCell |")
    }
    $SummaryLines.Add('')
    if ($Aborted) {
        $SummaryLines.Add('The pipeline stopped early on a failed candidate.')
    }
    else {
        $SummaryLines.Add('All selected candidates completed.')
    }
    $SummaryLines.Add('')
    $SummaryLines.Add('A `-` in the Commit column means the candidate created no commit of its own; a `-` in the Log column means no transcript was captured.')
    $SummaryPath = Join-Path -Path $RunDir -ChildPath 'SUMMARY.md'
    Set-Content -LiteralPath $SummaryPath -Value ($SummaryLines -join "`n") -Encoding utf8
    Write-Host ''
    Write-Host "Summary written: $SummaryPath" -ForegroundColor Green
}

Write-PhaseBanner -Title 'Evolve summary'
if (-not $NoBranch -and -not $DryRun) {
    Write-Host "Branch: $BranchName"
}
$Results | Format-Table -AutoSize Order, Id, Kind, Status, Commit

Write-Output $Results

# Exit non-zero when the pipeline aborted or any candidate failed - including a
# rejected checkpoint commit (commit-failed) - so callers and CI see the failure
# instead of reading an aborted run as a clean pass.
$RunFailures = @($Results | Where-Object { $_.Status -ne 'ok' }).Count
if ($Aborted) {
    Write-Host 'Pipeline stopped early on a failed candidate.' -ForegroundColor Yellow
    exit 1
}
if ($RunFailures -gt 0) {
    Write-Host "Completed with $RunFailures failed candidate(s)." -ForegroundColor Yellow
    exit 1
}
Write-Host 'All selected candidates completed.' -ForegroundColor Green
exit 0
