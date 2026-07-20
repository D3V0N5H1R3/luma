#Requires -Version 7.0

<#
.SYNOPSIS
    Review and fix the repository's source files for conformance to the
    project's coding-standard guides, one file at a time, via an agent CLI
    (Copilot or Claude).

.DESCRIPTION
    For each selected file-type target (C++, CSS, CMake, GitHub Actions,
    JavaScript, Luma, Markdown, PowerShell, Python, Rust, Shell, TypeScript)
    the runner enumerates every matching tracked file and runs one agent
    session per file. Each session is told to make that single file conform to
    the target's instruction/guide files under instructions/ (and documents/
    for Luma) and to fix every issue it finds.

    This is a mutating, gated pass built on the same safety model as
    Invoke-LumaFix.ps1 and it reuses that runner's shared helpers
    (LumaPipeline.psm1):

      * a clean working tree is required before starting (unless -AllowDirty);
      * work happens on a dedicated pipeline/conformance-<timestamp> branch so
        your current branch is untouched (unless -NoBranch);
      * a green build + test baseline is established first when a build-gated
        target is selected (unless -SkipBaseline);
      * each file is committed as its own checkpoint (unless -NoCommit);
      * the cmake + ctest gate runs for the build-affecting targets (C++, CMake)
        at the cadence set by -GateMode, and only a green tree continues;
      * the run stops at the first failure (unless -ContinueOnFailure).

    Only the C++ and CMake targets change what `cmake --build` + `ctest` covers,
    so only those are gated by the script. For every target the per-file
    instruction tells the agent to run the verification appropriate to that file
    type (cargo, npm/tsc, shellcheck, PSScriptAnalyzer, markdownlint, actionlint,
    the Luma test runner, ...) and to keep the project green.

    Nothing is pushed: `git push` is denied to the agent and the runner never
    merges or pushes the branch. Reviewing and merging the branch is left to you.

.PARAMETER Target
    One or more target filters (Id or Name substring, case-insensitive).
    Omit to run every target in order. Use -List to see the available targets.

.PARAMETER Path
    Optional case-insensitive substring; only files whose repository-relative
    path contains it are processed (e.g. 'core/runtime/vm/'). Applies to every
    selected target.

.PARAMETER MaxFiles
    Optional cap on the number of files processed per target (after sorting).
    Useful for a trial run over a large target such as C++ or Luma.

.PARAMETER ArtifactRoot
    Root directory for run artifacts. Defaults to <repo>/pipeline-artifacts
    (git-ignored). Runs land under <ArtifactRoot>/conformance-<timestamp>/.

.PARAMETER Preset
    CMake preset used for the build + test gate. Defaults to 'default'.

.PARAMETER Agent
    Agent CLI backend: 'copilot' (default) or 'claude'.

.PARAMETER Model
    Model for the chosen agent. Defaults to 'claude-opus-4.8' (Claude Opus 4.8,
    selectable through the Copilot CLI). Pass an empty string to let the agent
    choose its own default.

.PARAMETER Effort
    Reasoning effort. Defaults to 'max'. For the copilot agent this is validated
    against low|medium|high|xhigh|max; for claude the value is passed through and
    validated by the CLI itself. Pass an empty string to omit it.

