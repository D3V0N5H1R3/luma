#Requires -Version 7.0

<#
.SYNOPSIS
    Run the Luma read-only audit pipeline via an agent CLI (Copilot or Claude).

.DESCRIPTION
    Executes each read-only audit prompt (consistency check, bug hunts, code
    review, UX audit, refactor audit, performance audit) in the agent's plan
    mode and captures the ranked report each prompt produces. Nothing is built,
    run, or modified - this is the safe, triage-first half of the pipeline.

    Reports and per-phase logs are written under a timestamped run directory:

        <ArtifactRoot>/audit-<timestamp>/reports/<phase-id>.md
        <ArtifactRoot>/audit-<timestamp>/logs/

    Triage the reports, then feed them to Invoke-LumaFix.ps1.

.PARAMETER Scope
    Optional scope passed to every phase (e.g. 'core/runtime/vm/'). Overrides
    each phase's default scope. Omit to use the prompt-defined default scope.

.PARAMETER Phase
    One or more phase filters (Id or Name substring, case-insensitive).
    Omit to run every audit phase in order.

.PARAMETER ArtifactRoot
    Root directory for run artifacts. Defaults to <repo>/pipeline-artifacts.

.PARAMETER Agent
    Agent CLI backend: 'copilot' (default) or 'claude'.

.PARAMETER Model
    Optional model for the chosen agent (e.g. 'gpt-5.4' for copilot, 'sonnet'
    for claude). Omit to let the agent choose.

.PARAMETER Effort
    Optional reasoning effort. For the copilot agent this is validated against
    low|medium|high|xhigh|max; for claude the value is passed
    through and validated by the CLI itself.

.PARAMETER List
    List the audit phases in order and exit without running anything.

.PARAMETER DryRun
    Print the agent command each phase would run, without invoking anything.

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaAudit.ps1 -List

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaAudit.ps1 -DryRun

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaAudit.ps1 -Phase bug-search -Scope core/runtime/vm/
#>

[CmdletBinding()]
param(
    [string]$Scope,

    [string[]]$Phase,

    [string]$ArtifactRoot,

    [ValidateSet('copilot', 'claude')]
    [string]$Agent = 'copilot',

    [string]$Model,

    [string]$Effort,

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
$AllPhases = Get-AuditPhase

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
            Order  = $Index
            Id     = $_.Id
            Name   = $_.Name
            Prompt = $_.Prompt
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
        throw "No audit phase matched: $($Phase -join ', '). Use -List to see the available phases."
    }
}
else {
    $Selected = $AllPhases
}

# Fall back to 'auto' when the CLI cannot select the requested model, so a
# mis-entitled or unknown model name never aborts every phase.
$Model = Resolve-CopilotModel -Agent $Agent -Model $Model -DryRun:$DryRun -RepoRoot $RepoRoot

# Prepare the run directory.
if (-not $ArtifactRoot) {
    $ArtifactRoot = Join-Path -Path $RepoRoot -ChildPath 'pipeline-artifacts'
}
# Make the artifact root absolute so the report and log paths resolve to the
# directory created here regardless of the caller's working directory:
# Invoke-AgentPhase writes each report after Push-Location $RepoRoot, so a
# relative path would otherwise resolve against the repo root instead.
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot, (Get-Location).ProviderPath)
$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunDir = Join-Path -Path $ArtifactRoot -ChildPath "audit-$Timestamp"
$ReportDir = Join-Path -Path $RunDir -ChildPath 'reports'
$LogDir = Join-Path -Path $RunDir -ChildPath 'logs'

if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
}

Write-PhaseBanner -Title 'Luma audit pipeline (read-only)' -Subtitle "Repo: $RepoRoot"
Write-Host "  Phases : $($Selected.Count)"
Write-Host "  Agent  : $Agent"
Write-Host "  Output : $RunDir"
if ($DryRun) { Write-Host '  Mode   : DRY RUN (no agent invoked)' -ForegroundColor Yellow }

$Results = [System.Collections.Generic.List[psobject]]::new()
$Position = 0

