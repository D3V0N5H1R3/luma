# Binary Asset Naming Convention

This document is the human-readable reference for how GitHub release asset names are constructed across all Luma editor extensions (VS Code and Zed). The machine-readable source of truth is [`shared/platform-map.json`](shared/platform-map.json); the per-editor platform lookups are generated from it by [`shared/generate-platform-code.py`](shared/generate-platform-code.py), so no extension hardcodes the suffix table.

## Format

```text
{binary}-{os}-{arch}.{ext}
```

| Field    | Values                                   |
| -------- | ---------------------------------------- |
| `binary` | `luma`, `luma_lsp`, `luma_dap`           |
| `os`     | `linux`, `macos`, `windows`              |
| `arch`   | `x86_64`, `aarch64`                      |
| `ext`    | `tar.gz` (Linux, macOS), `zip` (Windows) |

## Platform Suffix Mapping

These six suffixes are the values stored in [`shared/platform-map.json`](shared/platform-map.json). The `os` and `arch` tokens are the canonical keys onto which each extension maps its native platform identifiers.

| OS (`os`) | Arch (`arch`) | Suffix                 |
| --------- | ------------- | ---------------------- |
| `linux`   | `x86_64`      | `linux-x86_64.tar.gz`  |
| `linux`   | `aarch64`     | `linux-aarch64.tar.gz` |
| `macos`   | `x86_64`      | `macos-x86_64.tar.gz`  |
| `macos`   | `aarch64`     | `macos-aarch64.tar.gz` |
| `windows` | `x86_64`      | `windows-x86_64.zip`   |
| `windows` | `aarch64`     | `windows-aarch64.zip`  |

## Examples

Per-binary archives, each containing a single executable:

```text
luma_lsp-linux-x86_64.tar.gz
luma_lsp-macos-aarch64.tar.gz
luma_lsp-windows-x86_64.zip
luma_dap-linux-x86_64.tar.gz
luma_dap-windows-aarch64.zip
```

Full distribution bundle, containing `luma`, `luma_lsp`, and `luma_dap`:

```text
luma-linux-x86_64.tar.gz
luma-macos-aarch64.tar.gz
luma-windows-x86_64.zip
```

## Checksums

Every release also publishes a single `SHA256SUMS` file next to the archives. It contains one `{64-char hex hash}  {asset}` line per archive (two-space separator, matching `sha256sum` output), and extensions verify a downloaded archive against it before extracting. See [`shared/download-spec.md`](shared/download-spec.md) for the full verify step.

## Extension Implementations

Each extension maps its native platform identifiers (Node's `process.platform` and `process.arch`, or Zed's `zed::Os` and `zed::Architecture`) to the canonical `os` and `arch` keys, then looks up the suffix in code generated from `platform-map.json`.

| Extension | Platform entry point        | Source file                           |
| --------- | --------------------------- | ------------------------------------- |
| VS Code   | `getPlatformAssetName()`    | `vscode/src/utils/binary/platform.ts` |
| Zed       | `platform_asset_name_for()` | `zed/src/download.rs`                  |

## Related Files

- [`shared/platform-map.json`](shared/platform-map.json) — canonical platform-to-suffix mapping (machine-readable).
- [`shared/generate-platform-code.py`](shared/generate-platform-code.py) — generates the per-editor suffix lookups from the mapping.
- [`shared/download-spec.md`](shared/download-spec.md) — full download protocol: resolve, download, verify, extract, finalize.
- [`shared/download-constants.json`](shared/download-constants.json) — release URLs, checksum format, retry policy, and timeouts.
- [`shared/BINARY_RESOLUTION.md`](shared/BINARY_RESOLUTION.md) — binary resolution order for each editor.
