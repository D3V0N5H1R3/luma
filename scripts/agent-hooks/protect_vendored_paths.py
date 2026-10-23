#!/usr/bin/env python3
"""PreToolUse agent hook: block edits to vendored code under ``external/``.

The project's boundary — stated across the agent guides and enforced in review
— is: **never modify vendored, third-party code in ``external/``**. This hook
turns that guideline into a deterministic gate: a file-mutating tool (edit /
write / create / patch) that targets ``external/`` is denied *before* it runs.
Patch-format edits (``apply_patch``) are covered too: their target paths are
read from the patch headers.

Design: the hook is deliberately **fail-open**. It denies only when it is
confident the operation is a file write to a protected path. Reads, searches,
shell commands, and every other tool — plus any payload it cannot confidently
interpret — are allowed. The worst-case failure mode is a silent no-op, never a
wrongful block. Shell-based mutation (for example ``rm external/...``) is out of
scope by design: detecting it reliably would require shell parsing and would
risk blocking legitimate reads such as ``cat``/``grep`` over ``external/``.

Invoked by ``.github/hooks/protect-vendored-paths.json`` and, for Claude Code,
by ``.claude/settings.json``. See ``.github/hooks/DIRECTORY.md`` for the
rationale and how to test it.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from hook_common import (
    candidate_paths,
    get_tool_input,
    get_tool_name,
    is_shadowed_registration,
    read_payload,
)

# The protected top-level directory.
PROTECTED_TOP = "external"

# Substrings that identify a file-mutating tool name: Claude names (``Write``,
# ``Edit``, ``MultiEdit``, ``NotebookEdit``) and runtime names (``create``,
# ``edit``, ``apply_patch``, ``str_replace_editor``).
WRITE_VERBS = ("edit", "write", "create", "patch")


def is_write_tool(tool_name: object) -> bool:
    """True for file-mutating tools (Edit / Write / Create / Patch variants).

    Matching on the verb keeps read-only tools (view, grep, search, …) — which
    may legitimately target ``external/`` — from ever being blocked.
    """
    if not isinstance(tool_name, str):
        return False
    lowered = tool_name.lower()
    return any(verb in lowered for verb in WRITE_VERBS)


def find_repo_root(start: Path) -> Path:
    """Walk up from ``start`` to the nearest ancestor containing a ``.git`` entry."""
    for directory in (start, *start.parents):
        if (directory / ".git").exists():
            return directory
    return start


def is_protected(raw_path: str, base: Path) -> bool:
    """True when ``raw_path`` resolves under ``external/``."""
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
    return bool(parts) and parts[0].casefold() == PROTECTED_TOP


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


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else argv
    payload = read_payload()
    if is_shadowed_registration(payload, args):
        return 0  # The .github/hooks copy of this guard handles this call.
    if not is_write_tool(get_tool_name(payload)):
        return 0  # Not a file-mutating tool — allow.

    cwd = payload.get("cwd")
    base = Path(cwd) if isinstance(cwd, str) and cwd else Path.cwd()
    try:
        base = base.resolve()
    except OSError:
        base = Path.cwd()

    for raw_path in candidate_paths(get_tool_input(payload)):
        if is_protected(raw_path, base):
            return deny(
                f"Editing vendored code under '{PROTECTED_TOP}/' is blocked by the "
                f"protect-vendored-paths hook (target: {raw_path}). "
                "If this change is genuinely required, make it deliberately and "
                "outside the agent."
            )
    return 0


if __name__ == "__main__":
    sys.exit(main())
