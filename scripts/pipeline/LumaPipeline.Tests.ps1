#Requires -Version 7.2

<#
.SYNOPSIS
    Pester tests for LumaPipeline.psm1.

.DESCRIPTION
    Pins the result contracts of the module's gate and agent helpers. Each one
    runs native tools (cmake, ctest, python, the agent CLI) whose stdout, if left
    uncaptured, joins the function's return value: a failed gate then comes back
    as a non-empty - and therefore truthy - array, and an agent outcome as an
    array whose .Success throws under StrictMode. The tests drive the helpers with
    fake tools that print output and exit with a chosen code, then assert the
    exact type and value returned.

    Run with: pwsh -File scripts/run_pester.ps1
#>

BeforeAll {
    Import-Module -Name (Join-Path -Path $PSScriptRoot -ChildPath 'LumaPipeline.psm1') -Force

    function New-FakeTool {
        <#
        .SYNOPSIS
            Write an executable stand-in that prints lines and exits with a code.
        .DESCRIPTION
            Creates a .cmd shim on Windows and a /bin/sh script elsewhere in the
            fake-tool directory, which the tests put first on PATH. Output lines
            must be plain text: they are emitted verbatim by cmd `echo` or a
            single-quoted sh `printf`, so avoid shell metacharacters and quotes.
        #>
        [CmdletBinding(SupportsShouldProcess)]
        param(
            [Parameter(Mandatory)]
            [string]$Name,

            [int]$ExitCode = 0,

            [string[]]$OutputLine = @(),

            [switch]$ReadStdin,

            [ValidateRange(0, 300)]
            [int]$SleepSeconds = 0
        )

        $Body = [System.Collections.Generic.List[string]]::new()
        if ($IsWindows) {
            $Body.Add('@echo off')
            if ($ReadStdin) {
                $Body.Add('more >nul')
            }
            if ($SleepSeconds -gt 0) {
                # cmd has no portable sleep that works with redirected stdin; ping waits ~1s per echo.
                $Body.Add("ping -n $($SleepSeconds + 1) 127.0.0.1 >nul")
            }
            foreach ($Line in $OutputLine) {
                $Body.Add("echo $Line")
            }
            $Body.Add("exit /b $ExitCode")
            $Path = Join-Path -Path $script:ToolDir -ChildPath "$Name.cmd"
        }
        else {
            $Body.Add('#!/bin/sh')
            if ($ReadStdin) {
                $Body.Add('cat >/dev/null')
            }
            if ($SleepSeconds -gt 0) {
                $Body.Add("sleep $SleepSeconds")
            }
            foreach ($Line in $OutputLine) {
                $Body.Add("printf '%s\n' '$Line'")
            }
            $Body.Add("exit $ExitCode")
            $Path = Join-Path -Path $script:ToolDir -ChildPath $Name
        }

        if (-not $PSCmdlet.ShouldProcess($Path, 'Write fake tool')) {
            return
        }
        Set-Content -LiteralPath $Path -Value $Body -Encoding ascii
        if (-not $IsWindows) {
            chmod +x $Path
        }
    }

    $script:ToolDir = Join-Path -Path $TestDrive -ChildPath 'bin'
    $script:RepoRoot = Join-Path -Path $TestDrive -ChildPath 'repo'
    New-Item -ItemType Directory -Force -Path $script:ToolDir | Out-Null
    # An existing build/ directory makes Test-BuildAndTest skip the configure step.
    New-Item -ItemType Directory -Force -Path (Join-Path -Path $script:RepoRoot -ChildPath 'build') | Out-Null

    $script:OriginalPath = $env:PATH
    $env:PATH = $script:ToolDir + [System.IO.Path]::PathSeparator + $env:PATH
}

AfterAll {
    $env:PATH = $script:OriginalPath
}

Describe 'Test-BuildAndTest' {
    It 'returns a bare $false when ctest fails after printing output' {
        New-FakeTool -Name 'cmake' -OutputLine 'cmake build output'
        New-FakeTool -Name 'ctest' -ExitCode 1 -OutputLine '1 test failed'

        $Result = Test-BuildAndTest -RepoRoot $script:RepoRoot -WarningAction SilentlyContinue

        $Result.GetType() | Should -Be ([bool])
        $Result | Should -BeFalse
    }

    It 'returns a bare $false when the build fails after printing output' {
        New-FakeTool -Name 'cmake' -ExitCode 2 -OutputLine 'compile error'
        New-FakeTool -Name 'ctest' -OutputLine 'all tests passed'

        $Result = Test-BuildAndTest -RepoRoot $script:RepoRoot -WarningAction SilentlyContinue

        $Result.GetType() | Should -Be ([bool])
        $Result | Should -BeFalse
    }

    It 'returns a bare $true when build and tests pass after printing output' {
        New-FakeTool -Name 'cmake' -OutputLine 'cmake build output'
        New-FakeTool -Name 'ctest' -OutputLine 'all tests passed'

        $Result = Test-BuildAndTest -RepoRoot $script:RepoRoot

        $Result.GetType() | Should -Be ([bool])
        $Result | Should -BeTrue
    }
}

