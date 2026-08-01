---
description: "Diagnose and fix a bug in the Luma interpreter or standard library"
agent: "agent"
argument-hint: "Bug description, e.g. 'string interpolation crashes on empty expressions'"
version: 1
lastUpdated: "2026-08-01"
---

# Bug Fix

Diagnose and fix a bug in the Luma interpreter. Follow a structured approach:

> **Scope:** This prompt covers the interpreter core (lexer → VM) and the standard library. If the fault is in a different subsystem, use the dedicated prompt instead: [bug-fix-language-server.prompt.md](bug-fix-language-server.prompt.md) for the language server (`luma_lsp`), [bug-fix-debugger.prompt.md](bug-fix-debugger.prompt.md) for the debugger (`luma_dap`), or [bug-fix-editor-extension.prompt.md](bug-fix-editor-extension.prompt.md) for the VS Code or Zed extensions. The language server and debugger reuse this interpreter front-end, so a bug that first surfaces there may still root-cause to the lexer, parser, or type checker covered here.

1. **Reproduce** the bug with a minimal Luma program or C++ test case. Confirm the failure.
2. **Isolate the phase** where the bug occurs by tracing through the pipeline:
    - Lexer (`core/analysis/lexer/`) — incorrect tokenisation?
    - Parser (`core/analysis/parser/`) — malformed AST?
    - Include Resolver (`core/runtime/include/`) — wrong file resolution or missing deduplication?
    - Type Checker (`core/analysis/types/`) — wrong type error or missed validation?
    - Linter (`core/analysis/linter/`) — false positive/negative warning?
    - Compiler (`core/runtime/compiler/`) — incorrect bytecode generation, or wrong variable/slot resolution? (Live variable→slot resolution is done here by `VariableResolver`; the standalone `core/analysis/resolver/` `NameResolver` is not wired into the run path.)
    - Optimizer (`core/runtime/compiler/optimizer.*`) — unsound transformation or value-block stack-slot corruption?
    - VM (`core/runtime/vm/`) — incorrect execution, stack corruption, or wrong dispatch?
    - Standard Library (`core/runtime/stdlib/`, a runtime concern — not a pipeline phase) — wrong built-in function behaviour?
3. Read [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) — especially §6 (Processing Pipeline) and §7 (Bytecode Compiler and Virtual Machine Internals) — and the relevant source files to understand the current behaviour.
4. **Fix** the root cause with the smallest correct change.
5. **Add a regression test** at the appropriate level:
    - C++ unit test in the matching `tests/analysis/*_test.cpp` or `tests/runtime/*_test.cpp` file.
    - C++ integration test in `tests/integration/` if the bug spans multiple phases (source → execution).
    - C++ platform test in `tests/platform/` if the bug is OS-specific (e.g. Win32 UTF-8 handling).
    - Luma test in `tests/features/language/` or `tests/features/stdlib/` if the bug is user-visible.
6. Build and run the full test suite to verify the fix and confirm nothing else broke. See [build-and-test.prompt.md](build-and-test.prompt.md) for the canonical build-and-test workflow.

## Verification Tiers

Verify incrementally as you work — don't wait until the end to discover a cascade of failures.

| After… | Run | Why |
|---------|-----|-----|
| Writing the fix | `cmake --build --preset default` (compile only) | Catches typos, missing includes, type errors immediately |
| Compiling cleanly | The **single most-relevant test** (e.g. `ctest -R stdlib_test_string`) | Confirms the fix works in isolation |
| Targeted test passes | `ctest --preset default` (full C++ suite) | Catches regressions in other subsystems |
| Full suite passes | `build/Release/luma --strict --test <feature-test>.luma` | Validates the Luma-level behaviour |

If a tier fails, fix the failure before advancing to the next tier. If a fix introduces a *new* failure you can't resolve in two attempts, revert to your last green state (`git stash` or `git checkout -- <files>`) and try a different approach.

## Example

Bug: `"hello" |> String.slice(1, 3)` returns `"hel"` instead of `"el"`.

1. **Reproduce**: Write a test `assert("hello" |> String.slice(1, 3) == "el")` — fails.
2. **Isolate**: The lexer, parser, type checker, and compiler are not involved (this is a runtime stdlib function). Check `string_module.cpp`.
3. **Root cause**: `String.slice` uses `substr(start, end)` but `std::string::substr` takes `(pos, count)`, not `(start, end)`. Fix: `substr(start, end - start)`.
4. **Regression test**: Add to `tests/features/stdlib/string_functions.luma`:

    ```luma
    @test
    function void test_string_slice_range() {
        assert("hello" |> String.slice(1, 3) == "el")
    }
    ```

5. **Build and verify**: `cmake --build --preset default && ctest --preset default`.