.PARAMETER GateMode
    When the cmake + ctest gate runs for build-affecting targets (C++, CMake):
      * per-target (default) - once after all of a target's files;
      * per-file             - after every file (slowest, most precise);
      * off                  - never (rely on the agent's own verification).

.PARAMETER AllowDirty
    Do not require a clean working tree before starting.

.PARAMETER SkipBaseline
    Skip the initial green build + test baseline check.

.PARAMETER SkipBuild
    Skip the build step of every gate (test only).

.PARAMETER SkipTest
    Skip the test step of every gate (build only).

.PARAMETER NoBranch
    Do not create a dedicated branch; run on the current branch.

.PARAMETER NoCommit
    Do not commit a checkpoint after each file.

.PARAMETER ContinueOnFailure
    Keep going after a failed file or gate instead of stopping.

.PARAMETER RevertOnFailure
    On a failure, hard-reset tracked changes: to HEAD after a failed file (or
    per-file gate), discarding that file's uncommitted edits; to the target's
    starting commit after a failed per-target gate, discarding the whole target.
    Untracked files a failed session created are left in place. With -NoCommit
    there are no checkpoints, so a per-target revert falls back to the run's
    starting commit.

.PARAMETER List
    List the targets in order (with file counts) and exit without running.

.PARAMETER ListFiles
    List the files each selected target would process and exit without running.

.PARAMETER DryRun
    Print every agent / cmake / ctest / git command without executing it.

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaConformance.ps1 -List

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaConformance.ps1 -Target cpp -ListFiles

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaConformance.ps1 -Target markdown -DryRun

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaConformance.ps1 -Target shell,powershell
#>

[CmdletBinding()]
param(
    [string[]]$Target,

    [string]$Path,

    [ValidateRange(1, 100000)]
    [int]$MaxFiles,

    [string]$ArtifactRoot,

    [string]$Preset = 'default',

    [ValidateSet('copilot', 'claude')]
    [string]$Agent = 'copilot',

    [string]$Model = 'claude-opus-4.8',

    [string]$Effort = 'max',

    [ValidateSet('per-target', 'per-file', 'off')]
    [string]$GateMode = 'per-target',

    [switch]$AllowDirty,

    [switch]$SkipBaseline,

    [switch]$SkipBuild,

    [switch]$SkipTest,

    [switch]$NoBranch,

    [switch]$NoCommit,

    [switch]$ContinueOnFailure,

    [switch]$RevertOnFailure,

    [switch]$List,

    [switch]$ListFiles,

    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Report native command failures via $LASTEXITCODE, not thrown errors, regardless
# of a user profile that flips this on (see the note in LumaPipeline.psm1).
$PSNativeCommandUseErrorActionPreference = $false

Import-Module -Name (Join-Path -Path $PSScriptRoot -ChildPath 'LumaPipeline.psm1') -Force

function Get-ConformanceTarget {
    <#
    .SYNOPSIS
        The ordered file-type targets, each with the guides its files must
        conform to, the extension pattern that selects those files, whether the
        cmake + ctest gate is meaningful, and the verification the agent should
        run for that file type.
    .DESCRIPTION
        Gate is $true only for the targets that change what `cmake --build` +
        `ctest` covers (C++ sources and CMake build files). Every other target
        is verified by the agent through Verify, because the cmake gate would not
        exercise it (editor extensions build through npm, Rust through cargo,
        Luma through its own test runner, and docs/scripts not at all).
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.Generic.List[psobject]])]
    param()

    $Instr = 'instructions/'
    $Docs = 'documents/'
    $Targets = [System.Collections.Generic.List[psobject]]::new()

    $Targets.Add([pscustomobject]@{
            Id           = 'cpp'
            Name         = 'C++ sources'
            Pattern      = '\.(cpp|hpp|h)$'
            Gate         = $true
            Instructions = @("${Instr}cpp.instructions.md", "${Instr}testing.instructions.md")
            Verify       = 'build and test with cmake --build --preset default and ctest --preset default (or the relevant subset)'
        })
    $Targets.Add([pscustomobject]@{
            Id           = 'css'
            Name         = 'CSS files'
            Pattern      = '\.css$'
            Gate         = $false
            Instructions = @("${Instr}css.instructions.md")
            Verify       = 'run the CSS lint (for example npx stylelint) if the project configures one'
        })
    $Targets.Add([pscustomobject]@{
            Id           = 'cmake'
            Name         = 'CMake files'
            Pattern      = '(^|/)CMakeLists\.txt$|\.cmake$'
            Gate         = $true
            Instructions = @("${Instr}cmake.instructions.md")
            Verify       = 're-configure and build with the default preset to confirm the change is valid'
        })
    $Targets.Add([pscustomobject]@{
            Id           = 'github-actions'
            Name         = 'GitHub Actions workflows'
            Pattern      = '^\.github/workflows/.*\.(ya?ml)$'
            Gate         = $false
            Instructions = @("${Instr}github-actions.instructions.md", "${Instr}github-actions-recipes.instructions.md")
            Verify       = 'validate the workflow (for example actionlint) and confirm the YAML is well-formed'
        })
    $Targets.Add([pscustomobject]@{
            Id           = 'javascript'
            Name         = 'JavaScript sources'
            Pattern      = '\.(js|mjs|cjs)$'
            Gate         = $false
            Instructions = @("${Instr}javascript.instructions.md", "${Instr}testing.instructions.md")
            Verify       = 'run the owning package''s lint/test (for example npm run lint and npm test) if configured'
        })
    $Targets.Add([pscustomobject]@{
            Id           = 'luma'
            Name         = 'Luma sources'
            Pattern      = '\.luma$'
            Gate         = $false
            Instructions = @(
                "${Instr}luma.instructions.md", "${Instr}testing.instructions.md",
                "${Docs}Luma_User_Manual.md", "${Docs}Luma_Standard_Library_Reference.md",
                "${Docs}Luma_Solaris_Guide.md", "${Docs}Luma_GraphicalUi_Guide.md", "${Docs}Luma_Performance_Guide.md",
                "${Docs}Luma_Error_Handling.md", "${Docs}Luma_Coding_Guidelines.md")
            Verify       = 'run the file through the built luma interpreter (for example build/<config>/luma <file> --test, or python scripts/run_examples.py / python scripts/run_luma_tests.py) as appropriate'
        })
    $Targets.Add([pscustomobject]@{
            Id           = 'markdown'
            Name         = 'Markdown files'
            Pattern      = '\.md$'
            Gate         = $false
            Instructions = @("${Instr}markdown.instructions.md", "${Instr}readme.instructions.md")
            Verify       = 'run the Markdown lint (for example npx markdownlint-cli2) if configured, and keep every link valid'
        })
    $Targets.Add([pscustomobject]@{
            Id           = 'powershell'
            Name         = 'PowerShell scripts'
            Pattern      = '\.(ps1|psm1|psd1)$'
            Gate         = $false
            Instructions = @("${Instr}powershell.instructions.md")
            Verify       = 'run PSScriptAnalyzer (for example pwsh -File scripts/run_psscriptanalyzer.ps1, or Invoke-ScriptAnalyzer)'
        })
    $Targets.Add([pscustomobject]@{
            Id           = 'python'
            Name         = 'Python sources'
            Pattern      = '\.py$'
            Gate         = $false
            Instructions = @("${Instr}python.instructions.md", "${Instr}testing.instructions.md")
            Verify       = 'run the Python checks (for example python scripts/lint.py --only ruff) and any affected tests'
        })
    $Targets.Add([pscustomobject]@{
            Id           = 'rust'
            Name         = 'Rust sources'
            Pattern      = '\.rs$'
            Gate         = $false
            Instructions = @("${Instr}rust.instructions.md", "${Instr}testing.instructions.md")
            Verify       = 'run cargo fmt --check, cargo clippy, and cargo test in the owning crate'
        })
    $Targets.Add([pscustomobject]@{
            Id           = 'shell'
            Name         = 'Shell scripts'
            Pattern      = '\.(sh|bash)$'
            Gate         = $false
            Instructions = @("${Instr}shell.instructions.md")
            Verify       = 'run shellcheck on the script and confirm it still parses'
        })
    $Targets.Add([pscustomobject]@{
            Id           = 'typescript'
            Name         = 'TypeScript sources'
            Pattern      = '\.(ts|tsx)$'
            Gate         = $false
            Instructions = @("${Instr}typescript.instructions.md", "${Instr}testing.instructions.md")
            Verify       = 'run the owning package''s type-check and lint/test (for example tsc --noEmit, npm run lint, npm test) if configured'
        })
    return $Targets
}

