# Pester specification for scripts/pipeline/LumaPipeline.psm1.
#
# Focuses on the per-agent argument builders extracted from Invoke-AgentPhase.
# The ordered argument vector each builder returns is exactly what the -DryRun
# rendering and the real agent invocation consume, so pinning the vectors here
# guards both agents' CLI contracts (and the dry-run output) against drift.
#
# Written for the Windows built-in Pester 3.x (Should Be), the version this
# repository's environment provides. Run with:
#     Import-Module Pester; Invoke-Pester scripts/pipeline/LumaPipeline.Tests.ps1

$ModulePath = Join-Path -Path $PSScriptRoot -ChildPath 'LumaPipeline.psm1'
Import-Module -Name $ModulePath -Force

Describe 'Build-ClaudeArgumentList' {
    It 'renders read-only plan mode' {
        InModuleScope LumaPipeline {
            $argList = Build-ClaudeArgumentList -Instruction 'DO' -Mode 'plan'
            ($argList -join '|') | Should Be '-p|DO|--output-format|text|--permission-mode|plan'
        }
    }

    It 'renders acceptEdits agent mode with a push denial' {
        InModuleScope LumaPipeline {
            $argList = Build-ClaudeArgumentList -Instruction 'DO' -Mode 'agent'
            ($argList -join '|') | Should Be '-p|DO|--output-format|text|--permission-mode|acceptEdits|--allowedTools|Bash|Edit|Write|--disallowedTools|Bash(git push *)'
        }
    }

    It 'appends model and effort when supplied' {
        InModuleScope LumaPipeline {
            $argList = Build-ClaudeArgumentList -Instruction 'DO' -Mode 'plan' -Model 'm' -Effort 'high'
            ($argList -join '|') | Should Be '-p|DO|--output-format|text|--permission-mode|plan|--model|m|--effort|high'
        }
    }
}

Describe 'Build-CopilotArgumentList' {
    It 'renders read-only plan mode as JSONL with a write denial' {
        InModuleScope LumaPipeline {
            $argList = Build-CopilotArgumentList -Instruction 'DO' -Mode 'plan'
            ($argList -join '|') | Should Be '-p|DO|--allow-all-tools|--no-ask-user|--no-color|--plan|--output-format|json|--deny-tool=write'
        }
    }

    It 'renders agent mode with a git-push denial' {
        InModuleScope LumaPipeline {
            $argList = Build-CopilotArgumentList -Instruction 'DO' -Mode 'agent'
            ($argList -join '|') | Should Be '-p|DO|--allow-all-tools|--no-ask-user|--no-color|--deny-tool=shell(git push)'
        }
    }

    It 'appends log-dir, model, and effort when supplied' {
        InModuleScope LumaPipeline {
            $argList = Build-CopilotArgumentList -Instruction 'DO' -Mode 'agent' -Model 'm' -Effort 'high' -LogDir 'L'
            ($argList -join '|') | Should Be '-p|DO|--allow-all-tools|--no-ask-user|--no-color|--deny-tool=shell(git push)|--log-dir=L|--model=m|--effort=high'
        }
    }
}

Describe 'Get-PythonExecutable' {
    It 'prefers python over python3 when both resolve' {
        InModuleScope LumaPipeline {
            Mock Get-Command { [pscustomobject]@{ Source = '/usr/bin/python' } } -ParameterFilter { $Name -eq 'python' }
            Get-PythonExecutable | Should Be '/usr/bin/python'
        }
    }

    It 'falls back to python3 when python is absent' {
        InModuleScope LumaPipeline {
            Mock Get-Command { } -ParameterFilter { $Name -eq 'python' }
            Mock Get-Command { [pscustomobject]@{ Source = '/usr/bin/python3' } } -ParameterFilter { $Name -eq 'python3' }
            Get-PythonExecutable | Should Be '/usr/bin/python3'
        }
    }

    It 'returns $null when no interpreter is on PATH' {
        InModuleScope LumaPipeline {
            Mock Get-Command { } -ParameterFilter { $Name -eq 'python' -or $Name -eq 'python3' }
            Get-PythonExecutable | Should Be $null
        }
    }
}

