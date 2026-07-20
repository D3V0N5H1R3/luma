---
description: "Iteratively review, fix, build, and test the project until no significant issue remains"
agent: "agent"
---

# Iterative Improvement

Iteratively improve the project by repeating the cycle below until a complete review surfaces no further significant issues. This prompt orchestrates the project's focused prompts — use them as the primary reference for each phase, and fix every issue you find as you go.

A **significant** issue is anything that affects correctness, security, or reliability, or that violates the project's documented design and conventions. Cosmetic preferences that the style guides and the guides under `instructions/` do not mandate are out of scope.

## The Cycle

Repeat the full sequence of phases until one complete pass finds no significant issues and leaves every build and test clean.

- Work through Phases 1–5 in order; each phase must complete cleanly before the next.
- Fix every significant issue as you find it. After each fix, re-run the build and the affected tests so regressions surface immediately.
- You need not restart the whole sequence for every individual fix — finish and re-verify the current phase, then continue.
- After completing Phase 5, return to Phase 1 and review the entire project again. Stop when a complete pass surfaces nothing significant, then confirm the Final Gate.

## Guidelines

- Comply with the conventions in every relevant guide under `instructions/`.
- Respect the design and behaviour described in the documents under `documents/`.
- Run, test, and check all Luma source in strict mode — the `--strict` (`-s`) flag, which treats warnings as errors.
- Prefer the smallest change that fully resolves an issue. Make surgical, well-scoped fixes; do not refactor unrelated code or introduce regressions.

## Phase 1 — Review and Refactor

Run these prompts in sequence on the full project:

1. **Code review** — Run the [code-review](code-review.prompt.md) prompt on each major subsystem:
    - `core/runtime/` (VM, compiler, stdlib)
    - `core/analysis/` (lexer, parser, type checker, resolver, linter)
    - `core/common/` (shared utilities: resource limits, result types, caches, UTF-8)
    - `shared/` (JSON, protocol transport, stdlib metadata, symbols)
    - `language-server/source/`
    - `debugger/source/`
      Fix all Bug and Security findings. Fix Performance and Style findings where the fix is safe.

2. **Refactor** — Run the [refactor](refactor.prompt.md) prompt for any structural issues found during review.

3. **Update documentation** — Update all documents in `documents/` to reflect any code changes made during review and refactoring. Ensure the user manual and architecture document are current.

If a full pass through this phase surfaces no significant issues — and the most recent build, tests, and consistency check are still clean — the project needs no further improvement. Confirm the Final Gate and stop.

## Phase 2 — Consistency

Run the [consistency-check](consistency-check.prompt.md) prompt on the full project. Fix all discrepancies found across:

- Source code vs CMakeLists.
- Runtime vs type checker vs language server vs debugger.
- Documentation vs implementation (both directions: undocumented features and unimplemented documented features).
- Tests, benchmarks, and examples vs current API.
- Editor extensions vs language syntax — tree-sitter and TextMate grammars, shared generated config, and the feature-parity matrix.
- Configurations, scripts, and CI workflows.

## Phase 3 — Build

1. Clean all build artefacts:

    ```bash
    # Linux/macOS
    rm -rf build build-debug build-fuzz
    ```

    ```powershell
    # Windows
    Remove-Item -Recurse -Force build, build-debug, build-fuzz -ErrorAction SilentlyContinue
    ```

2. Configure and build from scratch with warnings as errors:

    ```bash
    cmake --preset default
    cmake --build --preset default
    ```

3. Fix any build errors or warnings. Zero warnings is the goal.

## Phase 4 — Test

Run the [full-test-sweep](full-test-sweep.prompt.md) prompt. All categories must pass:

- C++ unit tests (CTest)
- Luma feature tests (strict mode)
- Fuzz tests (smoke run)
- Benchmarks (correctness, not performance)
- Examples (strict mode parse and type-check)

## Phase 5 — Lint and Format

Run the [lint-and-format](lint-and-format.prompt.md) prompt (C++ and Markdown) and the [source-code-cleanup](source-code-cleanup.prompt.md) prompt (every other language), applying each language's formatter and linter:

- `clang-tidy` fixes for bugs, security, and performance, then `clang-format` on all C++ source files.
- Ruff (`ruff check` and `ruff format`) on Python.
- ShellCheck on shell scripts.
- PSScriptAnalyzer on PowerShell scripts.
- Stylelint on CSS.
- rustfmt and Clippy on Rust.
- Prettier and ESLint on TypeScript and JavaScript.
- `luma --strict` on Luma sources.
- markdownlint-cli2 on Markdown, then review against [markdown.instructions.md](../../instructions/markdown.instructions.md).

After formatting, rebuild and rerun tests to confirm no regressions, then return to Phase 1.

## Final Gate

Stop only when a complete pass satisfies every condition below:

- [ ] A full review (Phase 1) and consistency pass (Phase 2) surfaced no significant issues.
- [ ] All build targets compile with zero warnings.
- [ ] All C++ unit tests pass.
- [ ] All Luma feature tests pass in strict mode.
- [ ] All fuzz targets run without crashing.
- [ ] All benchmarks execute without errors.
- [ ] All examples parse and type-check in strict mode.
- [ ] No clang-tidy bugs or security warnings remain.
- [ ] All language formatters and linters report clean (Python, Shell, PowerShell, CSS, Rust, TypeScript/JavaScript, Luma).
- [ ] All Markdown documents follow project formatting rules.
- [ ] Documentation matches implementation (both directions).