function Get-RepoRelativePath {
    <#
    .SYNOPSIS
        Return a path relative to the repository root using forward slashes,
        to match the output of `git ls-files`.
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory)]
        [string]$FullPath,

        [Parameter(Mandatory)]
        [string]$RepoRoot
    )

    $Full = [System.IO.Path]::GetFullPath($FullPath)
    $Root = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
    if ($Full.StartsWith($Root, [System.StringComparison]::OrdinalIgnoreCase)) {
        $Rel = $Full.Substring($Root.Length).TrimStart('\', '/')
        return $Rel -replace '\\', '/'
    }
    return ($Full -replace '\\', '/')
}

function Get-TargetFile {
    <#
    .SYNOPSIS
        Enumerate the tracked files a target should process.
    .DESCRIPTION
        Lists tracked files with `git ls-files`, keeps those matching the
        target's extension pattern, drops vendored code under external/ (keeping
        the first-party external/gui-framework/), applies the optional path
        substring filter, drops the runner's own active files, sorts, and caps
        at MaxFiles.
    #>
    [CmdletBinding()]
    [OutputType([string[]])]
    param(
        [Parameter(Mandatory)]
        [string]$RepoRoot,

        [Parameter(Mandatory)]
        [string]$Pattern,

        [string]$PathFilter,

        [int]$Limit,

        [string[]]$Exclude = @()
    )

    Push-Location -LiteralPath $RepoRoot
    try {
        $Tracked = git ls-files
    }
    finally {
        Pop-Location
    }

    $Files = [System.Collections.Generic.List[string]]::new()
    foreach ($Item in @($Tracked)) {
        if ([string]::IsNullOrWhiteSpace($Item)) { continue }
        $Rel = $Item.Trim()
        # Vendored third-party code is off limits (matches the protect-vendored
        # agent hook); the one first-party exception is external/gui-framework/.
        if ($Rel -match '^external/' -and $Rel -notmatch '^external/gui-framework/') { continue }
        # Case-insensitive by default (PowerShell -notmatch), so an uppercase
        # extension such as .CPP still matches; the shell runner mirrors this
        # with `grep -Ei`. Keep both sides case-insensitive for identical
        # enumeration -- do not switch this to -cnotmatch.
        if ($Rel -notmatch $Pattern) { continue }
        if ($PathFilter -and -not $Rel.Contains($PathFilter, [System.StringComparison]::OrdinalIgnoreCase)) { continue }
        if ($Exclude -contains $Rel) { continue }
        $Files.Add($Rel)
    }

    # Ordinal sort so the order is deterministic and matches `git ls-files` /
    # the shell runner's byte-ordered listing.
    $Files.Sort([System.StringComparer]::Ordinal)
    if ($Limit -gt 0 -and $Files.Count -gt $Limit) {
        return [string[]]@($Files | Select-Object -First $Limit)
    }
    return [string[]]@($Files)
}

