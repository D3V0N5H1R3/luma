// Binary download for Zed (Rust/WASM).
//
// One of three per-editor implementations of the shared download protocol.
// The cross-editor rationale (why there is no shared CLI downloader), the
// list of shared configuration files, and the per-editor differences all
// live in the single source of truth: extensions/shared/download-spec.md.
//
// Zed-specific note: this extension runs in a WASM sandbox, but the pure-Rust
// `sha2` crate works there, so downloaded archives ARE verified against the
// release SHA256SUMS (see `try_verify_archive_checksum`). The raw archive is
// fetched once (uncompressed), checksummed against SHA256SUMS, and only then
// extracted IN-PROCESS from those exact verified bytes (see `extract_archive`,
// backed by the pure-Rust `flate2`/`tar`/`zip` crates that also run in the WASM
// sandbox). Because the bytes that are verified are the bytes that are
// extracted — a single download, no re-fetch — this mirrors the VS Code
// "download → verify → extract" order with no TOCTOU window. Checksum
// verification is skipped (non-fatal) only when the release publishes no
// SHA256SUMS asset at all; when SHA256SUMS is present, any verification failure
// — download error, unreadable manifest, missing entry, or hash mismatch —
// aborts the install (matching VS Code and download-spec.md, Verify section).
use sha2::{Digest, Sha256};
use zed_extension_api::{self as zed};

use crate::generated::download_constants::CHECKSUMS_FILENAME;

/// Prefix for all Luma extension log lines written to stderr.
const LOG_PREFIX: &str = "luma: ";

/// Abstraction over binary download operations.
/// Currently implemented directly via `zed::*` API calls.
/// The trait boundary enables mocking in tests.
pub(crate) trait BinaryDownloader {
    fn download_file(
        &self,
        url: &str,
        output_path: &str,
        file_type: zed::DownloadedFileType,
    ) -> Result<(), String>;
    fn make_executable(&self, path: &str) -> Result<(), String>;
    /// Read file contents as a string. Used to read downloaded SHA256SUMS files.
    fn read_text_file(&self, path: &str) -> Result<String, String>;
    /// Extract a downloaded archive at `archive_path` into `dest_dir`,
    /// in-process. `file_type` selects the archive format (`GzipTar` or `Zip`).
    /// Runs on the exact bytes already verified against SHA256SUMS.
    fn extract_archive(
        &self,
        archive_path: &str,
        dest_dir: &str,
        file_type: zed::DownloadedFileType,
    ) -> Result<(), String>;
}

pub(crate) struct ZedDownloader;

impl BinaryDownloader for ZedDownloader {
    fn download_file(
        &self,
        url: &str,
        output_path: &str,
        file_type: zed::DownloadedFileType,
    ) -> Result<(), String> {
        zed::download_file(url, output_path, file_type).map_err(|e| e.to_string())
    }

    fn make_executable(&self, path: &str) -> Result<(), String> {
        zed::make_file_executable(path).map_err(|e| e.to_string())
    }

    fn read_text_file(&self, path: &str) -> Result<String, String> {
        std::fs::read_to_string(path).map_err(|e| format!("Failed to read {path}: {e}"))
    }

    fn extract_archive(
        &self,
        archive_path: &str,
        dest_dir: &str,
        file_type: zed::DownloadedFileType,
    ) -> Result<(), String> {
        let bytes = std::fs::read(archive_path)
            .map_err(|e| format!("Failed to read archive {archive_path}: {e}"))?;
        match file_type {
            zed::DownloadedFileType::GzipTar => extract_gzip_tar(&bytes, dest_dir),
            zed::DownloadedFileType::Zip => extract_zip(&bytes, dest_dir),
            zed::DownloadedFileType::Gzip | zed::DownloadedFileType::Uncompressed => {
                Err("Unsupported archive type for in-process extraction".to_string())
            }
        }
    }
}

/// Extract a gzip-compressed tar archive (pure Rust, WASM-safe).
///
/// Permission and mtime restoration are disabled because the WASM sandbox does
/// not support the underlying `chmod`/`utimes` syscalls (and the binary is made
/// executable separately). Both `tar` and `zip` guard against path-traversal
/// ("zip slip") entries, and the bytes are already SHA-256 verified.
fn extract_gzip_tar(bytes: &[u8], dest_dir: &str) -> Result<(), String> {
    let decoder = flate2::read::GzDecoder::new(bytes);
    let mut archive = tar::Archive::new(decoder);
    archive.set_preserve_permissions(false);
    archive.set_preserve_mtime(false);
    archive
        .unpack(dest_dir)
        .map_err(|e| format!("Failed to extract tar.gz archive: {e}"))
}

