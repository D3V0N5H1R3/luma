#Requires -Version 7.0

# LumaPipeline.psm1 - shared helpers for the Luma prompt-pipeline runners.
#
# Consumed by Invoke-LumaAudit.ps1 (read-only report generation) and
# Invoke-LumaFix.ps1 (gated, mutating fixes). Every side effect flows through a
# helper here so the two runners stay thin and the -DryRun path is uniform.
#
# See scripts/pipeline/README.md for the pipeline ordering and safety model.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Report native (non-PowerShell) command failures via $LASTEXITCODE instead of
# throwing, even if a user profile flips this preference on. Every gate/agent/git
# helper here checks $LASTEXITCODE explicitly; a thrown native error - e.g.
# `git diff --cached --quiet` exiting 1 precisely when there ARE staged changes -
# would otherwise misfire under $ErrorActionPreference = 'Stop'.
$PSNativeCommandUseErrorActionPreference = $false

function Get-LumaRepoRoot {
    <#
    .SYNOPSIS
        Resolve the Luma repository root.
    .DESCRIPTION
        The module lives in scripts/pipeline/, so the repository root is two
        levels up. The candidate is verified by the presence of CMakePresets.json;
        if that fails, `git rev-parse --show-toplevel` is used as a fallback.
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [ValidateNotNullOrEmpty()]
        [string]$StartPath = $PSScriptRoot
    )

    $Candidate = Split-Path -Parent (Split-Path -Parent $StartPath)
    if (Test-Path -LiteralPath (Join-Path -Path $Candidate -ChildPath 'CMakePresets.json')) {
        return $Candidate
    }

    $Toplevel = git -C $StartPath rev-parse --show-toplevel 2>$null
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($Toplevel)) {
        return $Toplevel.Trim()
    }

    throw "Could not resolve the Luma repository root from '$StartPath'."
}

function Get-AgentExecutable {
    <#
    .SYNOPSIS
        Return the full path to the requested agent CLI executable.
    .DESCRIPTION
        Resolves the 'copilot' or 'claude' command on PATH. The environment
        variables LUMA_COPILOT / LUMA_CLAUDE override the executable name or path.
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [ValidateSet('copilot', 'claude')]
        [string]$Agent = 'copilot'
    )

    $EnvName = if ($Agent -eq 'claude') { 'LUMA_CLAUDE' } else { 'LUMA_COPILOT' }
    $Override = [Environment]::GetEnvironmentVariable($EnvName)
    $Name = if ($Override) { $Override } else { $Agent }

    $Command = Get-Command -Name $Name -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $Command) {
        throw "The $Agent CLI ('$Name') was not found on PATH. Install it (or set $EnvName) and re-run."
    }
    return $Command.Source
}

function Write-PhaseBanner {
    <#
    .SYNOPSIS
        Print a visual banner for a pipeline phase (user-facing progress).
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Title,

        [string]$Subtitle
    )

    $Rule = '=' * 72
    Write-Host ''
    Write-Host $Rule -ForegroundColor Cyan
    Write-Host "  $Title" -ForegroundColor Cyan
    if ($Subtitle) {
        Write-Host "  $Subtitle" -ForegroundColor DarkGray
    }
    Write-Host $Rule -ForegroundColor Cyan
}

function Get-AuditPhase {
    <#
    .SYNOPSIS
        The ordered read-only audit phases, each producing a ranked report.
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.Generic.List[psobject]])]
    param()

    $Phases = [System.Collections.Generic.List[psobject]]::new()
    $Phases.Add([pscustomobject]@{ Id = '01-bug-search-core';             Name = 'Bug search (interpreter + stdlib)'; Prompt = 'bug-search.prompt.md';                  DefaultScope = '' })
    $Phases.Add([pscustomobject]@{ Id = '02-bug-search-debugger';         Name = 'Bug search (debugger)';             Prompt = 'bug-search-debugger.prompt.md';         DefaultScope = '' })
    $Phases.Add([pscustomobject]@{ Id = '03-bug-search-language-server';  Name = 'Bug search (language server)';      Prompt = 'bug-search-language-server.prompt.md';  DefaultScope = '' })
    $Phases.Add([pscustomobject]@{ Id = '04-bug-search-editor-extension'; Name = 'Bug search (editor extensions)';    Prompt = 'bug-search-editor-extension.prompt.md'; DefaultScope = '' })
    $Phases.Add([pscustomobject]@{ Id = '05-consistency-check';           Name = 'Consistency check';                 Prompt = 'consistency-check.prompt.md';           DefaultScope = '' })
    $Phases.Add([pscustomobject]@{ Id = '06-code-review';                 Name = 'Code review (deep net)';            Prompt = 'code-review.prompt.md';                 DefaultScope = 'core/ shared/ language-server/source/ debugger/source/' })
    $Phases.Add([pscustomobject]@{ Id = '07-ux-audit';                    Name = 'UX audit';                          Prompt = 'ux-audit.prompt.md';                    DefaultScope = '' })
    $Phases.Add([pscustomobject]@{ Id = '08-refactor-audit';              Name = 'Refactor audit';                    Prompt = 'refactor-audit.prompt.md';              DefaultScope = '' })
    $Phases.Add([pscustomobject]@{ Id = '09-performance-audit';           Name = 'Performance audit';                 Prompt = 'performance-audit.prompt.md';           DefaultScope = '' })
    return $Phases
}

