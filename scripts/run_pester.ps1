#Requires -Version 7.2

<#
.SYNOPSIS
    Run the Pester tests for the first-party PowerShell scripts.

.DESCRIPTION
    Discovers every tracked *.Tests.ps1 file, runs it with the pinned Pester
    version, and exits non-zero when any test fails. Used by the PowerShell CI
    gate (.github/workflows/ci-powershell.yml).

.EXAMPLE
    pwsh -File scripts/run_pester.ps1
#>

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Keep in sync with the Pester pin in .devcontainer/devcontainer.json.
$RequiredVersion = '6.2.0'
$RepoRoot = Split-Path -Parent $PSScriptRoot

# Run from the repository root so `git ls-files` yields repo-relative paths.
# This script runs in its own process, so the location change does not leak to
# the caller.
Set-Location -Path $RepoRoot

# Install the pinned Pester only when it is not already available, so CI (a
# fresh runner) installs it while local runs reuse an existing copy. The
# Windows-bundled Pester 3.x is never picked up because the version is exact.
if (-not (Get-Module -ListAvailable -Name Pester |
            Where-Object { $_.Version -eq $RequiredVersion })) {
    Install-Module -Name Pester -RequiredVersion $RequiredVersion `
        -Scope CurrentUser -Force -SkipPublisherCheck
}
Import-Module Pester -RequiredVersion $RequiredVersion

# Include untracked (but not ignored) files so a new test runs before it is added.
$Files = @(git ls-files --cached --others --exclude-standard '*.Tests.ps1')
if ($Files.Count -eq 0) {
    Write-Host 'No Pester tests to run.'
    exit 0
}

Write-Host "Running $($Files.Count) Pester test file(s)..."
$Configuration = New-PesterConfiguration
$Configuration.Run.Path = $Files
$Configuration.Run.PassThru = $true
$Configuration.Output.Verbosity = 'Detailed'
$Result = Invoke-Pester -Configuration $Configuration

if ($Result.FailedCount -gt 0 -or $Result.Result -ne 'Passed') {
    throw "Pester: $($Result.FailedCount) test(s) failed."
}

Write-Host "Pester: all $($Result.PassedCount) test(s) passed."
