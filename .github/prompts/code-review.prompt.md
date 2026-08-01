---
description: "Review code for bugs, security issues, performance pitfalls, and style violations"
agent: "agent"
tools: ["search", "read"]
argument-hint: "File or directory to review, e.g. 'core/runtime/vm/' or 'core/analysis/types/type_checker.cpp'"
version: 1
lastUpdated: "2026-08-01"
---

# Code Review

> **Scope vs sibling prompts:** This prompt surfaces defects — bugs, security, performance, and style. Behaviour-preserving structural improvements (duplication, oversized units, tight coupling) belong in [refactor-audit.prompt.md](refactor-audit.prompt.md), and drift between artefacts (code vs CMake, runtime vs type checker, docs vs implementation) belongs in [consistency-check.prompt.md](consistency-check.prompt.md). Note such findings and cross-reference the right prompt rather than expanding the review here.

Perform a thorough code review of the specified file(s). Read the relevant coding guidelines before starting:

- [cpp.instructions.md](../../instructions/cpp.instructions.md) (`.cpp`, `.hpp`, `.h`) for C++ style and idioms.
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) for general C++ best practices.
- [css.instructions.md](../../instructions/css.instructions.md) (`.css`) for CSS style and idioms.
- [javascript.instructions.md](../../instructions/javascript.instructions.md) (`.js`, `.mjs`, `.cjs`) for JavaScript style and idioms.
- [rust.instructions.md](../../instructions/rust.instructions.md) (`.rs`) for Rust style and idioms.
- [typescript.instructions.md](../../instructions/typescript.instructions.md) (`.ts`, `.tsx`) for TypeScript style and idioms.
- [python.instructions.md](../../instructions/python.instructions.md) (`.py`) for Python style and idioms.
- [shell.instructions.md](../../instructions/shell.instructions.md) (`.sh`, `.bash`) for shell script style and portability.
- [powershell.instructions.md](../../instructions/powershell.instructions.md) (`.ps1`, `.psm1`, `.psd1`) for PowerShell style and idioms.
- [luma.instructions.md](../../instructions/luma.instructions.md) (`.luma`) for Luma language conventions.
- [testing.instructions.md](../../instructions/testing.instructions.md) (`tests/**`) for test code.
- [cmake.instructions.md](../../instructions/cmake.instructions.md) (`CMakeLists.txt`) for CMake files.
- [software-architecture.instructions.md](../../instructions/software-architecture.instructions.md) (`core/`, `shared/`, `language-server/`, `debugger/`) for architecture, modularity, and separation-of-concerns principles.
- [markdown.instructions.md](../../instructions/markdown.instructions.md) (`.md`) for Markdown documentation.
- [readme.instructions.md](../../instructions/readme.instructions.md) (`README.md`) for README files.
- [github-actions.instructions.md](../../instructions/github-actions.instructions.md) (`.github/workflows/**`) for GitHub Actions workflow files.

Verify that:

- **C++ code** follows `cpp.instructions.md` and the C++ Core Guidelines. Flag any deviations.
- **CSS code** follows `css.instructions.md` (including its checklist). Flag any deviations.
- **JavaScript code** follows `javascript.instructions.md` (including its checklist). Flag any deviations.
- **Rust code** follows `rust.instructions.md` (including its checklist). Flag any deviations.
- **TypeScript code** follows `typescript.instructions.md` (including its checklist). Flag any deviations.
- **Python code** follows `python.instructions.md` (including its checklist). Flag any deviations.
- **Shell scripts** follow `shell.instructions.md` (including its checklist). Flag any deviations.
- **PowerShell scripts** follow `powershell.instructions.md` (including its checklist). Flag any deviations.
- **Luma code** follows `luma.instructions.md` (including its checklist). Flag any deviations.
- **Test code** follows `testing.instructions.md` (including its checklist). Flag any deviations.
- **CMake files** follow `cmake.instructions.md` (including its checklist). Flag any deviations.
- **Markdown docs** follow `markdown.instructions.md` (including its checklist). Flag any deviations.
- **README files** follow `readme.instructions.md` (including its checklist). Flag any deviations.
- **GitHub Actions workflows** follow `github-actions.instructions.md` (including its checklist). Flag any deviations.
- **Architecture and design** follow `software-architecture.instructions.md` (including its checklist). Flag any deviations.