function Get-ConformanceInstruction {
    <#
    .SYNOPSIS
        Build the per-file agent instruction for one target file.
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory)]
        [psobject]$TargetInfo,

        [Parameter(Mandatory)]
        [string]$RelativePath
    )

    $Lines = [System.Collections.Generic.List[string]]::new()
    $Lines.Add("You are reviewing and fixing a single $($TargetInfo.Name -replace 's$', '') in the Luma project for conformance to the project's coding standards.")
    $Lines.Add('')
    $Lines.Add('Target file (this is the only file you should change, unless a fix strictly requires an adjustment to a directly-coupled file such as its test):')
    $Lines.Add("    $RelativePath")
    $Lines.Add('')
    $Lines.Add('Authoritative standards - read each one in full and apply every rule that is relevant to this file:')
    foreach ($Doc in $TargetInfo.Instructions) {
        $Lines.Add("    $Doc")
    }
    $Lines.Add('    instructions/learnings.instructions.md   (always-on: accumulated project pitfalls)')
    $Lines.Add('')
    $Lines.Add('Do this:')
    $Lines.Add('1. Read the target file and the standards above.')
    $Lines.Add('2. Find every place the file violates those standards - naming, structure, style, idioms, error handling, comments, and anything else the guides require - and any bugs you notice while reviewing.')
    $Lines.Add('3. Fix all of them with the smallest correct changes. Preserve the existing behaviour and public interface unless a guide requires otherwise.')
    $Lines.Add('4. If the file is a test, keep it aligned with instructions/testing.instructions.md; if a fix changes tested behaviour, update the matching test.')
    $Lines.Add("5. Verify your change: $($TargetInfo.Verify). Keep the full build and test suite green.")
    $Lines.Add('')
    $Lines.Add('Constraints:')
    $Lines.Add('- Do not modify unrelated files. Do not touch vendored code under external/ (except external/gui-framework/).')
    $Lines.Add('- Do not push to any remote. Do not amend, rebase, or rewrite existing commits. Leave staging and committing to the pipeline.')
    $Lines.Add('- If the file already fully conforms, make no changes and say so briefly.')
    return $Lines -join "`n"
}

