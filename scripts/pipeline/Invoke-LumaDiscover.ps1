#Requires -Version 7.0

<#
.SYNOPSIS
    Run the Luma read-only language-evolution discovery via an agent CLI.

.DESCRIPTION
    Executes the new-requirements prompt in the agent's plan mode. That prompt
    studies other languages, libraries, and GUI frameworks and produces a
    prioritised, routed list of candidate additions - language features, stdlib
    modules, types, and functions - that fit Luma's philosophy. Nothing is
    built, run, or modified: this is the safe, triage-first half of the
    evolution pipeline (the discover -> evolve counterpart of audit -> fix).

    The report and per-phase log are written under a timestamped run directory:

        <ArtifactRoot>/discover-<timestamp>/reports/new-requirements.md
        <ArtifactRoot>/discover-<timestamp>/logs/

    Triage the report, then hand its candidates to Invoke-LumaEvolve.ps1. Each
    candidate carries a Handoff kind (function|type|module|feature) that the
    evolve runner routes to the matching implementer prompt.

.PARAMETER Focus
    Optional focus area passed to the prompt (e.g. 'string handling',
    'date/time', 'functional iteration'). Omit for a broad survey.

.PARAMETER ArtifactRoot
    Root directory for run artifacts. Defaults to <repo>/pipeline-artifacts.

.PARAMETER Agent
    Agent CLI backend: 'copilot' (default) or 'claude'.

.PARAMETER Model
    Optional model for the chosen agent (e.g. 'gpt-5.4' for copilot, 'sonnet'
    for claude). Omit to let the agent choose.

.PARAMETER Effort
    Optional reasoning effort. For the copilot agent this is validated against
    low|medium|high|xhigh|max; for claude the value is passed through and
    validated by the CLI itself.

.PARAMETER DryRun
    Print the agent command that would run, without invoking anything.

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaDiscover.ps1 -DryRun

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaDiscover.ps1 -Focus 'string handling'

.EXAMPLE
    pwsh -File scripts/pipeline/Invoke-LumaDiscover.ps1 -Agent claude -Effort max
#>

[CmdletBinding()]
param(
    [string]$Focus,

    [string]$ArtifactRoot,

    [ValidateSet('copilot', 'claude')]
    [string]$Agent = 'copilot',

    [string]$Model,

    [string]$Effort,

    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Report native command failures via $LASTEXITCODE, not thrown errors, regardless
# of a user profile that flips this on (see the note in LumaPipeline.psm1).
$PSNativeCommandUseErrorActionPreference = $false

Import-Module -Name (Join-Path -Path $PSScriptRoot -ChildPath 'LumaPipeline.psm1') -Force

$RepoRoot = Get-LumaRepoRoot

# The single read-only discovery prompt. Kept as a constant (not a phase table)
# because discovery is one prompt; the routed candidates it produces become the
# evolve runner's many phases.
$DiscoverId = 'new-requirements'
$DiscoverName = 'Language-evolution discovery'
$DiscoverPrompt = 'new-requirements.prompt.md'

# Validate -Effort against Copilot's documented set only. Claude accepts a
# different set, so for claude we defer to the CLI's own validation rather than
# maintain a second allow-list.
$CopilotEffortValues = @('low', 'medium', 'high', 'xhigh', 'max')
if ($Effort -and $Agent -eq 'copilot' -and $Effort -notin $CopilotEffortValues) {
    throw "Invalid -Effort '$Effort' for copilot (use $($CopilotEffortValues -join ', '))."
}

# Prepare the run directory.
if (-not $ArtifactRoot) {
    $ArtifactRoot = Join-Path -Path $RepoRoot -ChildPath 'pipeline-artifacts'
}
# Make the artifact root absolute so the report and log paths resolve to the
# directory created here regardless of the caller's working directory:
# Invoke-AgentPhase writes the report after Push-Location $RepoRoot, so a
# relative path would otherwise resolve against the repo root instead.
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot, (Get-Location).ProviderPath)
$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunDir = Join-Path -Path $ArtifactRoot -ChildPath "discover-$Timestamp"
$ReportDir = Join-Path -Path $RunDir -ChildPath 'reports'
$LogDir = Join-Path -Path $RunDir -ChildPath 'logs'