function Get-FixPhase {
    <#
    .SYNOPSIS
        The ordered mutating fix phases. Each consumes an audit report where one
        exists and is gated/committed according to its flags.
    .DESCRIPTION
        Gate         = run the authoritative cmake+ctest gate after the phase.
        Commit       = create a git checkpoint when the phase leaves the tree green.
        SelfVerifies = the prompt already builds/tests exhaustively, so the
                       script-side gate is skipped to avoid redundant rebuilds.
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.Generic.List[psobject]])]
    param()

    $Phases = [System.Collections.Generic.List[psobject]]::new()
    $Phases.Add([pscustomobject]@{ Id = 'bug-fix-core';             Name = 'Bug fix (interpreter + stdlib)'; Prompt = 'bug-fix.prompt.md';                  InputReport = '01-bug-search-core.md';             Gate = $true;  Commit = $true;  SelfVerifies = $false })
    $Phases.Add([pscustomobject]@{ Id = 'bug-fix-debugger';         Name = 'Bug fix (debugger)';             Prompt = 'bug-fix-debugger.prompt.md';         InputReport = '02-bug-search-debugger.md';         Gate = $true;  Commit = $true;  SelfVerifies = $false })
    $Phases.Add([pscustomobject]@{ Id = 'bug-fix-language-server';  Name = 'Bug fix (language server)';      Prompt = 'bug-fix-language-server.prompt.md';  InputReport = '03-bug-search-language-server.md';  Gate = $true;  Commit = $true;  SelfVerifies = $false })
    $Phases.Add([pscustomobject]@{ Id = 'bug-fix-editor-extension'; Name = 'Bug fix (editor extensions)';    Prompt = 'bug-fix-editor-extension.prompt.md'; InputReport = '04-bug-search-editor-extension.md'; Gate = $true;  Commit = $true;  SelfVerifies = $false })
    $Phases.Add([pscustomobject]@{ Id = 'refactor';                Name = 'Refactor';                       Prompt = 'refactor.prompt.md';                 InputReport = '08-refactor-audit.md';              Gate = $true;  Commit = $true;  SelfVerifies = $false })
    $Phases.Add([pscustomobject]@{ Id = 'optimize';                Name = 'Optimize';                       Prompt = 'optimize.prompt.md';                 InputReport = '09-performance-audit.md';           Gate = $true;  Commit = $true;  SelfVerifies = $false })
    $Phases.Add([pscustomobject]@{ Id = 'ux-improve';              Name = 'UX improvement';                 Prompt = 'ux-improve.prompt.md';               InputReport = '07-ux-audit.md';                    Gate = $true;  Commit = $true;  SelfVerifies = $false })
    $Phases.Add([pscustomobject]@{ Id = 'iterative-improvement';   Name = 'Iterative improvement (loop)';   Prompt = 'iterative-improvement.prompt.md';    InputReport = '';                                  Gate = $false; Commit = $true;  SelfVerifies = $true })
    $Phases.Add([pscustomobject]@{ Id = 'release-verification';    Name = 'Release verification (gate)';    Prompt = 'release-verification.prompt.md';     InputReport = '';                                  Gate = $false; Commit = $false; SelfVerifies = $true })
    $Phases.Add([pscustomobject]@{ Id = 'update-learnings';        Name = 'Update learnings';               Prompt = 'update-learnings.prompt.md';         InputReport = '';                                  Gate = $false; Commit = $true;  SelfVerifies = $false })
    return $Phases
}



function Get-AgentReportFromJsonl {
    <#
    .SYNOPSIS
        Reduce a Copilot JSONL event stream to the agent's final report text.
    .DESCRIPTION
        Copilot's --output-format json emits one JSON event per line. The report
        is the 'content' of the last 'assistant.message' event that carries text:
        tool-call-only steps have empty content, and a background sub-agent's
        messages arrive before the main agent's closing synthesis, so the last
        text message is the real report. Returns an empty string when no such
        message is present, so the caller can keep the raw transcript instead.
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [string[]]$JsonlLines
    )

    $Report = ''
    foreach ($Line in $JsonlLines) {
        if ([string]::IsNullOrWhiteSpace($Line)) { continue }
        try { $ParsedEvent = $Line | ConvertFrom-Json -ErrorAction Stop }
        catch { continue }
        # A line whose JSON value is literally `null` (or an empty array, which
        # the pipeline unrolls to nothing) makes ConvertFrom-Json yield $null;
        # $null has no .PSObject, so the probe below would throw under StrictMode.
        if ($null -eq $ParsedEvent) { continue }
        # Probe each property through PSObject.Properties rather than dotting
        # into it: the module runs under Set-StrictMode -Version Latest, where a
        # direct reference to an absent property throws PropertyNotFoundException.
        # This mirrors the tolerant `.get()` / `&&` chains in the bash reducers so
        # a tool-call-only message (no 'content'), a bare/partial line, or any
        # non-object event is skipped instead of aborting the whole phase.
        $TypeProp = $ParsedEvent.PSObject.Properties['type']
        if (-not $TypeProp -or $TypeProp.Value -ne 'assistant.message') { continue }
        $DataProp = $ParsedEvent.PSObject.Properties['data']
        if (-not $DataProp -or $null -eq $DataProp.Value) { continue }
        $ContentProp = $DataProp.Value.PSObject.Properties['content']
        if (-not $ContentProp) { continue }
        $Content = $ContentProp.Value
        if ($Content -is [string] -and $Content.Trim()) { $Report = $Content }
    }
    return $Report
}

