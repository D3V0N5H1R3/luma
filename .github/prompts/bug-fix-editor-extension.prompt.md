---
description: "Diagnose and fix a bug in a Luma editor extension (VS Code or Zed)"
agent: "agent"
argument-hint: "Bug description, e.g. 'Zed highlights choice-type variants as plain identifiers'"
---

# Bug Fix — Editor Extension

Diagnose and fix a bug in a Luma editor extension. Two extensions live under `extensions/`: **VS Code** (`extensions/vscode/`, TypeScript) and **Zed** (`extensions/zed/`, Rust → WebAssembly + tree-sitter grammar). They share canonical data and code generators in `extensions/shared/`, so a bug may live in one editor, in the shared grammar, or in generated code that drifted from its source. Follow a structured approach:

1. **Reproduce** the bug with a minimal `.luma` document and the exact editor interaction that misbehaves (syntax highlighting, indentation, folding, bracket matching, LSP feature, DAP session, test runner, playground, binary download, snippet, or keybinding). Note which editor(s) show the bug — a fault present in **both** usually lives in the shared grammar or shared data, while an editor-specific fault lives in that editor's source.

2. **Isolate the editor and layer** where the bug occurs.

    **First, decide whether the bug is shared or editor-specific.** Check [extensions/FEATURE_PARITY.md](../../extensions/FEATURE_PARITY.md) — some differences are intentional (for example, code lens, the playground, and the debug visualiser are VS Code only). A genuine bug present across editors points at the shared layer:

    - Shared tree-sitter grammar (`extensions/zed/grammars/tree-sitter-luma/grammar.js`) — the **single source of truth** for syntax; Zed derives highlighting from it (VS Code is the exception — it uses its own hand-maintained TextMate grammar, see below). Wrong node structure breaks Zed highlighting, folding, and indentation.
    - Shared canonical data (`extensions/shared/defaults.json`, `download-constants.json`, `resolution-order.json`, `platform-map.json`, `keybindings.json`, `snippets/luma.json`, `queries/highlights.scm`) — wrong defaults, download constants, platform mapping, snippets, or shared queries.
    - Shared code generators (`extensions/shared/generate-*.py`, `generate-all.py`, `codegen_common.py`) — if a **generated** file is wrong, fix the canonical source and/or the generator, never the generated output (see step 4).
    - Binary asset naming (`extensions/BINARY_ASSETS.md`) — the `{binary}-{os}-{arch}.{ext}` release-asset convention that every editor's download code must match.

    **Then isolate the layer inside the affected editor:**

    **VS Code** (`extensions/vscode/`):
    - Activation & feature wiring (`src/extension.ts`, `src/utils/feature-registry.ts`) — feature not registered or activated?
    - LSP client (`src/lsp/client-manager.ts`, `src/lsp/commands.ts`, `src/lsp/code-actions.ts`, `src/lsp/types.ts`) — client lifecycle, server restart, command, or client-side code action wrong?
    - DAP debugging (`src/debugger/debug.ts`, `src/debugger/visualizer.ts`, `src/debugger/visualizer-renderers.ts`) — debug adapter wiring or the debug visualiser webview wrong?
    - Test runner (`src/testing/testing.ts`, `src/testing/coverage.ts`, and **generated** `src/generated/test-discovery.ts` for the `@test`/`@main` match patterns) — test discovery, run, or coverage wrong?
    - Tasks & playground (`src/tasks.ts`, `src/playground/`) — task provider or playground command wrong?
    - Binary download (`src/utils/binary-download.ts`, `src/utils/checksum.ts`, `src/utils/http.ts`, the hand-written `src/utils/binary/` helpers, and **generated** `src/generated/platform.ts` for the platform→asset map and `src/generated/download-constants.ts` for the checksum-manifest name) — platform detection, checksum, or download flow wrong?
    - Config (`src/utils/config.ts`, `src/utils/constants.ts`, and **generated** `src/generated/config.ts`, `config-accessor.ts`) — setting read or default wrong?
    - TextMate grammar (`syntaxes/luma.tmLanguage.json`, `syntaxes/luma.markdown-injection.json`) — **hand-maintained by design** (VS Code lacks native tree-sitter); guarded by `src/test/suite/grammar.test.ts`. Wrong scope or rule ordering?
    - Editor behaviour (`language-configuration.json`, `themes/`) — brackets, comments, auto-closing, or theme colour wrong?
    - Manifest (`package.json`) — `contributes` entry (command, config, debugger, language, grammar) missing or wrong?

    **Zed** (`extensions/zed/`):
    - Extension entry (`src/lib.rs`) — LSP/DAP launcher or binary resolution (system PATH vs downloaded) wrong?
    - LSP label rendering (`src/labels.rs`) — completion / symbol → `CodeLabel` rendering wrong?
    - Binary download & utilities (`src/download.rs` — `platform_asset_name_for()` builds the release-asset name from the **generated** `src/generated/platform.rs` map, plus `download_binary()` and `try_verify_asset_checksum()`; `src/util.rs` — `merge_json()` config merge) — asset-name, download, checksum, or JSON merge wrong?
    - Generated config & constants (`src/generated/` — `config.rs`, `config_defaults.rs`, `download_constants.rs`, `platform.rs`) — **generated**; fix the canonical source, not the output.
    - Manifest & language config (`extension.toml`, `languages/luma/config.toml`, `grammars/luma.toml`) — grammar reference, language settings, or capability wrong?
    - Tree-sitter queries (`languages/luma/highlights.scm`, `brackets.scm`, `folds.scm`, `indents.scm`, `injections.scm`, `outline.scm`, `overrides.scm`, `redactions.scm`, `runnables.scm`, `textobjects.scm`) — query maps a node to the wrong scope/behaviour?

