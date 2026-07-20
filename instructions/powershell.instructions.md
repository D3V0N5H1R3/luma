---
description: "Use when writing, reviewing, or modifying PowerShell scripts (.ps1, .psm1, .psd1 files). Covers naming, style, error handling, pipeline patterns, modules, and idiomatic PowerShell."
applyTo: "**/*.{ps1,psm1,psd1}"
---

# Working with PowerShell

These instructions govern how you write PowerShell scripts and modules. Every function, module, and script you produce must follow these principles. They are aligned with the [PowerShell Practice and Style Guide](https://poshcode.gitbook.io/powershell-practice-and-style/), the official [PowerShell documentation](https://learn.microsoft.com/en-us/powershell/), and community best practices.

---

## Table of Contents

1. [Simplicity First](#1--simplicity-first)
2. [Naming Conventions](#2--naming-conventions)
3. [Consistent Style](#3--consistent-style)
4. [Variables and Scope](#4--variables-and-scope)
5. [Functions and Cmdlets](#5--functions-and-cmdlets)
6. [Pipeline and Output](#6--pipeline-and-output)
7. [Error Handling](#7--error-handling)
8. [Parameters and Validation](#8--parameters-and-validation)
9. [Modules and Script Structure](#9--modules-and-script-structure)
10. [Objects and Types](#10--objects-and-types)
11. [Strings and Formatting](#11--strings-and-formatting)
12. [File and Path Handling](#12--file-and-path-handling)
13. [Collections and Iteration](#13--collections-and-iteration)
14. [Self-Documenting Code](#14--self-documenting-code)
15. [Whitespace as Structure](#15--whitespace-as-structure)
16. [Security Essentials](#16--security-essentials)
17. [Testing](#17--testing)
18. [Performance](#18--performance)
19. [Anti-Patterns](#19--anti-patterns)
20. [Checklist](#20--checklist)

---

## 1 — Simplicity First

Write the simplest script that solves the problem correctly.

- Prefer straightforward control flow over clever one-liners.
- Use built-in cmdlets and .NET methods before writing custom implementations.
- A developer unfamiliar with the script should understand what it does within minutes.
- Leverage the pipeline — it is PowerShell's defining feature and often the simplest approach.

**Test:** Before committing to an approach, ask — _is there a simpler way?_

---

## 2 — Naming Conventions

| Entity             | Convention           | Examples                                               |
| ------------------ | -------------------- | ------------------------------------------------------ |
| Functions/cmdlets  | `PascalCase`         | `Get-BuildOutput`, `Invoke-LumaTest`                   |
| Verb-Noun pairing  | Approved verbs       | `Get-`, `Set-`, `New-`, `Remove-`, `Invoke-`, `Test-`  |
| Variables          | `PascalCase`         | `$TotalCount`, `$OutputDir`, `$IsVerbose`              |
| Parameters         | `PascalCase`         | `-FilePath`, `-OutputDirectory`, `-Force`              |
| Constants          | `PascalCase`         | `$MaxRetries`, `$DefaultTimeout`                       |
| Private functions  | `PascalCase`         | Not exported from module — visibility controls access  |
| File names         | `PascalCase`         | `Build-Project.ps1`, `LumaHelpers.psm1`               |
| Boolean variables  | question             | `$IsReady`, `$HasErrors`, `$ShouldClean`               |
| Script parameters  | `PascalCase`         | `param($BuildDir, $Configuration)`                     |

- Use [approved verbs](https://learn.microsoft.com/en-us/powershell/scripting/developer/cmdlet/approved-verbs-for-windows-powershell-commands) for function names. Run `Get-Verb` to list them.
- Name what the value represents, not its type. `$OutputDirectory` — good. `$d` — bad.
- Avoid abbreviations unless universally understood (`$Id`, `$Url`, `$Pid`).
- Use full cmdlet names in scripts, not aliases (`Get-ChildItem` not `gci`, `ForEach-Object` not `%`).

---

## 3 — Consistent Style

- **Indentation:** 4 spaces. No tabs.
- **Line length:** 100 characters maximum. Use splatting or backtick continuation at logical boundaries.
- **Braces:** Opening brace on the same line as the statement. Closing brace on its own line, aligned with the keyword.
- **Semicolons:** Do not use semicolons to separate statements. One statement per line.
- **Quoting:** Use double quotes for interpolated strings. Use single quotes for literal strings.
- **Cmdlet names:** Use full cmdlet names in scripts. Never use aliases in saved scripts or modules.

```powershell
# Good — consistent style.
function Build-Project {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$BuildDir,

        [string]$Configuration = 'Release'
    )

    if (-not (Test-Path -Path $BuildDir)) {
        New-Item -Path $BuildDir -ItemType Directory -Force | Out-Null
    }

    cmake -B $BuildDir -DCMAKE_BUILD_TYPE=$Configuration
    cmake --build $BuildDir --parallel
}

# Bad — aliases, inconsistent style, semicolons.
function build {
    param($d, $c = 'Release')
    if(!(test-path $d)){md $d | Out-Null}; cmake -B $d; cmake --build $d
}
```

---

## 4 — Variables and Scope

- Declare variables close to their first use.
- Use `$script:` or `$global:` scope modifiers sparingly and deliberately. Prefer passing data through parameters and return values.
- Use `[type]` constraints on variables when the type matters: `[string]$Name`, `[int]$Count`.
- Avoid `$global:` variables. Use module-scoped variables (`$script:`) only for module state.
- Use `Set-StrictMode -Version Latest` in scripts and modules for stricter variable checking.

```powershell
# Good — typed, scoped, strict.
Set-StrictMode -Version Latest

[string]$BuildDir = Join-Path -Path $PSScriptRoot -ChildPath 'build'
[int]$MaxRetries = 3

# Bad — untyped, global.
$global:builddir = "$PSScriptRoot\build"
$retries = 3
```

---

## 5 — Functions and Cmdlets

- Keep functions small — one logical operation per function.
- Use `[CmdletBinding()]` on all non-trivial functions for consistent parameter handling and `-Verbose`/`-Debug` support.
- Use `[OutputType()]` to declare what the function returns.
- Use `begin`/`process`/`end` blocks for pipeline-aware functions.
- Prefer early returns with `return` to avoid deep nesting.
- Write functions that accept pipeline input when processing collections of items.

```powershell
# Good — CmdletBinding, OutputType, pipeline support.
function Get-LumaSourceFile {
    [CmdletBinding()]
    [OutputType([System.IO.FileInfo])]
    param(
        [Parameter(Mandatory, ValueFromPipeline)]
        [string]$Path
    )

    process {
        Get-ChildItem -Path $Path -Filter '*.luma' -Recurse -File
    }
}

# Good — early return, guard clause.
function Invoke-Build {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$ProjectPath
    )

    if (-not (Test-Path -Path $ProjectPath)) {
        Write-Error "Project path not found: $ProjectPath"
        return
    }

    cmake --build $ProjectPath --config Release
}
```

---

## 6 — Pipeline and Output

The pipeline is PowerShell's core strength. Work with it, not against it.

- Output objects, not formatted text. Let the caller decide how to format.
- Use `Write-Output` (or implicit output) for data. Use `Write-Host` only for user-facing interactive messages.
- Use `Write-Verbose`, `Write-Debug`, and `Write-Information` for diagnostic messages.
- Use `Write-Error` for non-terminating errors. Use `throw` for terminating errors.
- Suppress unwanted output with `| Out-Null` or `[void]`, not by assigning to `$null` and discarding.
- Do not use `return` to output values — PowerShell outputs everything not captured. Use `return` only for flow control.

```powershell
# Good — outputs objects, pipeline-friendly.
function Get-BuildResult {
    [CmdletBinding()]
    [OutputType([PSCustomObject])]
    param(
        [string]$BuildDir = 'build'
    )

    Get-ChildItem -Path $BuildDir -Filter '*.exe' -File | ForEach-Object {
        [PSCustomObject]@{
            Name     = $_.BaseName
            Path     = $_.FullName
            SizeKB   = [math]::Round($_.Length / 1KB, 2)
            Modified = $_.LastWriteTime
        }
    }
}

# Bad — outputs formatted text, not composable.
function Get-BuildResult {
    Get-ChildItem build -Filter *.exe | ForEach-Object {
        Write-Host "$($_.Name) - $($_.Length) bytes"
    }
}
```

---

## 7 — Error Handling

- Use `$ErrorActionPreference = 'Stop'` at the top of scripts to make all errors terminating.
- Use `try`/`catch`/`finally` for critical operations.
- Catch specific exception types when possible, not bare `catch`.
- Use `Write-Error` for non-terminating errors the caller can recover from.
- Use `throw` for terminating errors that should halt execution.
- Always clean up resources in `finally` blocks.
- Write error messages that explain _what_ failed and _why_.

```powershell
# Good — strict error handling, specific catch, cleanup.
$ErrorActionPreference = 'Stop'

try {
    $Result = Invoke-RestMethod -Uri $ApiUrl -Method Get
    $Result | ConvertTo-Json | Set-Content -Path $OutputFile
}
catch [System.Net.WebException] {
    Write-Error "Network error fetching $ApiUrl : $_"
}
catch {
    Write-Error "Unexpected error: $_"
    throw
}
finally {
    if ($TempFile -and (Test-Path -Path $TempFile)) {
        Remove-Item -Path $TempFile -Force
    }
}
```

---

## 8 — Parameters and Validation

- Use `[Parameter()]` attributes for mandatory parameters, pipeline input, and position.
- Use `[ValidateNotNullOrEmpty()]`, `[ValidateSet()]`, `[ValidateRange()]`, `[ValidatePattern()]`, and `[ValidateScript()]` for input validation.
- Provide sensible defaults for optional parameters.
- Use `[switch]` for boolean flags — never use `[bool]` parameters.
- Group related parameters with parameter sets when a function has mutually exclusive modes.

```powershell
# Good — validated parameters, sensible defaults.
function Export-TestResults {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory, Position = 0)]
        [ValidateScript({ Test-Path -Path $_ -PathType Container })]
        [string]$TestDir,

        [ValidateSet('xml', 'json', 'csv')]
        [string]$Format = 'json',

        [ValidateRange(1, 100)]
        [int]$MaxResults = 50,

        [switch]$IncludeSkipped
    )

    # ...
}
```

---

## 9 — Modules and Script Structure

- Use modules (`.psm1`) for reusable function libraries. Use scripts (`.ps1`) for executable tasks.
- Export only public functions from modules. Use `Export-ModuleMember` or a module manifest (`.psd1`).
- Use `$PSScriptRoot` for resolving paths relative to the script/module location.
- Place `#Requires` statements at the top for PowerShell version and module dependencies.
- Organise large modules into multiple files and dot-source them from the `.psm1`.

```powershell
# LumaHelpers.psm1 — module with selective exports.
#Requires -Version 7.0

. "$PSScriptRoot/Private/Format-Output.ps1"
. "$PSScriptRoot/Public/Build-Project.ps1"
. "$PSScriptRoot/Public/Invoke-LumaTest.ps1"

Export-ModuleMember -Function @(
    'Build-Project'
    'Invoke-LumaTest'
)
```

```powershell
# build.ps1 — standalone script with requirements.
#Requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$BuildDir = Join-Path -Path $PSScriptRoot -ChildPath 'build'
# ...
```

---

## 10 — Objects and Types

- Output `[PSCustomObject]` for structured data. Avoid hashtables for output — they lack type information and display poorly.
- Use classes (`class`) for complex types with methods and validation.
- Use `[ordered]` hashtables when key order matters.
- Prefer .NET types over string parsing: `[System.IO.Path]`, `[System.Uri]`, `[datetime]`.
- Use `[enum]` values instead of magic strings where applicable.

```powershell
# Good — structured output with PSCustomObject.
[PSCustomObject]@{
    Name    = 'luma'
    Version = '1.0.0'
    Path    = $ExePath
    Status  = 'Success'
}

# Good — .NET types over string manipulation.
$FileName = [System.IO.Path]::GetFileNameWithoutExtension($FilePath)
$Extension = [System.IO.Path]::GetExtension($FilePath)

# Bad — string parsing for paths.
$FileName = $FilePath.Split('\')[-1].Split('.')[0]
```

---

## 11 — Strings and Formatting

- Use double-quoted strings for interpolation: `"Build completed: $BuildDir"`.
- Use single-quoted strings for literals: `'no interpolation here'`.
- Use here-strings (`@"..."@` or `@'...'@`) for multiline text.
- Use `-f` format operator or `[string]::Format()` for complex formatting.
- Use `Join-Path` for path construction — never string concatenation.

```powershell
# Good — interpolation with subexpression.
Write-Verbose "Found $($Files.Count) files in $Directory"

# Good — here-string for multiline.
$Usage = @"
Usage: build.ps1 [-Configuration <string>] [-Clean]

Options:
    -Configuration  Build configuration (Debug, Release). Default: Release.
    -Clean          Remove build artifacts before building.
"@

# Good — format operator for alignment.
'{0,-20} {1,10}' -f $Name, $Value

# Bad — string concatenation for paths.
$FullPath = $RootDir + '\' + $SubDir + '\' + $FileName
```

---

## 12 — File and Path Handling

- Always use `Join-Path` for constructing paths.
- Use `$PSScriptRoot` for paths relative to the current script.
- Use `Resolve-Path` to normalise paths. Use `Test-Path` before accessing files.
- Use `[System.IO.Path]` methods for path manipulation.
- Prefer `Get-Content`/`Set-Content` with `-Encoding utf8` for text files.
- Use `New-TemporaryFile` for temporary files and clean up in `finally` blocks.

```powershell
# Good — portable path handling.
$ProjectRoot = Split-Path -Path $PSScriptRoot -Parent
$BuildDir = Join-Path -Path $ProjectRoot -ChildPath 'build'
$OutputFile = Join-Path -Path $BuildDir -ChildPath 'results.json'

if (-not (Test-Path -Path $BuildDir)) {
    New-Item -Path $BuildDir -ItemType Directory -Force | Out-Null
}

# Good — temporary file with cleanup.
$TempFile = New-TemporaryFile

try {
    Set-Content -Path $TempFile.FullName -Value $Data -Encoding utf8
    # ... process ...
}
finally {
    Remove-Item -Path $TempFile.FullName -Force -ErrorAction SilentlyContinue
}
```

---

## 13 — Collections and Iteration

- Use `@()` to ensure array results from commands that might return a single item or `$null`.
- Use `ForEach-Object` for pipeline iteration. Use `foreach` statement for in-memory collections.
- Use `Where-Object` for filtering. Use `Select-Object` for projection and limiting.
- Avoid `+=` for building arrays in loops — it creates a new array on every iteration. Use `[System.Collections.Generic.List[object]]` or collect pipeline output.
- Use `Group-Object`, `Sort-Object`, and `Measure-Object` for aggregation.

```powershell
# Good — List for accumulation.
$Results = [System.Collections.Generic.List[string]]::new()

foreach ($File in $Files) {
    $Results.Add($File.FullName)
}

# Good — pipeline collection.
$Results = Get-ChildItem -Path $Dir -Filter '*.luma' | ForEach-Object {
    $_.FullName
}

# Bad — array += in loop (O(n²)).
$Results = @()

foreach ($File in $Files) {
    $Results += $File.FullName
}
```

---

## 14 — Self-Documenting Code

Write scripts that explain themselves. Reserve comments for _why_, not _what_.

- Use comment-based help (`<# .SYNOPSIS #>`) for all public functions and scripts.
- Use descriptive variable and parameter names so the code reads naturally.
- Delete stale or redundant comments. A wrong comment is worse than no comment.
- Use `Write-Verbose` as executable documentation for complex operations.

```powershell
# Bad — restates the code.
# Get all files.
$Files = Get-ChildItem -Path $Dir

# Good — explains a non-obvious constraint.
# CMake requires forward slashes in paths even on Windows.
$NormalizedPath = $BuildDir -replace '\\', '/'

<#
.SYNOPSIS
    Runs the Luma test suite and returns structured results.

.DESCRIPTION
    Discovers .luma test files under the specified directory,
    runs each with `luma --test`, and outputs pass/fail objects.

.PARAMETER TestDir
    The directory containing .luma test files.

.EXAMPLE
    Invoke-LumaTest -TestDir tests/features
#>
function Invoke-LumaTest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$TestDir
    )

    # ...
}
```

---

## 15 — Whitespace as Structure

Use blank lines to reveal logical structure — like paragraphs in prose.

- **One blank line** between function definitions.
- **One blank line** between logical blocks within a function (setup, processing, result).
- **One blank line** after `param()` blocks.
- **No** multiple consecutive blank lines.
- **No** trailing whitespace. One trailing newline at end of file.
- Align nothing with extra spaces. Let consistent indentation handle grouping.

---

## 16 — Security Essentials

- Never use `Invoke-Expression` with untrusted input — this is code injection.
- Validate and sanitise all external input before use.
- Use `SecureString` or `PSCredential` for sensitive data — never store passwords as plain strings.
- Avoid `-ExecutionPolicy Bypass` in scripts — configure execution policy properly at the system level.
- Use `-LiteralPath` when paths come from external input to prevent wildcard expansion.
- Do not store secrets in scripts. Use environment variables, credential stores, or secret management modules.
- Use `[System.Net.ServicePointManager]::SecurityProtocol` to enforce TLS 1.2+ for web requests.

```powershell
# Good — LiteralPath for external input.
Get-Content -LiteralPath $UserProvidedPath

# Bad — Path allows wildcard expansion with untrusted input.
Get-Content -Path $UserProvidedPath

# Good — secure credential handling.
$Credential = Get-Credential

# Bad — plaintext password.
$Password = 'hunter2'

# Bad — code injection.
Invoke-Expression $UserInput
```

---

## 17 — Testing

- Use [Pester](https://pester.dev/) as the test framework.
- Name test files `*.Tests.ps1` alongside the code they test.
- Name test blocks descriptively: `It 'Returns empty array for missing directory'`.
- Use `Describe`/`Context`/`It` for test organisation.
- Use `BeforeAll`/`BeforeEach`/`AfterAll`/`AfterEach` for setup and teardown.
- Use `Should` assertions: `-Be`, `-BeExactly`, `-BeNullOrEmpty`, `-Throw`, `-HaveCount`.
- Mock external dependencies with `Mock` to isolate units.

```powershell
# Build-Project.Tests.ps1
Describe 'Build-Project' {
    BeforeAll {
        . "$PSScriptRoot/Build-Project.ps1"
    }

    Context 'When build directory does not exist' {
        It 'Creates the directory' {
            Mock New-Item {}
            Mock Test-Path { $false }

            Build-Project -BuildDir 'build'

            Should -Invoke New-Item -Times 1
        }
    }

    Context 'When build succeeds' {
        It 'Returns zero exit code' {
            $Result = Build-Project -BuildDir 'build'
            $Result.ExitCode | Should -Be 0
        }
    }
}
```

---

## 18 — Performance

- Avoid `+=` on arrays in loops. Use `[List[T]]` or pipeline collection.
- Avoid `Write-Host` in performance-critical code — it is slow.
- Use `[hashtable]` for O(1) lookups instead of repeated `Where-Object` filtering.
- Use `-Filter` parameter on cmdlets (`Get-ChildItem -Filter`) instead of `Where-Object` — it filters at the provider level.
- Avoid repeated cmdlet calls in loops. Cache results in variables.
- Use `[System.Text.StringBuilder]` for intensive string building.
- Use `ForEach-Object -Parallel` (PowerShell 7+) for CPU-bound parallel tasks.
- Profile with `Measure-Command` before optimising.

```powershell
# Good — hashtable for O(1) lookup.
$UserLookup = @{}

foreach ($User in $Users) {
    $UserLookup[$User.Id] = $User
}

foreach ($Id in $TargetIds) {
    $User = $UserLookup[$Id]
    # ...
}

# Bad — O(n²) repeated filtering.
foreach ($Id in $TargetIds) {
    $User = $Users | Where-Object { $_.Id -eq $Id }
    # ...
}
```

---

## 19 — Anti-Patterns

- **Aliases in scripts.** Use full cmdlet names (`Get-ChildItem` not `gci`, `ForEach-Object` not `%`, `Where-Object` not `?`).
- **`Invoke-Expression` with external input.** This is code injection. Never use it on untrusted data.
- **`$global:` variables.** Pass data through parameters and return values.
- **`Write-Host` for output.** Use `Write-Output` for data, `Write-Verbose` for diagnostics.
- **Array `+=` in loops.** Use `[List[T]]` or pipeline collection.
- **String concatenation for paths.** Use `Join-Path`.
- **Bare `catch` blocks.** Catch specific exception types when possible.
- **`[bool]` parameters.** Use `[switch]` instead.
- **Missing `[CmdletBinding()]`.** Include it on all non-trivial functions.
- **Hardcoded paths.** Use `$PSScriptRoot`, `Join-Path`, and parameters.
- **Semicolons as statement separators.** One statement per line.

---

## 20 — Checklist

- [ ] Full cmdlet names used — no aliases in scripts or modules.
- [ ] `[CmdletBinding()]` on all non-trivial functions.
- [ ] Parameters use `[Parameter()]` attributes and validation (`[ValidateNotNullOrEmpty()]`, etc.).
- [ ] `[switch]` used for boolean flags — not `[bool]`.
- [ ] `Set-StrictMode -Version Latest` and `$ErrorActionPreference = 'Stop'` set in scripts.
- [ ] `try`/`catch`/`finally` for critical operations with specific exception types.
- [ ] Objects output instead of formatted text. `Write-Verbose` for diagnostics.
- [ ] No array `+=` in loops. `[List[T]]` or pipeline collection used.
- [ ] Paths constructed with `Join-Path`. `$PSScriptRoot` for relative paths.
- [ ] No `Invoke-Expression` on untrusted input. `-LiteralPath` for external paths.
- [ ] Pester tests cover the primary success and failure paths.
- [ ] Names follow the conventions in §2. Approved verbs used.
- [ ] Files end with a single trailing newline.