Describe 'Test-LintAndFormat' {
    It 'is a no-op that reports success in dry-run mode' {
        InModuleScope LumaPipeline {
            # Guard: dry-run must never resolve or invoke an interpreter.
            Mock Get-PythonExecutable { throw 'must not resolve Python in dry-run' }
            Test-LintAndFormat -RepoRoot 'X' -DryRun | Should Be $true
        }
    }

    It 'reports success in check-only dry-run mode' {
        InModuleScope LumaPipeline {
            Mock Get-PythonExecutable { throw 'must not resolve Python in dry-run' }
            Test-LintAndFormat -RepoRoot 'X' -CheckOnly -DryRun | Should Be $true
        }
    }

    It 'skips (reports success) when no Python interpreter is available' {
        InModuleScope LumaPipeline {
            Mock Get-PythonExecutable { $null }
            Test-LintAndFormat -RepoRoot 'X' | Should Be $true
        }
    }
}

Describe 'Get-VcvarsScript' {
    It 'returns $null when vswhere cannot be found' {
        InModuleScope LumaPipeline {
            Mock Test-Path { $false }
            Get-VcvarsScript | Should Be $null
        }
    }
}

Describe 'Initialize-CompileDatabase' {
    It 'is a no-op that reports success in dry-run mode' {
        InModuleScope LumaPipeline {
            # Guard: dry-run must not probe the toolchain or touch the tree.
            Mock Get-Command { throw 'must not probe tools in dry-run' }
            Initialize-CompileDatabase -RepoRoot 'X' -DryRun | Should Be $true
        }
    }

    It 'skips (reports success) when clang-tidy is absent' {
        InModuleScope LumaPipeline {
            Mock Test-Path { $false }
            Mock Get-Command { } -ParameterFilter { $Name -eq 'clang-tidy' }
            Initialize-CompileDatabase -RepoRoot 'X' | Should Be $true
        }
    }

    It 'is a no-op (reports success) when the database already exists' {
        InModuleScope LumaPipeline {
            Mock Test-Path { $true } -ParameterFilter { $LiteralPath -like '*compile_commands.json' }
            # If it short-circuits correctly it never probes for tools.
            Mock Get-Command { throw 'must not probe tools when the database exists' }
            Initialize-CompileDatabase -RepoRoot 'X' | Should Be $true
        }
    }
}

