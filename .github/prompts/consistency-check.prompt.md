---
description: "Check that all project artefacts are consistent and up to date with each other"
agent: "agent"
tools: ["search", "read"]
argument-hint: "Optional focus area, e.g. 'docs vs implementation' or 'CMake vs source files'"
---

# Consistency Check

Verify that all project artefacts are mutually consistent. Work through each section methodically, reporting any discrepancies found.

## 1. Source Code vs CMakeLists

- Every `.cpp` and `.hpp` file under `core/`, `shared/`, `language-server/`, `debugger/`, `tests/`, and `fuzz/` is listed in the appropriate `CMakeLists.txt` target (test sources under `language-server/tests/` and `debugger/tests/` are registered in `tests/CMakeLists.txt`, and fuzz targets only build when `LUMA_BUILD_FUZZ=ON`).
- No stale entries referencing deleted or renamed files.
- Include directories match the actual directory structure.

## 2. Pipeline Consistency

- The lexer, parser, include resolver, name resolver, type checker, linter, compiler, and VM agree on the set of language constructs they support.
- Every AST node produced by the parser is handled by every full-AST consumer — the name resolver (`core/analysis/resolver/`), type checker, linter, and compiler — and its runtime semantics are implemented in the VM. The centralized dispatch in `core/analysis/ast/ast_dispatch.hpp` (the `ExpressionHandler` / `StatementHandler` / `DeclarationHandler` concepts) is the enforcement point — a handler missing a node kind should fail to compile rather than silently skip it.
- New keywords or syntax added to the lexer/parser are reflected through the entire pipeline.
- Opcodes emitted by the compiler are handled by the VM and understood by every other per-opcode consumer — the optimizer (`core/runtime/compiler/optimizer*`), the bytecode verifier (`core/runtime/compiler/verifier*`), and the disassembler (`Chunk::disassemble` in `core/runtime/compiler/chunk.cpp`) — with no stale or unhandled opcodes in any direction. Every opcode has an entry in the central opcode table (`core/runtime/compiler/opcode.hpp`, with operand sizes and categories in `opcode_metadata.hpp`), enforced by the table's `static_assert` position checks.

## 3. Standard Library Consistency

Every stdlib function must be identical across all five touchpoints. For each function, verify:

- **Module name** — the same `PascalCase` module name in all touchpoints.
- **Function name** — the same `snake_case` function name in all touchpoints.
- **Parameter names** — the same names in the same order in all touchpoints.
- **Parameter types** — the same types in the same order in all touchpoints.
- **Return type** — the same return type in all touchpoints.
- **Error handling** — follows [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md) §6 (Standard Library Conventions): infallible functions return plain values, fallible functions return `result<T>`, higher-order callbacks wrap runtime errors as `failure`, and truly unrecoverable errors are runtime errors.

The five touchpoints are:

1. **Runtime registration** — `core/runtime/stdlib/` (the `ModuleBuilder` DSL — e.g. `ModuleBuilder{"String", env}.func("length", 1).extract_body(...)`).
2. **Type checker** — `core/analysis/types/stdlib_type_signatures.cpp` (converts catalog metadata to `TypeInfo` and adds return-type refinement). Note: arities and parameter types are *derived* from the shared catalog — there are no manual entries here, so a new function is added to the catalog, not this file.
3. **Shared stdlib catalog** — `shared/stdlib/stdlib_catalog*.cpp` (per-module entries split across files such as `stdlib_catalog_string.cpp`). This is the single source of truth for arity, parameter names/types, and return types; the type checker and the language server both derive from it.
4. **Documentation** — [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md) (stdlib reference), plus any module-specific guide that documents the same surface — notably [Luma_GraphicalUi_Guide.md](../../documents/Luma_GraphicalUi_Guide.md) for the `GraphicalUi` module (and [Luma_Solaris_Guide.md](../../documents/Luma_Solaris_Guide.md) for the `Solaris` surface).
5. **Tests** — `tests/runtime/stdlib_test_*.cpp` and `tests/features/stdlib/` (function names, parameter usage), plus the automated catalog conformance test `tests/runtime/stdlib_catalog_conformance_test.cpp`, which asserts every catalog entry is registered at runtime and every registered namespaced function is in the catalog.