$RepoRoot = Get-LumaRepoRoot
$AllTargets = Get-ConformanceTarget

# Validate -Effort against Copilot's documented set only. Claude accepts a
# different set, so for claude we defer to the CLI's own validation.
$CopilotEffortValues = @('low', 'medium', 'high', 'xhigh', 'max')
if ($Effort -and $Agent -eq 'copilot' -and $Effort -notin $CopilotEffortValues) {
    throw "Invalid -Effort '$Effort' for copilot (use $($CopilotEffortValues -join ', '))."
}

# The runner's own files are excluded from enumeration so a session never edits a
# script that is currently executing (a real hazard for the shell runner, and
# kept symmetric for PowerShell). Their relative paths use forward slashes.
$SelfExclude = @(
    (Get-RepoRelativePath -FullPath $PSCommandPath -RepoRoot $RepoRoot),
    (Get-RepoRelativePath -FullPath (Join-Path -Path $PSScriptRoot -ChildPath 'LumaPipeline.psm1') -RepoRoot $RepoRoot)
)

# Select the targets to run.
if ($Target) {
    $Target = @($Target | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $Selected = [System.Collections.Generic.List[psobject]]::new()
    foreach ($Filter in $Target) {
        # Prefer an exact target-id match so that, for example, "shell" selects
        # only the shell target and not "powershell" as a substring.
        $Matched = @($AllTargets | Where-Object { $_.Id -eq $Filter })
        if ($Matched.Count -eq 0) {
            $Matched = @($AllTargets | Where-Object {
                    "$($_.Id) $($_.Name)".Contains($Filter, [System.StringComparison]::OrdinalIgnoreCase)
                })
        }
        if ($Matched.Count -eq 0) {
            throw "No target matched '$Filter'. Use -List to see the available targets."
        }
        foreach ($Item in $Matched) {
            if (-not ($Selected | Where-Object { $_.Id -eq $Item.Id })) {
                $Selected.Add($Item)
            }
        }
    }
}
else {
    $Selected = $AllTargets
}

if ($List) {
    $Index = 0
    foreach ($Item in $Selected) {
        $Index++
        $Count = @(Get-TargetFile -RepoRoot $RepoRoot -Pattern $Item.Pattern -PathFilter $Path -Limit $MaxFiles -Exclude $SelfExclude).Count
        [pscustomobject]@{
            Order        = $Index
            Id           = $Item.Id
            Name         = $Item.Name
            Files        = $Count
            Gated        = $Item.Gate
            Instructions = ($Item.Instructions -join ', ')
        }
    }
    return
}

if ($ListFiles) {
    foreach ($Item in $Selected) {
        $Files = @(Get-TargetFile -RepoRoot $RepoRoot -Pattern $Item.Pattern -PathFilter $Path -Limit $MaxFiles -Exclude $SelfExclude)
        Write-PhaseBanner -Title "$($Item.Name) [$($Item.Id)]" -Subtitle "$($Files.Count) file(s)"
        foreach ($File in $Files) {
            Write-Host "  $File"
        }
    }
    return
}

# Prepare the run directory (git-ignored default keeps artifacts out of commits).
if (-not $ArtifactRoot) {
    $ArtifactRoot = Join-Path -Path $RepoRoot -ChildPath 'pipeline-artifacts'
}
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot, (Get-Location).ProviderPath)
$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunDir = Join-Path -Path $ArtifactRoot -ChildPath "conformance-$Timestamp"
$LogDir = Join-Path -Path $RunDir -ChildPath 'logs'
if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
}

