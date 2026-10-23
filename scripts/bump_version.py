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


def render_vscode_package_json(new_version: str) -> str:
    """Return package.json with its 'version' field set to *new_version*."""
    data = json.loads(VSCODE_PACKAGE_JSON.read_text(encoding="utf-8"))
    data["version"] = new_version
    return json.dumps(data, indent=4, ensure_ascii=False) + "\n"


def render_first_version_field(path: Path, new_version: str) -> str:
    """Return *path* with its first top-level ``version = "..."`` set to *new_version*.

    Only the first match (the ``[package]`` version in a ``Cargo.toml``) is
    replaced, leaving dependency version fields untouched. Exits with an error
    when no such field exists.
    """
    content = path.read_text(encoding="utf-8")
    updated = re.sub(
        r'^(version\s*=\s*")([^"]+)(")',
        rf"\g<1>{new_version}\3",
        content,
        count=1,
        flags=re.MULTILINE,
    )
    if updated == content:
        sys.exit(f"Error: Could not find 'version = \"...\"' in {path.name}.")
    return updated


def render_zed_cargo_lock(new_version: str) -> str | None:
    """Return Cargo.lock with the ``luma-zed`` package version set to *new_version*.

    The lock file records the workspace package's own version, so a version
    bump must update it too — otherwise the committed lock drifts out of sync
    with Cargo.toml and the next ``cargo`` invocation (e.g. building or
    installing the dev extension in Zed) rewrites it, producing a spurious
    diff. Only the ``[[package]] name = "luma-zed"`` entry's version is
    touched; dependency versions are left untouched.

    Returns ``None`` when the file is absent or already at *new_version*.
    """
    if not ZED_CARGO_LOCK.exists():
        return None
    content = ZED_CARGO_LOCK.read_text(encoding="utf-8")
    # Anchor on the luma-zed name line so only its own version line changes.
    pattern = re.compile(r'(name = "luma-zed"\r?\nversion = ")([^"]+)(")')
    if pattern.search(content) is None:
        sys.exit("Error: Could not find the luma-zed package entry in Cargo.lock.")
    updated = pattern.sub(rf"\g<1>{new_version}\3", content, count=1)
    return None if updated == content else updated


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


def replace_version_token(text: str, old: str, new: str) -> str:
    """Replace every standalone occurrence of the version *old* with *new*.

    A match must not be glued to a preceding digit or dot, nor to a following
    digit, so ``0.5`` never rewrites ``10.5``, ``1.0.5``, or ``0.50``. A ``v``
    prefix (``v0.5.0``) is not a digit, so tagged forms are covered too.
    """
    pattern = re.compile(rf"(?<![\d.]){re.escape(old)}(?!\d)")
    return pattern.sub(new, text)


def render_doc_version_references(old_version: str, new_version: str) -> list[tuple[Path, str]]:
    """Return the documentation files whose version references change.

    Rewrites *old_version* to *new_version* in two forms: the full version
    (``0.5.0`` / ``v0.5.0``) and, when it changes, the short major.minor form
    (``0.5`` / ``v0.5``). Returns ``(path, new_content)`` pairs for every file
    that differs.
    """
    old_major, old_minor, _ = parse_semver(old_version)
    new_major, new_minor, _ = parse_semver(new_version)

    old_short = f"{old_major}.{old_minor}"
    new_short = f"{new_major}.{new_minor}"

    rendered: list[tuple[Path, str]] = []

    for filepath in DOC_VERSION_FILES:
        if not filepath.exists():
            continue
        content = filepath.read_text(encoding="utf-8")

        new_content = replace_version_token(content, old_version, new_version)
        if old_short != new_short:
            new_content = replace_version_token(new_content, old_short, new_short)

        if new_content != content:
            rendered.append((filepath, new_content))

    return rendered


def plan_updates(current: str, new_version: str) -> list[tuple[Path, str]]:
    """Compute every file rewrite needed to move from *current* to *new_version*.

    Nothing is written here: each source is read and validated first, so a
    missing version field exits before any file changes and the bump stays
    all-or-nothing.
    """
    updates: list[tuple[Path, str]] = [
        (VERSION_FILE, f"{new_version}\n"),
        (VSCODE_PACKAGE_JSON, render_vscode_package_json(new_version)),
        (ZED_EXTENSION_TOML, render_first_version_field(ZED_EXTENSION_TOML, new_version)),
        (ZED_CARGO_TOML, render_first_version_field(ZED_CARGO_TOML, new_version)),
    ]

    cargo_lock = render_zed_cargo_lock(new_version)
    if cargo_lock is not None:
        updates.append((ZED_CARGO_LOCK, cargo_lock))

    updates.extend(render_doc_version_references(current, new_version))
    return updates


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

    updates = plan_updates(current, new_version)

    # The repository normalises text to LF (.gitattributes), so write LF on every
    # platform rather than letting Windows translate newlines to CRLF.
    for path, content in updates:
        path.write_text(content, encoding="utf-8", newline="\n")

    print(f"Version bumped: {current} -> {new_version}")
    for path, _ in updates:
        print(f"  Updated: {path.relative_to(REPO_ROOT).as_posix()}")


if __name__ == "__main__":
    main()
