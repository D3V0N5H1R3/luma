#Requires -Version 7.0

<#
.SYNOPSIS
    Run the Luma audit and then the fix pipeline back-to-back through one agent
    CLI - and, optionally, the per-file conformance pass afterwards - defaulting
    to the GitHub Copilot CLI driving Claude Opus 4.8 at max reasoning effort.

.DESCRIPTION
    A thin orchestrator that invokes the pipeline entry points in order:

        1. Invoke-LumaAudit.ps1        (read-only, plan mode) - ranked reports.
        2. Invoke-LumaFix.ps1          (mutating, agent mode) - gated fixes.
        3. Invoke-LumaConformance.ps1  (mutating, agent mode) - per-file
           conformance pass, only when -IncludeConformance is given.

    Each runner is launched as its own PowerShell child process, exactly as a
    user would invoke it, so their exit codes and console output are preserved
    and their strict-mode/scope settings never leak between phases. The audit
    runs first; if it exits non-zero (a usage or setup failure) the remaining
    phases are skipped. Otherwise the fix runner starts and feeds on the freshest
    audit reports it auto-detects.

    The conformance phase is opt-in because it is organised on a different axis
    from the improvement prompts - it walks every tracked file of each selected
    type and runs one agent session per file - so a full run over the repository
    is expensive. It is off unless -IncludeConformance is passed, and it is
    almost always worth scoping with -ConformanceArgs (for example
    -ConformanceArgs '-Target', 'cpp'). It shares the fix runner's safety model
    (clean tree, dedicated branch, per-file commit, build + test gate for the
    build-affecting targets, no push).

    The three agent knobs default to the requested configuration - the Copilot
    CLI backend, the 'claude-opus-4.8' model (Claude Opus 4.8, selectable through
    the Copilot CLI), and 'max' reasoning effort - and are forwarded to every
    runner. Preview everything first with -DryRun.

.PARAMETER Agent
    Agent CLI backend forwarded to both runners: 'copilot' (default) or 'claude'.

.PARAMETER Model
    Model passed to the agent's --model flag. Defaults to 'claude-opus-4.8'.
    Pass an empty string to let the agent choose its own default.

.PARAMETER Effort
    Reasoning effort passed to the agent's --effort flag. Defaults to 'max'. For
    the copilot backend each runner validates this against
    low|medium|high|xhigh|max; pass an empty string to omit it.

.PARAMETER SkipAudit
    Skip the audit phase and run only the later phases.

.PARAMETER SkipFix
    Skip the fix phase. With -IncludeConformance this runs audit then
    conformance; otherwise it runs only the audit.

.PARAMETER IncludeConformance
    Also run Invoke-LumaConformance.ps1 as a third phase, after the fix. Opt-in
    rather than a -Skip* switch: a full run is one agent session per tracked file
    (900+ for C++ alone), so it stays off by default. Scope it with
    -ConformanceArgs (for example -ConformanceArgs '-Target', 'cpp').

.PARAMETER AuditArgs
    Extra arguments forwarded verbatim to Invoke-LumaAudit.ps1
    (e.g. -AuditArgs '-Scope', 'core/runtime/vm/').

.PARAMETER FixArgs
    Extra arguments forwarded verbatim to Invoke-LumaFix.ps1
    (e.g. -FixArgs '-NoCommit', '-ContinueOnFailure').

.PARAMETER ConformanceArgs
    Extra arguments forwarded verbatim to Invoke-LumaConformance.ps1 when
    -IncludeConformance is set (e.g. -ConformanceArgs '-Target', 'cpp',
    '-MaxFiles', '5').

.PARAMETER DryRun
    Print the commands each runner would execute without invoking the agent,
    building, or committing. Forwarded to every runner.

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaAll.ps1 -DryRun

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaAll.ps1

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaAll.ps1 -FixArgs '-RevertOnFailure'

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaAll.ps1 -IncludeConformance -ConformanceArgs '-Target', 'cpp'
#>