Describe 'Save-PipelineArtifact' {
    It 'is a no-op that returns $null in dry-run mode' {
        InModuleScope LumaPipeline {
            Mock git { throw 'must not invoke git in dry-run' }
            Save-PipelineArtifact -ArtifactRoot 'X' -RepoRoot 'Y' -DryRun | Should Be $null
        }
    }

    It 'returns $null when the artifact root does not exist' {
        InModuleScope LumaPipeline {
            $Repo = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid())
            New-Item -ItemType Directory -Force -Path $Repo | Out-Null
            try {
                Mock git { throw 'must not invoke git for a missing root' }
                $Missing = Join-Path $Repo 'pipeline-artifacts'
                Save-PipelineArtifact -ArtifactRoot $Missing -RepoRoot $Repo | Should Be $null
            }
            finally {
                Remove-Item -LiteralPath $Repo -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }

    It 'returns $null for an artifact root outside the working tree' {
        InModuleScope LumaPipeline {
            $Repo = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid())
            $Outside = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid())
            New-Item -ItemType Directory -Force -Path $Repo | Out-Null
            New-Item -ItemType Directory -Force -Path $Outside | Out-Null
            try {
                Mock git { throw 'must not invoke git for an out-of-tree root' }
                Save-PipelineArtifact -ArtifactRoot $Outside -RepoRoot $Repo | Should Be $null
            }
            finally {
                Remove-Item -LiteralPath $Repo -Recurse -Force -ErrorAction SilentlyContinue
                Remove-Item -LiteralPath $Outside -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }

    It 'moves an in-tree artifact root into .git and returns the backup path' {
        InModuleScope LumaPipeline {
            $Repo = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid())
            New-Item -ItemType Directory -Force -Path $Repo | Out-Null
            try {
                git -C $Repo init --quiet | Out-Null
                $Art = Join-Path $Repo 'pipeline-artifacts'
                New-Item -ItemType Directory -Force -Path $Art | Out-Null
                Set-Content -LiteralPath (Join-Path $Art 'SUMMARY.md') -Value 'keep me'

                $Backup = Save-PipelineArtifact -ArtifactRoot $Art -RepoRoot $Repo

                # git rev-parse yields a forward-slash path on Windows, so assert
                # on structure rather than an exact string: the root moved away,
                # the backup carries the distinct name, and the content survived.
                $Backup | Should Not BeNullOrEmpty
                (Split-Path -Leaf $Backup) | Should Be '_pipeline-artifacts.pipeline.bak'
                (Test-Path -LiteralPath $Art) | Should Be $false
                (Test-Path -LiteralPath (Join-Path $Backup 'SUMMARY.md')) | Should Be $true
            }
            finally {
                Remove-Item -LiteralPath $Repo -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

Describe 'Restore-PipelineArtifact' {
    It 'is a no-op when the backup path is empty' {
        InModuleScope LumaPipeline {
            { Restore-PipelineArtifact -BackupPath '' -ArtifactRoot 'X' } | Should Not Throw
        }
    }

    It 'is a no-op when the backup directory is missing' {
        InModuleScope LumaPipeline {
            $Missing = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid())
            { Restore-PipelineArtifact -BackupPath $Missing -ArtifactRoot 'X' } | Should Not Throw
        }
    }

    It 'moves the backup back when the artifact root is absent' {
        InModuleScope LumaPipeline {
            $Base = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid())
            $Backup = Join-Path $Base 'backup'
            $Art = Join-Path $Base 'pipeline-artifacts'
            New-Item -ItemType Directory -Force -Path $Backup | Out-Null
            Set-Content -LiteralPath (Join-Path $Backup 'SUMMARY.md') -Value 'restored'
            try {
                Restore-PipelineArtifact -BackupPath $Backup -ArtifactRoot $Art
                (Test-Path -LiteralPath $Backup) | Should Be $false
                (Get-Content -LiteralPath (Join-Path $Art 'SUMMARY.md')) | Should Be 'restored'
            }
            finally {
                Remove-Item -LiteralPath $Base -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }

    It 'merges backed-up entries when the artifact root was recreated' {
        InModuleScope LumaPipeline {
            $Base = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid())
            $Backup = Join-Path $Base 'backup'
            $Art = Join-Path $Base 'pipeline-artifacts'
            New-Item -ItemType Directory -Force -Path $Backup | Out-Null
            New-Item -ItemType Directory -Force -Path $Art | Out-Null
            Set-Content -LiteralPath (Join-Path $Backup 'old.log') -Value 'old'
            Set-Content -LiteralPath (Join-Path $Art 'new.log') -Value 'new'
            try {
                Restore-PipelineArtifact -BackupPath $Backup -ArtifactRoot $Art
                (Test-Path -LiteralPath $Backup) | Should Be $false
                (Test-Path -LiteralPath (Join-Path $Art 'old.log')) | Should Be $true
                (Test-Path -LiteralPath (Join-Path $Art 'new.log')) | Should Be $true
            }
            finally {
                Remove-Item -LiteralPath $Base -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

Describe 'Invoke-GitCheckpoint' {
    # Locks in the checkpoint contract the runners depend on: a rejected commit
    # (e.g. the pre-commit hook) throws, so callers can mark the file
    # 'commit-failed' and revert/stop instead of silently re-staging the
    # un-committable change into every later checkpoint (git add -A).

    It 'returns $null in dry-run mode without touching git' {
        InModuleScope LumaPipeline {
            Invoke-GitCheckpoint -RepoRoot 'X' -Subject 'chore: noop' -DryRun |
                Should BeNullOrEmpty
        }
    }

    It 'commits staged changes and returns the short SHA' {
        InModuleScope LumaPipeline {
            $Repo = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid())
            New-Item -ItemType Directory -Force -Path $Repo | Out-Null
            try {
                git -C $Repo init --quiet | Out-Null
                git -C $Repo config user.email 'test@example.com' | Out-Null
                git -C $Repo config user.name 'Luma Test' | Out-Null
                git -C $Repo config commit.gpgsign false | Out-Null
                Set-Content -LiteralPath (Join-Path $Repo 'file.txt') -Value 'hello'

                $Sha = Invoke-GitCheckpoint -RepoRoot $Repo -Subject 'chore: add file' -Agent 'copilot'

                $Sha | Should Not BeNullOrEmpty
                "$(git -C $Repo rev-parse --short HEAD)".Trim() | Should Be $Sha
                "$(git -C $Repo rev-list --count HEAD)".Trim() | Should Be '1'
            }
            finally {
                Remove-Item -LiteralPath $Repo -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }

    It 'returns $null when there is nothing to commit' {
        InModuleScope LumaPipeline {
            $Repo = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid())
            New-Item -ItemType Directory -Force -Path $Repo | Out-Null
            try {
                git -C $Repo init --quiet | Out-Null
                git -C $Repo config user.email 'test@example.com' | Out-Null
                git -C $Repo config user.name 'Luma Test' | Out-Null
                git -C $Repo config commit.gpgsign false | Out-Null
                Set-Content -LiteralPath (Join-Path $Repo 'file.txt') -Value 'hello'
                git -C $Repo add -A | Out-Null
                git -C $Repo commit -m 'initial' --quiet | Out-Null

                Invoke-GitCheckpoint -RepoRoot $Repo -Subject 'chore: noop' -Agent 'copilot' |
                    Should BeNullOrEmpty
            }
            finally {
                Remove-Item -LiteralPath $Repo -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }

    It 'throws when git commit is rejected' {
        # Invoke-GitCheckpoint is exported, so call it in the normal test scope.
        # Use an explicit try/catch boolean rather than `Should Throw`: in Pester
        # 3.4 the positive `Should Throw` did not reliably observe the terminating
        # error raised out of the module function here, whereas a direct
        # try/catch is unambiguous. The return-value cases above are unaffected.
        $Repo = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid())
        New-Item -ItemType Directory -Force -Path $Repo | Out-Null
        $Threw = $false
        try {
            git -C $Repo init --quiet | Out-Null
            git -C $Repo config user.email 'test@example.com' | Out-Null
            git -C $Repo config user.name 'Luma Test' | Out-Null
            # Force `git commit` to fail deterministically on every platform:
            # signing is required but the gpg program does not exist, so the
            # commit is rejected exactly as a failing pre-commit hook would.
            git -C $Repo config commit.gpgsign true | Out-Null
            git -C $Repo config gpg.program 'luma-no-such-gpg-program' | Out-Null
            Set-Content -LiteralPath (Join-Path $Repo 'file.txt') -Value 'hello'

            try {
                Invoke-GitCheckpoint -RepoRoot $Repo -Subject 'chore: blocked' -Agent 'copilot' | Out-Null
            }
            catch {
                $Threw = $true
            }

            $Threw | Should Be $true
        }
        finally {
            Remove-Item -LiteralPath $Repo -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

Describe 'Test-ShouldAbort' {
    # Pins the shared stop/continue rule both runners depend on. The key contract
    # is the last two cases: a 'commit-failed' is systemic (a pre-commit hook or
    # git keeps rejecting the commit, and git add -A re-stages it every time), so
    # it stops the run even under -ContinueOnFailure -- UNLESS -RevertOnFailure is
    # set, which resets the tree clean and makes carrying on safe.

    It 'never aborts on ok' {
        InModuleScope LumaPipeline {
            Test-ShouldAbort -Status 'ok' -ContinueOnFailure:$false -RevertOnFailure:$false | Should Be $false
            Test-ShouldAbort -Status 'ok' -ContinueOnFailure:$true -RevertOnFailure:$false | Should Be $false
        }
    }

    It 'aborts on any failure without -ContinueOnFailure' {
        InModuleScope LumaPipeline {
            Test-ShouldAbort -Status 'agent-failed' -ContinueOnFailure:$false -RevertOnFailure:$false | Should Be $true
            Test-ShouldAbort -Status 'gate-failed' -ContinueOnFailure:$false -RevertOnFailure:$false | Should Be $true
            Test-ShouldAbort -Status 'commit-failed' -ContinueOnFailure:$false -RevertOnFailure:$false | Should Be $true
            Test-ShouldAbort -Status 'error' -ContinueOnFailure:$false -RevertOnFailure:$false | Should Be $true
        }
    }

    It 'skips per-file failures under -ContinueOnFailure' {
        InModuleScope LumaPipeline {
            Test-ShouldAbort -Status 'agent-failed' -ContinueOnFailure:$true -RevertOnFailure:$false | Should Be $false
            Test-ShouldAbort -Status 'gate-failed' -ContinueOnFailure:$true -RevertOnFailure:$false | Should Be $false
            Test-ShouldAbort -Status 'error' -ContinueOnFailure:$true -RevertOnFailure:$false | Should Be $false
        }
    }

    It 'stops a systemic commit-failed even under -ContinueOnFailure' {
        InModuleScope LumaPipeline {
            Test-ShouldAbort -Status 'commit-failed' -ContinueOnFailure:$true -RevertOnFailure:$false | Should Be $true
        }
    }

    It 'lets a reverted commit-failed continue under -ContinueOnFailure' {
        InModuleScope LumaPipeline {
            Test-ShouldAbort -Status 'commit-failed' -ContinueOnFailure:$true -RevertOnFailure:$true | Should Be $false
        }
    }
}