3. **Read the relevant design docs and source.** [Luma_Syntax_Highlighting.md](../../documents/Luma_Syntax_Highlighting.md) covers token categories, TextMate scopes, and tree-sitter query strategy; [extensions/shared/README.md](../../extensions/shared/README.md) covers the canonical-grammar and code-generation model. For LSP- or DAP-backed features, also read [Luma_Language_Server.md](../../documents/Luma_Language_Server.md) or [Luma_Debugger.md](../../documents/Luma_Debugger.md) — but first confirm whether the fault is in the **server/adapter** (a C++ bug — use [bug-fix-language-server.prompt.md](bug-fix-language-server.prompt.md) or [bug-fix-debugger.prompt.md](bug-fix-debugger.prompt.md) instead) or in the **extension's client-side wiring**.

4. **Fix** the root cause with the smallest correct change. Watch for the editor-extension pitfalls:
    - **Never hand-edit a generated file.** Files under `src/generated/` (VS Code, Zed) are produced by `extensions/shared/generate-*.py` from canonical JSON. Fix the canonical source (and the generator if its logic is wrong), then regenerate — otherwise `ci-check-generated.py` will fail and the next regeneration will silently revert your edit.
    - **The tree-sitter grammar is canonical** for syntax in Zed. After editing `grammars/tree-sitter-luma/grammar.js`, regenerate the parser with `tree-sitter generate` and update the queries to match any renamed nodes — do not patch one editor's queries in isolation when the grammar itself is wrong. The `highlights.scm` query is *separately* canonical in `extensions/shared/queries/`; after changing it, hand-update each editor's copy to match (they carry per-editor capture-name adaptations and, in Zed's case, reordering/reformatting), then run the shared validation checks to confirm no structural drift.
    - **The VS Code TextMate grammar is the deliberate exception** — it is hand-maintained (VS Code has no native tree-sitter) and must be edited directly in `syntaxes/luma.tmLanguage.json`, keeping `src/test/suite/grammar.test.ts` passing.
    - **Keep editors consistent.** Shared defaults, download constants, and platform mappings are validated across editors by `extensions/tests/validate-*.test.mjs`; a one-editor change to shared data will break them.
    - **Respect the binary-asset contract** in [BINARY_ASSETS.md](../../extensions/BINARY_ASSETS.md) when touching any download path.
    - **Follow the per-language style guide** for the file you edit: [typescript.instructions.md](../../instructions/typescript.instructions.md) / [javascript.instructions.md](../../instructions/javascript.instructions.md) (VS Code), [rust.instructions.md](../../instructions/rust.instructions.md) (Zed), and [css.instructions.md](../../instructions/css.instructions.md) for any webview styling.

5. **Add a regression test** in the affected editor's suite:
    - VS Code: a `*.test.ts` file under `extensions/vscode/src/test/suite/` (Mocha) — e.g. `grammar.test.ts` for grammar/scope regressions, `binary-download.test.ts` for download logic.
    - Zed: a `#[test]` in `extensions/zed/src/tests.rs` (cargo test).
    - Shared grammar: add or extend a fixture in `extensions/tests/fixtures/` so `parse_fixtures.js` covers the construct; add a `validate-*.test.mjs` assertion for shared-data regressions.

6. **Verify** the affected extension (and the shared checks if you touched shared data or grammar). Run the same checks CI runs, plus the per-editor unit tests from step 5:

    **VS Code:**

    ```bash
    cd extensions/vscode
    npm install
    npm run lint          # ESLint + tsc --noEmit
    npm run test:unit     # Mocha unit tests
    npm run format:check  # Prettier
    ```

    **Zed** (the build target is `wasm32-wasip1` — the old `wasm32-wasi` name no longer resolves on current toolchains):

    ```bash
    cd extensions/zed
    cargo fmt --check
    cargo clippy --target wasm32-wasip1 -- -D warnings
    cargo build --release --target wasm32-wasip1
    cargo test   # host-target unit tests in src/tests.rs
    ```

    **Shared grammar / data (run whenever you touch `grammar.js`, queries, or canonical JSON):**

    ```bash
    # Regenerate the parser inside the grammar directory:
    (cd extensions/zed/grammars/tree-sitter-luma && npm install && npx tree-sitter generate)
    # Then, from the repository root:
    node extensions/tests/parse_fixtures.js
    node extensions/tests/validate-defaults.test.mjs
    node extensions/tests/validate-download.test.mjs
    node extensions/tests/validate-download-constants.test.mjs
    node extensions/tests/validate-resolution-order.test.mjs
    python extensions/shared/ci-check-generated.py
    ```

    If the bug is in an LSP- or DAP-backed feature and the fix touched the C++ server or adapter, also build and test the interpreter per [build-and-test.prompt.md](build-and-test.prompt.md) (`ctest --preset default -L lsp` or `-L dap`).