## Review Checklist

### Correctness

- Logic errors, off-by-one mistakes, incorrect control flow.
- Unhandled edge cases (empty input, null/optional, integer overflow, boundary values).
- Incorrect assumptions about data (ordering, uniqueness, size).
- Ignored return values — dropped `[[nodiscard]]`, `result<T>`, `optional`, or error codes that must be checked.
- Floating-point pitfalls — equality comparison on doubles, NaN/infinity handling, `integer` vs `number` confusion.
- Uninitialized variables and constructor member-initializer order (declaration order, not init-list order).
- Resource leaks (missing RAII, unclosed handles, dangling references).
- Concurrency issues (data races, deadlocks, use-after-move).
- Any other semantic bug: wrong return values, swapped arguments, broken invariants, incorrect operator usage.

### Security (OWASP Top 10)

- Injection vulnerabilities (unescaped user input, format strings, command/process injection).
- Buffer overflows, out-of-bounds access, or integer overflow feeding allocations or indices.
- Improper input validation at system boundaries.
- Denial of service via resource exhaustion — unbounded recursion, ReDoS, or untrusted lengths driving `reserve()`/allocation (respect `resource_limits.hpp`).
- Path traversal in file or include paths (`..`, symlinks, absolute-path escapes).
- Unsafe deserialization of untrusted input (e.g. `.lumc` bytecode) — validate before trusting counts, offsets, and indices.
- Hardcoded secrets, credentials, or tokens in source.
- Information leakage in error messages (internal paths, stack details, sensitive values).

### Performance

- Unnecessary copies where moves or references would suffice.
- Redundant allocations in hot paths.
- Algorithmic complexity issues (quadratic where linear is possible).
- Missed `reserve()` for known-size containers.
- Loop-invariant work that should be hoisted out of the loop.
- Repeated expensive computation that should be cached, memoized, or lazily initialized.

### Style and Idioms

The bullets below are a quick reference for the reviewer. The authoritative rules — and the full per-language/format checklist — live in each instructions file (linked above); defer to that file on any conflict.

**C++:**

- Naming conventions (`snake_case` functions/variables, `PascalCase` types).
- `const` correctness and `[[nodiscard]]` usage.
- Explicit single-argument constructors.
- West-const style (`const int` not `int const`).
- Braces on all control structures.

**Rust:**

- Naming conventions per `rust.instructions.md` (`snake_case` functions/variables/modules, `PascalCase` types/traits/enum variants, `UPPER_CASE` constants).
- Ownership and borrowing — prefer `&T`/`&mut T` over owned parameters; no unnecessary `clone()`.
- `Result<T, E>` and `Option<T>` always handled — no unguarded `unwrap()`. Use `?` for propagation.
- `unsafe` blocks isolated, minimal, and documented with `// SAFETY:` comments.
- `cargo fmt` and `cargo clippy` clean. Trailing commas in multiline structures.
- Default to private visibility — expose only what consumers need.

**TypeScript:**

- Naming conventions per `typescript.instructions.md` (`camelCase` variables/functions, `PascalCase` types/classes/interfaces, `UPPER_CASE` constants).
- `strict: true` — no `any` types. Use `unknown` and narrow with type guards.
- Explicit return types on exported and public functions.
- `const` by default. `readonly` on properties that should not change. No `var`.
- `async`/`await` over raw `.then()` chains. No floating promises.
- No non-null assertions (`!`) without clear justification.
- Semicolons required. Double quotes. Trailing commas in multiline structures.

