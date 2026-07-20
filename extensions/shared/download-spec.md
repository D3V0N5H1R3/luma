# Download Service Specification

> **Canonical specification:** See [`binary-download/SPECIFICATION.md`](binary-download/SPECIFICATION.md) for the detailed reference implementation.
> This document provides a concise overview of the download protocol, URL patterns, retry policy, error handling, and per-extension implementation differences.

Concise reference for the shared binary download protocol used by both Luma editor extensions. For the full specification with implementation matrix and reference implementation details, see [`binary-download/SPECIFICATION.md`](binary-download/SPECIFICATION.md).

## Background

Binary download logic is duplicated across two editor extensions, each written in a different language:

| Extension | File | Language |
|-----------|------|----------|
| VS Code | `vscode/src/utils/binary-download.ts` | TypeScript |
| Zed | `zed/src/download.rs` | Rust |

A shared CLI downloader was investigated but is **not practical** because:

1. **Zed's WASM sandbox cannot spawn subprocesses** — Zed would still need its own implementation regardless.
2. **Bootstrapping problem** — a CLI downloader must itself be downloaded before it can download other binaries.
3. **Deep editor API coupling** — each extension uses editor-specific APIs for progress reporting, error display, and state persistence that cannot be abstracted behind a subprocess call without losing UX quality.

### What IS shared

Platform-to-suffix mappings are generated from `extensions/shared/platform-map.json` via `generate-platform-code.py`. Download protocol constants (retry policy, timeouts, URL patterns, checksum format) are defined in `extensions/shared/download-constants.json`. Both implementations are kept in sync by following the same algorithm and referencing the same shared configuration files.

### Per-extension implementation differences

| Concern | VS Code | Zed |
|---------|---------|-----|
| HTTP client | Node.js `fetch` / custom `fetch_json` | Zed WASM API |
| Checksum | `crypto.createHash('sha256')` | `sha2` crate (pure Rust, runs in WASM) |
| Archive extraction | `child_process.execFile` | Zed API `download_file` (handles extraction) |
| Progress reporting | `vscode.window.withProgress` | `set_language_server_installation_status` |
| Version tracking | VS Code `globalState` | Version-prefixed directory |

## Download Flow

Every extension follows a five-phase pipeline. Each phase must succeed before proceeding to the next.

```text
1. Resolve  →  2. Download  →  3. Verify  →  4. Extract  →  5. Finalize
```

### 1 — Resolve

Determine which release and asset to download.

