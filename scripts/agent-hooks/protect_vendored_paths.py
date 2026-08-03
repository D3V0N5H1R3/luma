#!/usr/bin/env python3
"""PreToolUse agent hook: block edits to vendored code under ``external/``.

The project's boundary — stated across the agent guides and enforced in review
— is: **never modify vendored, third-party code in ``external/``**, with the
single exception of the first-party GraphicalUi front-end in
``external/gui-framework/``. This hook turns that guideline into a
deterministic gate: a file-mutating tool (edit / write / create) that targets
``external/`` (outside ``gui-framework/``) is denied *before* it runs.

Design: the hook is deliberately **fail-open**. It denies only when it is
confident the operation is a file write to a protected path. Reads, searches,
shell commands, and every other tool — plus any payload it cannot confidently
interpret — are allowed. The worst-case failure mode is a silent no-op, never a
wrongful block. Shell-based mutation (for example ``rm external/...``) is out of
scope by design: detecting it reliably would require shell parsing and would
risk blocking legitimate reads such as ``cat``/``grep`` over ``external/``.

Invoked by ``.github/hooks/protect-vendored-paths.json``. See
``.github/hooks/DIRECTORY.md`` for the rationale and how to test it.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

# tool_input keys that may hold a target file path, across differing tool
# schemas (Claude-canonical ``file_path``, this CLI's ``path``, camelCase, and
# notebook variants).
PATH_KEYS = ("file_path", "path", "filePath", "notebook_path", "notebookPath")

# The protected top-level directory and the one first-party exception within it.
PROTECTED_TOP = "external"
EXCEPTION_SUBDIR = "gui-framework"


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


def is_write_tool(tool_name: object) -> bool:
    """True for file-mutating tools (Edit / Write / Create / MultiEdit / NotebookEdit).

    Matching on the verb keeps read-only tools (view, grep, search, …) — which
    may legitimately target ``external/`` — from ever being blocked.
    """
    if not isinstance(tool_name, str):
        return False
    lowered = tool_name.lower()
    return any(verb in lowered for verb in ("edit", "write", "create"))


def candidate_paths(tool_input: object) -> list[str]:
    """Collect path-like strings from a ``tool_input`` object."""
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


def find_repo_root(start: Path) -> Path:
    """Walk up from ``start`` to the nearest ancestor containing a ``.git`` entry."""
    for directory in (start, *start.parents):
        if (directory / ".git").exists():
            return directory
    return start


def is_protected(raw_path: str, base: Path) -> bool:
    """True when ``raw_path`` resolves under ``external/`` but not ``external/gui-framework/``."""
    candidate = Path(raw_path)
    if not candidate.is_absolute():
        candidate = base / candidate
    try:
        candidate = candidate.resolve()
    except OSError:
        return False
    root = find_repo_root(base)
    try:
        rel = candidate.relative_to(root)
    except ValueError:
        return False  # Outside the repository — not this hook's concern.
    parts = rel.parts
    if not parts or parts[0].casefold() != PROTECTED_TOP:
        return False
    # external/gui-framework/... is first-party and editable; everything
    # else under external/ is vendored and protected. Case-fold the segments
    # so the guard does not depend on Path.resolve() having canonicalised the
    # on-disk case (which it cannot do when external/ is an unpopulated
    # submodule dir) to stay correct on case-insensitive filesystems.
    return not (len(parts) >= 2 and parts[1].casefold() == EXCEPTION_SUBDIR)


def deny(reason: str) -> int:
    """Emit a PreToolUse deny decision on stdout.

    The decision is written in several compatible shapes so it takes effect
    across the different agent runtimes this repo targets: the current
    ``permissionDecision: "deny"`` form (top-level and nested under
    ``hookSpecificOutput``) and the older top-level ``decision: "block"`` form.
    All carry the same human-readable reason.
    """
    decision = {
        "decision": "block",
        "reason": reason,
        "permissionDecision": "deny",
        "permissionDecisionReason": reason,
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        },
    }
    print(json.dumps(decision))
    return 0


def main() -> int:
    payload = read_payload()
    if not is_write_tool(payload.get("tool_name")):
        return 0  # Not a file-mutating tool — allow.

    cwd = payload.get("cwd")
    base = Path(cwd) if isinstance(cwd, str) and cwd else Path.cwd()
    try:
        base = base.resolve()
    except OSError:
        base = Path.cwd()

    for raw_path in candidate_paths(payload.get("tool_input")):
        if is_protected(raw_path, base):
            return deny(
                f"Editing vendored code under '{PROTECTED_TOP}/' is blocked by the "
                f"protect-vendored-paths hook (target: {raw_path}). Only "
                f"'{PROTECTED_TOP}/{EXCEPTION_SUBDIR}/' is first-party and editable. "
                "If this change is genuinely required, make it deliberately and "
                "outside the agent."
            )
    return 0


if __name__ == "__main__":
    sys.exit(main())