Also verify:

- Stdlib types in `core/analysis/types/stdlib_type_arities.cpp` match the records/choices actually constructed at runtime.
- No touchpoint references a removed or renamed module, function, or type.

## 4. Language Server vs Implementation

- The language server handles all current language constructs across its full feature surface — diagnostics, hover, completions, signature help, go-to-definition, references, rename, document/workspace symbols, semantic tokens, code actions / quick fixes, code lens, formatting, folding, inlay hints, call/type hierarchy, and linked editing all reflect the current parser, type checker, and stdlib.
- New syntax or keywords added to the pipeline are supported by the language server. In particular, the keyword catalog (`lsp_keyword_catalog.cpp`) stays in sync with the lexer keywords (`core/analysis/lexer/lexer.cpp`) — a keyword added to the lexer is reflected in completions, hover, and rename-keyword rejection (the `reserved_keyword_names()` test hook exists to enforce this).
- The semantic token classifier and legend (`lsp_token_classifier.hpp` and the `SemanticTokenType` legend in `lsp_constants.hpp`) classify every token type the lexer can produce — a new token type is mapped to a semantic category rather than left unclassified.
- The server's advertised capabilities (`lsp_capabilities.cpp`) match the handlers actually registered — no capability is announced without a handler, and no implemented handler is omitted from the advertised capabilities.
- The language server's stdlib metadata in `shared/stdlib/` matches the runtime and type checker (covered in §3).

## 5. Debugger vs Implementation

- The debugger correctly handles all current value types, runtime structures, and stdlib modules — in particular, the variable inspector (`variable_inspector.cpp`) displays every `Value` kind the VM can produce.
- Opcodes referenced by the debugger match the current compiler output.
- New language features (e.g., new expression types, new value kinds) are reflected in the debugger's variable display, stepping, and evaluation. The expression evaluator (`expression_compiler.cpp`) supports the current expression syntax.
- The debugger's advertised capabilities (`dap_feature_manager.hpp`) match the features actually implemented — e.g. conditional/hit/function/data breakpoints, logpoints, step-back, and configuration-done are only announced when the corresponding handler exists.

## 6. Language Server and Debugger Consistency

- The language server (`luma_lsp`) and debugger (`luma_dap`) expose a consistent view of the language — the value kinds, types, and stdlib surface one tool understands are reflected in the other where applicable (e.g. a new value kind appears in both the debugger's variable display and the language server's hover and completion metadata).
- Shared code in `shared/json/`, `shared/protocol/`, and `shared/symbols/` is consumed identically by both tools — neither has a divergent local copy or fork of a shared type (`SymbolKind`, `QualifiedName`, the JSON value/builder, transport framing) that has drifted from the shared definition.
- Both tools frame their JSON-based protocol messages over the shared `shared/protocol/` transport (Content-Length framed JSON over stdio) without divergent framing or encoding.
- Symbol metadata (`shared/symbols/`) and stdlib metadata (`shared/stdlib/`) are interpreted the same way by the language server (completions, hover, document symbols) and the debugger (variable display, expression evaluation).

## 7. Documentation vs Implementation