function Build-ClaudeArgumentList {
    <#
    .SYNOPSIS
        Assemble the Claude Code CLI argument list for one agent phase.
    .DESCRIPTION
        Returns the complete argument vector for a `claude` run in the given
        mode, isolated from Invoke-AgentPhase so the agent's contract is easy to
        compare and unit-test. The order is significant and mirrors the rendered
        -DryRun line exactly.
    .OUTPUTS
        [System.Collections.Generic.List[string]] - the ordered arguments.
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.Generic.List[string]])]
    param(
        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$Instruction,

        [Parameter(Mandatory)]
        [ValidateSet('plan', 'agent')]
        [string]$Mode,

        [string]$Model,

        [string]$Effort
    )

    $Arguments = [System.Collections.Generic.List[string]]::new()
    $Arguments.Add('-p')
    $Arguments.Add($Instruction)

    # Claude Code: -p is headless; text output keeps stdout clean to capture.
    $Arguments.Add('--output-format')
    $Arguments.Add('text')
    if ($Mode -eq 'plan') {
        # Plan mode is strictly read-only: no edits, no mutating commands.
        $Arguments.Add('--permission-mode')
        $Arguments.Add('plan')
    }
    else {
        # Auto-accept edits and allow the tools a fixer needs, but deny any
        # push so nothing leaves the machine (deny rules beat allow rules).
        $Arguments.Add('--permission-mode')
        $Arguments.Add('acceptEdits')
        $Arguments.Add('--allowedTools')
        $Arguments.Add('Bash'); $Arguments.Add('Edit'); $Arguments.Add('Write')
        $Arguments.Add('--disallowedTools')
        $Arguments.Add('Bash(git push *)')
    }
    if ($Model) { $Arguments.Add('--model'); $Arguments.Add($Model) }
    if ($Effort) { $Arguments.Add('--effort'); $Arguments.Add($Effort) }

    # -NoEnumerate returns the List intact (an unwrapped `, $Arguments` reads to
    # the analyzer as object[], desyncing it from the declared OutputType).
    Write-Output -InputObject $Arguments -NoEnumerate
}

function Build-CopilotArgumentList {
    <#
    .SYNOPSIS
        Assemble the Copilot CLI argument list for one agent phase.
    .DESCRIPTION
        Returns the complete argument vector for a `copilot` run in the given
        mode, isolated from Invoke-AgentPhase so the agent's contract is easy to
        compare and unit-test. The order is significant and mirrors the rendered
        -DryRun line exactly.
    .OUTPUTS
        [System.Collections.Generic.List[string]] - the ordered arguments.
    #>
    [CmdletBinding()]
    [OutputType([System.Collections.Generic.List[string]])]
    param(
        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$Instruction,

        [Parameter(Mandatory)]
        [ValidateSet('plan', 'agent')]
        [string]$Mode,

        [string]$Model,

        [string]$Effort,

        [string]$LogDir
    )

    $Arguments = [System.Collections.Generic.List[string]]::new()
    $Arguments.Add('-p')
    $Arguments.Add($Instruction)

    $Arguments.Add('--allow-all-tools')
    $Arguments.Add('--no-ask-user')
    $Arguments.Add('--no-color')
    if ($Mode -eq 'plan') {
        # Read-only audit. Do NOT use Copilot's interactive `--plan` mode here:
        # in a non-interactive `-p` run it delivers its result through the
        # exit_plan_mode tool / a plan.md file rather than stdout, so it emits an
        # empty report (the agent exits 0 having written nothing to stdout).
        # Instead restrict the model to the read/search tools with an allow-list,
        # which guarantees it cannot modify, create, or run anything, and still
        # lets it write its report as the final message.
        #
        # Emit the structured JSONL event stream instead of text. In text
        # mode Copilot streams every intermediate assistant message to stdout
        # - and, when a prompt fans out to parallel background sub-agents,
        # their transcripts interleave into corrupted output - so a plain
        # capture is not a clean report (--silent only drops the stats
        # footer, it does not suppress the narration). The caller reduces the
        # JSONL to the agent's final message.
        $Arguments.Add('--output-format')
        $Arguments.Add('json')
        $Arguments.Add('--available-tools=view,grep,glob')
    }
    else {
        # Belt-and-suspenders: the phase must never publish to a remote.
        $Arguments.Add('--deny-tool=shell(git push)')
    }
    if ($LogDir) { $Arguments.Add("--log-dir=$LogDir") }
    if ($Model) { $Arguments.Add("--model=$Model") }
    if ($Effort) { $Arguments.Add("--effort=$Effort") }

    # -NoEnumerate returns the List intact (an unwrapped `, $Arguments` reads to
    # the analyzer as object[], desyncing it from the declared OutputType).
    Write-Output -InputObject $Arguments -NoEnumerate
}