- **Latest release API:** `https://api.github.com/repos/d3v0n5h1r3/luma/releases/latest`
- **Tagged release API:** `https://api.github.com/repos/d3v0n5h1r3/luma/releases/tags/{tag}`
- **Required fields:** `tag_name` (string), `assets` (array of `{ name, browser_download_url }`).
- Construct the expected asset name using the platform map (see [Platform Mapping](#platform-mapping)).
- If the asset is not found in the release, **abort** and log the available asset names.

### 2 — Download

- **Direct download URL:** `https://github.com/d3v0n5h1r3/luma/releases/download/{tag}/{asset}`
- **Latest shortcut:** `https://github.com/d3v0n5h1r3/luma/releases/latest/download/{asset}`
- **SHA256SUMS URL:** `https://github.com/d3v0n5h1r3/luma/releases/download/{tag}/SHA256SUMS`
- Download to a temporary location inside the extension's storage directory.
- Use HTTPS and reject HTTPS → HTTP redirect downgrades.

### 3 — Verify (SHA-256)

- Download `SHA256SUMS` from the same release.
- **Checksums format:** one line per asset — `{64-char hex hash}  {filename}` (two-space separator, matching `sha256sum` output).
- Compute the SHA-256 hash of the downloaded archive.
- Compare lowercase hex digests. On mismatch:
  1. Delete the downloaded archive.
  2. Warn the user about possible tampering.
  3. **Abort** — do not extract.
- If `SHA256SUMS` is missing from the release, **refuse the download** (VS Code). Zed instead **skips verification** (non-fatal) and proceeds, relying on HTTPS transport integrity; it still hard-fails on an actual checksum mismatch. Both extensions verify SHA-256 when `SHA256SUMS` is present.

| Platform   | SHA-256 method                                                        |
| ---------- | --------------------------------------------------------------------- |
| TypeScript | `crypto.createHash("sha256")` with streaming `fs.createReadStream`    |
| Rust/WASM  | `sha2::Sha256` crate (pure Rust, works in WASM)                      |

#### On checksum mismatch

1. Delete the downloaded archive immediately.
2. Warn the user that the file may have been tampered with (modal error severity).
3. **Abort** — do not extract or install.
4. Log both expected and actual hashes for debugging.

#### On missing entry in SHA256SUMS

Log that the asset name was not found in the checksums file and abort. List available entries for debugging.

### 4 — Extract

| Platform | Archive format | Extraction method                               |
| -------- | -------------- | ------------------------------------------------ |
| Windows  | `.zip`         | PowerShell `Expand-Archive` / Zed API `Zip`      |
| Linux    | `.tar.gz`      | `tar -xzf` / Zed API `GzipTar`                   |
| macOS    | `.tar.gz`      | `tar -xzf` / Zed API `GzipTar`                   |

- Binaries are at the **top level** of the archive (no nested directories).
- After extraction, delete the archive file.

### 5 — Finalize

1. **Set permissions:** on Unix, `chmod +x` or mode `0o755`.
2. **Record the installed version** for future update checks.
3. **Update configuration** to point to the installed binary path.
4. **Notify the user** of successful installation (notification level, not modal).

## Platform Mapping

Both extensions derive asset names from the canonical mapping in [`platform-map.json`](platform-map.json):

```json
{
    "linux":   { "x86_64": "linux-x86_64.tar.gz",   "aarch64": "linux-aarch64.tar.gz"   },
    "macos":   { "x86_64": "macos-x86_64.tar.gz",   "aarch64": "macos-aarch64.tar.gz"   },
    "windows": { "x86_64": "windows-x86_64.zip",    "aarch64": "windows-aarch64.zip"    }
}
```

### Asset naming pattern

```text
{binary}-{os}-{arch}.{ext}
```

| Component | Values                                   |
| --------- | ---------------------------------------- |
| `binary`  | `luma_lsp`, `luma_dap`, or `luma`        |
| `os`      | `linux`, `macos`, `windows`              |
| `arch`    | `x86_64`, `aarch64`                      |
| `ext`     | `tar.gz` (Linux, macOS), `zip` (Windows) |

**Examples:**

- `luma_lsp-linux-x86_64.tar.gz`
- `luma_dap-windows-x86_64.zip`

### Platform suffix lookup

Each extension maps its native platform identifiers to the canonical `{os}` and `{arch}` keys:

| Runtime value                        | Canonical OS | Canonical arch |
| ------------------------------------ | ------------ | -------------- |
| Node.js `win32`                      | `windows`    | —              |
| Node.js `linux`                      | `linux`      | —              |
| Node.js `darwin`                     | `macos`      | —              |
| Zed `Os::Windows`                    | `windows`    | —              |
| Zed `Os::Linux`                      | `linux`      | —              |
| Zed `Os::Mac`                        | `macos`      | —              |
| Node.js `x64`                        | —            | `x86_64`       |
| Node.js `arm64`                      | —            | `aarch64`      |
| Zed `Architecture::X8664`            | —            | `x86_64`       |
| Zed `Architecture::Aarch64`          | —            | `aarch64`      |

Platform-specific code is generated from `platform-map.json` by [`generate-platform-code.py`](generate-platform-code.py). Extensions must not hardcode platform mappings.

If the host platform does not appear in the mapping, the extension must **not** attempt a download and should suggest building from source or setting a manual binary path.

## Retry Policy

### Parameters

| Constant             | Value      | Description                              |
| -------------------- | ---------- | ---------------------------------------- |
| `MAX_RETRIES`        | 3          | Maximum number of retry attempts         |
| `RETRY_BASE_DELAY`   | 1000 ms    | Initial delay before first retry         |
| `MAX_RETRY_DELAY`    | 30000 ms   | Upper bound on any single retry delay    |
| `REQUEST_TIMEOUT`    | 30000 ms   | Per-request (metadata/checksum) timeout  |
| `DOWNLOAD_TIMEOUT`   | 120000 ms  | Per-archive download timeout             |
| `MAX_REDIRECTS`      | 5          | Maximum redirect hops before aborting    |

### Backoff formula

```text
delay = min(RETRY_BASE_DELAY × 2^attempt, MAX_RETRY_DELAY)
```

Where `attempt` is zero-indexed (first retry: 1000 ms, second: 2000 ms, third: 4000 ms).

### Retry scope

All HTTP operations are individually retried:

- GitHub API metadata fetch (`fetch_json`)
- Archive download (`download_file`)
- SHA256SUMS fetch (`fetch_text`)

If all retry attempts are exhausted, the error is propagated to the caller for user notification.

### Transport security

- All requests must use HTTPS.
- HTTPS → HTTP redirect downgrades must be rejected.

### Zed differences

Zed uses `zed::download_file()` (no retry — single attempt via the editor API). Retry logic is currently a VS Code-only implementation detail, but the Zed extension should adopt equivalent retry behaviour if its runtime permits.

## Resolution Order

Before downloading, extensions must check other sources first. See [`resolution-order.json`](resolution-order.json) for the canonical order:

1. **User-configured path** — explicit setting (e.g. `luma.lsp.path`).
2. **Bundled binary** — previously downloaded binary in extension storage.
3. **Auto-download** — fetch from GitHub releases (this protocol).
4. **System PATH** — `which`/`where` lookup as final fallback.

Download is only attempted at step 3 if steps 1 and 2 fail.

## Version Checking Logic

### When to skip download

A download is skipped (binary reused from cache) when **all** of the following are true:

1. The binary file exists at the expected path.
2. The recorded installed version matches the target version.

| Extension | Version record location                     | Comparison method                    |
| --------- | ------------------------------------------- | ------------------------------------ |
| VS Code   | `globalState.get(config.installed_tag_key)`  | Exact string match against `tag_name` |
| Zed       | Version directory name (`luma_lsp-{version}`) | File existence check in versioned dir |

### When to download

A download is triggered when any of the following are true:

- The binary file does not exist at the expected path.
- No version record exists (first install).
- The recorded version does not match the requested/latest version (update needed).

### Update flow (VS Code)

1. Fetch latest release tag from GitHub API.
2. Compare against `globalState` recorded tag.
3. If different, prompt the user: "Update available: {new_tag} (current: {old_tag})".
4. On acceptance:
   a. Back up existing binary (rename to `.tmp`).
   b. Download and install new version.
   c. On success: delete backup, update `globalState`, offer LSP restart.
   d. On failure: restore backup from `.tmp`, log error.

### Update flow (Zed)

1. Call `zed::latest_github_release()`.
2. Check if `luma_lsp-{version}/{binary}` already exists on disk.
3. If file exists, return cached path (no download).
4. If not, download into new version directory.

## Progress Reporting Conventions

Extensions must report progress through their editor's native progress API. The following phases must be communicated:

| Phase        | VS Code                                  | Zed                                        |
| ------------ | ---------------------------------------- | ------------------------------------------ |
| Downloading  | `withProgress` notification: "Downloading {display_name}…" | `set_language_server_installation_status(Downloading)` |
| Extracting   | Progress update: "Extracting {display_name}…"              | (implicit in `download_file` API)          |
| Complete     | Progress update: "{Display_name} ready."                   | `set_language_server_installation_status(None)` |
| Failed       | `showWarningMessage` with manual install suggestion        | `set_language_server_installation_status(Failed(msg))` |

### Rules

- **Success is log-only.** Routine checksum verification success goes to the output log, not a user notification.
- **Failure is actionable.** Every error shown to the user must include what they can do (install manually, set a path, check connection).
- **Progress is non-blocking.** Download progress must not prevent the user from using the editor. VS Code uses `ProgressLocation.Notification` (dismissible). Zed uses background status.
- **Cancellation is not supported.** Downloads run to completion or failure (too short and infrequent to warrant cancellation UI).

## Error Handling

See [`error-handling.md`](error-handling.md) for the full severity contract. Download-specific requirements:

| Error case                      | Severity        | Required action                                                |
| ------------------------------- | --------------- | -------------------------------------------------------------- |
| Network unavailable             | Notification    | Log error, suggest manual install, do not crash                 |
| GitHub API rate limited         | Notification    | Log error, suggest manual install                               |
| Request timeout (30 s)          | Retry           | Retry up to 3 times with exponential backoff                   |
| HTTPS → HTTP redirect           | Immediate abort | Reject as security downgrade, no retry                         |
| Too many redirects (>5)         | Immediate abort | Reject, no retry                                               |
| No matching asset in release    | Log-only        | List available asset names for debugging                        |
| Checksum mismatch               | Modal error     | Delete archive, warn about tampering, abort                    |
| SHA256SUMS missing              | Log + abort (VS Code); log + continue (Zed) | Refuse download (VS Code); Zed skips verification (non-fatal) and proceeds over HTTPS |
| SHA256SUMS entry missing        | Log + abort     | Log available entries, do not install                           |
| Extraction failure              | Notification    | Log error, delete archive and partial files                    |
| Binary not found after extract  | Log-only        | List directory contents for debugging                          |
| Unsupported platform            | Notification    | Suggest building from source or setting manual path            |
| All retries exhausted           | Notification    | Show final error, suggest manual install                       |

### Cleanup invariant

On any failure after download begins, the extension must delete:

- The downloaded archive file
- Any temporary checksum files (`SHA256SUMS`)
- Any partially extracted content

No partial or corrupt files may remain on disk after a failed download.

## Expected File System Layout After Download

### VS Code

```text
{globalStorageUri}/
└── bin/
    ├── luma_lsp          (or luma_lsp.exe on Windows)
    └── luma_dap          (or luma_dap.exe on Windows)
```

Version is tracked in VS Code's `globalState` under keys like `luma.lsp.installedTag`.

### Zed

```text
{extension_working_dir}/
├── luma_lsp-{version}/
│   └── luma_lsp          (or luma_lsp.exe on Windows)
└── luma_dap-{version}/
    └── luma_dap          (or luma_dap.exe on Windows)
```

Each version lives in its own directory. Old versions are not automatically cleaned up.

### Archive contents

Archives contain binaries at the **top level** (no nested directories). After extraction, the binary is directly at `{install_dir}/{binary_name}`.

### File permissions

On Unix platforms, installed binaries must be set executable:

- VS Code: `fs.chmodSync(path, 0o755)`
- Zed: `zed::make_file_executable()`

## Implementation Locations

| Extension | Primary module                                    | HTTP / retry       | Checksum            |
| --------- | ------------------------------------------------- | ------------------ | ------------------- |
| VS Code   | `extensions/vscode/src/utils/binary-download.ts`  | `src/utils/http.ts` | `src/utils/checksum.ts` |
| Zed       | `extensions/zed/src/download.rs`                  | `zed::download_file` | `sha2` crate       |

## Related Files

- `extensions/shared/platform-map.json` — Canonical platform-to-suffix mapping
- `extensions/shared/download-constants.json` — Download protocol constants (retry, timeouts, URLs)
- `extensions/shared/binary-download/SPECIFICATION.md` — Full download protocol
- `extensions/BINARY_ASSETS.md` — Asset naming specification
- `extensions/shared/BINARY_RESOLUTION.md` — Binary resolution order per editor