foreach ($Current in $Selected) {
    $Position++
    $EffectiveScope = if ($PSBoundParameters.ContainsKey('Scope') -and $Scope) { $Scope } else { $Current.DefaultScope }
    $ReportFile = Join-Path -Path $ReportDir -ChildPath "$($Current.Id).md"

    Write-PhaseBanner -Title "[$Position/$($Selected.Count)] $($Current.Name)" -Subtitle "Prompt: $($Current.Prompt)"

    $ScopeClause = if ($EffectiveScope) {
        "Restrict the analysis to the following scope: $EffectiveScope."
    }
    else {
        'Use the default scope defined by the prompt.'
    }

    $Lines = [System.Collections.Generic.List[string]]::new()
    $Lines.Add("You are running the Luma project audit: `"$($Current.Name)`".")
    $Lines.Add('')
    $Lines.Add("Follow the workflow in the prompt file `".github/prompts/$($Current.Prompt)`" exactly - its ground rules, what-to-look-for, prioritisation, exclusions, and Output Format sections all apply.")
    $Lines.Add('')
    $Lines.Add($ScopeClause)
    $Lines.Add('')
    $Lines.Add('This is a STRICTLY READ-ONLY analysis: do not modify, create, build, run, format, or stage any files. Read every source you cite and verify each location before reporting it.')
    $Lines.Add('')
    $Lines.Add('Produce the complete, prioritised report exactly as that prompt''s "Output Format" section specifies. Output only the report as your final message, with no preamble and no closing chatter, and do not wrap the report or any table within it in a Markdown code fence, so it can be saved verbatim as a Markdown file.')
    $Instruction = $Lines -join "`n"

    try {
        $Outcome = Invoke-AgentPhase -Instruction $Instruction -Mode 'plan' -Agent $Agent -RepoRoot $RepoRoot `
            -OutputFile $ReportFile -LogDir $LogDir -Model $Model -Effort $Effort -DryRun:$DryRun

        $Results.Add([pscustomobject]@{
                Order   = $Position
                Id      = $Current.Id
                Name    = $Current.Name
                Status  = if ($Outcome.Success) { 'ok' } else { 'failed' }
                Report  = $ReportFile
            })
    }
    catch {
        Write-Warning "Phase '$($Current.Id)' failed: $($_.Exception.Message)"
        $Results.Add([pscustomobject]@{
                Order  = $Position
                Id     = $Current.Id
                Name   = $Current.Name
                Status = 'error'
                Report = $ReportFile
            })
    }
}

# Write an index of the run.
if (-not $DryRun) {
    $IndexLines = [System.Collections.Generic.List[string]]::new()
    $IndexLines.Add('# Luma audit run')
    $IndexLines.Add('')
    $IndexLines.Add("- Generated: $Timestamp")
    $IndexLines.Add("- Repository: $RepoRoot")
    $IndexLines.Add('')
    $IndexLines.Add('| Order | Phase | Status | Report |')
    $IndexLines.Add('| ----- | ----- | ------ | ------ |')
    foreach ($Row in $Results) {
        $IndexLines.Add("| $($Row.Order) | $($Row.Name) | $($Row.Status) | [$($Row.Id).md](reports/$($Row.Id).md) |")
    }
    $IndexLines.Add('')
    $IndexLines.Add('Triage these reports, then run `Invoke-LumaFix.ps1` to apply fixes.')
    $IndexPath = Join-Path -Path $RunDir -ChildPath 'INDEX.md'
    Set-Content -LiteralPath $IndexPath -Value ($IndexLines -join "`n") -Encoding utf8
    Write-Host ''
    Write-Host "Index written: $IndexPath" -ForegroundColor Green
}

Write-PhaseBanner -Title 'Audit summary'
$Results | Format-Table -AutoSize Order, Id, Status
Write-Output $Results

$HasFailures = ($Results | Where-Object { $_.Status -eq 'failed' -or $_.Status -eq 'error' }).Count -gt 0
if ($HasFailures) {
    Write-Warning 'Completed with one or more failed phases.'
    exit 1
}
Write-Host 'All selected phases completed.' -ForegroundColor Green
exit 0
