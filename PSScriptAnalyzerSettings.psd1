# ─────────────────────────────────────────────
# Luma — PSScriptAnalyzer Configuration
#
# Static-analysis settings for PowerShell scripts. Applied by the
# PowerShell CI gate (.github/workflows/ci-powershell.yml) and
# auto-discovered by the VS Code PowerShell extension.
#
# The complete set of built-in rules runs, minus the documented
# exclusion below.
# ─────────────────────────────────────────────

@{
    # Run every built-in rule, then subtract the exclusions below.
    IncludeDefaultRules = $true

    ExcludeRules = @(
        # PSAvoidUsingWriteHost — disabled intentionally.
        #
        # The project's PowerShell style guide
        # (instructions/powershell.instructions.md) explicitly permits
        # Write-Host for user-facing interactive messages, while reserving
        # Write-Output for data and Write-Verbose/Write-Information for
        # diagnostics. The first-party scripts use Write-Host solely for
        # progress messages a user expects to see when running them
        # interactively, which is the sanctioned use — so this rule would
        # only produce false positives.
        'PSAvoidUsingWriteHost'
    )
}