if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
}

Write-PhaseBanner -Title 'Luma discover pipeline (read-only)' -Subtitle "Repo: $RepoRoot"
Write-Host "  Agent  : $Agent"
Write-Host "  Focus  : $(if ($Focus) { $Focus } else { '(broad survey)' })"
Write-Host "  Output : $RunDir"
if ($DryRun) { Write-Host '  Mode   : DRY RUN (no agent invoked)' -ForegroundColor Yellow }

$ReportFile = Join-Path -Path $ReportDir -ChildPath "$DiscoverId.md"

Write-PhaseBanner -Title $DiscoverName -Subtitle "Prompt: $DiscoverPrompt"

$FocusClause = if ($Focus) {
    "Focus the survey on: $Focus."
}
else {
    'Survey broadly across the standard library and language surface; do not narrow to a single area.'
}

$Lines = [System.Collections.Generic.List[string]]::new()
$Lines.Add('You are running the Luma language-evolution discovery: candidate additions that fit the language.')
$Lines.Add('')
$Lines.Add("Follow the workflow in the prompt file `".github/prompts/$DiscoverPrompt`" exactly - its ground rules, research method, philosophy-fit criteria, prioritisation, exclusions, and Output Format sections all apply.")
$Lines.Add('')
$Lines.Add($FocusClause)
$Lines.Add('')
$Lines.Add('This is a STRICTLY READ-ONLY analysis: do not modify, create, build, run, format, or stage any files. Verify every claim about the current language and stdlib against the actual sources before reporting it.')
$Lines.Add('')
$Lines.Add('Produce the complete, prioritised report exactly as that prompt''s "Output Format" section specifies, including a Handoff line for each candidate naming its kind (function, type, module, or feature). Output only the report as your final message, with no preamble and no closing chatter, and do not wrap the report or any table within it in a Markdown code fence, so it can be saved verbatim as a Markdown file.')
$Instruction = $Lines -join "`n"

$Results = [System.Collections.Generic.List[psobject]]::new()

try {
    $Outcome = Invoke-AgentPhase -Instruction $Instruction -Mode 'plan' -Agent $Agent -RepoRoot $RepoRoot `
        -OutputFile $ReportFile -LogDir $LogDir -Model $Model -Effort $Effort -DryRun:$DryRun

    $Results.Add([pscustomobject]@{
            Order  = 1
            Id     = $DiscoverId
            Name   = $DiscoverName
            Status = if ($Outcome.Success) { 'ok' } else { 'failed' }
            Report = $ReportFile
        })
}
catch {
    Write-Warning "Discovery failed: $($_.Exception.Message)"
    $Results.Add([pscustomobject]@{
            Order  = 1
            Id     = $DiscoverId
            Name   = $DiscoverName
            Status = 'error'
            Report = $ReportFile
        })
}

# Write an index of the run.
if (-not $DryRun) {
    $IndexLines = [System.Collections.Generic.List[string]]::new()
    $IndexLines.Add('# Luma discover run')
    $IndexLines.Add('')
    $IndexLines.Add("- Generated: $Timestamp")
    $IndexLines.Add("- Repository: $RepoRoot")
    if ($Focus) { $IndexLines.Add("- Focus: $Focus") }
    $IndexLines.Add('')
    $IndexLines.Add('| Order | Phase | Status | Report |')
    $IndexLines.Add('| ----- | ----- | ------ | ------ |')
    foreach ($Row in $Results) {
        $IndexLines.Add("| $($Row.Order) | $($Row.Name) | $($Row.Status) | [$($Row.Id).md](reports/$($Row.Id).md) |")
    }
    $IndexLines.Add('')
    $IndexLines.Add('Triage this report, then run `Invoke-LumaEvolve.ps1` to implement chosen candidates.')
    $IndexPath = Join-Path -Path $RunDir -ChildPath 'INDEX.md'
    Set-Content -LiteralPath $IndexPath -Value ($IndexLines -join "`n") -Encoding utf8
    Write-Host ''
    Write-Host "Index written: $IndexPath" -ForegroundColor Green
}

Write-PhaseBanner -Title 'Discover summary'
$Results | Format-Table -AutoSize Order, Id, Status
Write-Output $Results