function Invoke-AgentPhase {
    <#
    .SYNOPSIS
        Run a single agent CLI phase (copilot or claude), in read-only 'plan'
        mode or mutating 'agent' mode.
    .DESCRIPTION
        In 'plan' mode the agent's final report is captured to OutputFile while
        the agent is held read-only. Copilot runs with --output-format json and a
        read-only tool allow-list (--available-tools=view,grep,glob); its JSONL
        event stream is reduced to the final message - a plain text capture would
        also save intermediate narration and, when a prompt fans out to parallel
        sub-agents, interleaved output. Claude runs with --permission-mode plan
        and its -p text output is already only the final message. In 'agent' mode
        the run streams to the console and is teed to LogFile, with `git push`
        denied so nothing can leave the machine.
    #>
    [CmdletBinding()]
    [OutputType([pscustomobject])]
    param(
        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$Instruction,

        [Parameter(Mandatory)]
        [ValidateSet('plan', 'agent')]
        [string]$Mode,

        [ValidateSet('copilot', 'claude')]
        [string]$Agent = 'copilot',

        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$RepoRoot,

        [string]$OutputFile,

        [string]$LogFile,

        [string]$LogDir,

        [string]$Model,

        [string]$Effort,

        [switch]$DryRun
    )

    if ($Agent -eq 'claude') {
        $Arguments = Build-ClaudeArgumentList -Instruction $Instruction -Mode $Mode -Model $Model -Effort $Effort
    }
    else {
        $Arguments = Build-CopilotArgumentList -Instruction $Instruction -Mode $Mode -Model $Model -Effort $Effort -LogDir $LogDir
    }

    if ($DryRun) {
        $Rendered = ($Arguments | ForEach-Object {
                if ($_ -eq $Instruction) { '"<instruction>"' }
                elseif ($_ -match '\s') { '"' + $_ + '"' }
                else { $_ }
            }) -join ' '
        Write-Host "    [dry-run] $Agent $Rendered" -ForegroundColor DarkGray
        if ($OutputFile) { Write-Host "    [dry-run] stdout -> $OutputFile" -ForegroundColor DarkGray }
        if ($LogFile) { Write-Host "    [dry-run] tee     -> $LogFile" -ForegroundColor DarkGray }
        return [pscustomobject]@{ Mode = $Mode; ExitCode = 0; Success = $true; DryRun = $true; OutputFile = $OutputFile; LogFile = $LogFile }
    }

    $Executable = Get-AgentExecutable -Agent $Agent
    $ExitCode = 0

    Push-Location -LiteralPath $RepoRoot
    try {
        if ($Mode -eq 'plan') {
            if (-not $OutputFile) { throw 'Plan mode requires -OutputFile to capture the report.' }
            if ($Agent -eq 'copilot') {
                # Copilot streams JSONL events (see the arg construction above);
                # collect them, then reduce to the agent's final message. The
                # agent's own exit code stands regardless of extraction. stderr is
                # captured separately so a diagnostic (e.g. an auth error) is not
                # lost, and can be surfaced when the agent produces no report.
                $ErrFile = [System.IO.Path]::GetTempFileName()
                try {
                    $RawOutput = & $Executable @Arguments 2> $ErrFile
                    $ExitCode = $LASTEXITCODE
                    $Report = Get-AgentReportFromJsonl -JsonlLines @($RawOutput)
                    if (-not [string]::IsNullOrWhiteSpace($Report)) {
                        # Normalise to exactly one trailing newline to match the
                        # bash runner, where command substitution strips trailing
                        # newlines and printf re-adds a single one, so both emit
                        # byte-identical report files.
                        $Report = $Report.TrimEnd("`r", "`n") + "`n"
                        Set-Content -LiteralPath $OutputFile -Value $Report -NoNewline -Encoding utf8
                    }
                    elseif (@($RawOutput).Where({ -not [string]::IsNullOrWhiteSpace($_) }).Count -gt 0) {
                        # Output but no extractable final message: keep the raw
                        # transcript so nothing is silently dropped.
                        Write-Warning 'Could not extract a final report from Copilot output; keeping the raw JSONL transcript.'
                        Set-Content -LiteralPath $OutputFile -Value (@($RawOutput) -join "`n") -NoNewline -Encoding utf8
                    }
                    else {
                        # The agent wrote nothing to stdout: this phase did no
                        # work. Surface the captured stderr in the report and
                        # force a failure so the pipeline never reports a hollow
                        # success.
                        Write-Warning 'The agent produced no report for this phase (empty output); see the captured error in the report.'
                        $ErrText = (Get-Content -LiteralPath $ErrFile -Raw -ErrorAction SilentlyContinue)
                        if ([string]::IsNullOrWhiteSpace($ErrText)) { $ErrText = '(none)' }
                        $Fence = '```'
                        $Body = @(
                            '# Phase produced no report'
                            ''
                            "The agent exited with code $ExitCode and wrote nothing to standard output, so no report could be produced."
                            ''
                            'Captured standard error:'
                            ''
                            $Fence
                            $ErrText.TrimEnd("`r", "`n")
                            $Fence
                        ) -join "`n"
                        Set-Content -LiteralPath $OutputFile -Value ($Body + "`n") -NoNewline -Encoding utf8
                        if ($ExitCode -eq 0) { $ExitCode = 1 }
                    }
                }
                finally {
                    Remove-Item -LiteralPath $ErrFile -Force -ErrorAction SilentlyContinue
                }
            }
            else {
                # Claude's headless -p text mode already prints only the final
                # message, so a direct stdout capture is the clean report.
                & $Executable @Arguments 1> $OutputFile
                $ExitCode = $LASTEXITCODE
            }
        }
        else {
            if ($LogFile) {
                & $Executable @Arguments 2>&1 | Tee-Object -FilePath $LogFile
            }
            else {
                & $Executable @Arguments
            }
            $ExitCode = $LASTEXITCODE
        }
    }
    finally {
        Pop-Location
    }

    return [pscustomobject]@{
        Mode       = $Mode
        ExitCode   = $ExitCode
        Success    = ($ExitCode -eq 0)
        DryRun     = $false
        OutputFile = $OutputFile
        LogFile    = $LogFile
    }
}

