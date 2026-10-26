#!/usr/bin/env python3
"""PostToolUse agent hook: clang-format C++ files the agent just edited.

The hook reads the lifecycle payload as JSON on stdin, pulls the edited file
path(s) out of the tool arguments — including the headers of a patch-format
(``apply_patch``) edit — and, when a target is a first-party C++ translation
unit or header (inside the repository, outside ``external/``) that exists on
disk, reformats it in place with ``clang-format`` (20+). This mirrors the
project's ``scripts/git-hooks/pre-commit`` gate and the CI *Formatting* job, so
edits made by the agent land already matching ``.clang-format`` instead of
failing the check later. An editor tool's read-only ``view`` command is ignored.

When formatting actually changes a file, the hook returns ``additionalContext``
telling the model the file was rewritten, so its next edit is based on the
formatted text rather than a stale copy.

Design: the hook is deliberately **fail-open and non-blocking**. A missing or
too-old ``clang-format``, an unrecognised payload, or any error yields a clean
no-op (exit 0, no output) so the agent is never impeded. It only ever formats;
it never blocks a tool call.

Invoked by ``.github/hooks/format-cpp-on-edit.json`` and, for Claude Code, by
``.claude/settings.json``. See ``.github/hooks/DIRECTORY.md`` for the rationale
and how to test it.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from hook_common import (
    VENDORED_DIR,
    candidate_paths,
    find_repo_root,
    get_tool_input,
    is_read_only_call,
    is_shadowed_registration,
    read_payload,
)

# The project requires clang-format 20+ (the minimum that parses .clang-format)
# to match CI and the pre-commit hook.
MIN_CLANG_FORMAT_MAJOR = 20

# Only these suffixes are formatted — the exact set the pre-commit hook and the
# CI Formatting job cover (the first-party C++ tree uses .cpp/.hpp throughout).
CPP_SUFFIXES = frozenset({".cpp", ".hpp"})

# Subprocess budgets, in seconds, kept inside the 30 s hook timeout so a slow
# clang-format is abandoned by the script rather than killed by the runtime.
VERSION_TIMEOUT = 5
FORMAT_TIMEOUT = 20


def find_visual_studio_clang_format() -> str | None:
    """Return Visual Studio's bundled ``clang-format``, or ``None``.

    Visual Studio's "C++ Clang tools for Windows" component installs it under
    ``VC\\Tools\\Llvm\\<arch>\\bin`` but puts it on ``PATH`` only inside a
    Developer prompt. Mirrors ``find_visual_studio_llvm_tool`` in
    ``scripts/_common.py``; the hooks import only the standard library.
    """
    if sys.platform != "win32":
        return None
    program_files = os.environ.get("PROGRAMFILES(X86)")
    if not program_files:
        return None
    vswhere = Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return None
    # An ARM64 binary does not start on an x64 host, so pick by architecture.
    arch = "ARM64" if os.environ.get("PROCESSOR_ARCHITECTURE", "").upper() == "ARM64" else "x64"
    try:
        result = subprocess.run(
            [
                str(vswhere),
                "-latest",
                "-prerelease",
                "-products",
                "*",
                "-utf8",
                "-find",
                rf"VC\Tools\Llvm\{arch}\bin\clang-format.exe",
            ],
            capture_output=True,
            encoding="utf-8",
            timeout=VERSION_TIMEOUT,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    for line in result.stdout.splitlines():
        candidate = line.strip()
        if candidate and Path(candidate).is_file():
            return candidate
    return None


def find_clang_format() -> str | None:
    """Return the ``clang-format`` on ``PATH``, else Visual Studio's bundled copy."""
    return shutil.which("clang-format") or find_visual_studio_clang_format()


def clang_format_is_usable(executable: str) -> bool:
    """Return True when ``clang-format`` runs and reports version 20 or newer."""
    try:
        result = subprocess.run(
            [executable, "--version"],
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            timeout=VERSION_TIMEOUT,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return False

    # Take the number after "version" (as the pre-commit hook does) so a vendor
    # prefix with digits in the banner cannot be mistaken for the major version.
    match = re.search(r"version (\d+)", result.stdout)
    return match is not None and int(match.group(1)) >= MIN_CLANG_FORMAT_MAJOR


def resolve(base: str | None, raw_path: str) -> Path:
    """Resolve ``raw_path`` against the hook ``cwd`` (or the process cwd)."""
    candidate = Path(raw_path)
    if not candidate.is_absolute() and base:
        candidate = Path(base) / candidate

    try:
        return candidate.resolve()
    except OSError:
        return candidate


def is_first_party(path: Path, repo_root: Path) -> bool:
    """Return True when ``path`` lies inside the repository but outside ``external/``.

    Files elsewhere on disk are left alone: without the project's ``.clang-format``
    above them, clang-format would rewrite them in its fallback style.
    """
    try:
        relative_path = path.relative_to(repo_root)
    except ValueError:
        return False

    parts = relative_path.parts
    return bool(parts) and parts[0].casefold() != VENDORED_DIR


def format_file(executable: str, path: Path) -> bool:
    """Run ``clang-format -i`` on ``path``; return True only when the content changed."""
    try:
        before = path.read_bytes()
        # Output is discarded, so keep it as bytes: decoding clang-format's
        # diagnostics with the locale codec could raise and break fail-open.
        subprocess.run(
            [executable, "-i", str(path)],
            capture_output=True,
            timeout=FORMAT_TIMEOUT,
            check=True,
        )
        return path.read_bytes() != before
    except (OSError, subprocess.SubprocessError):
        return False  # Fail-open: never impede the agent over a format hiccup.


def report_reformatted(paths: list[str]) -> None:
    """Tell the model which files were rewritten so it does not edit stale text.

    ``additionalContext`` is the Copilot field; ``hookSpecificOutput`` carries the
    same message for Claude Code. Each runtime ignores the other's field.
    """
    message = (
        f"clang-format reformatted {', '.join(paths)} after this edit to match "
        ".clang-format. Re-read the file before editing it again."
    )
    output = {
        "additionalContext": message,
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": message,
        },
    }
    print(json.dumps(output))


def main(argv: list[str] | None = None) -> int:
    """Format the C++ files named by the stdin payload; always exit 0."""
    args = sys.argv[1:] if argv is None else argv
    payload = read_payload()
    if is_shadowed_registration(payload, args):
        return 0  # The .github/hooks copy of this formatter handles this call.

    tool_input = get_tool_input(payload)
    if is_read_only_call(tool_input):
        return 0  # An editor tool's ``view`` command changed nothing.

    cwd = payload.get("cwd")
    base = cwd if isinstance(cwd, str) else None
    repo_root = find_repo_root(resolve(None, base or "."))

    # Cheap filter first: only probe for clang-format when the agent actually
    # edited a first-party C++ file that exists on disk — a non-C++ edit must
    # not pay for spawning ``clang-format --version``.
    targets: list[Path] = []
    for raw_path in candidate_paths(tool_input):
        path = resolve(base, raw_path)
        if (
            path.suffix.lower() in CPP_SUFFIXES
            and path.is_file()
            and is_first_party(path, repo_root)
            and path not in targets
        ):
            targets.append(path)

    if not targets:
        return 0

    executable = find_clang_format()
    if executable is None or not clang_format_is_usable(executable):
        return 0  # No usable formatter — nothing to do.

    changed = [str(path) for path in targets if format_file(executable, path)]
    if changed:
        report_reformatted(changed)

    return 0


if __name__ == "__main__":
    sys.exit(main())
