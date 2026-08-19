#!/usr/bin/env python3
"""Shared binary downloader for Luma editor extensions.

DEPRECATED: This standalone downloader is not used by any editor extension.
Each extension implements its own download logic using editor-native APIs
(see download-spec.md for rationale). This script is retained as
a reference implementation and for potential CI/build use.

Downloads and verifies Luma binaries from GitHub releases. The repository URLs
and checksum-manifest name are loaded from ../download-constants.json and the
archive suffixes from ../platform-map.json, so no canonical constant is
hardcoded here.

Usage:
    python download.py --target-dir <dir> [--binary luma|luma_lsp|luma_dap] [--version latest]

Exit codes:
    0 — success
    1 — download failed
    2 — checksum verification failed
    3 — unsupported platform
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path
from typing import Any


def load_platform_map() -> dict[str, dict[str, str]]:
    """Load platform-map.json from the shared directory (canonical archive suffixes)."""
    shared_dir = Path(__file__).resolve().parent.parent
    map_path = shared_dir / "platform-map.json"
    with open(map_path, encoding="utf-8") as f:
        return json.load(f)


def load_download_constants() -> dict[str, Any]:
    """Load download-constants.json — the canonical repo slug, URLs and checksum name.

    Sourcing these here keeps the downloader from re-hardcoding values that would
    otherwise drift from the machine-readable constants the editor extensions use.
    """
    shared_dir = Path(__file__).resolve().parent.parent
    constants_path = shared_dir / "download-constants.json"
    with open(constants_path, encoding="utf-8") as f:
        return json.load(f)


def detect_platform() -> tuple[str, str]:
    """Detect the current OS and architecture using canonical names.

    Returns:
        Tuple of (os_name, arch) matching keys in platform-map.json.

    Raises:
        SystemExit: If the platform is unsupported (exit code 3).
    """
    system = platform.system().lower()
    machine = platform.machine().lower()

    # Bridge Python's platform strings to the canonical OS/arch names that key
    # platform-map.json. These are stable interpreter-runtime aliases, not
    # release constants, so there is no canonical JSON to load them from.
    os_map = {"linux": "linux", "darwin": "macos", "windows": "windows"}
    arch_map = {
        "x86_64": "x86_64",
        "amd64": "x86_64",
        "aarch64": "aarch64",
        "arm64": "aarch64",
    }

    os_name = os_map.get(system)
    arch = arch_map.get(machine)

    if not os_name or not arch:
        emit_progress("error", f"Unsupported platform: {system}/{machine}")
        sys.exit(3)

    return os_name, arch


def get_asset_suffix(platform_map: dict, os_name: str, arch: str) -> str:
    """Look up the archive suffix for the given platform."""
    suffix = platform_map.get(os_name, {}).get(arch)
    if not suffix:
        emit_progress("error", f"No binary available for {os_name}/{arch}")
        sys.exit(3)
    return suffix


def emit_progress(event: str, message: str = "", **kwargs: Any) -> None:
    """Emit a JSON-lines progress event to stdout for extensions to parse."""
    payload: dict[str, Any] = {"event": event, "message": message}
    payload.update(kwargs)
    print(json.dumps(payload), flush=True)


def fetch_json(url: str) -> Any:
    """Fetch JSON from a URL."""
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read().decode("utf-8"))


def fetch_release(version: str, urls: dict[str, str]) -> dict[str, Any]:
    """Fetch release metadata from GitHub API.

    Args:
        version: "latest" or a specific tag (e.g. "v1.2.3").
        urls: The ``urls`` block from download-constants.json (canonical API
            endpoints), so the repository slug is not hardcoded here.
    """
    url = urls["api_latest"] if version == "latest" else urls["api_tagged"].format(tag=version)

    emit_progress("fetch_release", f"Fetching release info ({version})...")
    try:
        return fetch_json(url)
    except Exception as e:
        emit_progress("error", f"Failed to fetch release: {e}")
        sys.exit(1)


def download_file(url: str, dest: Path) -> None:
    """Download a file with progress reporting."""
    emit_progress("download_start", f"Downloading {dest.name}...", url=url)

    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=120) as resp:
        total = int(resp.headers.get("Content-Length", 0))
        downloaded = 0
        chunk_size = 65536

        with open(dest, "wb") as f:
            while True:
                chunk = resp.read(chunk_size)
                if not chunk:
                    break
                f.write(chunk)
                downloaded += len(chunk)
                if total > 0:
                    pct = int(downloaded * 100 / total)
                    emit_progress("download_progress", percent=pct, bytes=downloaded, total=total)

    emit_progress("download_complete", f"Downloaded {dest.name} ({downloaded} bytes)")


def verify_checksum(file_path: Path, expected_hash: str) -> bool:
    """Verify SHA-256 checksum of a downloaded file.

    Returns:
        True if checksum matches.
    """
    emit_progress("checksum_start", f"Verifying checksum for {file_path.name}...")
    sha256 = hashlib.sha256()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            sha256.update(chunk)

    actual = sha256.hexdigest().lower()
    expected = expected_hash.lower()

    if actual != expected:
        emit_progress(
            "checksum_failed",
            f"Checksum mismatch: expected {expected}, got {actual}",
        )
        return False

    emit_progress("checksum_ok", "Checksum verified")
    return True


# A SHA256SUMS line: a 64-hex digest, whitespace, an optional "*" binary-mode
# marker, then the filename. This mirrors the shared parser contract exercised
# by the golden fixture (../sha256sums-sample.txt) so the downloader agrees with
# the VS Code and Zed checksum parsers.
_CHECKSUM_LINE = re.compile(r"^([0-9a-fA-F]{64})\s+\*?(.+)$")


def find_asset_hash(manifest_text: str, asset_name: str) -> str | None:
    """Return the lowercased SHA-256 digest for ``asset_name``, or ``None``.

    The digest is the first whitespace-delimited token and must be exactly 64
    hex characters; the filename is the remainder with an optional leading ``*``
    binary-mode marker stripped. Blank lines, comments and non-64-hex ("short")
    digests are ignored, matching ../sha256sums-sample.txt.
    """
    for raw in manifest_text.splitlines():
        match = _CHECKSUM_LINE.match(raw.strip())
        if match and match.group(2).strip() == asset_name:
            return match.group(1).lower()
    return None


def fetch_expected_checksum(release: dict, asset_name: str, checksums_filename: str) -> str | None:
    """Download the checksum manifest from the release and find asset_name's hash.

    ``checksums_filename`` comes from download-constants.json (``checksums.filename``)
    so the manifest name is never hardcoded here.
    """
    checksums_asset = next((a for a in release["assets"] if a["name"] == checksums_filename), None)
    if not checksums_asset:
        emit_progress(
            "warning", f"No {checksums_filename} asset in release — skipping verification"
        )
        return None

    url = checksums_asset["browser_download_url"]
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=30) as resp:
        content = resp.read().decode("utf-8")

    digest = find_asset_hash(content, asset_name)
    if digest is None:
        emit_progress("warning", f"{checksums_filename} does not contain entry for {asset_name}")
    return digest


def _reject_unsafe_member(target: Path, dest_real: Path, name: str) -> None:
    """Raise ValueError if an archive member would extract outside ``dest_real``.

    Shared by the zip and tar extractors so the path-traversal guard — a
    security-sensitive check — lives in exactly one place.
    """
    if not str(target).startswith(str(dest_real) + os.sep) and target != dest_real:
        raise ValueError(f"Path traversal attempt in archive: {name}")


def _safe_extract_zip(archive_path: Path, dest_dir: Path) -> None:
    """Extract a zip archive, rejecting entries with path-traversal components."""
    dest_real = dest_dir.resolve()
    with zipfile.ZipFile(archive_path, "r") as zf:
        for member in zf.namelist():
            _reject_unsafe_member((dest_dir / member).resolve(), dest_real, member)
        zf.extractall(dest_dir)


def _safe_extract_tar(archive_path: Path, dest_dir: Path) -> None:
    """Extract a tar archive, rejecting entries with path-traversal components or unsafe links."""
    dest_real = dest_dir.resolve()
    with tarfile.open(archive_path, "r:gz") as tf:
        for member in tf.getmembers():
            if member.issym() or member.islnk() or member.isdev() or member.isfifo():
                raise ValueError(
                    f"Unsafe archive member type ({member.type!r}) for: {member.name}"
                )
            _reject_unsafe_member((dest_dir / member.name).resolve(), dest_real, member.name)
        if hasattr(tarfile, "data_filter"):
            tf.extractall(dest_dir, filter="data")
        else:
            tf.extractall(dest_dir)


def extract_archive(archive_path: Path, dest_dir: Path) -> None:
    """Extract a tar.gz or zip archive to the destination directory."""
    emit_progress("extract_start", f"Extracting {archive_path.name}...")

    if archive_path.suffix == ".zip" or str(archive_path).endswith(".zip"):
        _safe_extract_zip(archive_path, dest_dir)
    elif str(archive_path).endswith(".tar.gz") or str(archive_path).endswith(".tgz"):
        _safe_extract_tar(archive_path, dest_dir)
    else:
        emit_progress("error", f"Unknown archive format: {archive_path.name}")
        sys.exit(1)

    emit_progress("extract_complete", "Extraction complete")


def make_executable(path: Path) -> None:
    """Set executable permissions on Unix systems."""
    if os.name != "nt":
        path.chmod(path.stat().st_mode | 0o755)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Download and verify Luma binaries from GitHub releases."
    )
    parser.add_argument(
        "--target-dir",
        required=True,
        help="Directory to install binaries into.",
    )
    parser.add_argument(
        "--binary",
        choices=["luma", "luma_lsp", "luma_dap"],
        default="luma_lsp",
        help="Which binary to download (default: luma_lsp).",
    )
    parser.add_argument(
        "--version",
        default="latest",
        help="Release version to download (default: latest).",
    )

    args = parser.parse_args()
    target_dir = Path(args.target_dir).resolve()
    target_dir.mkdir(parents=True, exist_ok=True)

    # Canonical constants (repo URLs, checksum manifest name) and platform map.
    constants = load_download_constants()

    # Detect platform
    platform_map = load_platform_map()
    os_name, arch = detect_platform()
    suffix = get_asset_suffix(platform_map, os_name, arch)
    asset_name = f"{args.binary}-{suffix}"

    emit_progress("platform_detected", f"{os_name}/{arch} → {asset_name}")

    # Fetch release
    release = fetch_release(args.version, constants["urls"])
    tag = release.get("tag_name", args.version)

    # Find asset
    asset = next((a for a in release["assets"] if a["name"] == asset_name), None)
    if not asset:
        available = [a["name"] for a in release["assets"]]
        emit_progress("error", f"Asset '{asset_name}' not found. Available: {available}")
        sys.exit(1)

    # Download
    archive_path = target_dir / asset_name
    download_file(asset["browser_download_url"], archive_path)

    # Verify checksum
    expected_hash = fetch_expected_checksum(release, asset_name, constants["checksums"]["filename"])
    if not expected_hash:
        emit_progress("error", f"Checksum verification failed: no hash available for '{asset_name}'")
        archive_path.unlink(missing_ok=True)
        sys.exit(2)
    if not verify_checksum(archive_path, expected_hash):
        archive_path.unlink(missing_ok=True)
        sys.exit(2)

    # Extract
    extract_archive(archive_path, target_dir)
    archive_path.unlink(missing_ok=True)

    # Make executable
    bin_ext = ".exe" if os_name == "windows" else ""
    binary_path = target_dir / f"{args.binary}{bin_ext}"
    if binary_path.exists():
        make_executable(binary_path)

    emit_progress(
        "complete",
        f"Binary installed: {binary_path}",
        binary_path=str(binary_path),
        version=tag,
    )


if __name__ == "__main__":
    main()