- All documents in `documents/` are up to date with the current implementation.
- [Luma_User_Manual.md](../../documents/Luma_User_Manual.md) reflects the current language — every keyword, type, operator, and language construct it documents matches the lexer, parser, type checker, and VM.
- [Luma_Initial_Concept.md](../../documents/Luma_Initial_Concept.md) design goals and motivation remain consistent with the implemented language (no documented core principle is contradicted by the implementation).
- [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md) documents all current stdlib modules, functions, and types with correct signatures (parameter names, parameter types, return types).
- [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md) conventions are reflected in the implementation — infallible functions do not return `result<T>`, fallible functions do.
- [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) reflects the current module layout and pipeline.
- [Luma_Debugger.md](../../documents/Luma_Debugger.md) reflects the current debugger architecture and capabilities.
- [Luma_Concurrent_Debugging_Guide.md](../../documents/Luma_Concurrent_Debugging_Guide.md) reflects the current concurrent debugging behaviour (task-to-thread mapping, stepping across tasks, structured-concurrency lifetimes).
- [Luma_Language_Server.md](../../documents/Luma_Language_Server.md) reflects the current language server features.
- [Luma_Syntax_Highlighting.md](../../documents/Luma_Syntax_Highlighting.md) reflects the current token types, keywords, and grammar rules.
- [Luma_Coding_Guidelines.md](../../documents/Luma_Coding_Guidelines.md) is consistent with `luma.instructions.md`.
- [Luma_Performance_Guide.md](../../documents/Luma_Performance_Guide.md) reflects current performance characteristics and optimisation advice.
- [Luma_GraphicalUi_Guide.md](../../documents/Luma_GraphicalUi_Guide.md) documents the current `GraphicalUi` module — widgets, layout containers, overlays, charts, commands, subscriptions, and styling helpers match the runtime (`core/runtime/stdlib/io/graphicalui_*`) and the embedded GUI framework (`external/gui-framework/`); [Luma_Solaris_Guide.md](../../documents/Luma_Solaris_Guide.md) documents the beginner-first `Solaris` MVU surface.
- [Luma_REPL_Guide.md](../../documents/Luma_REPL_Guide.md) reflects the current REPL behaviour — commands (`:quit`, `:help`, `:clear`, `:file`), multi-line input, tab completion, and history.
- The setup guide ([Luma_Setup.md](../../documents/Luma_Setup.md)) references valid paths, extension versions, and configuration for all supported editors.
- The documentation index ([documents/README.md](../../documents/README.md)) lists every document in `documents/` — a newly added, removed, or renamed document is reflected in the index's table of contents, and every link in the index resolves to an existing file.
- The root [README.md](../../README.md) is up to date with recent changes.
- The root [CONTRIBUTING.md](../../CONTRIBUTING.md) and [SECURITY.md](../../SECURITY.md) reference valid build commands, paths, workflow names, and supported-version information.
- The component and directory READMEs are current and reference valid paths, targets, and commands: `language-server/README.md`, `debugger/README.md`, `fuzz/README.md`, `tests/README.md`, `examples/README.md`, `benchmarks/README.md`, `documents/README.md`, `instructions/README.md`, and the editor-extension READMEs (`extensions/vscode/README.md`, `extensions/zed/README.md`, `extensions/shared/README.md`, `extensions/tests/README.md`). Vendored `external/*/README.md` files are excluded.
- The Doxygen configuration (`Doxyfile.in`) is current: every `INPUT` path (`README.md`, `core/`, `shared/`, `language-server/`, `debugger/`) still exists and covers the documented source tree, `USE_MDFILE_AS_MAINPAGE` points to an existing file, and the version comes from CMake (`@LUMA_VERSION@`) rather than a hardcoded number.
- The CMake preset documentation (`cmake/PRESETS.md`) matches `CMakePresets.json` — every preset it lists exists in the JSON with the same generator, build type, and options — and the preset set agrees with [build.instructions.md](../../instructions/build.instructions.md).

## 8. Tests vs Implementation

