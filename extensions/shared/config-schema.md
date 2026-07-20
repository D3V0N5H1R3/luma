# Configuration Schema

> **Note:** This document is manually maintained. The canonical source of truth for
> configuration defaults is [`defaults.json`](defaults.json). If you update configuration
> settings, update this document and `defaults.json` accordingly. Run
> `python ci-check-generated.py` to verify all generated editor files are in sync
> (or `python test_generated_consistency.py` for the runtime config constants only).

Canonical configuration reference for both Luma editor extensions. Each editor maps these settings to its native configuration format. The machine-readable schema is in [`defaults.json`](./defaults.json).

## Settings Reference

### Language Server

| Setting | Type | Default | Description |
| --- | --- | --- | --- |
| `lsp.enabled` | `boolean` | `true` | Enable or disable the Luma language server integration. |
| `lsp.path` | `string` | `""` | Absolute path to the `luma_lsp` binary. Empty means auto-download or PATH lookup. |
| `lsp.autoUpdate` | `boolean` | `true` | Automatically check for language server updates on activation. |
| `lsp.trace` | `string` | `"off"` | Trace level for LSP communication. One of `off`, `messages`, `verbose`. |

### Debugger

| Setting | Type | Default | Description |
| --- | --- | --- | --- |
| `dap.enabled` | `boolean` | `true` | Enable or disable the Luma debug adapter integration. |
| `dap.path` | `string` | `""` | Absolute path to the `luma_dap` binary. Empty means PATH lookup. |

### Interpreter

| Setting | Type | Default | Description |
| --- | --- | --- | --- |
| `interpreter.path` | `string` | `""` | Absolute path to the `luma` interpreter binary. Empty means PATH lookup. |

### Diagnostics

| Setting | Type | Default | Description |
| --- | --- | --- | --- |
| `diagnostics.onSave` | `boolean` | `false` | Only report linter warnings on save. Syntax and type errors are always immediate. |
| `diagnostics.maxFileSize` | `integer` | `1048576` | Maximum file size (bytes) for analysis. Files exceeding this are skipped. |

### Editor Features

| Setting | Type | Default | Description |
| --- | --- | --- | --- |
| `inlayHints.enabled` | `boolean` | `true` | Show inferred type annotations as inlay hints. |
| `codeLens.enabled` | `boolean` | `true` | Show reference counts above functions and types. |

### Playground

| Setting | Type | Default | Description |
| --- | --- | --- | --- |
| `playground.timeout` | `integer` | `10000` | Maximum execution time (ms) for playground snippets. Range: 1000–120000. |
| `playground.maxOutputSize` | `integer` | `1048576` | Maximum output buffer (bytes) for playground snippets. Range: 1024–10485760. |

### Auto-Download

| Setting | Type | Default | Description |
| --- | --- | --- | --- |
| `autoDownload.enabled` | `boolean` | `true` | Automatically download pre-built binaries when not found locally. |
| `autoDownload.version` | `string` | `"latest"` | Version tag to download (`latest` or a specific tag like `v0.5.0`). |

## Editor-Specific Mapping

### VS Code

Settings use the `luma.` prefix in `contributes.configuration` (defined in `extensions/vscode/package.json`).

| Canonical Key | VS Code Key | Notes |
| --- | --- | --- |
| `lsp.path` | `luma.lsp.path` | Supports `${workspaceFolder}` variables. |
| `lsp.autoUpdate` | `luma.lsp.autoUpdate` | — |
| `dap.path` | `luma.dap.path` | Supports `${workspaceFolder}` variables. |
| `interpreter.path` | `luma.path` | Note: uses `luma.path` not `luma.interpreter.path`. |
| `diagnostics.onSave` | `luma.diagnostics.onSave` | — |
| `inlayHints.enabled` | `luma.inlayHints.enabled` | — |
| `codeLens.enabled` | `luma.codeLens.enabled` | — |
| `playground.timeout` | `luma.playground.timeout` | — |
| `playground.maxOutputSize` | `luma.playground.maxOutputSize` | — |
| `lsp.enabled` | *(implicit)* | Untrusted workspaces disable LSP via `restrictedConfigurations`. |
| `dap.enabled` | *(implicit)* | Controlled by workspace trust. |
| `lsp.trace` | `luma.trace.server` | VS Code uses standard language client trace setting. |

### Zed

Settings are handled by the Rust extension (defined in `extensions/zed/extension.toml` and `src/`). Zed resolves binaries via `worktree.which()` and falls back to automatic GitHub release download.

| Canonical Key | Zed Behaviour | Notes |
| --- | --- | --- |
| `lsp.path` | Resolved via `worktree.which("luma_lsp")` or auto-downloaded. | No explicit user setting; relies on PATH. |
| `dap.path` | Configured via `debuggers` section in `extension.toml`. | Uses `command = "luma_dap"`. |
| `autoDownload.enabled` | Always enabled (built into extension logic). | No user toggle. |

## Adding a New Setting

When adding a new configuration option:

1. **Add to `defaults.json`** — Add the property to `extensions/shared/defaults.json` under the `settings` object with type, default, description, and any constraints (min/max, enum values). Settings are ordered alphabetically.

2. **Regenerate editor code** — Run `python extensions/shared/generate-all.py` to regenerate all editor generated files. For a new setting this updates:
   - `extensions/vscode/package.json` (splices the `contributes.configuration` property) and `extensions/vscode/src/generated/config-accessor.ts` (the typed `luma_config` accessor)
   - `extensions/zed/src/generated/config_defaults.rs` (typed default constant)

3. **Implement in each editor**:
   - **VS Code**: Add the property to `contributes.configuration` in `package.json` and handle it in `src/extension.ts`.
   - **Zed**: Handle the setting in `src/lib.rs`.

4. **Update this document** — Add a row to the Settings Reference table and the editor-specific mapping tables.

5. **Test** — Verify the setting works in at least one editor before merging.

## Related Files

- [`defaults.json`](./defaults.json) — Canonical configuration schema and defaults consumed by the config generators.
- [`generate-config.py`](./generate-config.py) — Generates shared runtime constants (`config.ts`/`.rs`).
- [`generate-config-code.py`](./generate-config-code.py) — Generates editor-native config (package.json properties, typed accessors, default tables).
- [`platform-map.json`](./platform-map.json) — Platform/architecture binary naming.
- [`download-spec.md`](./download-spec.md) — Binary download protocol specification.
- [`error-handling.md`](./error-handling.md) — Error severity contract for diagnostics.