function Test-BuildAndTest {
    <#
    .SYNOPSIS
        Run the authoritative build+test gate (cmake --build, then ctest).
    .OUTPUTS
        [bool] - $true when both build and tests succeed.
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$RepoRoot,

        [ValidateNotNullOrEmpty()]
        [string]$Preset = 'default',

        [switch]$SkipBuild,

        [switch]$SkipTest,

        [switch]$DryRun
    )

    if ($DryRun) {
        $BuildDir = Join-Path -Path $RepoRoot -ChildPath 'build'
        if (-not (Test-Path -LiteralPath $BuildDir)) {
            Write-Host "    [dry-run] cmake --preset $Preset" -ForegroundColor DarkGray
        }
        if (-not $SkipBuild) {
            Write-Host "    [dry-run] cmake --build --preset $Preset" -ForegroundColor DarkGray
        }
        if (-not $SkipTest) {
            Write-Host "    [dry-run] ctest --preset $Preset" -ForegroundColor DarkGray
        }
        return $true
    }

    Push-Location -LiteralPath $RepoRoot
    try {
        $BuildDir = Join-Path -Path $RepoRoot -ChildPath 'build'
        if (-not (Test-Path -LiteralPath $BuildDir)) {
            Write-Host "    Configuring (build dir missing): cmake --preset $Preset"
            cmake --preset $Preset
            if ($LASTEXITCODE -ne 0) {
                Write-Warning 'CMake configure failed.'
                return $false
            }
        }

        if (-not $SkipBuild) {
            Write-Host "    Building: cmake --build --preset $Preset"
            cmake --build --preset $Preset
            if ($LASTEXITCODE -ne 0) {
                Write-Warning 'Build failed.'
                return $false
            }
        }

        if (-not $SkipTest) {
            Write-Host "    Testing: ctest --preset $Preset"
            ctest --preset $Preset
            if ($LASTEXITCODE -ne 0) {
                Write-Warning 'Tests failed.'
                return $false
            }
        }

        return $true
    }
    finally {
        Pop-Location
    }
}

function Test-CleanWorkingTree {
    <#
    .SYNOPSIS
        Return $true when the git working tree has no staged or unstaged changes.
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$RepoRoot,

        [switch]$DryRun
    )

    if ($DryRun) {
        Write-Host '    [dry-run] git status --porcelain (assuming clean)' -ForegroundColor DarkGray
        return $true
    }

    Push-Location -LiteralPath $RepoRoot
    try {
        $Status = git status --porcelain
        return [string]::IsNullOrWhiteSpace(($Status | Out-String))
    }
    finally {
        Pop-Location
    }
}

function Test-ShouldAbort {
    <#
    .SYNOPSIS
        Decide whether a failed per-file/per-phase step should stop the run.
    .DESCRIPTION
        Shared by both runners so the abort rule stays in one place and cannot
        drift between them. The rule:

        * Without -ContinueOnFailure, any failure stops the run.
        * With -ContinueOnFailure, a per-file failure (agent-failed, gate-failed,
          error) is skipped and the run continues -- that is the switch's purpose.
        * A 'commit-failed' is different: it is systemic. A pre-commit hook or git
          itself keeps rejecting the commit, so advancing to the next file cannot
          make it succeed, and because Invoke-GitCheckpoint stages the whole tree
          (git add -A), the un-committable change is re-staged into every later
          checkpoint and re-fails identically -- a pile-up. So a commit-failed
          stops the run even under -ContinueOnFailure, UNLESS -RevertOnFailure is
          set (which resets the tree clean, making it safe to carry on).
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory)]
        [string]$Status,

        [switch]$ContinueOnFailure,

        [switch]$RevertOnFailure
    )

    if ($Status -eq 'ok') { return $false }
    if (-not $ContinueOnFailure) { return $true }
    if ($Status -eq 'commit-failed' -and -not $RevertOnFailure) { return $true }
    return $false
}

function Invoke-GitCheckpoint {
    <#
    .SYNOPSIS
        Stage the tree and commit a checkpoint, if anything changed.
    .DESCRIPTION
        Run artifacts are kept out of the commit: the default pipeline-artifacts/
        directory is git-ignored, and any custom artifact root that lives inside
        the working tree is unstaged before committing. Git resolves whether the
        path is inside the repo, so an out-of-tree artifact root is a harmless
        no-op (git reports it is outside the repository and the error is ignored).
        The Co-authored-by trailer names the agent that did the work.
    .OUTPUTS
        [string] - the short commit SHA, or $null when there was nothing to commit.
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$RepoRoot,

        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$Subject,

        [ValidateSet('copilot', 'claude')]
        [string]$Agent = 'copilot',

        [string]$ArtifactRoot,

        [switch]$DryRun
    )

    $Trailer = if ($Agent -eq 'claude') {
        'Co-authored-by: Claude <noreply@anthropic.com>'
    }
    else {
        'Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>'
    }

    if ($DryRun) {
        $ExcludeShown = if ($ArtifactRoot) { $ArtifactRoot } else { '<artifact-root>' }
        Write-Host "    [dry-run] git add -A ; git reset -- `"$ExcludeShown`" ; git commit -m `"$Subject`"" -ForegroundColor DarkGray
        return $null
    }

    Push-Location -LiteralPath $RepoRoot
    try {
        git add -A
        if ($LASTEXITCODE -ne 0) { throw 'git add failed.' }

        if ($ArtifactRoot) {
            # Keep run artifacts out of the commit even when the artifact root
            # lives inside the tree and is not git-ignored. A root outside the
            # repo makes git error here; that is expected and ignored.
            git reset -q -- $ArtifactRoot 2>$null | Out-Null
        }

        git diff --cached --quiet
        if ($LASTEXITCODE -eq 0) {
            Write-Host '    No changes to commit for this phase.' -ForegroundColor DarkGray
            return $null
        }

        git commit -m $Subject -m $Trailer | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'git commit failed.' }

        $Sha = (git rev-parse --short HEAD).Trim()
        Write-Host "    Checkpoint $Sha : $Subject" -ForegroundColor Green
        return $Sha
    }
    finally {
        Pop-Location
    }
}