/// Extract a zip archive (pure Rust, WASM-safe).
fn extract_zip(bytes: &[u8], dest_dir: &str) -> Result<(), String> {
    crate::zip_extract::extract_zip(bytes, dest_dir)
}

// ─── Platform asset helpers ───────────────────────────────────────
// Platform mapping generated from extensions/shared/platform-map.json.
// See extensions/BINARY_ASSETS.md for the canonical asset naming specification.

use crate::generated::platform::platform_suffix_for;

/// Maps a Zed OS enum to the canonical OS string used in platform-map.json.
fn os_to_canonical(platform: zed::Os) -> Option<&'static str> {
    match platform {
        zed::Os::Mac => Some("macos"),
        zed::Os::Linux => Some("linux"),
        zed::Os::Windows => Some("windows"),
    }
}

/// Maps a Zed Architecture enum to the canonical arch string used in platform-map.json.
fn arch_to_canonical(arch: zed::Architecture) -> Option<&'static str> {
    match arch {
        zed::Architecture::Aarch64 => Some("aarch64"),
        zed::Architecture::X8664 => Some("x86_64"),
        _ => None,
    }
}

/// Returns the platform-specific archive suffix (e.g. "macos-aarch64.tar.gz").
pub(crate) fn platform_suffix(platform: zed::Os, arch: zed::Architecture) -> Option<&'static str> {
    let os = os_to_canonical(platform)?;
    let arch_str = arch_to_canonical(arch)?;
    platform_suffix_for(os, arch_str)
}

/// Returns the full asset name for a binary on the given platform.
pub(crate) fn platform_asset_name_for(
    binary: &str,
    platform: zed::Os,
    arch: zed::Architecture,
) -> Option<String> {
    platform_suffix(platform, arch).map(|suffix| format!("{binary}-{suffix}"))
}

/// Returns the binary filename (with .exe on Windows) for the given base name.
pub(crate) fn binary_filename(base: &str, platform: zed::Os) -> String {
    if platform == zed::Os::Windows {
        format!("{base}.exe")
    } else {
        base.to_string()
    }
}

// ─── Binary resolution ────────────────────────────────────────────
// Both LSP and DAP binaries use the shared platform_asset_name_for() and
// binary_filename() helpers above, eliminating the previously duplicated
// platform match tables.

/// Check if a binary is already cached at the given path.
/// Returns `Some(path)` if the file exists and is a regular file.
fn check_cached_binary(binary_path: &str) -> Option<String> {
    match std::fs::metadata(binary_path) {
        Ok(m) if m.is_file() => Some(binary_path.to_string()),
        Ok(_) => {
            eprintln!("{LOG_PREFIX}{binary_path} exists but is not a file");
            None
        }
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => None,
        Err(e) => {
            eprintln!("{LOG_PREFIX}cache check failed for {binary_path}: {e}");
            None
        }
    }
}

/// Returns the appropriate download file type for the given platform.
fn file_type_for_platform(platform: zed::Os) -> zed::DownloadedFileType {
    match platform {
        zed::Os::Windows => zed::DownloadedFileType::Zip,
        _ => zed::DownloadedFileType::GzipTar,
    }
}

/// Find a named asset in a GitHub release.
fn find_release_asset<'a>(
    release: &'a zed::GithubRelease,
    asset_name: &str,
) -> Result<&'a zed::GithubReleaseAsset, String> {
    release
        .assets
        .iter()
        .find(|a| a.name == asset_name)
        .ok_or_else(|| {
            format!(
                "No asset named '{asset_name}' in release {}",
                release.version
            )
        })
}

/// Reports download progress and errors to the user.
pub(crate) trait StatusReporter {
    fn report_downloading(&self);
    fn report_failed(&self, msg: &str);
    fn report_success(&self);
}

/// Reports status via `zed::set_language_server_installation_status`.
pub(crate) struct LspStatusReporter<'a>(pub(crate) &'a zed::LanguageServerId);

