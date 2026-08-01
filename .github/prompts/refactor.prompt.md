---
description: "Refactor Luma code — interpreter, language server, debugger, or editor extensions — while keeping all tests green"
agent: "agent"
argument-hint: "Refactoring goal, e.g. 'extract token validation into a shared helper'"
version: 1
lastUpdated: "2026-08-01"
---

# Refactor

Refactor code anywhere in the Luma project — the interpreter core, the language server (`luma_lsp`), the debugger (`luma_dap`), or the editor extensions (VS Code and Zed). Prioritise safety and correctness:

1. Read [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) to understand module boundaries and the pipeline design. Then, **if the refactoring touches a specific subsystem**, also read the matching design doc to learn the behaviour that must be preserved:
    - Lexer, parser, type checker, compiler, or VM → [Luma_User_Manual.md](../../documents/Luma_User_Manual.md) for the language semantics.
    - Standard library (`core/runtime/stdlib/`) → [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md), plus [Luma_GraphicalUi_Guide.md](../../documents/Luma_GraphicalUi_Guide.md) for the GraphicalUi module.
    - Error-handling paths, runtime errors, exit codes, or stdlib failure conventions → [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md) for the conventions and policy to preserve.
    - Language server (`language-server/source/`) → [Luma_Language_Server.md](../../documents/Luma_Language_Server.md).
    - Debugger (`debugger/source/`) → [Luma_Debugger.md](../../documents/Luma_Debugger.md).
    - REPL (`core/runtime/repl/`) → [Luma_REPL_Guide.md](../../documents/Luma_REPL_Guide.md).
    - Editor extensions and grammars (`extensions/`) → [Luma_Syntax_Highlighting.md](../../documents/Luma_Syntax_Highlighting.md), plus [FEATURE_PARITY.md](../../extensions/FEATURE_PARITY.md) for the cross-editor feature contract and the shared-grammar dependency (Zed uses the tree-sitter grammar, while the VS Code TextMate grammar is hand-maintained and guarded by its own grammar test). Each extension's own `README.md` feature table records the user-facing capabilities to preserve.
2. Read the source files involved in the refactoring. Understand all callers and dependencies before changing any interfaces — search for every reference so no call site is missed.
3. Build and run the full test suite **before** starting to establish a green baseline, and record any pre-existing failures so they stay distinguishable from regressions. Each subsystem builds and tests differently — build and test whichever ones your change touches:
    - **Interpreter core, language server, debugger, and `shared/`** (C++, built together): `cmake --build --preset default`, then `ctest --preset default`. The LSP and DAP have dedicated suites (`language-server/tests/` linking `luma_lsp_lib`, `debugger/tests/` linking `luma_dap_lib`, including `dap_integration_test`) that are registered as CTest cases, so this one `ctest` run already exercises them alongside the C++ unit tests and the Luma feature tests — there is no separate LSP or DAP test command.
    - **VS Code extension** (TypeScript): `npm ci`, then `npm run compile` to build and `npm run test:unit` to test.
    - **Zed extension** (Rust): `cargo build --release --target wasm32-wasip1` to build (it compiles to WebAssembly, so the `wasm32-wasip1` target is required), plus `npx tree-sitter generate` when the grammar changes; the tree-sitter parse-fixture tests (`node extensions/tests/parse_fixtures.js`) validate it.
    - **Shared extension behaviour** (both editors): the cross-extension validation tests in `extensions/tests/` (`node tests/validate-*.test.mjs`, run from `extensions/`) guard shared defaults, binary download, and resolution order — run them whenever you touch `extensions/shared/` or any extension's download/config logic.

    The reusable composite actions `.github/actions/cmake-build`, `build-vscode-extension`, and `build-zed-extension`, together with each extension's CI workflow under `.github/workflows/`, are the authoritative source for these commands and their pinned tool versions.
4. Make changes incrementally:
    - Restructure code without changing observable behaviour.
    - Follow the naming and style conventions of the language you are editing: `instructions/cpp.instructions.md` for all first-party C++ (interpreter core, language server, debugger, and `shared/`), `instructions/javascript.instructions.md` and `instructions/css.instructions.md` for the GraphicalUi front-end assets (`external/gui-framework/`), `instructions/typescript.instructions.md` for the VS Code extension, `instructions/rust.instructions.md` for Zed, and `instructions/cmake.instructions.md` for build files.
    - Keep each step small enough that tests remain meaningful checkpoints.
    - Commit or stash each green checkpoint so any regression is easy to roll back.
5. After each significant change, rebuild and rerun the relevant test suite (the C++ `ctest` run, or the extension's own build and tests) to catch regressions early — a clean compile is the first checkpoint, passing tests the second.
6. If renaming files or moving code between modules, update whatever manifest lists them: `CMakeLists.txt` for C++ (sources are listed explicitly — there is no globbing) plus every affected `#include` directive; for an extension, the equivalent — `package.json` and import paths for VS Code, `Cargo.toml` and `mod` declarations for Zed.
7. Lint and format every language you touched, following [lint-and-format.prompt.md](lint-and-format.prompt.md) for the exact tooling, pinned versions, and commands (lint first, then format). For C++ this means the `tidy` (clang-tidy) target and clang-format — the CI **Formatting** job fails on *any* clang-format diff, and the `clang-analyzer-*`, `bugprone-*`, and `concurrency-*` categories are escalated to errors that fail the build, so fix those first.
8. Build and run the full test suite one final time (including the extension's own build and tests if you changed one), confirming it is green and that the linters and formatters for every language you touched are clean (per step 7) before finishing. For a deeper final sweep — especially when the refactor touched the lexer→VM pipeline or a stdlib parser — follow [full-test-sweep.prompt.md](full-test-sweep.prompt.md), which additionally exercises the fuzz smoke tests and benchmarks that `ctest` does not cover.
9. When the refactor touched the interpreter or the analysis pipeline the LSP and DAP share, run the example programs as a final behaviour guardrail (`python scripts/run_examples.py`) — they are not part of `ctest`, so this catches regressions the test suite does not reach. A pure extension refactor does not need this; its behaviour is covered by the extension's own unit, grammar, and validation tests.

## Example

Extract a `CompiledBreakpoint` helper from `BreakpointManager`:

1. Move breakpoint-condition parsing, compilation, and cached bytecode storage into a new `debugger/source/compiled_breakpoint.{hpp,cpp}` class.
2. Keep `BreakpointManager` focused on breakpoint storage, hit counting, and resolution — it should hold `CompiledBreakpoint` instances rather than raw condition strings plus ad-hoc compilation state.
3. Inject the existing condition-evaluation callback into the manager instead of letting it own compilation details directly.
4. Update `debugger/CMakeLists.txt`, includes, and tests so the new helper builds everywhere.
5. Rebuild and run the debugger suites (`ctest --preset default --output-on-failure --tests-regex "dap|breakpoint"`) to confirm behaviour is unchanged.