function Save-PipelineArtifact {
    <#
    .SYNOPSIS
        Move the pipeline artifact root aside into .git/ so a destructive
        `git clean` cannot delete it, returning the backup path.
    .DESCRIPTION
        The release-verification phase wipes the workspace with `git clean -Xdf`,
        which removes every git-ignored file - including the git-ignored
        pipeline-artifacts/ directory that holds this run's logs and reports. Git
        never scans inside the .git/ directory, so this moves the artifact root
        there for the duration of that phase; Restore-PipelineArtifact moves it
        back afterwards.

        A distinct backup name (_pipeline-artifacts.pipeline.bak) keeps this
        runner-owned safeguard from ever colliding with the agent-side set-aside
        in the release-verification prompt (_pipeline-artifacts.bak).

        Only an artifact root that exists and lives inside the working tree is
        protected; a root outside the repository is never reached by `git clean`,
        so the function returns $null and does nothing.
    .OUTPUTS
        [string] - the backup path, or $null when there was nothing to protect.
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$ArtifactRoot,

        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$RepoRoot,

        [switch]$DryRun
    )

    if ($DryRun) {
        Write-Host "    [dry-run] move `"$ArtifactRoot`" aside into .git/ across the clean" -ForegroundColor DarkGray
        return $null
    }

    if (-not (Test-Path -LiteralPath $ArtifactRoot -PathType Container)) {
        return $null
    }

    $RepoFull = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd([char]'\', [char]'/')
    $RootFull = [System.IO.Path]::GetFullPath($ArtifactRoot).TrimEnd([char]'\', [char]'/')
    $Sep = [System.IO.Path]::DirectorySeparatorChar
    if (-not $RootFull.StartsWith($RepoFull + $Sep, [System.StringComparison]::OrdinalIgnoreCase)) {
        # Outside the working tree: `git clean` never reaches it.
        return $null
    }

    $GitDir = git -C $RepoRoot rev-parse --absolute-git-dir 2>$null
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($GitDir)) {
        return $null
    }
    $BackupPath = Join-Path -Path $GitDir.Trim() -ChildPath '_pipeline-artifacts.pipeline.bak'

    if (Test-Path -LiteralPath $BackupPath) {
        Remove-Item -LiteralPath $BackupPath -Recurse -Force -ErrorAction SilentlyContinue
    }
    Move-Item -LiteralPath $ArtifactRoot -Destination $BackupPath
    return $BackupPath
}

function Restore-PipelineArtifact {
    <#
    .SYNOPSIS
        Move an artifact root saved by Save-PipelineArtifact back into place.
    .DESCRIPTION
        Best-effort and idempotent: a $null/empty backup path or a missing backup
        directory is a no-op, so a caller can invoke this unconditionally from a
        finally block. If the artifact root was recreated while it was set aside,
        the backed-up entries are merged back into it.
    #>
    [CmdletBinding()]
    param(
        [AllowNull()]
        [AllowEmptyString()]
        [string]$BackupPath,

        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$ArtifactRoot,

        [switch]$DryRun
    )

    if ($DryRun -or [string]::IsNullOrEmpty($BackupPath)) {
        return
    }
    if (-not (Test-Path -LiteralPath $BackupPath -PathType Container)) {
        return
    }

    if (-not (Test-Path -LiteralPath $ArtifactRoot)) {
        $Parent = Split-Path -Parent $ArtifactRoot
        if ($Parent -and -not (Test-Path -LiteralPath $Parent)) {
            New-Item -ItemType Directory -Force -Path $Parent | Out-Null
        }
        Move-Item -LiteralPath $BackupPath -Destination $ArtifactRoot
        return
    }

    # The artifact root exists again: merge the saved entries back into it.
    Get-ChildItem -LiteralPath $BackupPath -Force | ForEach-Object {
        $Target = Join-Path -Path $ArtifactRoot -ChildPath $_.Name
        if (Test-Path -LiteralPath $Target) {
            Remove-Item -LiteralPath $Target -Recurse -Force -ErrorAction SilentlyContinue
        }
        Move-Item -LiteralPath $_.FullName -Destination $Target
    }
    Remove-Item -LiteralPath $BackupPath -Recurse -Force -ErrorAction SilentlyContinue
}

function Get-PythonExecutable {
    <#
    .SYNOPSIS
        Resolve a Python interpreter for the lint/format scripts.
    .DESCRIPTION
        Returns the source path of the first interpreter found, preferring
        'python' (the usual name on Windows, where these runners most often run)
        and falling back to 'python3'. Returns $null when neither is on PATH so
        the caller can skip the lint/format gate rather than fail hard - matching
        the skip-if-missing philosophy of scripts/lint.py and scripts/format.py.
    .OUTPUTS
        [string] - the interpreter path, or $null when none is available.
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()

    foreach ($Name in @('python', 'python3')) {
        $Command = Get-Command -Name $Name -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($Command) {
            return $Command.Source
        }
    }
    return $null
}

function Get-VcvarsScript {
    <#
    .SYNOPSIS
        Locate the MSVC "vcvars64.bat" developer-environment script.
    .DESCRIPTION
        Uses vswhere (shipped with the Visual Studio Installer at a fixed path) to
        find the latest install carrying the VC toolchain and returns the path to
        its vcvars64.bat. Returns $null when vswhere or the script is absent - or
        when not on Windows - so callers degrade gracefully (clang-tidy stays
        skipped) rather than failing.
    .OUTPUTS
        [string] - path to vcvars64.bat, or $null when not found.
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()

    $ProgramFilesX86 = ${env:ProgramFiles(x86)}
    if (-not $ProgramFilesX86) {
        return $null
    }

    $VsWhere = Join-Path -Path $ProgramFilesX86 -ChildPath 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $VsWhere)) {
        return $null
    }

    $InstallPath = & $VsWhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null | Select-Object -First 1
    if (-not $InstallPath) {
        return $null
    }

    $Vcvars = Join-Path -Path $InstallPath -ChildPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (Test-Path -LiteralPath $Vcvars) {
        return $Vcvars
    }
    return $null
}

function Import-MsvcEnvironment {
    <#
    .SYNOPSIS
        Import the MSVC developer environment (INCLUDE/LIB/PATH) into this session.
    .DESCRIPTION
        On Windows, clang-tidy needs the MSVC and Windows SDK header search paths
        to resolve the C++ standard library; those live in the environment that
        vcvars64.bat sets. This runs vcvars in a child cmd, captures the resulting
        variables, and applies them to the current process so later in-process
        calls (the cmake configure below and python scripts/lint.py, whose
        clang-tidy subprocess inherits the environment) can find the headers.

        No-ops and returns $true when not on Windows or when a developer
        environment is already active (VCINSTALLDIR set / cl.exe on PATH). Returns
        $false only when the environment could not be located, so the caller can
        warn that clang-tidy may be skipped.
    .OUTPUTS
        [bool] - $true when an environment is active (already or newly imported).
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param()

    if ($IsWindows -eq $false) {
        return $true
    }
    if ($env:VCINSTALLDIR -or (Get-Command -Name 'cl' -CommandType Application -ErrorAction SilentlyContinue)) {
        return $true
    }

    $Vcvars = Get-VcvarsScript
    if (-not $Vcvars) {
        return $false
    }

    $Captured = & $env:ComSpec /c "call `"$Vcvars`" >nul 2>&1 && set"
    foreach ($Line in $Captured) {
        if ($Line -match '^([^=]+)=(.*)$') {
            Set-Item -Path ('Env:' + $Matches[1]) -Value $Matches[2] -ErrorAction SilentlyContinue
        }
    }
    return $true
}