[CmdletBinding()]
param(
    [ValidateSet('copilot', 'claude')]
    [string]$Agent = 'copilot',

    [string]$Model = 'claude-opus-4.8',

    [string]$Effort = 'max',

    [switch]$SkipAudit,

    [switch]$SkipFix,

    [switch]$IncludeConformance,

    [string[]]$AuditArgs = @(),

    [string[]]$FixArgs = @(),

    [string[]]$ConformanceArgs = @(),

    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Native command failures surface via $LASTEXITCODE, not thrown errors (matches
# the sibling runners, which note the same user-profile caveat).
$PSNativeCommandUseErrorActionPreference = $false

Import-Module -Name (Join-Path -Path $PSScriptRoot -ChildPath 'LumaPipeline.psm1') -Force

if ($SkipAudit -and $SkipFix -and -not $IncludeConformance) {
    throw 'Nothing to do: with both -SkipAudit and -SkipFix you must also pass -IncludeConformance.'
}

$AuditScript = Join-Path -Path $PSScriptRoot -ChildPath 'Invoke-LumaAudit.ps1'
$FixScript = Join-Path -Path $PSScriptRoot -ChildPath 'Invoke-LumaFix.ps1'
$ConformanceScript = Join-Path -Path $PSScriptRoot -ChildPath 'Invoke-LumaConformance.ps1'
foreach ($Script in @($AuditScript, $FixScript, $ConformanceScript)) {
    if (-not (Test-Path -LiteralPath $Script)) {
        throw "Runner not found: $Script"
    }
}

# Launch each runner with the same PowerShell host that is running this script,
# falling back to whatever pwsh/powershell is on PATH.
$PwshExe = (Get-Process -Id $PID).Path
if (-not $PwshExe) { $PwshExe = (Get-Command -Name 'pwsh' -ErrorAction SilentlyContinue).Source }
if (-not $PwshExe) { $PwshExe = (Get-Command -Name 'powershell' -ErrorAction SilentlyContinue).Source }
if (-not $PwshExe) { throw 'Could not locate a PowerShell executable to launch the runners.' }

# Common agent flags forwarded to every runner.
$CommonArgs = @('-Agent', $Agent)
if ($Model) { $CommonArgs += @('-Model', $Model) }
if ($Effort) { $CommonArgs += @('-Effort', $Effort) }
if ($DryRun) { $CommonArgs += '-DryRun' }

function Invoke-Runner {
    [CmdletBinding()]
    [OutputType([int])]
    param(
        [Parameter(Mandatory)]
        [string]$ScriptPath,

        [string[]]$ExtraArgs = @()
    )

    $Argv = @('-NoProfile', '-File', $ScriptPath) + $CommonArgs + $ExtraArgs
    # Stream the child's output to the host rather than letting it become this
    # function's return value; return only the child's exit code.
    & $PwshExe @Argv | Out-Host
    return [int]$LASTEXITCODE
}

# Number the banners over exactly the phases that will run, so a skipped phase or
# an added conformance phase keeps the "Phase i/N" labels accurate.
$EnabledPhases = [System.Collections.Generic.List[string]]::new()
if (-not $SkipAudit) { $EnabledPhases.Add('Audit') }
if (-not $SkipFix) { $EnabledPhases.Add('Fix') }
if ($IncludeConformance) { $EnabledPhases.Add('Conformance') }
$TotalPhases = $EnabledPhases.Count
$PhaseIndex = 0

$ModelLabel = if ($Model) { $Model } else { '(agent default)' }
$EffortLabel = if ($Effort) { $Effort } else { '(agent default)' }
Write-PhaseBanner -Title 'Luma audit + fix pipeline' `
    -Subtitle "Agent: $Agent   Model: $ModelLabel   Effort: $EffortLabel"
if ($DryRun) {
    Write-Host '  Mode : DRY RUN (nothing invoked, built, or committed)' -ForegroundColor Yellow
}

# Tracks the exit code of the last phase actually run, which becomes this
# orchestrator's exit code.
$FinalExit = 0

if (-not $SkipAudit) {
    $PhaseIndex++
    Write-PhaseBanner -Title "Phase $PhaseIndex/$TotalPhases - Audit (read-only)"
    $AuditExit = Invoke-Runner -ScriptPath $AuditScript -ExtraArgs $AuditArgs
    if ($AuditExit -ne 0) {
        Write-Warning "Audit runner exited with code $AuditExit; skipping the remaining phases."
        exit $AuditExit
    }
}
else {
    Write-Host 'Skipping audit phase (-SkipAudit).' -ForegroundColor DarkGray
}

if (-not $SkipFix) {
    $PhaseIndex++
    Write-PhaseBanner -Title "Phase $PhaseIndex/$TotalPhases - Fix (mutating, gated)"
    $FixExit = Invoke-Runner -ScriptPath $FixScript -ExtraArgs $FixArgs
    $FinalExit = $FixExit
    if ($FixExit -ne 0) {
        Write-Warning "Fix runner exited with code $FixExit."
        if ($IncludeConformance) {
            Write-Warning 'Skipping the conformance phase because the fix phase failed.'
        }
        Write-PhaseBanner -Title 'Pipeline complete'
        exit $FixExit
    }
}
else {
    Write-Host 'Skipping fix phase (-SkipFix).' -ForegroundColor DarkGray
}

if ($IncludeConformance) {
    $PhaseIndex++
    Write-PhaseBanner -Title "Phase $PhaseIndex/$TotalPhases - Conformance (mutating, gated)"
    $ConformanceExit = Invoke-Runner -ScriptPath $ConformanceScript -ExtraArgs $ConformanceArgs
    $FinalExit = $ConformanceExit
    if ($ConformanceExit -ne 0) {
        Write-Warning "Conformance runner exited with code $ConformanceExit."
    }
}
else {
    Write-Host 'Skipping conformance phase (pass -IncludeConformance to enable).' -ForegroundColor DarkGray
}

Write-PhaseBanner -Title 'Pipeline complete'
exit $FinalExit
