#!/usr/bin/env python3
"""Shared helpers for the Luma editor-extension code generators.

Centralises the scaffolding that was previously duplicated across
generate-config.py, generate-config-code.py, generate-platform-code.py and
generate-keybindings.py:

  * repository-root resolution and JSON loading,
  * deterministic file writing (LF newlines, parent-dir creation),
  * in-place editing of vscode/package.json (round-trips byte-for-byte),
  * literal/case emitters for Rust and TypeScript,
  * a uniform ``--vscode/--zed`` argument parser.

All generators import from this module so that conventions stay in one place.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from collections.abc import Callable, Iterable, Iterator
from pathlib import Path
from types import ModuleType
from typing import Any, TypeVar

SCRIPT_DIR = Path(__file__).resolve().parent

_Selection = TypeVar("_Selection")


def resolve_root() -> Path:
    """Return the extensions/ root, validating that the expected layout exists.

    Raised as a ``RuntimeError`` rather than ``assert`` so the check still runs
    under ``python -O`` (which strips assertions).
    """
    root = SCRIPT_DIR.parent
    if not all((root / editor).exists() for editor in EDITORS):
        raise RuntimeError(
            f"Expected extensions/ structure with {', '.join(f'{e}/' for e in EDITORS)} "
            f"subdirs at {root}"
        )
    return root


def load_json(path: Path) -> dict:
    """Load a JSON document from ``path``."""
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def write_file(path: Path, content: str) -> None:
    """Write ``content`` to ``path`` with LF newlines, creating parent dirs."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    print(f"  Generated: {path}")


def update_package_json(root: Path, mutate: Callable[[dict], None]) -> None:
    """Apply ``mutate`` to vscode/package.json and write it back.

    The file is parsed, mutated in place by ``mutate`` and re-serialised with
    4-space indentation. Because the manifest already uses that exact format,
    blocks the generator does not touch round-trip byte-for-byte, keeping diffs
    limited to the regenerated sections.
    """
    path = root / "vscode" / "package.json"
    data = load_json(path)
    mutate(data)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(data, indent=4, ensure_ascii=False) + "\n")
    print(f"  Updated: {path}")


# ── Generated-file headers ───────────────────────────────────────────────────

_DO_NOT_EDIT = "Do not edit manually."


def banner(
    comment_open: str,
    source_json: str,
    script: str,
    editor: str,
    *,
    notice: str = _DO_NOT_EDIT,
    comment_close: str = "",
) -> list[str]:
    """Return the two-line ``AUTO-GENERATED`` header shared by every generator.

    ``comment_open`` is the language's comment marker (``//``, ``--``); pass
    ``comment_close`` too for wrapping comments (``<!--`` / ``-->``). ``notice``
    defaults to the canonical wording; the ``generate-config-code`` Rust output
    overrides it to preserve its historical ``DO NOT EDIT.`` phrasing.
    """
    close = f" {comment_close}" if comment_close else ""
    return [
        f"{comment_open} AUTO-GENERATED from extensions/shared/{source_json}{close}",
        f"{comment_open} {notice} Run: python {script} --{editor}{close}",
    ]


# ── Case helpers ─────────────────────────────────────────────────────────────


def camel_to_snake(name: str) -> str:
    """Convert a camelCase token to snake_case (``inlayHints`` -> ``inlay_hints``)."""
    out: list[str] = []
    for ch in name:
        if ch.isupper() and out:
            out.append("_")
        out.append(ch.lower())
    return "".join(out)


def dotted_to_screaming_snake(key: str) -> str:
    """Convert a dotted/camel key to SCREAMING_SNAKE (``lsp.autoUpdate`` -> ``LSP_AUTO_UPDATE``)."""
    flattened = key.replace(".", "_")
    out: list[str] = []
    for i, ch in enumerate(flattened):
        if ch.isupper() and i > 0 and flattened[i - 1] != "_":
            out.append("_")
        out.append(ch.upper())
    return "".join(out)


# ── Literal emitters ─────────────────────────────────────────────────────────


RUST_TYPES = {"boolean": "bool", "string": "&str", "integer": "i64", "number": "f64"}


def rust_type(type_name: str) -> str:
    """Map a defaults.json type name to a Rust type."""
    return RUST_TYPES.get(type_name, "&str")


def rust_literal(value: Any, type_name: str) -> str:
    """Render a default value as a Rust literal for the given type."""
    if type_name == "boolean":
        return "true" if value else "false"
    if type_name in ("integer", "number"):
        return str(value)
    return f'"{value}"'


def ts_literal(value: Any) -> str:
    """Render a Python value as a TypeScript literal."""
    return json.dumps(value)


TS_TYPES = {"boolean": "boolean", "string": "string", "integer": "number", "number": "number"}


def ts_type(type_name: str) -> str:
    """Map a defaults.json type name to a TypeScript type."""
    return TS_TYPES.get(type_name, "string")


def snake_accessor(key: str) -> str:
    """Convert a canonical setting key to a snake_case accessor name.

    ``inlayHints.enabled`` -> ``inlay_hints_enabled``; ``lsp.autoUpdate`` ->
    ``lsp_auto_update``; ``interpreter.path`` -> ``interpreter_path``.
    """
    return camel_to_snake(key.replace(".", "_"))


# ── Settings helpers ─────────────────────────────────────────────────────────


def is_generated(spec: dict) -> bool:
    """Whether a setting is emitted to editor constant files (``generated`` flag)."""
    return spec.get("generated") is not False


