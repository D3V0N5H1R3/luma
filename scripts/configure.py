#!/usr/bin/env python3
"""Configure a Luma build using CMake presets or named configurations.

Provides a convenient wrapper around cmake --preset with a summary of
available configurations. Presets are defined in CMakePresets.json.

Usage:
    python scripts/configure.py                  # list available presets
    python scripts/configure.py <preset>         # configure with preset
    python scripts/configure.py <preset> --build # configure and build

Named presets are defined in CMakePresets.json. Run this script with no
arguments (or -h/--help) to print the current list with descriptions; the
listing is generated from the file, so it never drifts out of date.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

from _common import REPO_ROOT

PRESETS_FILE = REPO_ROOT / "CMakePresets.json"


def load_presets() -> list[dict]:
    """Load configure presets from CMakePresets.json."""
    if not PRESETS_FILE.exists():
        return []

    with open(PRESETS_FILE, encoding="utf-8") as f:
        data = json.load(f)

    return [p for p in data.get("configurePresets", []) if not p.get("hidden", False)]


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
        print(f"\nUsage: python {Path(__file__).name} <preset> [--build]")


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

    # Configure.
    print(f"=== Configuring with preset: {preset} ===\n")
    result = subprocess.run(
        ["cmake", "--preset", preset],
        cwd=str(REPO_ROOT),
    )
    if result.returncode != 0:
        return result.returncode

    # Enable the repository's Git hooks (best-effort; never fails configure).
    if (REPO_ROOT / ".git").exists():
        try:
            from install_hooks import enable_hooks

            enable_hooks()
        except Exception as exc:  # pragma: no cover - defensive
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