**JavaScript:**

- Naming conventions per `javascript.instructions.md` (`camelCase` variables/functions, `PascalCase` classes, `UPPER_CASE` constants, `#` prefix for private fields).
- `const` by default. No `var`. `let` only when reassignment is needed.
- Strict equality (`===`/`!==`) everywhere — no `==` or `!=`.
- `async`/`await` over raw `.then()` chains. No floating promises.
- No `eval()`, `new Function()`, or `innerHTML` with untrusted data.
- Semicolons required. Double quotes. Trailing commas in multiline structures.

**Python:**

- Naming conventions per `python.instructions.md` (`snake_case` variables/functions/modules, `PascalCase` classes, `UPPER_CASE` constants, `_` prefix for private members).
- Type hints on all function parameters and return types. No `Any`.
- Specific exception handling — no bare `except:`. Chain exceptions with `raise ... from err`.
- No mutable default arguments. No wildcard imports. No `eval()`/`exec()` on untrusted input.
- f-strings for interpolation. `str.join()` for concatenation. `with` for resource management.
- Imports grouped (stdlib → third-party → local) and sorted.

**Shell:**

- `#!/usr/bin/env bash` shebang. `set -euo pipefail` at the top.
- All variable expansions double-quoted. All function variables declared `local`. Constants declared `readonly`.
- `[[ ]]` for conditionals, `$(command)` for substitution — no backticks, no `[ ]`.
- No GNU-only flags (`sed -i`, `readlink -f`, `grep -P`) — must run on Linux and macOS.
- Temporary files use `mktemp` with `trap` cleanup. Error messages to stderr (`>&2`).
- No `eval`, no parsing of `ls` output, no useless `cat`. `shellcheck` clean.

**PowerShell:**

- Naming conventions per `powershell.instructions.md` (`PascalCase` variables/functions/parameters, approved Verb-Noun cmdlet names).
- `[CmdletBinding()]` on all non-trivial functions. `[switch]` for boolean flags — not `[bool]`.
- Full cmdlet names — no aliases in scripts (`Get-ChildItem` not `gci`, `ForEach-Object` not `%`).
- `$ErrorActionPreference = 'Stop'` and `Set-StrictMode -Version Latest`. `try`/`catch` with specific types.
- Objects output — not formatted text. `Write-Verbose` for diagnostics, not `Write-Host`.
- No array `+=` in loops. No `Invoke-Expression` on untrusted input. Paths via `Join-Path`.

**CSS:**

- Class names follow BEM or a consistent methodology — no IDs for styling.
- All colours and spacing use custom properties — no hardcoded values in components.
- Specificity is low and predictable — no `!important`, no deeply nested selectors.
- Layout uses Flexbox or Grid — no floats for layout purposes.
- Responsive design is mobile-first with `min-width` breakpoints.
- Focus styles visible on all interactive elements. `prefers-reduced-motion` respected.

**Luma:**

- Naming conventions per `luma.instructions.md` (`snake_case` variables/functions, `PascalCase` records/choices/interfaces).
- Explicit type annotations on all variables, parameters, and return types.
- Immutable by default — `mutable` only when mutation is genuinely needed.
- No semicolons. String interpolation over concatenation. Pipes for chained transformations.
- All `result<T>` values handled — no naked `Result.unwrap`.
- `task_scope` for concurrent tasks — no bare `spawn`/`await`.

**CMake:**

- Naming conventions per `cmake.instructions.md` (`snake_case` targets/functions, `UPPER_SNAKE_CASE` cache variables/options, options prefixed with the project name).
- Target-based configuration — `target_*` commands with explicit `PRIVATE`/`PUBLIC`/`INTERFACE` visibility. No global `CMAKE_CXX_FLAGS`.
- No `file(GLOB)` for sources — list files explicitly. Pin external dependency versions via namespaced imported targets.
- No legacy commands (`add_definitions`, `include_directories`, `link_directories`). Compiler warnings enabled on all project targets.
- 4-space indentation. Blank lines separate logical sections.

