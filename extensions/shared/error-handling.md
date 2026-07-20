# Error Handling Contract

This document defines when each Luma editor extension should use each error severity level. Both extensions must follow this contract to provide a consistent user experience across editors.

## Severity Levels

### Modal Error

**Editor equivalents:** `vscode.window.showErrorMessage` (VS Code), `Err(String)` surfaced by Zed UI.

**When to use:** only for errors that **block the user's intended action** and require acknowledgment.

| Scenario                                   | Example                                                  |
| ------------------------------------------ | -------------------------------------------------------- |
| Security violation (checksum mismatch)     | "Download failed integrity check. File may be tampered." |
| Corrupt or invalid configuration           | "Invalid luma.lsp.path: workspace folder not found."     |
| LSP/DAP failed to start (binary not found) | "Could not find luma_lsp. Install it or set the path."   |

### Notification Warning

**Editor equivalents:** `vscode.window.showWarningMessage` / `showInformationMessage` (VS Code), Zed status line.

**When to use:** for recoverable problems that the user should know about, but that do not block their workflow.

| Scenario                                    | Example                                                   |
| ------------------------------------------- | --------------------------------------------------------- |
| Binary not on PATH (will attempt download)  | "luma_lsp not found on PATH. Enable auto_download."       |
| Unsupported platform (no pre-built binary)  | "No pre-built binary for this platform. Build from source." |
| Configuration fallback applied              | "luma.lsp.path is invalid — falling back to PATH lookup." |
| Update available                            | "luma_lsp v0.6.0 available (current: v0.5.0)."           |
| Download failed (non-security)              | "Failed to download luma_lsp. Check your connection."     |
| Unknown configuration key                   | "luma.setup(): unknown config key 'foo'."                 |

### Log-only

**Editor equivalents:** `output.appendLine` (VS Code), stderr/extension log (Zed).

**When to use:** for diagnostic details useful during troubleshooting but not worth interrupting the user.

| Scenario                                  | Example                                                |
| ----------------------------------------- | ------------------------------------------------------ |
| Download progress steps                   | "Downloading luma_lsp binary…"                         |
| Available assets listed on asset mismatch | "Available: luma_lsp-linux-x86_64.tar.gz, …"           |
| Checksum verification succeeded           | "Checksum verified for luma_lsp-macos-aarch64.tar.gz." |
| Binary not found after extraction         | "Extraction succeeded but luma_lsp not found in /…"    |
| Auto-update check skipped (already current) | "luma_lsp is up to date (v0.5.0)."                   |
| Detailed error context for failures       | "SHA256SUMS does not contain an entry for …"           |

### Silent (No Output)

**When to use:** for expected conditions that require no user attention.

| Scenario                                  | Example                                      |
| ----------------------------------------- | -------------------------------------------- |
| Binary already installed, version matches | Skip download, proceed normally.             |
| Graceful fallback with no impact          | PATH lookup succeeds after empty config.     |

## Cross-Extension Consistency Rules

1. **Same severity for the same error.** If a checksum mismatch is a modal error in VS Code, it must be `Err` in Zed.

2. **Success is log-only, not notification.** Routine success (checksum verified, binary installed) goes to the output log, not a user-facing notification — unless it completes a user-initiated action (e.g. manual update check).

3. **Recoverable errors are warnings, not errors.** If the extension can fall back gracefully (e.g. try PATH after config path fails), use warning severity. Reserve error severity for situations where no fallback exists.

4. **All errors must be actionable.** Every error message shown to the user must include what they can do about it: install a binary, set a path, check their connection, build from source.

5. **Invalid configuration is a warning if recoverable.** If a config value is invalid but the extension can fall back to a default, show a warning (not an error). Only use a modal error when the invalid config prevents any useful operation.

## Implementation Locations

| Extension | Error handling patterns                                                                   |
| --------- | --------------------------------------------------------------------------------------- |
| VS Code   | `src/utils/binary-download.ts`, `src/lsp/client-manager.ts`, `src/utils/util.ts`, `src/extension.ts`, `src/utils/checksum.ts` |
| Zed       | `src/download.rs`, `src/lib.rs` (error returns, status updates, and settings validation) |

## Related Files

- [`download-spec.md`](./download-spec.md) — Download protocol, including the download-specific error table.
- [`config-schema.md`](./config-schema.md) — Configuration settings reference.
- [`binary-download/SPECIFICATION.md`](./binary-download/SPECIFICATION.md) — Canonical binary download specification.
