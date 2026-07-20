#!/usr/bin/env python3
"""CI check: verify that generated files are up-to-date.

Runs all generators and checks for uncommitted changes. Returns non-zero
if any generated files are stale.

Usage (in CI):
    python ci-check-generated.py

Intended to be added as a CI step to prevent generated file drift.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import codegen_common as cc

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent  # extensions/../ = repo root


def main() -> None:
    print("Running all generators...")
    for script, _description in cc.GENERATORS:
        result = subprocess.run(
            [sys.executable, str(SCRIPT_DIR / script), "--all"],
            cwd=str(SCRIPT_DIR),
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"Generator failed: {script} --all")
            print(result.stderr)
            sys.exit(1)

    # The set of generated files is derived from each generator's declared
    # GENERATED_OUTPUTS (see codegen_common.generated_files) so it stays in
    # lockstep with the generators instead of a hand-maintained parallel list.
    # package.json appears once even though two generators splice into it.
    print("\nChecking for uncommitted changes in generated files...")
    result = subprocess.run(
        ["git", "diff", "--name-only", "--", *cc.generated_files(REPO_ROOT)],
        cwd=str(REPO_ROOT),
        capture_output=True,
        text=True,
    )

    changed = [f for f in result.stdout.strip().splitlines() if f]

    if changed:
        print("\nERROR: The following generated files are stale:")
        for f in changed:
            print(f"  {f}")
        print("\nRun 'python extensions/shared/generate-all.py' and commit the results.")
        sys.exit(1)
    else:
        print("All generated files are up-to-date.")


if __name__ == "__main__":
    main()
