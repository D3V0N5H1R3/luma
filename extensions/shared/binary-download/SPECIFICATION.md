# Binary Download Specification

This document is the canonical, detailed specification for how Luma editor extensions download, verify, and install pre-built binaries from GitHub releases. Both extension implementations (VS Code TypeScript and Zed Rust) must follow it to ensure consistent behaviour across editors.

Values referenced throughout this document are defined once in machine-readable form and must not be hardcoded by extensions:

- [`download-constants.json`](../download-constants.json) — release URLs, checksum format, retry policy, timeouts, and transport rules.
- [`platform-map.json`](../platform-map.json) — canonical platform→archive-suffix mapping.
- [`resolution-order.json`](../resolution-order.json) — binary resolution order.

For a concise operational overview see [`download-spec.md`](../download-spec.md); for the per-editor resolution order see [`BINARY_RESOLUTION.md`](../BINARY_RESOLUTION.md).

## Table of Contents

1. [Overview](#1--overview)
2. [GitHub Release Structure](#2--github-release-structure)
3. [Asset Naming Convention](#3--asset-naming-convention)
4. [Platform Detection](#4--platform-detection)
5. [Download Protocol](#5--download-protocol)
6. [Checksum Verification](#6--checksum-verification)
7. [Archive Extraction](#7--archive-extraction)
8. [Binary Installation](#8--binary-installation)
9. [Error Handling](#9--error-handling)
10. [Implementation Matrix](#10--implementation-matrix)
11. [Reference Implementation](#11--reference-implementation)

---

## 1 — Overview

Each Luma editor extension needs access to pre-built binaries (`luma_lsp`, `luma_dap`, and optionally the `luma` interpreter). Rather than requiring users to install these manually, extensions download them automatically from GitHub releases on first use.

This specification defines the shared protocol so that both extensions behave identically from the user's perspective, even though each is written in a different language.

---

## 2 — GitHub Release Structure

All binaries are published as assets on GitHub releases in the `d3v0n5h1r3/luma` repository.

### API Endpoints

| Purpose         | URL                                                                     |
| --------------- | ----------------------------------------------------------------------- |
| Latest release  | `https://api.github.com/repos/d3v0n5h1r3/luma/releases/latest`         |
| Specific tag    | `https://api.github.com/repos/d3v0n5h1r3/luma/releases/tags/{tag}`     |
| Direct download | `https://github.com/d3v0n5h1r3/luma/releases/download/{tag}/{asset}`   |
| Latest download | `https://github.com/d3v0n5h1r3/luma/releases/latest/download/{asset}` |

### Release Object Fields

Extensions must read at minimum:

| Field      | Type            | Description                          |
| ---------- | --------------- | ------------------------------------ |
| `tag_name` | `string`        | Version tag (e.g. `v0.8.0`)         |
| `assets`   | `array<object>` | List of downloadable asset objects   |

Each asset object contains:

| Field                  | Type     | Description                      |
| ---------------------- | -------- | -------------------------------- |
| `name`                 | `string` | Filename of the asset            |
| `browser_download_url` | `string` | Direct download URL (VS Code)    |
| `download_url`         | `string` | Download URL (Zed extension API) |

> **Note:** Zed's `zed::GithubRelease` uses `download_url` rather than `browser_download_url`, so implementations must use whichever field their platform API provides.

---

## 3 — Asset Naming Convention

Binary assets follow a strict naming pattern:

```text
{binary}-{os}-{arch}.{ext}
```

| Component | Values                                   |
| --------- | ---------------------------------------- |
| `binary`  | `luma_lsp`, `luma_dap`, or `luma`        |
| `os`      | `windows`, `linux`, `macos`              |
| `arch`    | `x86_64`, `aarch64`                      |
| `ext`     | `zip` (Windows), `tar.gz` (Linux, macOS) |

### Supported Platform Assets

| Platform             | LSP asset                          | DAP asset                          |
| -------------------- | ---------------------------------- | ---------------------------------- |
| Linux x86_64         | `luma_lsp-linux-x86_64.tar.gz`     | `luma_dap-linux-x86_64.tar.gz`     |
| Linux aarch64        | `luma_lsp-linux-aarch64.tar.gz`    | `luma_dap-linux-aarch64.tar.gz`    |
| macOS x86_64         | `luma_lsp-macos-x86_64.tar.gz`     | `luma_dap-macos-x86_64.tar.gz`     |
| macOS aarch64        | `luma_lsp-macos-aarch64.tar.gz`    | `luma_dap-macos-aarch64.tar.gz`    |
| Windows x86_64       | `luma_lsp-windows-x86_64.zip`      | `luma_dap-windows-x86_64.zip`      |
| Windows aarch64      | `luma_lsp-windows-aarch64.zip`     | `luma_dap-windows-aarch64.zip`     |

### Binary Names

| Platform | LSP binary     | DAP binary     | Interpreter |
| -------- | -------------- | -------------- | ----------- |
| Windows  | `luma_lsp.exe` | `luma_dap.exe` | `luma.exe`  |
| Linux    | `luma_lsp`     | `luma_dap`     | `luma`      |
| macOS    | `luma_lsp`     | `luma_dap`     | `luma`      |

---

## 4 — Platform Detection

Each extension must map its host platform to the correct `{os}` and `{arch}` values.

### OS mapping

| Runtime value                       | `os`      |
| ----------------------------------- | --------- |
| Node.js `win32` / Zed `Os::Windows` | `windows` |
| Node.js `linux` / Zed `Os::Linux`   | `linux`   |
| Node.js `darwin` / Zed `Os::Mac`    | `macos`   |

### Architecture mapping

| Runtime value                                 | `arch`    |
| --------------------------------------------- | --------- |
| Node.js `x64` / Zed `Architecture::X8664`     | `x86_64`  |
| Node.js `arm64` / Zed `Architecture::Aarch64` | `aarch64` |

The detected `os` and `arch` are combined into the asset name (see [Asset Naming Convention](#3--asset-naming-convention)); both `x86_64` and `aarch64` are supported on every OS.

### Unsupported Platforms

If the host platform does not appear in the mapping table, the extension must:

1. Log a message explaining no pre-built binary is available.
2. Suggest building from source or setting a manual path.
3. **Not** attempt a download.

---

## 5 — Download Protocol

The download follows a sequential pipeline. Each step must succeed before proceeding to the next.

```text
Resolve release → Match asset → Download archive → Verify checksum → Extract → Set permissions → Clean up
```

### Step-by-step

1. **Resolve release.** Fetch the latest release metadata from the GitHub API, or use a user-specified version tag.
2. **Match asset.** Find the asset whose `name` matches the platform asset name constructed from the naming convention. If no matching asset exists, abort with a clear error.
3. **Download archive.** Download the asset to a temporary location within the extension's storage directory. Use HTTPS.
4. **Verify checksum.** Download the `SHA256SUMS` manifest and verify the archive's SHA-256 hash matches (see [Checksum Verification](#6--checksum-verification)).
5. **Extract archive.** Extract the binary from the archive into the installation directory (see [Archive Extraction](#7--archive-extraction)).
6. **Set permissions.** On Unix platforms, set the binary as executable (`chmod +x` or mode `0o755`).
7. **Clean up.** Delete the downloaded archive and any temporary files (e.g. `SHA256SUMS`).

### Transport Security

All network operations are subject to the following mandatory rules, regardless of editor (authoritative values in [`download-constants.json`](../download-constants.json) under `transport`):

- Requests must use HTTPS; plain HTTP is never used.
- An HTTPS→HTTP redirect is a security downgrade and must be rejected.
- Redirects are bounded by `max_redirects` (5); exceeding the limit aborts the request.
- Outgoing requests set a `User-Agent` beginning with `luma`.

### Retry and Timeouts

Transient failures (network errors, timeouts) should be retried with exponential backoff. Parameters live in [`download-constants.json`](../download-constants.json) under `retry_policy` and `timeouts`:

| Constant               | Value  | Purpose                                 |
| ---------------------- | ------ | --------------------------------------- |
| `max_retries`          | 3      | Maximum retry attempts per operation    |
| `base_delay_ms`        | 1000   | Initial backoff delay                   |
| `max_delay_ms`         | 30000  | Upper bound on any single backoff delay |
| `request_timeout_ms`   | 30000  | Per-request (metadata/checksum) timeout |
| `download_timeout_ms`  | 120000 | Per-archive download timeout            |

Backoff is `min(base_delay_ms × 2^attempt, max_delay_ms)` with a zero-indexed `attempt`. Retry support is editor-dependent: VS Code implements application-level retry (`src/utils/http.ts`), and Zed performs a single attempt through the editor API. Extensions should adopt equivalent retry behaviour where their runtime permits.

---

## 6 — Checksum Verification

Every release includes a `SHA256SUMS` file containing SHA-256 hashes for all assets.

### Checksums File Format

```text
{hash}  {filename}
{hash}  {filename}
...
```

Each line contains a 64-character lowercase hexadecimal SHA-256 hash, followed by two spaces, followed by the asset filename. This matches the output format of `sha256sum`.

### Verification Steps

1. Locate the `SHA256SUMS` asset in the release metadata, or download it from the direct download URL.
2. Parse the checksums file: for each line, extract the hash and filename.
3. Look up the entry matching the downloaded asset's filename.
4. Compute the SHA-256 hash of the downloaded archive.
5. Compare the computed hash (lowercase hex) against the expected hash (lowercase hex).
6. If they do not match, **abort the installation**, delete the downloaded archive, and report the mismatch.

### Platform-specific SHA-256 Computation

| Platform   | Method                                            |
| ---------- | ------------------------------------------------- |
| TypeScript | `crypto.createHash("sha256")` streaming           |
| Rust/WASM  | `sha2::Sha256` crate (pure Rust, works in the WASM sandbox) |

### Zed Behaviour

The Zed extension runs in a WASM sandbox and cannot spawn subprocesses, but the pure-Rust `sha2` crate works there, so downloaded assets **are** verified against the release `SHA256SUMS`. Verification hard-fails on a mismatch (the binary is deleted). It is skipped (non-fatal) only when the `SHA256SUMS` asset is missing or unreadable, in which case HTTPS transport integrity is the fallback. See `extensions/zed/src/download.rs`.

---

## 7 — Archive Extraction

### Archive Formats

| Platform | Format    | Extraction method                                         |
| -------- | --------- | --------------------------------------------------------- |
| Windows  | `.zip`    | PowerShell `Expand-Archive` (TypeScript) or `zed::DownloadedFileType::Zip` |
| Linux    | `.tar.gz` | `tar -xzf` (TypeScript) or `zed::DownloadedFileType::GzipTar`             |
| macOS    | `.tar.gz` | `tar -xzf` (TypeScript) or `zed::DownloadedFileType::GzipTar`             |

### Extraction Expectations

- The archive contains the binary at the **top level** (no nested directories inside the archive for individual binary assets).
- After extraction, the binary should be directly accessible at `{install_dir}/{binary_name}`.

---

## 8 — Binary Installation

### Binary Resolution Order

Before any download, each extension resolves the binary through a fixed precedence (canonical order in [`resolution-order.json`](../resolution-order.json); per-editor detail in [`BINARY_RESOLUTION.md`](../BINARY_RESOLUTION.md)):

1. **User-configured path** — an explicit setting (e.g. `luma.lsp.path`).
2. **Bundled binary** — a previously downloaded binary in the extension's storage directory.
3. **Auto-download** — fetch from GitHub releases using the protocol in this document.
4. **System `PATH`** — a `which`/`where` lookup as the final fallback.

The download pipeline ([Download Protocol](#5--download-protocol)) is reached only at step 3, when steps 1 and 2 do not yield a usable binary. This is what makes installation idempotent: an already-resolved binary is reused rather than re-downloaded.

### Installation Directories

Each extension stores downloaded binaries in its own managed directory:

| Extension | Installation directory                                     |
| --------- | ---------------------------------------------------------- |
| VS Code   | `{globalStorageUri}/bin/`                                  |
| Zed       | `luma_lsp-{version}/` in the extension's working directory |

### Post-installation

After successful installation, each extension must:

1. **Record the installed version** for future update checks.
2. **Update configuration** to point to the installed binary path.
3. **Notify the user** that installation succeeded.

### Update Flow

When checking for updates:

1. Fetch the latest release tag from GitHub.
2. Compare against the recorded installed tag.
3. If a newer version is available, prompt the user (VS Code) or download automatically (Zed).
4. For in-place updates, back up the existing binary before overwriting. Restore it if the update fails.

---

## 9 — Error Handling

All implementations must handle the following error cases gracefully:

| Error case                     | Required behaviour                                              |
| ------------------------------ | --------------------------------------------------------------- |
| Network unavailable            | Log the error, suggest manual install, do not crash              |
| GitHub API rate limited        | Log the error, suggest manual install                            |
| Request timeout                | Retry up to `max_retries` times with exponential backoff         |
| HTTPS→HTTP redirect or too many redirects | Reject as a security downgrade; abort without retrying |
| All retries exhausted          | Report the final error, suggest manual install                   |
| No matching asset in release   | List available assets in the log for debugging                   |
| Checksum mismatch              | Delete the archive, warn about possible tampering, abort         |
| SHA256SUMS missing from release | Refuse the download (VS Code); Zed skips verification (non-fatal) and proceeds over HTTPS |
| Extraction failure             | Log the error, clean up partial files                            |
| Unsupported platform           | Inform the user, suggest building from source                    |
| Binary not found after extract | Log the error, list directory contents for debugging             |

---

## 10 — Implementation Matrix

Current state of each extension's conformance to this specification:

| Capability              | VS Code (TypeScript)     | Zed (Rust/WASM)          |
| ----------------------- | ------------------------ | ------------------------ |
| Platform detection      | ✅ `process.platform`    | ✅ `zed::current_platform()` |
| Asset name construction | ✅ Per-binary            | ✅ Per-binary            |
| GitHub API fetch        | ✅ `fetch_json`          | ✅ `zed::latest_github_release()` |
| Archive download        | ✅ `download_file`       | ✅ `zed::download_file()`  |
| SHA-256 verification    | ✅ Node.js `crypto`      | ✅ `sha2` crate          |
| Archive extraction      | ✅ `Expand-Archive`/`tar`| ✅ Zed API               |
| Set executable          | ✅ `chmod 0o755`         | ✅ `zed::make_file_executable()` |
| Version tracking        | ✅ `globalState`         | ✅ Version directory     |
| Update checking         | ✅ Interactive prompt    | ✅ Automatic per-version |
| Multi-binary support    | ✅ LSP + DAP configs     | ✅ LSP + DAP             |

---

## 11 — Reference Implementation

The VS Code extension is the reference implementation. Its `binary-download.ts` is a **barrel module** that re-exports focused submodules under `src/utils/binary/`:

| Submodule            | Responsibility                                                                                                            |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `binary/types.ts`    | `BinaryConfig`, `GithubRelease`/`GithubAsset` types, `LSP_CONFIG`/`DAP_CONFIG`, and `parseGithubRelease()` validation.    |
| `binary/platform.ts` | `getPlatformAssetName()` (canonical platform→asset-name mapping), `getBinaryFilename()`, and `getBundledBinaryPath()`.    |
| `binary/archive.ts`  | `extractArchive()` — cross-platform `.zip`/`.tar.gz` extraction.                                                          |
| `binary/resolve.ts`  | `fetchLatestRelease()`, `downloadBinary()` (download → verify → extract → set permissions), and `resolveBinaryCommand()` (the four-step [resolution order](#8--binary-installation)). |
| `binary/update.ts`   | `checkForBinaryUpdate()` — update checking with interactive/silent modes and rollback on failure.                        |

SHA-256 verification lives in `src/utils/checksum.ts`, and HTTP fetching with retry and backoff lives in `src/utils/http.ts`.

The Zed implementation follows the same logical steps, adapted to its runtime APIs. Where behaviour diverges — for example Zed downloads in a single attempt through the editor API (no application-level retry) and treats a missing `SHA256SUMS` as a non-fatal skip — the deviation is documented in the Zed extension's source with a comment referencing this specification.

### File Locations

| Extension | Modules                                                                                                                                              |
| --------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| VS Code   | `extensions/vscode/src/utils/binary-download.ts` (barrel) re-exporting `src/utils/binary/{types,platform,archive,resolve,update}.ts`; checksum in `src/utils/checksum.ts`; HTTP/retry in `src/utils/http.ts` |
| Zed       | `extensions/zed/src/download.rs`                                                                                                                     |
