#!/usr/bin/env python3
"""Run all code generators for the Luma editor extensions.

Convenience wrapper that invokes all individual generators with --all.
Run this after modifying defaults.json, keybindings.json, or platform-map.json.

Usage:
    python generate-all.py
"""

from __future__ import annotations

import subprocess
import sys

import codegen_common as cc


def main() -> None:
    print("Running all Luma extension code generators...\n")
    failed = False

    for script, description in cc.GENERATORS:
        print(f"-- {description}")
        cmd = [sys.executable, str(cc.SCRIPT_DIR / script), "--all"]
        result = subprocess.run(cmd, cwd=str(cc.SCRIPT_DIR))
        if result.returncode != 0:
            print(f"  FAILED (exit code {result.returncode})")
            failed = True
        print()

    if failed:
        print("Some generators failed — see above.")
        sys.exit(1)
    else:
        print("All generators completed successfully.")


if __name__ == "__main__":
    main()