def generated_settings(defaults: dict) -> Iterator[tuple[str, dict]]:
    """Yield ``(key, spec)`` for settings emitted to editor constant files."""
    for key, spec in defaults["settings"].items():
        if is_generated(spec):
            yield key, spec


def vscode_property_key(key: str, spec: dict) -> str:
    """Return the VS Code configuration property name for a setting.

    Defaults to ``luma.<key>``; a ``vscode.key`` override wins (used by
    ``interpreter.path`` -> ``luma.path``).
    """
    override = (spec.get("vscode") or {}).get("key")
    return override if override else f"luma.{key}"


def vscode_config_key(property_key: str) -> str:
    """Strip the ``luma.`` section prefix used by LumaConfig accessors."""
    return property_key[len("luma.") :] if property_key.startswith("luma.") else property_key


def vscode_settings(defaults: dict) -> Iterator[tuple[str, str, dict]]:
    """Yield ``(canonical_key, property_key, spec)`` for VS Code-exposed settings.

    A setting participates in VS Code generation iff it carries a ``vscode``
    block. This is independent of the ``generated`` flag (e.g. playground
    settings are VS Code-only yet still belong in package.json).
    """
    for key, spec in defaults["settings"].items():
        if spec.get("vscode") is not None:
            yield key, vscode_property_key(key, spec), spec


# ── Argument parsing ─────────────────────────────────────────────────────────

EDITORS = ("vscode", "zed")


def editor_arg_parser(
    description: str, editors: Iterable[str] = EDITORS
) -> argparse.ArgumentParser:
    """Build a parser exposing one ``--<editor>`` flag per editor plus ``--all``."""
    parser = argparse.ArgumentParser(description=description)
    for editor in editors:
        parser.add_argument(f"--{editor}", action="store_true", help=f"Generate {editor} output")
    parser.add_argument("--all", action="store_true", help="Generate all outputs")
    return parser


def selected_editors(args: argparse.Namespace, editors: Iterable[str] = EDITORS) -> list[str]:
    """Resolve which editors to generate for from parsed args (``--all`` selects all)."""
    editors = list(editors)
    if args.all:
        return editors
    return [e for e in editors if getattr(args, e, False)]


def require_editors(parser: argparse.ArgumentParser, selected: _Selection) -> _Selection:
    """Exit with the parser's help text when nothing was selected.

    Centralises the ``no editor chosen -> print help + exit(1)`` guard that each
    generator's ``main`` otherwise copy-pastes. ``selected`` is whatever the
    caller uses to represent its choice — a list from :func:`selected_editors` or
    a boolean ``or`` of the per-editor flags — and is returned unchanged when
    truthy so callers can write ``editors = require_editors(parser, ...)``.
    """
    if not selected:
        parser.print_help()
        sys.exit(1)
    return selected


def run_generator(
    description: str,
    json_path: Path,
    outputs: dict[str, tuple[Path, Callable[[dict], str]]],
    done_message: str,
) -> None:
    """Drive a simple ``--<editor>`` generator end to end.

    Owns the skeleton shared by the config/download/platform generators: build the
    parser, guard against an empty editor selection, load ``json_path`` once and
    write each selected editor's output. ``outputs`` maps an editor to
    ``(path, generate)`` where ``generate`` renders the file contents from the
    loaded JSON document.
    """
    parser = editor_arg_parser(description)
    args = parser.parse_args()

    editors = require_editors(parser, selected_editors(args))

    data = load_json(json_path)
    for editor in editors:
        path, generate = outputs[editor]
        write_file(path, generate(data))

    print(done_message)


# ── Generator registry ───────────────────────────────────────────────────────

# The canonical list of generators, shared by generate-all.py (batch runner) and
# ci-check-generated.py (staleness gate) so the set of generators lives in one
# place. Each entry is ``(script, description)``; both callers invoke the script
# with ``--all``.
GENERATORS: list[tuple[str, str]] = [
    ("generate-config.py", "Configuration constants (config.ts, config.rs)"),
    (
        "generate-config-code.py",
        "Editor-native config (package.json properties, config_defaults.rs)",
    ),
    ("generate-download-code.py", "Download constants (download-constants.ts, .rs)"),
    ("generate-platform-code.py", "Platform maps (platform.ts, platform.rs)"),
    ("generate-keybindings.py", "Keybindings (package.json keybindings, KEYBINDINGS.md)"),
    ("generate-test-discovery.py", "Test discovery patterns (test-discovery.ts)"),
    ("generate-builtin-types.py", "Built-in type list (builtin-types.ts)"),
]


def load_generator(script: str) -> ModuleType:
    """Import a generator module by its hyphenated filename (not normally importable)."""
    module_name = script.removesuffix(".py").replace("-", "_")
    spec = importlib.util.spec_from_file_location(module_name, SCRIPT_DIR / script)
    assert spec and spec.loader, f"Cannot load generator {script}"
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def generated_files(repo_root: Path) -> list[str]:
    """Collect every generator's declared outputs as repo-root-relative POSIX paths.

    Each generator exposes a ``GENERATED_OUTPUTS`` list of the files it writes; the
    de-duplicated union (package.json is written by two generators) is what the CI
    staleness check diffs. Deriving it from the generators keeps the output set in
    lockstep with them instead of a hand-maintained parallel list.
    """
    repo_root = repo_root.resolve()
    seen: dict[str, None] = {}
    for script, _description in GENERATORS:
        module = load_generator(script)
        for path in module.GENERATED_OUTPUTS:
            seen.setdefault(path.resolve().relative_to(repo_root).as_posix(), None)
    return list(seen)