- Front-end analysis components have C++ unit tests in `tests/analysis/` (lexer, parser, type checker, resolver, linter, diagnostics, include resolver, pipeline) covering the current behaviour.
- Runtime components have C++ unit tests in `tests/runtime/` — every stdlib function has at least one test in the `stdlib_test_*.cpp` suite, and the compiler, VM, optimizer, verifier, bytecode serializer, concurrency, REPL, and CLI have their own `*_test.cpp` files.
- Test assertions match the current function signatures and error handling behaviour.
- Every language feature has a Luma test in `tests/features/language/` or `tests/features/stdlib/`.
- Integration tests in `tests/integration/` cover the current pipeline behaviour; platform-specific tests in `tests/platform/` cover OS-dependent behaviour (e.g. Win32 UTF-8).
- The LSP and DAP suites (`language-server/tests/`, `debugger/tests/`) cover the current language-server and debugger behaviour.
- Snapshot baselines in `tests/analysis/snapshots/*.expected` match the current diagnostic output (regenerate with `UPDATE_SNAPSHOTS=1` only when the change is intentional).
- Fuzz targets in `fuzz/` (authoritative list in `fuzz/CMakeLists.txt`) cover the current pipeline stages (lexer, parser, resolver, type checker, linter, compiler, optimizer, include resolver, VM), the trust-boundary stdlib parsers (JSON, CSV, XML, regex, compression, encoder, datetime, KeyValueStore, GraphicalUi CSS, …), the protocol transport, and the bytecode deserializer (with round-trip oracles) — and use current APIs.
- No tests reference removed or renamed functions/modules.

## 9. Benchmarks and Examples

- Files in `benchmarks/` and `examples/` use current Luma syntax and stdlib API.
- Every example under `examples/` runs cleanly through the headless harness (`scripts/run_examples.py`) — non-interactive, console (scripted stdin), terminal raw-mode, and GraphicalUi (`LUMA_GUI_HEADLESS=1`) examples all complete, and any `@test` block they declare passes.
- Every `bench_*.luma` is included and invoked by `benchmarks/suite.luma` (they are library modules that cannot run standalone) and uses the `benchmark_harness.luma` helpers.
- No references to deprecated or removed features.

## 10. Editor Extensions

- The tree-sitter grammar (`extensions/zed/grammars/tree-sitter-luma/grammar.js`) is the canonical source of truth for Luma syntax and recognises all current keywords, token types, and syntax constructs. Zed consumes it directly; VS Code ships a **hand-maintained** TextMate grammar (`extensions/vscode/syntaxes/luma.tmLanguage.json`) by design — VS Code lacks native tree-sitter — so a syntax change must be mirrored there too (guarded by `extensions/vscode/src/test/suite/grammar.test.ts`).
- Shared tree-sitter queries (`extensions/shared/queries/`) are the canonical highlight source; each editor keeps a hand-adapted copy (capture-group names differ per editor, and Zed reorders/reformats). `extensions/shared/sync-queries.py --check` is a structural gate that flags a genuinely added or removed node/terminal while ignoring those intentional adaptations — the copies are hand-edited, not generated.
- `extensions/shared/` is a code-generation single source of truth: per-editor configuration, keybindings, platform detection, download-protocol constants, and test-discovery patterns are generated from shared JSON (`defaults.json`, `keybindings.json`, `platform-map.json`, `download-constants.json`, `test-discovery-pattern.json`, …) via `generate-all.py`. After editing any shared source the generators must be re-run so the generated per-editor files stay in sync — `extensions/shared/ci-check-generated.py` is the CI guard that fails when they drift.
- Cross-editor contract tests in `extensions/tests/` (`validate-defaults`, `validate-download*`, `validate-resolution-order`, `validate_queries`) pass against the current shared JSON, and the per-editor test suites (`extensions/vscode/src/test/`, `extensions/zed/`) pass and reference current syntax and behaviour.
- Grammar definitions and highlighting rules match [Luma_Syntax_Highlighting.md](../../documents/Luma_Syntax_Highlighting.md).
- The feature matrix in `extensions/FEATURE_PARITY.md` matches the actual capabilities of each extension — no feature is marked supported that the extension does not implement, and no implemented feature is missing from the matrix.
- The release asset naming convention in `extensions/BINARY_ASSETS.md` matches the asset names produced by the release workflows and the names each extension expects when downloading the `luma`, `luma_lsp`, and `luma_dap` binaries.