impl StatusReporter for LspStatusReporter<'_> {
    fn report_downloading(&self) {
        zed::set_language_server_installation_status(
            self.0,
            &zed::LanguageServerInstallationStatus::Downloading,
        );
    }

    fn report_failed(&self, msg: &str) {
        zed::set_language_server_installation_status(
            self.0,
            &zed::LanguageServerInstallationStatus::Failed(msg.to_string()),
        );
    }

    fn report_success(&self) {
        zed::set_language_server_installation_status(
            self.0,
            &zed::LanguageServerInstallationStatus::None,
        );
    }
}

/// Reports status via `eprintln!`.
pub(crate) struct StderrStatusReporter;

impl StatusReporter for StderrStatusReporter {
    fn report_downloading(&self) {
        eprintln!("{LOG_PREFIX}downloading binary...");
    }

    fn report_failed(&self, msg: &str) {
        eprintln!("{LOG_PREFIX}{msg}");
    }

    fn report_success(&self) {
        eprintln!("{LOG_PREFIX}binary installed successfully");
    }
}

/// Downloads and installs a binary from a GitHub release.
///
/// The `reporter` controls how progress/errors are surfaced to the user.
/// `recovery_hint` is appended to download failure messages (e.g.
/// `" Syntax highlighting will still work."` for the LSP binary).
pub(crate) fn download_binary(
    downloader: &dyn BinaryDownloader,
    reporter: &dyn StatusReporter,
    binary_name: &str,
    platform: zed::Os,
    asset_name: &str,
    release: &zed::GithubRelease,
    recovery_hint: &str,
) -> zed::Result<String> {
    let asset = find_release_asset(release, asset_name)?;

    let version_dir = format!("{binary_name}-{}", release.version);
    let bin_name = binary_filename(binary_name, platform);
    let binary_path = format!("{version_dir}/{bin_name}");

    if let Some(path) = check_cached_binary(&binary_path) {
        return Ok(path);
    }

    reporter.report_downloading();

    // The install directory must exist before the uncompressed archive is
    // written into it (the extraction step below unpacks into the same dir).
    let _ = std::fs::create_dir_all(&version_dir);

    // Fetch the raw archive (uncompressed) so it can be verified against
    // SHA256SUMS and then extracted from the SAME on-disk bytes — a single
    // download with no re-fetch (see the file header and download-spec.md).
    let archive_path = format!("{version_dir}/{asset_name}");
    if let Err(e) = downloader.download_file(
        &asset.download_url,
        &archive_path,
        zed::DownloadedFileType::Uncompressed,
    ) {
        // Error severity: notification warning (recoverable — user can install manually).
        // See extensions/shared/error-handling.md.
        let user_msg = format!(
            "Failed to download {binary_name}: {e}. \
             Install it manually or add it to PATH.{recovery_hint}"
        );
        reporter.report_failed(&user_msg);
        return Err(user_msg);
    }

    // Verify the archive against SHA256SUMS (skipped only when the release has
    // no SHA256SUMS asset). On failure, remove the unverified archive and abort
    // before it is ever extracted.
    if let Err(e) =
        try_verify_archive_checksum(downloader, release, asset_name, &archive_path, &version_dir)
    {
        let _ = std::fs::remove_file(&archive_path);
        reporter.report_failed(&e);
        return Err(e);
    }

    // Checksum verified (or skipped): extract the binary from the verified
    // archive bytes in-process. The archive is only needed for extraction, so
    // remove it afterwards regardless of outcome.
    let extract_result = downloader.extract_archive(
        &archive_path,
        &version_dir,
        file_type_for_platform(platform),
    );
    let _ = std::fs::remove_file(&archive_path);
    if let Err(e) = extract_result {
        let user_msg = format!(
            "Failed to extract {binary_name}: {e}. \
             Install it manually or add it to PATH.{recovery_hint}"
        );
        reporter.report_failed(&user_msg);
        return Err(user_msg);
    }

    if let Err(e) = downloader.make_executable(&binary_path) {
        let user_msg = format!("Failed to make {binary_name} executable: {e}");
        reporter.report_failed(&user_msg);
        return Err(user_msg);
    }

    reporter.report_success();

    Ok(binary_path)
}

// ─── Checksum verification ────────────────────────────────────────
// Binary resolution order: see extensions/shared/resolution-order.json.
// CHECKSUMS_FILENAME is generated from extensions/shared/download-constants.json.

