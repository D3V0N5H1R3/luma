"""Shared payload parsing for the agent hooks in this directory.

Both hook scripts receive the same lifecycle payload on stdin, but the shape
varies by runtime and by how the hook is registered:

* **PascalCase events** (``PreToolUse``) — used by ``.github/hooks/*.json`` and
  ``.claude/settings.json`` — deliver snake_case fields (``tool_name``,
  ``tool_input``) and report Claude tool names (``Write``, ``Edit``).
* **camelCase events** (``preToolUse``) deliver ``toolName`` and ``toolArgs``,
  where ``toolArgs`` may arrive as a JSON-encoded string.
* **Patch tools** (``apply_patch``, reported as ``Edit``) carry their target
  paths inside the patch text instead of a ``path`` field.

This module normalises all of these so neither script silently skips a payload
shape it did not anticipate. It imports only the standard library: the hooks
must stay dependency-free so they run before any project tooling is set up.
"""

from __future__ import annotations

import json
import os
import re
import sys
from collections.abc import Mapping, Sequence

# tool_input keys that may hold a target file path, across differing tool
# schemas (Claude-canonical ``file_path``, the Copilot runtime's ``path``,
# camelCase, and notebook variants).
PATH_KEYS = ("file_path", "path", "filePath", "notebook_path", "notebookPath")

# Passed by the ``.claude/settings.json`` registrations so a script can tell it
# was launched from that file rather than from ``.github/hooks/``.
CLAUDE_SETTINGS_FLAG = "--claude-settings"

# A patch-format edit (``apply_patch``) opens with this marker line.
PATCH_BEGIN = "*** Begin Patch"

# Every patch header that names a file the patch creates, modifies, moves to,
# or deletes.
_PATCH_PATH_LINE = re.compile(
    r"^\*\*\* (?:Add File|Update File|Delete File|Move to): (.+?)\s*$",
    re.MULTILINE,
)


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


def get_tool_name(payload: Mapping[str, object]) -> str | None:
    """Return the tool name from either payload format, or ``None``."""
    for key in ("tool_name", "toolName"):
        value = payload.get(key)
        if isinstance(value, str) and value:
            return value
    return None


def get_tool_input(payload: Mapping[str, object]) -> object:
    """Return the tool arguments from either payload format.

    A JSON-encoded string is decoded when it holds an object; any other string
    (for example a raw patch) is returned unchanged.
    """
    value = payload.get("tool_input")
    if value is None:
        value = payload.get("toolArgs")
    if isinstance(value, str):
        try:
            decoded = json.loads(value)
        except (ValueError, TypeError):
            return value
        if isinstance(decoded, dict):
            return decoded
    return value


def patch_paths(text: str) -> list[str]:
    """Return every file path named by the headers of a patch-format edit."""
    if not text.lstrip().startswith(PATCH_BEGIN):
        return []
    return [match.group(1) for match in _PATCH_PATH_LINE.finditer(text)]


def candidate_paths(tool_input: object) -> list[str]:
    """Collect every target path a tool call carries.

    Covers single-path tools (``path``/``file_path``), multi-edit tools whose
    ``edits`` list holds per-edit paths, and patch tools whose paths live inside
    the patch text — passed either as the whole input or as a string field.
    """
    if isinstance(tool_input, str):
        return patch_paths(tool_input)
    if not isinstance(tool_input, Mapping):
        return []

    paths: list[str] = []

    def collect(container: object) -> None:
        if not isinstance(container, Mapping):
            return
        for key in PATH_KEYS:
            value = container.get(key)
            if isinstance(value, str) and value:
                paths.append(value)

    collect(tool_input)
    edits = tool_input.get("edits")
    if isinstance(edits, list):
        for edit in edits:
            collect(edit)
    for value in tool_input.values():
        if isinstance(value, str):
            paths.extend(patch_paths(value))
    return paths


def is_shadowed_registration(
    payload: Mapping[str, object],
    argv: Sequence[str],
    environ: Mapping[str, str] | None = None,
) -> bool:
    """True when this run is a duplicate that another registration already covers.

    The Copilot runtime reads ``.claude/settings.json`` in addition to
    ``.github/hooks/*.json``, so without this check every edit would run each
    hook twice. The ``.claude/settings.json`` copy therefore stands down when it
    is clearly running under Copilot: the runtime sets ``COPILOT_PROJECT_DIR``
    and, unlike Claude Code, sends no ``transcript_path`` with tool events. Under
    Claude Code — which never reads ``.github/hooks/`` — it always runs.
    """
    env = os.environ if environ is None else environ
    return (
        CLAUDE_SETTINGS_FLAG in argv
        and bool(env.get("COPILOT_PROJECT_DIR"))
        and "transcript_path" not in payload
    )