## 11. Editor and IDE Settings

- `.vscode/` settings, tasks, and launch configs reference valid paths and targets.
- Extensions listed in `.vscode/extensions.json` are still relevant.
- `.zed/` workspace settings reference valid paths, build directories, and executable names.
- Clang-format and clang-tidy configs (`.clang-format`, `.clang-tidy`) and `.editorconfig` match the coding guidelines.
- Per-language linter and formatter configs (`ruff.toml`, `stylelint.config.mjs`, `.markdownlint-cli2.jsonc`, `.cmakelintrc`, `PSScriptAnalyzerSettings.psd1`) match the rules in the corresponding `instructions/*.instructions.md` guide and are covered by the path filter of the CI workflow that runs them.

## 12. Scripts

- Scripts in `scripts/` reference valid paths, build directories, and executable names. The directory is predominantly Python 3.10+ (the version gate lives in `scripts/_common.py`, imported by the others — except the standalone agent hooks in `scripts/agent-hooks/`, which import only the standard library so they stay dependency-free and fail open); the non-Python helpers are `generate_gui_assets.mjs` (Node), `container-build.sh` (shell), and the `scripts/hooks/` git hooks installed by `install_hooks.py`.
- Scripts that enforce cross-artefact consistency still match the artefacts they check: `check_warning_sync.py` against the GCC/Clang flags in `cmake/LumaCompilerFlags.cmake` and the checks in `.clang-tidy`, and `generate_gui_assets.mjs` against the embedded GUI assets in `core/runtime/stdlib/io/graphicalui_assets.hpp` (regenerated when `external/gui-framework/` or the vendored front-end libraries change).
- Scripts invoked by CI workflows (e.g. `run_luma_tests.py`, `run_examples.py`, `compare_benchmarks.py` / `parse_benchmark_results.py`, `generate_coverage.py`, `check_warning_sync.py`, and the `tsan_suppressions.txt` suppression file) exist with the names and interfaces the workflows expect.
- PowerShell and shell scripts elsewhere in the tree (e.g. `extensions/zed/scripts/Download-Binaries.ps1` and `download_binaries.sh`) work with the current project structure.

## 13. Git and CI

- `.gitignore` covers all generated artefacts (build dirs, IDE caches, OS files).
- GitHub Actions workflows in `.github/workflows/` use correct build commands, paths, and dependency versions.
- Each first-party language has its own path-filtered lint/format workflow (`ci.yml` for C++, `ci-python.yml`, `ci-css.yml`, `ci-markdown.yml`, `ci-shell.yml`, `ci-cmake.yml`, `ci-powershell.yml`, `ci-vscode.yml`, `ci-zed.yml`), and each linter's root config file (covered in §11) is on that workflow's path filter so editing the config re-runs the gate. Tool versions pinned in a workflow (e.g. PSScriptAnalyzer, stylelint, markdownlint-cli2, ruff) match any version pinned in the corresponding config or instructions guide.
- The C++ `ci.yml` static-analysis job runs clang-tidy against `.clang-tidy`, and the warning-sync gate (`scripts/check_warning_sync.py`) keeps the GCC/Clang warning flags in `cmake/LumaCompilerFlags.cmake` aligned with the `.clang-tidy` checks.
- Composite actions in `.github/actions/` (`apt-install`, `build-vscode-extension`, `build-zed-extension`, `cmake-build`, `package-binaries`) reference valid commands, paths, and inputs, and the reusable workflow (`reusable-linux-build.yml`) inputs and outputs match every caller.
- `.github/dependabot.yml` lists package ecosystems and directories that still exist (currently `github-actions` at `/`; the npm and cargo lockfiles under `extensions/` are not tracked by Dependabot by design).
- The `VERSION` file is the single source of truth for the project version — it flows into the CMake `project()` version, the generated `common/version.hpp`, and CPack metadata — and the version strings in the editor-extension manifests (`extensions/vscode/package.json`, `extensions/zed/extension.toml`), which are maintained separately, match it.
- Branch protection rules and CI matrix align with supported platforms.