Describe 'Test-LintAndFormat' {
    BeforeAll {
        # Keep the gate hermetic: no MSVC environment import or compile-database
        # configure, and a fake interpreter standing in for python.
        Mock -ModuleName 'LumaPipeline' -CommandName 'Import-MsvcEnvironment' -MockWith { $true }
        Mock -ModuleName 'LumaPipeline' -CommandName 'Initialize-CompileDatabase' -MockWith { $true }
        Mock -ModuleName 'LumaPipeline' -CommandName 'Get-PythonExecutable' -MockWith {
            (Get-Command -Name 'luma-fake-python' -CommandType Application).Source
        }
    }

    It 'returns a bare $false when lint.py fails after printing output' {
        New-FakeTool -Name 'luma-fake-python' -ExitCode 1 -OutputLine 'lint finding'

        $Result = Test-LintAndFormat -RepoRoot $script:RepoRoot -CheckOnly -WarningAction SilentlyContinue

        $Result.GetType() | Should -Be ([bool])
        $Result | Should -BeFalse
    }

    It 'returns a bare $true when format.py and lint.py pass after printing output' {
        New-FakeTool -Name 'luma-fake-python' -OutputLine 'all checks passed'

        $Result = Test-LintAndFormat -RepoRoot $script:RepoRoot

        $Result.GetType() | Should -Be ([bool])
        $Result | Should -BeTrue
    }
}

Describe 'Invoke-AgentPhase in agent mode' {
    BeforeAll {
        $script:OriginalCopilot = $env:LUMA_COPILOT
        $env:LUMA_COPILOT = 'luma-fake-copilot'
        $script:LogFile = Join-Path -Path $TestDrive -ChildPath 'agent.log'
    }

    AfterAll {
        $env:LUMA_COPILOT = $script:OriginalCopilot
    }

    It 'returns one outcome object and tees the transcript for a zero exit' {
        New-FakeTool -Name 'luma-fake-copilot' -ReadStdin -OutputLine 'agent transcript line'

        $Outcome = Invoke-AgentPhase -Instruction 'do the work' -Mode 'agent' -Agent 'copilot' `
            -RepoRoot $script:RepoRoot -LogFile $script:LogFile

        @($Outcome).Count | Should -Be 1
        $Outcome.Success | Should -BeTrue
        $Outcome.ExitCode | Should -Be 0
        Get-Content -LiteralPath $script:LogFile -Raw | Should -Match 'agent transcript line'
    }

    It 'reports the agent exit code on failure' {
        New-FakeTool -Name 'luma-fake-copilot' -ExitCode 3 -ReadStdin -OutputLine 'agent gave up'

        $Outcome = Invoke-AgentPhase -Instruction 'do the work' -Mode 'agent' -Agent 'copilot' `
            -RepoRoot $script:RepoRoot -LogFile $script:LogFile

        @($Outcome).Count | Should -Be 1
        $Outcome.Success | Should -BeFalse
        $Outcome.ExitCode | Should -Be 3
    }

    It 'fails a zero-exit phase whose transcript shows quota exhaustion' {
        New-FakeTool -Name 'luma-fake-copilot' -ReadStdin `
            -OutputLine 'You have exceeded your monthly quota'

        $Outcome = Invoke-AgentPhase -Instruction 'do the work' -Mode 'agent' -Agent 'copilot' `
            -RepoRoot $script:RepoRoot -LogFile $script:LogFile -WarningAction SilentlyContinue

        $Outcome.Success | Should -BeFalse
        $Outcome.ExitCode | Should -Be 1
    }
}

Describe 'Resolve-CopilotModel' {
    BeforeAll {
        $script:OriginalCopilot = $env:LUMA_COPILOT
        $env:LUMA_COPILOT = 'luma-fake-copilot'
    }

    AfterAll {
        $env:LUMA_COPILOT = $script:OriginalCopilot
    }

    It 'falls back to auto when the CLI reports the model is not available' {
        New-FakeTool -Name 'luma-fake-copilot' -ExitCode 1 -ReadStdin `
            -OutputLine 'Error: Model claude-opus-4.6 from --model flag is not available'

        $Result = Resolve-CopilotModel -Model 'claude-opus-4.6' -RepoRoot $script:RepoRoot `
            -WarningAction SilentlyContinue

        $Result | Should -BeExactly 'auto'
    }

    It 'keeps the requested model when the CLI accepts it' {
        New-FakeTool -Name 'luma-fake-copilot' -ReadStdin -OutputLine 'ok'

        $Result = Resolve-CopilotModel -Model 'claude-opus-4.6' -RepoRoot $script:RepoRoot

        $Result | Should -BeExactly 'claude-opus-4.6'
    }

    It 'keeps the requested model when the probe exceeds its timeout' {
        New-FakeTool -Name 'luma-fake-copilot' -SleepSeconds 30

        $Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        $Result = Resolve-CopilotModel -Model 'claude-opus-4.6' -RepoRoot $script:RepoRoot `
            -TimeoutSeconds 1
        $Stopwatch.Stop()

        $Result | Should -BeExactly 'claude-opus-4.6'
        $Stopwatch.Elapsed.TotalSeconds | Should -BeLessThan 15
    }
}

Describe 'Test-CleanWorkingTree' {
    It 'throws when the directory is not a git working tree' {
        $NotARepo = Join-Path -Path $TestDrive -ChildPath 'not-a-repo'
        New-Item -ItemType Directory -Force -Path $NotARepo | Out-Null

        { Test-CleanWorkingTree -RepoRoot $NotARepo 2>$null } | Should -Throw '*git status failed*'
    }

    It 'distinguishes a clean tree from a dirty one' {
        $Repo = Join-Path -Path $TestDrive -ChildPath 'git-repo'
        New-Item -ItemType Directory -Force -Path $Repo | Out-Null
        git -C $Repo init --quiet

        Test-CleanWorkingTree -RepoRoot $Repo | Should -BeTrue

        Set-Content -LiteralPath (Join-Path -Path $Repo -ChildPath 'new.txt') -Value 'change'
        Test-CleanWorkingTree -RepoRoot $Repo | Should -BeFalse
    }
}
