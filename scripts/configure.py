#!/usr/bin/env python3
"""Configure a Luma build using CMake presets or named configurations.

Provides a convenient wrapper around cmake --preset with a summary of
available configurations. Presets are defined in CMakePresets.json.

Usage:
    python scripts/configure.py                  # list available presets
    python scripts/configure.py <preset>         # configure with preset
    python scripts/configure.py <preset> --build # configure and build
    python scripts/configure.py <preset> --if-needed --build
                                                 # configure only when the build
                                                 # tree is missing or stale, then build

Named presets are defined in CMakePresets.json. Run this script with no
arguments (or -h/--help) to print the current list with descriptions; the
listing is generated from the file, so it never drifts out of date.

A preset whose `condition` excludes the current host (for example `sanitize`
and `coverage` on Windows) is rejected with an explanatory message instead of
CMake's terse "Could not use disabled preset" error.
"""

import argparse
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

from _common import REPO_ROOT

PRESETS_FILE = REPO_ROOT / "CMakePresets.json"
USER_PRESETS_FILE = REPO_ROOT / "CMakeUserPresets.json"


def load_all_presets() -> list[dict]:
    """Load every configure preset from CMakePresets.json, including hidden ones."""
    if not PRESETS_FILE.exists():
        return []

    with open(PRESETS_FILE, encoding="utf-8") as f:
        data = json.load(f)

    return data.get("configurePresets", [])


def load_presets() -> list[dict]:
    """Load the user-selectable (non-hidden) configure presets."""
    return [p for p in load_all_presets() if not p.get("hidden", False)]


def host_system_name() -> str:
    """Return the host name CMake uses for ${hostSystemName} (Windows, Linux, Darwin)."""
    return platform.system()


def _expand_macros(value: str, macros: dict[str, str]) -> str:
    """Expand the ${name} macros we know; leave any others untouched."""
    return re.sub(r"\$\{(\w+)\}", lambda m: macros.get(m.group(1), m.group(0)), value)


def condition_is_met(condition: dict | bool | None, macros: dict[str, str]) -> bool:
    """Evaluate a CMake preset `condition` object.

    Unknown condition types evaluate to True so CMake stays the final authority.
    """
    if condition is None:
        return True
    if isinstance(condition, bool):
        return condition

    kind = condition.get("type")
    if kind == "const":
        return bool(condition.get("value", True))
    if kind in ("equals", "notEquals"):
        lhs = _expand_macros(condition.get("lhs", ""), macros)
        rhs = _expand_macros(condition.get("rhs", ""), macros)
        return (lhs == rhs) == (kind == "equals")
    if kind in ("inList", "notInList"):
        needle = _expand_macros(condition.get("string", ""), macros)
        haystack = [_expand_macros(item, macros) for item in condition.get("list", [])]
        return (needle in haystack) == (kind == "inList")
    if kind in ("matches", "notMatches"):
        subject = _expand_macros(condition.get("string", ""), macros)
        found = re.search(condition.get("regex", ""), subject) is not None
        return found == (kind == "matches")
    if kind == "anyOf":
        return any(condition_is_met(c, macros) for c in condition.get("conditions", []))
    if kind == "allOf":
        return all(condition_is_met(c, macros) for c in condition.get("conditions", []))
    if kind == "not":
        return not condition_is_met(condition.get("condition"), macros)
    return True


def preset_macros(name: str) -> dict[str, str]:
    """Return the CMake preset macros needed to evaluate conditions and binaryDir."""
    return {
        "sourceDir": REPO_ROOT.as_posix(),
        "sourceDirName": REPO_ROOT.name,
        "presetName": name,
        "hostSystemName": host_system_name(),
    }


def resolve_binary_dir(name: str, presets_by_name: dict[str, dict]) -> Path | None:
    """Resolve a preset's binaryDir, following `inherits` depth-first like CMake."""
    pending = [name]
    seen: set[str] = set()
    while pending:
        current = pending.pop(0)
        if current in seen or current not in presets_by_name:
            continue
        seen.add(current)
        preset = presets_by_name[current]
        if "binaryDir" in preset:
            return Path(_expand_macros(preset["binaryDir"], preset_macros(name)))
        inherits = preset.get("inherits", [])
        pending[0:0] = [inherits] if isinstance(inherits, str) else list(inherits)
    return None


def configure_stamp(binary_dir: Path) -> Path:
    """Return the file touched after every successful configure of a build tree."""
    return binary_dir / "luma-configure.stamp"


def newest_preset_mtime() -> float:
    """Return the latest modification time of the preset files (0 when absent)."""
    preset_files = [f for f in (PRESETS_FILE, USER_PRESETS_FILE) if f.exists()]
    return max((f.stat().st_mtime for f in preset_files), default=0.0)


