# Binary Resolution Order

Documents how each editor extension resolves the `luma_lsp` and `luma_dap` binaries.

## Background

Binary download logic (platform detection, GitHub release fetching, checksum verification, archive extraction) is independently implemented in two different languages across the editor extensions:

- **VS Code:** `src/utils/binary-download.ts` + `src/utils/checksum.ts` (TypeScript)
- **Zed:** `src/download.rs` — download, extraction, and checksum logic; resolution entry points (`language_server_command`, `get_dap_binary`) live in `src/lib.rs` (Rust/WASM)

Since each extension uses a different language and runtime, a single shared library is not feasible. Instead, a **shared specification** defines the canonical download protocol that both implementations follow:

- **Specification:** [`binary-download/SPECIFICATION.md`](binary-download/SPECIFICATION.md) — asset naming convention, download steps, checksum verification, extraction, and error handling.
- **Reference implementation:** VS Code's `binary-download.ts` module provides a generic, configurable implementation that the Zed extension can use as a reference when updating its own logic.

### Status

- ✅ Shared specification created in `extensions/shared/binary-download/`.
- ✅ VS Code `binary-download.ts` extracted as a generic, reusable module with `BinaryConfig` abstraction (supports both LSP and DAP binaries).
- ✅ VS Code `checksum.ts` extracted as a separate module for SHA-256 verification.
- ✅ Zed verifies SHA-256 checksums via the pure-Rust `sha2` crate, which runs inside the WASM sandbox.
- ✅ Zed resolves and auto-downloads both LSP and DAP binaries (`language_server_command` and `get_dap_binary`).

## VS Code

**File:** `vscode/src/utils/binary-download.ts` (`resolveBinaryCommand`), called by `client-manager.ts` (LSP) and `debug.ts` (DAP)

Resolution order for both LSP and DAP:

1. **User-configured path** — Check the `luma.lsp.path` / `luma.dap.path` setting. Supports `${workspaceFolder}` variables.
2. **Bundled binary** — Check the extension's `globalStorageUri/bin/` directory for a previously downloaded binary.
3. **Auto-download** — Fetch the latest GitHub release, download the platform archive, verify SHA-256 checksum, extract, and install to `globalStorageUri/bin/`.
4. **PATH fallback** — Use the bare binary name (`luma_lsp` / `luma_dap`), relying on the system PATH.

## Zed

**File:** `zed/src/lib.rs` (`language_server_command`, `get_dap_binary`); download/extract/checksum in `zed/src/download.rs`

### LSP resolution (`language_server_command`)

1. **PATH lookup** — `worktree.which("luma_lsp")`.
2. **Auto-download** — Fetch the latest GitHub release, download the platform archive via Zed API, extract to a version-prefixed directory, and verify the SHA-256 checksum against `SHA256SUMS` using the `sha2` crate.

### DAP resolution (`get_dap_binary`)

1. **User-provided path** — `user_provided_debug_adapter_path` argument.
2. **PATH lookup** — `worktree.which("luma_dap")`.
3. **Auto-download** — Same as LSP.

**Key difference:** Zed checks PATH *first*, then falls back to auto-download. VS Code checks user config first. Zed does not support a settings-based path override for LSP (only DAP has `user_provided_debug_adapter_path`).

## Summary Matrix

| Step | VS Code | Zed LSP | Zed DAP |
|------|---------|---------|---------|
| 1 | User setting | PATH lookup | User-provided path |
| 2 | Bundled binary | Auto-download | PATH lookup |
| 3 | Auto-download | — | Auto-download |
| 4 | PATH fallback | — | — |

## Checksum Verification

| Extension | SHA-256 Verification |
|-----------|---------------------|
| VS Code | ✅ Yes — `SHA256SUMS` from release, `crypto.createHash('sha256')` |
| Zed | ✅ Yes — `SHA256SUMS` from release, `sha2` crate (pure Rust, runs in WASM). Hard-fails on mismatch; verification is skipped (non-fatal) only when `SHA256SUMS` is missing or unreadable. |

## Related Files

- `extensions/shared/download-spec.md` — Download protocol specification and consolidation rationale
- `extensions/shared/platform-map.json` — Canonical platform-to-suffix mapping
- `extensions/shared/binary-download/SPECIFICATION.md` — Full download protocol