$GatedSelected = @($Selected | Where-Object { $_.Gate })
$NeedsBaseline = ($GatedSelected.Count -gt 0) -and ($GateMode -ne 'off')

Write-PhaseBanner -Title 'Luma conformance pipeline (mutating, gated)' -Subtitle "Repo: $RepoRoot"
Write-Host "  Targets  : $($Selected.Count) ($(( $Selected | ForEach-Object { $_.Id }) -join ', '))"
Write-Host "  Preset   : $Preset"
Write-Host "  Agent    : $Agent"
Write-Host "  Model    : $(if ($Model) { $Model } else { '(agent default)' })"
Write-Host "  Effort   : $(if ($Effort) { $Effort } else { '(agent default)' })"
Write-Host "  GateMode : $GateMode"
Write-Host "  Output   : $RunDir"
if ($DryRun) { Write-Host '  Mode     : DRY RUN (nothing invoked, built, or committed)' -ForegroundColor Yellow }

# --- Safety: clean tree, dedicated branch, green baseline ---------------------

if (-not $AllowDirty) {
    if (-not (Test-CleanWorkingTree -RepoRoot $RepoRoot -DryRun:$DryRun)) {
        throw 'Working tree is not clean. Commit or stash your changes, or pass -AllowDirty.'
    }
}

$BranchName = "pipeline/conformance-$Timestamp"
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

