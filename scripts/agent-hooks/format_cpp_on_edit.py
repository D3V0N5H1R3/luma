#!/usr/bin/env python3
"""PostToolUse agent hook: clang-format C++ files the agent just edited.

The hook reads the lifecycle payload as JSON on stdin, pulls the edited file
path out of ``tool_input``, and — when the target is a first-party C++
translation unit or header that exists on disk — reformats it in place with
``clang-format`` (18+). This mirrors the project's ``scripts/git-hooks/pre-commit``
gate and the CI *Formatting* job, so edits made by the agent land already
matching ``.clang-format`` instead of failing the check later.

Design: the hook is deliberately **fail-open and non-blocking**. A missing or
too-old ``clang-format``, an unrecognised payload, or any error yields a clean
no-op (exit 0, no decision) so the agent is never impeded. It only ever
formats; it never blocks a tool call.

Invoked by ``.github/hooks/format-cpp-on-edit.json``. See
``.github/hooks/README.md`` for the rationale and how to test it.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

# The project requires clang-format 18+ to match CI and the pre-commit hook.
MIN_CLANG_FORMAT_MAJOR = 18

# Only these suffixes are formatted — the exact set the pre-commit hook and the
# CI Formatting job cover (the first-party C++ tree uses .cpp/.hpp throughout).
CPP_SUFFIXES = frozenset({".cpp", ".hpp"})

# tool_input keys that may hold an edited file path, across differing tool
# schemas (Claude-canonical ``file_path``, this CLI's ``path``, camelCase, and
# notebook variants).
PATH_KEYS = ("file_path", "path", "filePath", "notebook_path", "notebookPath")


def read_payload() -> dict:
    """Parse the hook JSON payload from stdin; return an empty dict on any problem."""
    try:
        raw = sys.stdin.read()
    except (OSError, ValueError):
        return {}
    if not raw.strip():
        return {}
    try:
        data = json.loads(raw)
    except (ValueError, TypeError):
        return {}
    return data if isinstance(data, dict) else {}


def candidate_paths(tool_input: object) -> list[str]:
    """Collect path-like strings from a ``tool_input`` object.

    Handles both the single-path tools (``file_path``/``path``) and multi-edit
    tools that carry a list of ``edits`` each with its own path.
    """
    paths: list[str] = []

    def collect(container: object) -> None:
        if not isinstance(container, dict):
            return
        for key in PATH_KEYS:
            value = container.get(key)
            if isinstance(value, str) and value:
                paths.append(value)

    collect(tool_input)
    if isinstance(tool_input, dict):
        edits = tool_input.get("edits")
        if isinstance(edits, list):
            for edit in edits:
                collect(edit)
    return paths


def clang_format_is_usable(exe: str) -> bool:
    """True when ``clang-format`` runs and reports version 18 or newer."""
    try:
        result = subprocess.run(
            [exe, "--version"],
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    match = re.search(r"(\d+)", result.stdout)
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


def main() -> int:
    payload = read_payload()

    base = payload.get("cwd")
    base = base if isinstance(base, str) else None

    # Cheap filter first: this hook has no tool matcher, so it runs after every
    # tool call. Only probe for clang-format when the agent actually edited a
    # C++ file that exists on disk — otherwise a non-C++ edit (or a read) must
    # not pay for spawning ``clang-format --version``.
    targets: list[Path] = []
    for raw_path in candidate_paths(payload.get("tool_input")):
        path = resolve(base, raw_path)
        if path.suffix.lower() in CPP_SUFFIXES and path.is_file():
            targets.append(path)
    if not targets:
        return 0

    exe = shutil.which("clang-format")
    if exe is None or not clang_format_is_usable(exe):
        return 0  # No usable formatter — nothing to do.

    formatted: list[str] = []
    for path in targets:
        try:
            subprocess.run(
                [exe, "-i", str(path)],
                capture_output=True,
                text=True,
                timeout=30,
                check=True,
            )
        except (OSError, subprocess.SubprocessError):
            continue  # Fail-open: never impede the agent over a format hiccup.
        formatted.append(str(path))

    if formatted:
        # Non-blocking transparency note; stderr is surfaced without blocking.
        print("clang-format applied to: " + ", ".join(formatted), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