/// Verify the SHA-256 checksum of the downloaded archive against SHA256SUMS.
///
/// Downloads the SHA256SUMS manifest from the release, looks up the expected
/// hash for `asset_name` (the archive), and verifies it against the raw archive
/// on disk at `archive_path`. This runs BEFORE extraction so a tampered archive
/// is never unpacked (see download-spec.md, Verify section).
///
/// Returns `Ok(())` only when the archive is verified, or when the release
/// publishes no SHA256SUMS asset at all (Zed-specific graceful degradation,
/// relying on HTTPS transport integrity). When SHA256SUMS IS present, any
/// failure to verify — download error, unreadable manifest, missing entry, or
/// hash mismatch — returns `Err` so the caller aborts the install (matching the
/// VS Code implementation).
fn try_verify_archive_checksum(
    downloader: &dyn BinaryDownloader,
    release: &zed::GithubRelease,
    asset_name: &str,
    archive_path: &str,
    version_dir: &str,
) -> Result<(), String> {
    let checksums_asset = match release.assets.iter().find(|a| a.name == CHECKSUMS_FILENAME) {
        Some(a) => a,
        None => {
            eprintln!(
                "{LOG_PREFIX}no SHA256SUMS asset in release — skipping checksum verification"
            );
            return Ok(());
        }
    };

    // SHA256SUMS is downloaded alongside the archive in `version_dir`.
    let checksums_path = format!("{version_dir}/{CHECKSUMS_FILENAME}");

    downloader
        .download_file(
            &checksums_asset.download_url,
            &checksums_path,
            zed::DownloadedFileType::Uncompressed,
        )
        .map_err(|e| format!("Failed to download SHA256SUMS: {e}. Aborting install."))?;

    // The manifest is only needed for the lookup below; remove it regardless of
    // whether the read succeeds so an unreadable or partial file is never left
    // behind on the abort path (mirrors the archive cleanup in the caller).
    let read_result = downloader.read_text_file(&checksums_path);
    let _ = std::fs::remove_file(&checksums_path);
    let checksums_content =
        read_result.map_err(|e| format!("Failed to read SHA256SUMS: {e}. Aborting install."))?;

    let expected_hash = lookup_expected_hash(&checksums_content, asset_name).ok_or_else(|| {
        format!("Asset '{asset_name}' has no entry in SHA256SUMS. Aborting install.")
    })?;

    verify_checksum(archive_path, &expected_hash).map_err(|e| {
        format!("{e}. The download may have been tampered with — aborting install.")
    })?;

    Ok(())
}

/// Verify SHA-256 checksum of a file against an expected hex hash.
///
/// Uses the `sha2` crate (pure Rust, works in WASM sandbox).
pub(crate) fn verify_checksum(file_path: &str, expected_hash: &str) -> Result<(), String> {
    let data =
        std::fs::read(file_path).map_err(|e| format!("Failed to read file for checksum: {e}"))?;

    let mut hasher = Sha256::new();
    hasher.update(&data);
    let result = hasher.finalize();
    // sha2 0.11's `finalize()` returns a `hybrid_array::Array`, which no longer
    // implements `LowerHex` (unlike the `GenericArray` of 0.10), so format each
    // byte to lowercase hex explicitly rather than with `{result:x}`.
    let actual_hash: String = result.iter().map(|byte| format!("{byte:02x}")).collect();

    if actual_hash != expected_hash.to_lowercase() {
        return Err(format!(
            "Checksum mismatch for {file_path}:\n  Expected: {expected_hash}\n  Actual:   {actual_hash}"
        ));
    }

    Ok(())
}

/// Returns true if `s` is a 64-character hex SHA-256 digest.
fn is_sha256_hex(s: &str) -> bool {
    s.len() == 64 && s.bytes().all(|b| b.is_ascii_hexdigit())
}

/// Look up the expected hash for an asset in a SHA256SUMS-format string.
///
/// Each line is "<64-hex-hash> <separator> <filename>". The hash is the first
/// whitespace-delimited token; the filename is the remainder (a leading "*"
/// binary-mode marker is stripped). Lines whose first token is not a 64-char
/// hex digest are ignored, matching the VS Code implementation.
pub(crate) fn lookup_expected_hash(checksums_content: &str, asset_name: &str) -> Option<String> {
    for line in checksums_content.lines() {
        let mut parts = line.splitn(2, char::is_whitespace);
        if let (Some(hash), Some(rest)) = (parts.next(), parts.next()) {
            let name = rest.trim();
            let name = name.strip_prefix('*').unwrap_or(name);
            if is_sha256_hex(hash) && name == asset_name {
                return Some(hash.to_lowercase());
            }
        }
    }
    None
}