## 14. Prompt, Agent, Hook, and Instruction Files

- Prompt files in `.github/prompts/` reference valid document paths and current project structure, and every cross-reference between prompts resolves to an existing sibling prompt. The prompt index ([.github/prompts/README.md](README.md)) lists every prompt in `.github/prompts/` — a newly added, removed, or renamed prompt is reflected in the index — and every link in it resolves.
- Agent definitions in `.github/agents/` (`docs`, `implement`, `plan`, `review`, `test`) and the planning template (`.github/plan-template.md`) reference valid paths, tools, build/test commands, and project structure; agent `handoffs` target agents that exist.
- The Claude-native mirror in `.claude/` stays in sync with its `.github/` sources of truth, following the sync rules in [.claude/README.md](../../.claude/README.md): every `.github/prompts/*.prompt.md` has exactly one matching `.claude/commands/*.md` wrapper that defers to it (the workflow is never duplicated), and the wrapper count stated in that README (`N in total`) equals the actual number of prompts; every `.github/agents/*.agent.md` role (`docs`, `implement`, `plan`, `review`, `test`) has a matching `.claude/agents/*.md` port with the same role, responsibilities, workflow, and boundaries, differing only in the translated tool vocabulary (for example `search` → `Grep, Glob`, `edit` → `Edit, Write, MultiEdit`, `execute` → `Bash`), the link-path depth, and the prose hand-off note that stands in for the VS Code `handoffs:` frontmatter; and the two `.claude/settings.json` hooks mirror the `.github/hooks/*.json` lifecycle events and invoke the same shared `scripts/agent-hooks/` scripts.
- Agent hooks in `.github/hooks/` (`format-cpp-on-edit`, `protect-vendored-paths`) register valid lifecycle events, invoke scripts that exist in `scripts/agent-hooks/`, and stay aligned with the gates they mirror — the C++ formatter with `scripts/hooks/pre-commit` and the CI *Formatting* job (same tool, `.clang-format`, and `.cpp`/`.hpp` scope), and the vendored-path guard with the `external/` boundary, preserving the `external/gui-framework/` exception. Both configs and their scripts are documented in [.github/hooks/README.md](../hooks/README.md).
- Instruction files in `instructions/` are consistent with each other and with `copilot-instructions.md`. Each file's `applyTo` glob in its YAML frontmatter targets a path pattern that actually occurs in the tree, and overlapping guides do not contradict each other.
- The instruction index (`instructions/README.md`) and the linked list of guides in `.github/copilot-instructions.md` both enumerate every file in `instructions/` — a newly added, removed, or renamed instruction file (and any changed `applyTo` pattern) is reflected in both, and every link resolves.
- `CLAUDE.md` stays in sync with the canonical agent guidance it imports (`.github/copilot-instructions.md`).
- The nested `CLAUDE.md` files in the primary source subtrees (`core/`, `shared/`, `language-server/`, `debugger/`, `tests/`, `examples/`, `extensions/`, `scripts/`, `documents/`) name — by repo-root-relative `instructions/…` path, for Claude Code to read on demand — only guides whose `applyTo` glob in `instructions/README.md` actually matches files in that subtree (plus `cmake`/`build` where the subtree contains a `CMakeLists.txt`), and the referenced set and the subtree list stay aligned with those globs and with the mapping documented in `CLAUDE.md`.

## Output Format

For each inconsistency found, report:

1. **Area** — Which section above.
2. **Files involved** — The specific files that disagree.
3. **Discrepancy** — What is inconsistent.
4. **Suggested fix** — Which file should be updated and how.

If an area is fully consistent, state that briefly and move on.