**Markdown:**

- One level-one heading matching the document subject — no skipped heading levels.
- Every fenced code block has a language identifier. No indented (four-space) code blocks.
- Tables padded so pipe characters align vertically across all rows.
- Descriptive link text — no bare URLs or "click here". Internal links resolve to files and anchors that exist.
- No multiple consecutive blank lines. File ends with a single trailing newline.

**GitHub Actions:**

- Naming conventions per `github-actions.instructions.md` (`kebab-case` job IDs, step IDs, and artifact names). 2-space YAML indentation.
- Explicit least-privilege `permissions` — not relying on defaults. Every job has `timeout-minutes`.
- `concurrency` with `cancel-in-progress`. Path filters set on triggers to avoid unnecessary runs.
- Actions pinned to a full commit SHA. `actions/checkout` sets `persist-credentials: false` with minimal `fetch-depth`.
- Secrets and untrusted inputs passed via `env:`, never interpolated directly into `run:` blocks (script injection).
- Every step has a descriptive `name:`. Shell logic under ~15 lines — longer scripts live in a file.

### Readability

- Meaningful, self-documenting identifiers — names should convey intent without needing a comment.
- Logical grouping through blank lines — separate distinct steps, sections, or concerns within a function.
- Consistent spacing around operators, after commas, and inside control structures.
- Avoid deep nesting; prefer early returns, guard clauses, pipes, or helper functions to flatten logic.
- Comments explain _why_, not _what_ — delete redundant or stale comments.

### Simplicity and Responsibility

- Functions should be small and do one thing (Single Responsibility Principle).
- Classes (C++) / structs, enums, and traits (Rust) / classes and interfaces (TypeScript) / records and choice types (Luma) should have a single, well-defined responsibility.
- Prefer simple, straightforward logic over clever or compact code.
- Flag functions that are too long or mix multiple concerns.
- One concept per file (Luma). One primary export per file (TypeScript). One module per file (Rust).

### Maintainability

- Dead code or unreachable branches.
- Overly complex functions that should be decomposed.
- Missing or misleading comments on non-obvious logic.
- Duplicated logic that should be shared.
- Magic numbers and literals that should be named constants (centralize limits in `resource_limits.hpp`).
- Reinvented functionality — prefer existing shared utilities (`core/common/`, `shared/`) over re-implementing.
- Hard-to-test code — hidden dependencies or missing seams that should be injected for isolation.

### Developer Experience (Code as a Product)

- Public APIs are easy to use correctly and hard to misuse — sensible defaults, least astonishment, symmetric operations (open/close, lock/unlock, push/pop).
- Function signatures are clear — minimal parameters, no boolean traps, no adjacent same-type arguments that are easy to swap; prefer enums or structs over bare flags.
- Naming is consistent with the surrounding codebase vocabulary — the same concept uses the same word everywhere; new code follows established patterns rather than reinventing them.
- Public contracts are documented — preconditions, ownership/lifetime, and error/`result` semantics stated where they are not obvious from the signature.
- Diagnostics and error messages are a first-class surface — specific, actionable, and located (point at the source span); suggest a fix where possible ("did you mean?"). Applies especially to lexer, parser, type checker, linter, LSP, and DAP output.
- Public-interface changes preserve backward compatibility, or the break is intentional and called out.
- Accessibility is honored where code renders UI (GraphicalUi/CSS) — focus states, reduced-motion, semantic structure.

## Output Format

For each finding, report:

1. **File and location** (file path and line range).
2. **Severity** — Bug, Security, Performance, Style, or Suggestion.
3. **Description** — What the problem is and why it matters.
4. **Recommendation** — How to fix it (with a code snippet if helpful).

Prioritise bugs and security issues over style nits. Group findings by file.