if ($NeedsBaseline -and -not $SkipBaseline) {
    Write-PhaseBanner -Title 'Baseline gate' -Subtitle 'Verifying a green build + test before making changes'
    $BaselineGreen = Test-BuildAndTest -RepoRoot $RepoRoot -Preset $Preset `
        -SkipBuild:$SkipBuild -SkipTest:$SkipTest -DryRun:$DryRun
    if (-not $BaselineGreen -and -not $DryRun) {
        throw 'Baseline build + test is not green. Fix the baseline first, or pass -SkipBaseline.'
    }
}

# Capture the run's starting commit so a -NoCommit per-target revert has a
# sensible fallback (there are no per-target checkpoints to reset to).
$RunStartSha = $null
if (-not $DryRun) {
    $RunStartSha = (& git -C $RepoRoot rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -eq 0) { $RunStartSha = $RunStartSha.Trim() } else { $RunStartSha = $null }
}

# --- Run targets, file by file ------------------------------------------------

$Results = [System.Collections.Generic.List[psobject]]::new()
$Aborted = $false
$TargetPosition = 0

foreach ($Current in $Selected) {
    if ($Aborted) { break }
    $TargetPosition++
    $Files = @(Get-TargetFile -RepoRoot $RepoRoot -Pattern $Current.Pattern -PathFilter $Path -Limit $MaxFiles -Exclude $SelfExclude)
    $GateThisTarget = $Current.Gate -and ($GateMode -ne 'off')

    Write-PhaseBanner -Title "[$TargetPosition/$($Selected.Count)] $($Current.Name)" `
        -Subtitle "$($Files.Count) file(s)   gate: $(if ($GateThisTarget) { $GateMode } else { 'none' })"

    if ($Files.Count -eq 0) {
        Write-Host '  No files matched for this target.' -ForegroundColor DarkGray
        continue
    }

    # Starting point for a per-target revert.
    $TargetStartSha = $RunStartSha
    if (-not $DryRun) {
        $Head = (& git -C $RepoRoot rev-parse HEAD 2>$null)
        if ($LASTEXITCODE -eq 0) { $TargetStartSha = $Head.Trim() }
    }

    $TargetLogDir = Join-Path -Path $LogDir -ChildPath $Current.Id
    if (-not $DryRun) {
        New-Item -ItemType Directory -Force -Path $TargetLogDir | Out-Null
    }

    $FilePosition = 0
    foreach ($File in $Files) {
        $FilePosition++
        Write-Host ''
        Write-Host "  [$($Current.Id) $FilePosition/$($Files.Count)] $File" -ForegroundColor Cyan

        $Instruction = Get-ConformanceInstruction -TargetInfo $Current -RelativePath $File
        # Nest the per-file log under the target, mirroring the source path so two
        # files with the same leaf name never collide.
        $PhaseLog = Join-Path -Path $TargetLogDir -ChildPath ("$File.log" -replace '/', [System.IO.Path]::DirectorySeparatorChar)
        if (-not $DryRun) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $PhaseLog) | Out-Null
        }

        $Status = 'ok'
        $CommitSha = $null
        try {
            $Outcome = Invoke-AgentPhase -Instruction $Instruction -Mode 'agent' -Agent $Agent -RepoRoot $RepoRoot `
                -LogFile $PhaseLog -LogDir $TargetLogDir -Model $Model -Effort $Effort -DryRun:$DryRun

            if (-not $Outcome.Success) {
                Write-Warning "Agent reported a non-zero exit for '$File'."
                $Status = 'agent-failed'
            }

            # Per-file gate (only when the target is build-affecting and GateMode
            # is per-file).
            if ($Status -eq 'ok' -and $GateThisTarget -and $GateMode -eq 'per-file') {
                $Green = Test-BuildAndTest -RepoRoot $RepoRoot -Preset $Preset `
                    -SkipBuild:$SkipBuild -SkipTest:$SkipTest -DryRun:$DryRun
                if (-not $Green -and -not $DryRun) {
                    Write-Warning "Gate failed after '$File'."
                    $Status = 'gate-failed'
                }
            }

            if ($Status -eq 'ok' -and -not $NoCommit) {
                try {
                    $CommitSha = Invoke-GitCheckpoint -RepoRoot $RepoRoot `
                        -Subject "chore(conformance): $($Current.Id) $File" -Agent $Agent `
                        -ArtifactRoot $ArtifactRoot -DryRun:$DryRun
                }
                catch {
                    # A rejected commit (e.g. the pre-commit hook) must not look
                    # like success. Mark it commit-failed so the revert/stop path
                    # below runs, instead of re-staging the un-committable change
                    # into every later checkpoint (git add -A) and piling up edits.
                    Write-Warning "Checkpoint commit failed for '$File': $($_.Exception.Message)"
                    $Status = 'commit-failed'
                    $CommitSha = $null
                }
            }
        }
        catch {
            Write-Warning "File '$File' errored: $($_.Exception.Message)"
            $Status = 'error'
        }

        if ($Status -ne 'ok') {
            if ($RevertOnFailure) {
                Write-Host '    Reverting uncommitted changes to HEAD...' -ForegroundColor Yellow
                if ($DryRun) {
                    Write-Host "    DRY RUN > git -C `"$RepoRoot`" reset --hard HEAD" -ForegroundColor DarkGray
                }
                else {
                    & git -C $RepoRoot reset --hard HEAD
                    if ($LASTEXITCODE -ne 0) { Write-Warning 'Revert failed.' }
                }
            }
            if (Test-ShouldAbort -Status $Status -ContinueOnFailure:$ContinueOnFailure -RevertOnFailure:$RevertOnFailure) {
                $Aborted = $true
            }
        }

        $Results.Add([pscustomobject]@{
                Target = $Current.Id
                File   = $File
                Status = $Status
                Commit = if ($CommitSha) { $CommitSha } else { '-' }
            })

        if ($Aborted) {
            if ($Status -eq 'commit-failed' -and $ContinueOnFailure) {
                Write-Warning "Stopping after '$File': the commit was rejected and cannot be skipped (git add -A would re-stage it). Fix the blocker (e.g. the pre-commit hook), or pass -RevertOnFailure to discard failed files and keep going."
            }
            else {
                Write-Warning "Stopping after '$File' (pass -ContinueOnFailure to keep going)."
            }
            break
        }
    }

    if ($Aborted) { break }

    # Per-target gate: one build + test after all of the target's files.
    if ($GateThisTarget -and $GateMode -eq 'per-target') {
        Write-PhaseBanner -Title "Gate: $($Current.Name)" -Subtitle 'Build + test after target'
        $Green = Test-BuildAndTest -RepoRoot $RepoRoot -Preset $Preset `
            -SkipBuild:$SkipBuild -SkipTest:$SkipTest -DryRun:$DryRun
        if (-not $Green -and -not $DryRun) {
            Write-Warning "Per-target gate failed for '$($Current.Id)'."
            if ($RevertOnFailure -and $TargetStartSha) {
                Write-Host "  Reverting target to $($TargetStartSha.Substring(0, [Math]::Min(9, $TargetStartSha.Length)))..." -ForegroundColor Yellow
                & git -C $RepoRoot reset --hard $TargetStartSha
                if ($LASTEXITCODE -ne 0) { Write-Warning 'Target revert failed.' }
            }
            $Results.Add([pscustomobject]@{
                    Target = $Current.Id
                    File   = '(per-target gate)'
                    Status = 'gate-failed'
                    Commit = '-'
                })
            if (-not $ContinueOnFailure) {
                $Aborted = $true
                Write-Warning "Stopping after the '$($Current.Id)' gate (pass -ContinueOnFailure to keep going)."
            }
        }
    }
}

# --- Summary ------------------------------------------------------------------

if (-not $DryRun) {
    $SummaryLines = [System.Collections.Generic.List[string]]::new()
    $SummaryLines.Add('# Luma conformance run')
    $SummaryLines.Add('')
    $SummaryLines.Add("- Generated: $Timestamp")
    $SummaryLines.Add("- Repository: $RepoRoot")
    if (-not $NoBranch) { $SummaryLines.Add("- Branch: $BranchName") }
    $SummaryLines.Add("- Agent: $Agent   Model: $(if ($Model) { $Model } else { '(default)' })   Effort: $(if ($Effort) { $Effort } else { '(default)' })")
    $SummaryLines.Add("- Gate mode: $GateMode")
    $SummaryLines.Add('')
    $SummaryLines.Add('| Target | File | Status | Commit |')
    $SummaryLines.Add('| ------ | ---- | ------ | ------ |')
    foreach ($Row in $Results) {
        $SummaryLines.Add("| $($Row.Target) | $($Row.File) | $($Row.Status) | $($Row.Commit) |")
    }
    $SummaryLines.Add('')
    if ($Aborted) { $SummaryLines.Add('The run stopped early on a failure.') }
    else { $SummaryLines.Add('All selected files were processed.') }
    $SummaryPath = Join-Path -Path $RunDir -ChildPath 'SUMMARY.md'
    Set-Content -LiteralPath $SummaryPath -Value ($SummaryLines -join "`n") -Encoding utf8
    Write-Host ''
    Write-Host "Summary written: $SummaryPath" -ForegroundColor Green
}

Write-PhaseBanner -Title 'Conformance summary'
if (-not $NoBranch -and -not $DryRun) { Write-Host "Branch: $BranchName" }
$Grouped = $Results | Group-Object -Property Status | Sort-Object -Property Name
foreach ($Group in $Grouped) {
    Write-Host ("  {0,-14} {1}" -f $Group.Name, $Group.Count)
}
Write-Host "  total          $($Results.Count)"

Write-Output $Results

# Exit non-zero when the run aborted or any file failed - including a rejected
# checkpoint commit (commit-failed) - so Invoke-LumaAll.ps1 and CI see the
# failure instead of reading an aborted run as a clean pass.
$RunFailures = @($Results | Where-Object { $_.Status -ne 'ok' }).Count
if ($Aborted) {
    Write-Host 'Run stopped early on a failure.' -ForegroundColor Yellow
    exit 1
}
if ($RunFailures -gt 0) {
    Write-Host "Completed with $RunFailures failed file(s)." -ForegroundColor Yellow
    exit 1
}
Write-Host 'All selected files were processed.' -ForegroundColor Green
exit 0
