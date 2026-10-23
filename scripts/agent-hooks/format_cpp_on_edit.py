#!/usr/bin/env python3
"""PostToolUse agent hook: clang-format C++ files the agent just edited.

The hook reads the lifecycle payload as JSON on stdin, pulls the edited file
path(s) out of the tool arguments — including the headers of a patch-format
(``apply_patch``) edit — and, when a target is a first-party C++ translation
unit or header that exists on disk, reformats it in place with ``clang-format``
(18+). This mirrors the project's ``scripts/git-hooks/pre-commit`` gate and the
CI *Formatting* job, so edits made by the agent land already matching
``.clang-format`` instead of failing the check later.

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
import re
import shutil
import subprocess
import sys
from pathlib import Path

from hook_common import (
    candidate_paths,
    get_tool_input,
    is_shadowed_registration,
    read_payload,
)

# The project requires clang-format 18+ to match CI and the pre-commit hook.
MIN_CLANG_FORMAT_MAJOR = 18

# Only these suffixes are formatted — the exact set the pre-commit hook and the
# CI Formatting job cover (the first-party C++ tree uses .cpp/.hpp throughout).
CPP_SUFFIXES = frozenset({".cpp", ".hpp"})

# Subprocess budgets, in seconds, kept inside the 30 s hook timeout so a slow
# clang-format is abandoned by the script rather than killed by the runtime.
VERSION_TIMEOUT = 5
FORMAT_TIMEOUT = 20


def clang_format_is_usable(exe: str) -> bool:
    """True when ``clang-format`` runs and reports version 18 or newer."""
    try:
        result = subprocess.run(
            [exe, "--version"],
            capture_output=True,
            text=True,
            timeout=VERSION_TIMEOUT,
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


def format_file(exe: str, path: Path) -> bool:
    """Run ``clang-format -i`` on ``path``; True only when the content changed."""
    try:
        before = path.read_bytes()
        subprocess.run(
            [exe, "-i", str(path)],
            capture_output=True,
            text=True,
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
        "clang-format reformatted " + ", ".join(paths) + " after this edit to match "
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
    args = sys.argv[1:] if argv is None else argv
    payload = read_payload()
    if is_shadowed_registration(payload, args):
        return 0  # The .github/hooks copy of this formatter handles this call.

    base = payload.get("cwd")
    base = base if isinstance(base, str) else None

    # Cheap filter first: only probe for clang-format when the agent actually
    # edited a C++ file that exists on disk — a non-C++ edit must not pay for
    # spawning ``clang-format --version``.
    targets: list[Path] = []
    for raw_path in candidate_paths(get_tool_input(payload)):
        path = resolve(base, raw_path)
        if path.suffix.lower() in CPP_SUFFIXES and path.is_file() and path not in targets:
            targets.append(path)
    if not targets:
        return 0

    exe = shutil.which("clang-format")
    if exe is None or not clang_format_is_usable(exe):
        return 0  # No usable formatter — nothing to do.

    changed = [str(path) for path in targets if format_file(exe, path)]
    if changed:
        report_reformatted(changed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
