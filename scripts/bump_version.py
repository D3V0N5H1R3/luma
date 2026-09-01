#!/usr/bin/env python3
"""Bump the project version across all version-bearing files simultaneously.

Updates the version number in:
  - VERSION                                          (interpreter, language server, debugger)
  - extensions/vscode/package.json                   (VS Code extension)
  - extensions/zed/extension.toml                    (Zed extension manifest)
  - extensions/zed/Cargo.toml                        (Zed extension crate)
  - extensions/zed/Cargo.lock                        (Zed extension crate lock — luma-zed entry)

Also updates version references in documentation:
  - instructions/learnings.instructions.md           ("Alpha (X.Y)" status line)
  - CONTRIBUTING.md                                  (version bump examples)
  - SECURITY.md                                      (supported-versions table)
  - documents/Luma_Tutorial.md                       ("Luma X.Y.Z" / "Luma X.Y")
  - documents/Luma_Installation_Guide.md             (luma-language-X.Y.Z.vsix)
  - documents/Luma_Language_Server.md                ("version": "X.Y.Z")
  - extensions/vscode/DIRECTORY.md                   (luma-language-X.Y.Z.vsix)
  - extensions/zed/DIRECTORY.md                      (vX.Y.Z version tags)
  - extensions/shared/binary-download/SPECIFICATION.md (vX.Y.Z)
  - extensions/shared/config-schema.md               (vX.Y.Z)
  - extensions/shared/error-handling.md              (vX.Y.Z examples)

Usage:
    python scripts/bump_version.py 0.8.0          # set an explicit version
    python scripts/bump_version.py --major        # bump major (0.8.0 -> 1.0.0)
    python scripts/bump_version.py --minor        # bump minor (0.8.0 -> 0.9.0)
    python scripts/bump_version.py --patch        # bump patch (0.8.0 -> 0.8.1)
    python scripts/bump_version.py --current      # print current version and exit

Exit codes:
    0  Version updated successfully (or --current printed).
    1  Invalid arguments or version format.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

from _common import REPO_ROOT

VERSION_FILE: Path = REPO_ROOT / "VERSION"
VSCODE_PACKAGE_JSON: Path = REPO_ROOT / "extensions" / "vscode" / "package.json"
ZED_EXTENSION_TOML: Path = REPO_ROOT / "extensions" / "zed" / "extension.toml"
ZED_CARGO_TOML: Path = REPO_ROOT / "extensions" / "zed" / "Cargo.toml"
ZED_CARGO_LOCK: Path = REPO_ROOT / "extensions" / "zed" / "Cargo.lock"

SEMVER_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")


def read_current_version() -> str:
    """Read the current version from the VERSION file."""
    return VERSION_FILE.read_text(encoding="utf-8").strip()


def parse_semver(version: str) -> tuple[int, int, int]:
    """Parse a semver string into (major, minor, patch)."""
    match = SEMVER_RE.match(version)
    if not match:
        sys.exit(f"Error: '{version}' is not a valid semver (expected MAJOR.MINOR.PATCH).")
    return int(match.group(1)), int(match.group(2)), int(match.group(3))


def bump(version: str, part: str) -> str:
    """Bump the specified part of a semver string."""
    major, minor, patch = parse_semver(version)
    if part == "major":
        return f"{major + 1}.0.0"
    if part == "minor":
        return f"{major}.{minor + 1}.0"
    return f"{major}.{minor}.{patch + 1}"


def update_version_file(new_version: str) -> None:
    """Write the new version to the VERSION file."""
    VERSION_FILE.write_text(f"{new_version}\n", encoding="utf-8")


def update_vscode_package_json(new_version: str) -> None:
    """Update the 'version' field in the VS Code extension package.json."""
    content = VSCODE_PACKAGE_JSON.read_text(encoding="utf-8")
    data = json.loads(content)
    data["version"] = new_version
    VSCODE_PACKAGE_JSON.write_text(
        json.dumps(data, indent=4, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def update_zed_extension_toml(new_version: str) -> None:
    """Update the 'version' field in the Zed extension.toml."""
    content = ZED_EXTENSION_TOML.read_text(encoding="utf-8")
    updated = re.sub(
        r'^(version\s*=\s*")([^"]+)(")',
        rf"\g<1>{new_version}\3",
        content,
        count=1,
        flags=re.MULTILINE,
    )
    if updated == content:
        sys.exit("Error: Could not find 'version = \"...\"' in extension.toml.")
    ZED_EXTENSION_TOML.write_text(updated, encoding="utf-8")


def update_zed_cargo_toml(new_version: str) -> None:
    """Update the 'version' field in the Zed extension Cargo.toml.

    Only replaces the first ``version = "..."`` (the [package] version),
    leaving dependency version fields untouched.
    """
    content = ZED_CARGO_TOML.read_text(encoding="utf-8")
    updated = re.sub(
        r'^(version\s*=\s*")([^"]+)(")',
        rf"\g<1>{new_version}\3",
        content,
        count=1,
        flags=re.MULTILINE,
    )
    if updated == content:
        sys.exit("Error: Could not find 'version = \"...\"' in Cargo.toml.")
    ZED_CARGO_TOML.write_text(updated, encoding="utf-8")


def update_zed_cargo_lock(new_version: str) -> bool:
    """Update the ``luma-zed`` package version in the Zed extension Cargo.lock.

    The lock file records the workspace package's own version, so a version
    bump must update it too — otherwise the committed lock drifts out of sync
    with Cargo.toml and the next ``cargo`` invocation (e.g. building or
    installing the dev extension in Zed) rewrites it, producing a spurious
    diff. Only the ``[[package]] name = "luma-zed"`` entry's version is
    touched; dependency versions are left untouched.

    Returns True if the file was written, False if it is absent or already at
    *new_version*.
    """
    if not ZED_CARGO_LOCK.exists():
        return False
    content = ZED_CARGO_LOCK.read_text(encoding="utf-8")
    # Anchor on the luma-zed name line so only its own version line changes.
    pattern = re.compile(r'(name = "luma-zed"\r?\nversion = ")([^"]+)(")')
    if pattern.search(content) is None:
        sys.exit("Error: Could not find the luma-zed package entry in Cargo.lock.")
    updated = pattern.sub(rf"\g<1>{new_version}\3", content, count=1)
    if updated == content:
        return False
    ZED_CARGO_LOCK.write_text(updated, encoding="utf-8")
    return True


DOC_VERSION_FILES: list[Path] = [
    REPO_ROOT / "instructions" / "learnings.instructions.md",
    REPO_ROOT / "CONTRIBUTING.md",
    REPO_ROOT / "SECURITY.md",
    REPO_ROOT / "documents" / "Luma_Tutorial.md",
    REPO_ROOT / "documents" / "Luma_Installation_Guide.md",
    REPO_ROOT / "documents" / "Luma_Language_Server.md",
    REPO_ROOT / "extensions" / "vscode" / "DIRECTORY.md",
    REPO_ROOT / "extensions" / "zed" / "DIRECTORY.md",
    REPO_ROOT / "extensions" / "shared" / "binary-download" / "SPECIFICATION.md",
    REPO_ROOT / "extensions" / "shared" / "config-schema.md",
    REPO_ROOT / "extensions" / "shared" / "error-handling.md",
]


def update_doc_version_references(old_version: str, new_version: str) -> list[str]:
    """Replace version references in documentation files.

    Performs plain-text replacement of *old_version* with *new_version* in
    three forms: bare (``0.5.0``), v-prefixed (``v0.5.0``), and short
    major.minor (``0.5`` / ``v0.5``).

    Returns a list of repo-relative paths that were modified.
    """
    old_major, old_minor, _ = parse_semver(old_version)
    new_major, new_minor, _ = parse_semver(new_version)

    old_short = f"{old_major}.{old_minor}"
    new_short = f"{new_major}.{new_minor}"

    updated_files: list[str] = []

    for filepath in DOC_VERSION_FILES:
        if not filepath.exists():
            continue
        content = filepath.read_text(encoding="utf-8")
        new_content = content

        # Replace v-prefixed full version first (to avoid partial matches).
        new_content = new_content.replace(f"v{old_version}", f"v{new_version}")
        # Replace bare full version.
        new_content = new_content.replace(old_version, new_version)
        # Replace short major.minor forms.
        if old_short != new_short:
            new_content = new_content.replace(f"v{old_short}", f"v{new_short}")
            new_content = new_content.replace(old_short, new_short)

        if new_content != content:
            filepath.write_text(new_content, encoding="utf-8")
            updated_files.append(str(filepath.relative_to(REPO_ROOT)))

    return updated_files


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(
            "Usage: python scripts/bump_version.py"
            " {VERSION | --major | --minor | --patch | --current}"
        )

    arg = sys.argv[1]
    current = read_current_version()

    if arg == "--current":
        print(current)
        return

    if arg in ("--major", "--minor", "--patch"):
        new_version = bump(current, arg.lstrip("-"))
    else:
        # Treat the argument as an explicit version string.
        parse_semver(arg)  # validates format
        new_version = arg

    if new_version == current:
        print(f"Version is already {current} — nothing to do.")
        return

    update_version_file(new_version)
    update_vscode_package_json(new_version)
    update_zed_extension_toml(new_version)
    update_zed_cargo_toml(new_version)
    lock_updated = update_zed_cargo_lock(new_version)
    doc_updates = update_doc_version_references(current, new_version)

    print(f"Version bumped: {current} -> {new_version}")
    print("  Updated: VERSION")
    print("  Updated: extensions/vscode/package.json")
    print("  Updated: extensions/zed/extension.toml")
    print("  Updated: extensions/zed/Cargo.toml")
    if lock_updated:
        print("  Updated: extensions/zed/Cargo.lock")
    for doc_path in doc_updates:
        print(f"  Updated: {doc_path}")


if __name__ == "__main__":
    main()
