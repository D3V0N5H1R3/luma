# ─────────────────────────────────────────────
# Luma — PSScriptAnalyzer Configuration
#
# Static-analysis settings for PowerShell scripts. Applied by the
# PowerShell CI gate (.github/workflows/ci-powershell.yml) and
# auto-discovered by the VS Code PowerShell extension.
#
# PSScriptAnalyzer's default rule set always runs (minus the documented
# exclusion below). `Rules` additionally switches on the formatting rules it
# ships disabled that enforce the project's style guide
# (instructions/powershell.instructions.md); the end of that block lists the
# ones deliberately left off.
# ─────────────────────────────────────────────

@{
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

    Rules        = @{
        # §3 — 4-space indentation, no tabs. Continuation lines of a pipeline
        # are indented one level, matching the existing scripts.
        PSUseConsistentIndentation                = @{
            Enable              = $true
            Kind                = 'space'
            IndentationSize     = 4
            PipelineIndentation = 'IncreaseIndentationForFirstPipeline'
        }

        # §3 — opening brace on the same line as its statement.
        PSPlaceOpenBrace                          = @{
            Enable             = $true
            OnSameLine         = $true
            NewLineAfter       = $true
            IgnoreOneLineBlock = $true
        }

        # §3 — closing brace on its own line (`}` then `else {`/`catch {`).
        PSPlaceCloseBrace                         = @{
            Enable             = $true
            NewLineAfter       = $true
            IgnoreOneLineBlock = $true
            NoEmptyLineBefore  = $false
        }

        # §3 — no trailing semicolons. (No built-in rule detects mid-line
        # `a; b` statement separators.)
        PSAvoidSemicolonsAsLineTerminators        = @{
            Enable = $true
        }

        # §3 / §11 — single quotes for literal strings.
        PSAvoidUsingDoubleQuotesForConstantString = @{
            Enable = $true
        }

        # §3 — consistent spacing around braces, parentheses, pipes, and
        # parameters. CheckOperator and CheckSeparator stay off because the
        # guide's own examples (§6, §10) and the pipeline scripts align
        # hashtable `=` signs and tabular `;`-separated rows into columns.
        PSUseConsistentWhitespace                 = @{
            Enable                          = $true
            CheckInnerBrace                 = $true
            CheckOpenBrace                  = $true
            CheckOpenParen                  = $true
            CheckPipe                       = $true
            CheckPipeForRedundantWhitespace = $true
            CheckParameter                  = $true
            CheckOperator                   = $false
            CheckSeparator                  = $false
        }

        # Not enabled, deliberately:
        # - PSAvoidLongLines: §3 sets a 100-character limit, but ~100 existing
        #   lines exceed it; enable once the scripts are rewrapped.
        # - PSUseCorrectCasing: misreports Pester parameters (e.g. `It -Name`)
        #   as wrongly cased.
    }
}
