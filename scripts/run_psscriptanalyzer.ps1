# Run PSScriptAnalyzer over every first-party PowerShell script.
#
# Discovers all tracked *.ps1 / *.psm1 files, analyzes them with the repo's
# PSScriptAnalyzerSettings.psd1, prints a findings table, and exits non-zero
# when any blocking issue (ParseError, Error, or Warning) is found.
#
# Used by the PowerShell CI gate (.github/workflows/ci-powershell.yml) and
# runnable by hand from anywhere:
#
#     pwsh -File scripts/run_psscriptanalyzer.ps1

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RequiredVersion = '1.25.0'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$SettingsPath = Join-Path -Path $RepoRoot -ChildPath 'PSScriptAnalyzerSettings.psd1'

# Run from the repository root so `git ls-files` yields repo-relative paths
# that Invoke-ScriptAnalyzer can resolve. This script runs in its own process,
# so the location change does not leak to the caller.
Set-Location -Path $RepoRoot

# Install the pinned analyzer only when it is not already available, so CI
# (a fresh runner) installs it while local runs reuse an existing copy.
if (-not (Get-Module -ListAvailable -Name PSScriptAnalyzer |
            Where-Object { $_.Version -eq $RequiredVersion })) {
    Install-Module -Name PSScriptAnalyzer -RequiredVersion $RequiredVersion `
        -Scope CurrentUser -Force
}
Import-Module PSScriptAnalyzer -RequiredVersion $RequiredVersion

$Files = @(git ls-files '*.ps1' '*.psm1')
if ($Files.Count -eq 0) {
    Write-Host 'No PowerShell scripts to analyze.'
    exit 0
}

Write-Host "Analyzing $($Files.Count) PowerShell script(s)..."
# Pipe every file through a single Invoke-ScriptAnalyzer call so the rule engine
# and settings file are initialised once (in the cmdlet's BeginProcessing) and
# amortised across all files, rather than re-initialised per file. -Path binds a
# scalar [string], so the files are supplied via the pipeline (each is bound in
# ProcessRecord) rather than as an array argument, which would fail to convert.
$Results = $Files | Invoke-ScriptAnalyzer -Settings $SettingsPath

if ($Results) {
    $Results | Format-Table -AutoSize RuleName, Severity, ScriptName, Line, Message
}

$Blocking = @($Results | Where-Object { $_.Severity -in 'ParseError', 'Error', 'Warning' })
if ($Blocking.Count -gt 0) {
    throw "PSScriptAnalyzer found $($Blocking.Count) blocking issue(s)."
}

Write-Host 'PSScriptAnalyzer: no blocking issues found.'
