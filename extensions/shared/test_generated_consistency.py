#!/usr/bin/env python3
"""Validate that checked-in generated config files match what generate-config.py would produce.

Reads defaults.json via the same logic used by generate-config.py, re-generates
each output in memory, and compares it against the checked-in file.

Exit codes:
    0 — all checked-in files are consistent with the current defaults.json
    1 — one or more files are out of date (mismatches printed to stdout)

Usage:
    python test_generated_consistency.py
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

import codegen_common as cc

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent


# ── Checked-in output paths (must match generate-config.py paths) ────────────


def _checked_in_paths(root: Path) -> dict[str, Path]:
    return {
        "typescript": root / "vscode" / "src" / "generated" / "config.ts",
        "rust": root / "zed" / "src" / "generated" / "config.rs",
    }


# ── Comparison helpers ────────────────────────────────────────────────────────


def _read_file(path: Path) -> str | None:
    """Return file contents, or None if the file does not exist."""
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return None


def _compare(path: Path, expected: str) -> list[str]:
    """Return a list of error strings (empty on success)."""
    actual = _read_file(path)
    if actual is None:
        return [f"  MISSING  {path}"]
    if actual != expected:
        return [f"  MISMATCH {path}"]
    return []


# ── Consistency check ─────────────────────────────────────────────────────────


def _consistency_errors() -> list[str]:
    """Return per-file mismatch/missing messages (empty when all are current)."""
    gen = cc.load_generator("generate-config.py")
    defaults = cc.load_json(gen.DEFAULTS_PATH)
    paths = _checked_in_paths(ROOT_DIR)

    generators = {
        "typescript": gen.generate_vscode,
        "rust": gen.generate_zed,
    }

    errors: list[str] = []
    for key, generator_fn in generators.items():
        errors.extend(_compare(paths[key], generator_fn(defaults)))
    return errors


class GeneratedConfigConsistency(unittest.TestCase):
    """Run the same check under ``unittest discover`` (and thus the Python CI job).

    This module matches the ``test_*`` discovery pattern, so exposing the check as
    a real test — not only a standalone ``main()`` — means the Python CI step
    actually asserts it instead of importing a case-free module.
    """

    def test_checked_in_config_matches_regeneration(self) -> None:
        errors = _consistency_errors()
        self.assertEqual(errors, [], "\n".join(errors))


# ── Standalone entry point ────────────────────────────────────────────────────


def main() -> int:
    errors = _consistency_errors()
    if errors:
        print("Config consistency check FAILED. Out-of-date or missing files:")
        for msg in errors:
            print(msg)
        print()
        print("Re-run:  python extensions/shared/generate-config.py --all")
        return 1

    print("Config consistency check passed. All generated files are up to date.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
