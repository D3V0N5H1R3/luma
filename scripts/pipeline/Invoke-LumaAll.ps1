#Requires -Version 7.0

<#
.SYNOPSIS
    Run the Luma audit and then the fix pipeline back-to-back through one agent
    CLI, defaulting to the GitHub Copilot CLI driving Claude Opus 4.6 at medium
    reasoning effort.

.DESCRIPTION
    A thin orchestrator that invokes the pipeline entry points in order:

        1. Invoke-LumaAudit.ps1  (read-only, plan mode) - ranked reports.
        2. Invoke-LumaFix.ps1    (mutating, agent mode) - gated fixes.

    Each runner is launched as its own PowerShell child process, exactly as a
    user would invoke it, so their exit codes and console output are preserved
    and their strict-mode/scope settings never leak between phases. The audit
    runs first; if it exits non-zero (a usage or setup failure) the fix phase
    is skipped.

    The two agent knobs default to the requested configuration - the Copilot
    CLI backend, the 'claude-opus-4.6' model (Claude Opus 4.6, selectable through
    the Copilot CLI), and 'medium' reasoning effort - and are forwarded to every
    runner. Preview everything first with -DryRun.

.PARAMETER Agent
    Agent CLI backend forwarded to both runners: 'copilot' (default) or 'claude'.

.PARAMETER Model
    Model passed to the agent's --model flag. Defaults to 'claude-opus-4.6'.
    Pass an empty string to let the agent choose its own default.

.PARAMETER Effort
    Reasoning effort passed to the agent's --effort flag. Defaults to 'medium'. For
    the copilot backend each runner validates this against
    low|medium|high|xhigh|max; pass an empty string to omit it.

.PARAMETER SkipAudit
    Skip the audit phase and run only the fix phase.

.PARAMETER SkipFix
    Skip the fix phase and run only the audit.

.PARAMETER AuditArgs
    Extra arguments forwarded verbatim to Invoke-LumaAudit.ps1
    (e.g. -AuditArgs '-Scope', 'core/runtime/vm/').

.PARAMETER FixArgs
    Extra arguments forwarded verbatim to Invoke-LumaFix.ps1
    (e.g. -FixArgs '-NoCommit', '-ContinueOnFailure').

.PARAMETER DryRun
    Print the commands each runner would execute without invoking the agent,
    building, or committing. Forwarded to every runner.

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaAll.ps1 -DryRun

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaAll.ps1

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaAll.ps1 -FixArgs '-RevertOnFailure'
#>

[CmdletBinding()]
param(
    [ValidateSet('copilot', 'claude')]
    [string]$Agent = 'copilot',

    [string]$Model = 'claude-opus-4.6',

    [string]$Effort = 'medium',

    [switch]$SkipAudit,

    [switch]$SkipFix,

    [string[]]$AuditArgs = @(),

    [string[]]$FixArgs = @(),

    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Native command failures surface via $LASTEXITCODE, not thrown errors (matches
# the sibling runners, which note the same user-profile caveat).
$PSNativeCommandUseErrorActionPreference = $false

Import-Module -Name (Join-Path -Path $PSScriptRoot -ChildPath 'LumaPipeline.psm1') -Force

if ($SkipAudit -and $SkipFix) {
    throw 'Nothing to do: both -SkipAudit and -SkipFix are set.'
}

$AuditScript = Join-Path -Path $PSScriptRoot -ChildPath 'Invoke-LumaAudit.ps1'
$FixScript = Join-Path -Path $PSScriptRoot -ChildPath 'Invoke-LumaFix.ps1'
foreach ($Script in @($AuditScript, $FixScript)) {
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

# Number the banners over exactly the phases that will run.
$EnabledPhases = [System.Collections.Generic.List[string]]::new()
if (-not $SkipAudit) { $EnabledPhases.Add('Audit') }
if (-not $SkipFix) { $EnabledPhases.Add('Fix') }
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
        Write-Warning "Audit runner exited with code $AuditExit; skipping the fix phase."
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
    }
}
else {
    Write-Host 'Skipping fix phase (-SkipFix).' -ForegroundColor DarkGray
}

Write-PhaseBanner -Title 'Pipeline complete'
exit $FinalExit