def write_configure_stamp(binary_dir: Path) -> None:
    """Record a successful configure.

    The stamp is never older than the preset files, so a preset file with a
    future timestamp (clock skew, archive extraction) cannot force a configure
    on every build.
    """
    stamp = configure_stamp(binary_dir)
    stamp.touch()
    stamped_at = max(stamp.stat().st_mtime, newest_preset_mtime())
    os.utime(stamp, (stamped_at, stamped_at))


def needs_configure(binary_dir: Path | None) -> bool:
    """Return True when the build tree is missing or older than the preset files.

    `cmake --build` re-runs configure on its own when CMakeLists.txt changes, but
    not when a preset's cacheVariables change, so those files are compared too.
    CMake leaves CMakeCache.txt untouched when its content is unchanged, so the
    comparison also uses a stamp this script refreshes after each configure —
    otherwise an unrelated preset edit would force a configure on every build.
    """
    if binary_dir is None:
        return True
    cache = binary_dir / "CMakeCache.txt"
    if not cache.exists():
        return True
    stamp = configure_stamp(binary_dir)
    configured_at = max(f.stat().st_mtime for f in (cache, stamp) if f.exists())
    return configured_at < newest_preset_mtime()


def format_presets(presets: list[dict]) -> str:
    """Render the available presets as an aligned, human-readable listing."""
    if not presets:
        return "No configure presets found in CMakePresets.json."

    name_width = max(len(p["name"]) for p in presets)
    lines = ["Available build configurations:", ""]
    for p in presets:
        name = p["name"]
        desc = p.get("description", p.get("displayName", ""))
        lines.append(f"  {name:<{name_width}}  {desc}")
    return "\n".join(lines)


def list_presets(presets: list[dict]) -> None:
    """Print the available presets followed by a short usage hint."""
    print(format_presets(presets))
    if presets:
        print(f"\nUsage: python {Path(__file__).name} <preset> [--build] [--if-needed]")


def main() -> int:
    presets = load_presets()
    preset_names = [p["name"] for p in presets]

    parser = argparse.ArgumentParser(
        description="Configure a Luma build using CMake presets.",
        epilog=format_presets(presets),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "preset",
        nargs="?",
        metavar="preset",
        help="Preset to configure (omit to list the available presets)",
    )
    parser.add_argument("--build", action="store_true", help="Build immediately after configuring")
    parser.add_argument(
        "--if-needed",
        action="store_true",
        help="Skip configuring when the preset's build tree already exists and is up to date",
    )
    args = parser.parse_args()

    # No preset given: list the available configurations.
    if args.preset is None:
        list_presets(presets)
        return 0

    preset = args.preset

    # Validate the preset ourselves rather than via argparse `choices`, so an
    # unknown preset stays a domain error (exit 1) distinct from an argparse
    # usage error (exit 2).
    if preset not in preset_names:
        print(f"Error: unknown preset '{preset}'.", file=sys.stderr)
        print(f"Available presets: {', '.join(preset_names)}", file=sys.stderr)
        return 1

    presets_by_name = {p["name"]: p for p in load_all_presets()}
    condition = presets_by_name[preset].get("condition")
    if not condition_is_met(condition, preset_macros(preset)):
        print(
            f"Error: preset '{preset}' is not available on this host ({host_system_name()}).",
            file=sys.stderr,
        )
        print(f"Its condition in CMakePresets.json is: {json.dumps(condition)}", file=sys.stderr)
        if host_system_name() == "Windows":
            print("Use WSL or the Dev Container (.devcontainer/) instead.", file=sys.stderr)
        return 1

    binary_dir = resolve_binary_dir(preset, presets_by_name)
    if args.if_needed and not needs_configure(binary_dir):
        print(f"=== Preset {preset} is already configured in {binary_dir}; skipping configure ===")
    else:
        # Configure.
        print(f"=== Configuring with preset: {preset} ===\n")
        result = subprocess.run(
            ["cmake", "--preset", preset],
            cwd=str(REPO_ROOT),
        )
        if result.returncode != 0:
            return result.returncode
        if binary_dir is not None and binary_dir.is_dir():
            write_configure_stamp(binary_dir)

        # Enable the repository's Git hooks (best-effort; never fails configure).
        if (REPO_ROOT / ".git").exists():
            try:
                from install_git_hooks import enable_hooks

                if enable_hooks() != 0:
                    print(
                        "Warning: Git hooks were not enabled (see the error above).",
                        file=sys.stderr,
                    )
            except (ImportError, OSError) as exc:  # pragma: no cover - defensive
                print(f"Note: could not enable Git hooks ({exc}).", file=sys.stderr)

    # Optionally build.
    if args.build:
        print(f"\n=== Building preset: {preset} ===\n")
        result = subprocess.run(
            ["cmake", "--build", "--preset", preset],
            cwd=str(REPO_ROOT),
        )
        if result.returncode != 0:
            return result.returncode

    print("\nDone.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