function Initialize-CompileDatabase {
    <#
    .SYNOPSIS
        Ensure build/compile_commands.json exists so clang-tidy can run.
    .DESCRIPTION
        clang-tidy (run by scripts/lint.py) needs a compile database. The Makefile
        and Ninja generators emit one when CMAKE_EXPORT_COMPILE_COMMANDS is set (it
        is, in every preset), but the Visual Studio and Xcode generators do not -
        so on a default Windows build clang-tidy is skipped for want of a database.

        This closes that gap without disturbing the primary build/ tree (which on
        Windows is a Visual Studio tree that cannot be reconfigured to another
        generator in place): when build/compile_commands.json is missing, it runs a
        configure-only pass with a database-capable generator (Ninja, else NMake /
        Unix Makefiles) into a dedicated build-compiledb/ directory, then copies the
        resulting compile_commands.json into build/ where lint.py looks for it.

        Best-effort by design: it quietly does nothing when clang-tidy is not
        installed (lint.py would skip it anyway) and warns but still succeeds when
        the database could not be produced, so a missing generator never fails the
        gate - it only leaves clang-tidy skipped, exactly as before.
    .OUTPUTS
        [bool] - $true always; lint.py, not this helper, is the gate's authority.
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$RepoRoot,

        [switch]$DryRun
    )

    $BuildDb = Join-Path -Path $RepoRoot -ChildPath 'build/compile_commands.json'

    if ($DryRun) {
        Write-Host '    [dry-run] generate build/compile_commands.json for clang-tidy (configure-only)' -ForegroundColor DarkGray
        return $true
    }

    # Already present (Makefiles/Ninja emitted it, or a previous run copied it in).
    if (Test-Path -LiteralPath $BuildDb) {
        return $true
    }
    # No clang-tidy: lint.py skips it regardless, so a database would be moot.
    if (-not (Get-Command -Name 'clang-tidy' -CommandType Application -ErrorAction SilentlyContinue)) {
        return $true
    }
    if (-not (Get-Command -Name 'cmake' -CommandType Application -ErrorAction SilentlyContinue)) {
        Write-Warning 'cmake not found; cannot generate compile_commands.json (clang-tidy will be skipped).'
        return $true
    }

    # Pick a generator that honours CMAKE_EXPORT_COMPILE_COMMANDS.
    $OnWindows = ($IsWindows -ne $false)
    if (Get-Command -Name 'ninja' -CommandType Application -ErrorAction SilentlyContinue) {
        $Generator = 'Ninja'
    }
    elseif ($OnWindows) {
        $Generator = 'NMake Makefiles'
    }
    else {
        $Generator = 'Unix Makefiles'
    }

    $DbDir = Join-Path -Path $RepoRoot -ChildPath 'build-compiledb'
    Write-Host "    Generating compile_commands.json for clang-tidy (cmake -G `"$Generator`", configure-only)"
    & cmake -S $RepoRoot -B $DbDir -G $Generator `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON `
        -DLUMA_BUILD_TESTS=OFF
    if ($LASTEXITCODE -ne 0) {
        Write-Warning 'Could not configure a compile database; clang-tidy will be skipped this run.'
        return $true
    }

    $GeneratedDb = Join-Path -Path $DbDir -ChildPath 'compile_commands.json'
    if (-not (Test-Path -LiteralPath $GeneratedDb)) {
        Write-Warning 'Configure produced no compile_commands.json; clang-tidy will be skipped.'
        return $true
    }

    $BuildDir = Join-Path -Path $RepoRoot -ChildPath 'build'
    if (-not (Test-Path -LiteralPath $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }
    Copy-Item -LiteralPath $GeneratedDb -Destination $BuildDb -Force
    return $true
}

function Test-LintAndFormat {
    <#
    .SYNOPSIS
        Run the deterministic lint/format quality gate (format.py, then lint.py).
    .DESCRIPTION
        Enforces the repository's formatting and linting the same way CI does, by
        driving the two aggregate scripts:

          * scripts/format.py applies every available formatter plus the safe
            auto-fix subset of the linters (it already orders each language's
            lint --fix before its formatter internally), bringing the tree into
            shape.
          * scripts/lint.py then verifies what remains, including clang-tidy over
            the C++ sources. It is read-only and is the authority for the gate:
            the gate passes only when lint.py exits 0.

        Because lint.py is the verification pass it always runs last. With
        -CheckOnly the mutating format.py step is skipped and only lint.py runs,
        which is what the baseline check wants (verify the starting tree without
        changing it).

        clang-tidy needs build/compile_commands.json; this gate emits one on
        demand (a configure-only pass into build-compiledb/, wrapped in the MSVC
        developer environment on Windows so cl.exe and the headers resolve) and
        lint.py then runs clang-tidy against it. Both the environment import and
        the database generation are best-effort: if the toolchain is missing,
        lint.py simply skips clang-tidy as before rather than hard-failing.

        When no Python interpreter is available the gate is skipped (returns
        $true) rather than failing, mirroring the scripts' own skip-if-missing
        behaviour.
    .OUTPUTS
        [bool] - $true when lint.py passes (or the gate is skipped / dry-run).
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory)]
        [ValidateNotNullOrEmpty()]
        [string]$RepoRoot,

        [switch]$CheckOnly,

        [switch]$DryRun
    )

    if ($DryRun) {
        if (-not $CheckOnly) {
            Write-Host '    [dry-run] python scripts/format.py' -ForegroundColor DarkGray
        }
        Write-Host '    [dry-run] generate build/compile_commands.json for clang-tidy (configure-only)' -ForegroundColor DarkGray
        Write-Host '    [dry-run] python scripts/lint.py' -ForegroundColor DarkGray
        return $true
    }

    $Python = Get-PythonExecutable
    if (-not $Python) {
        Write-Warning 'No Python interpreter (python/python3) on PATH; skipping the lint/format gate.'
        return $true
    }

    # clang-tidy (invoked by lint.py) needs the MSVC header environment on Windows
    # and a compile database in build/. Provide both before linting; each is
    # best-effort and never fails the gate on its own - lint.py stays the authority.
    if (-not (Import-MsvcEnvironment)) {
        Write-Warning 'Could not load the MSVC developer environment; clang-tidy may be skipped. Run from a Developer PowerShell for VS, or ensure vswhere can locate your Visual Studio install.'
    }
    [void](Initialize-CompileDatabase -RepoRoot $RepoRoot)

    Push-Location -LiteralPath $RepoRoot
    try {
        if (-not $CheckOnly) {
            Write-Host '    Formatting: python scripts/format.py'
            & $Python 'scripts/format.py'
            # A non-zero format.py means a formatter hit a problem it could not
            # auto-fix; lint.py is the authority for the gate and surfaces the
            # same issue, so warn here but let lint.py decide pass/fail.
            if ($LASTEXITCODE -ne 0) {
                Write-Warning 'Formatter reported problems it could not auto-fix; see the lint output below.'
            }
        }

        Write-Host '    Linting: python scripts/lint.py'
        & $Python 'scripts/lint.py'
        if ($LASTEXITCODE -ne 0) {
            Write-Warning 'Lint checks failed.'
            return $false
        }

        return $true
    }
    finally {
        Pop-Location
    }
}

Export-ModuleMember -Function @(
    'Get-LumaRepoRoot'
    'Get-AgentExecutable'
    'Write-PhaseBanner'
    'Get-AuditPhase'
    'Get-FixPhase'
    'Invoke-AgentPhase'
    'Test-BuildAndTest'
    'Test-CleanWorkingTree'
    'Test-ShouldAbort'
    'Invoke-GitCheckpoint'
    'Save-PipelineArtifact'
    'Restore-PipelineArtifact'
    'Get-PythonExecutable'
    'Get-VcvarsScript'
    'Initialize-CompileDatabase'
    'Test-LintAndFormat'
)
