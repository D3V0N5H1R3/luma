#!/usr/bin/env python3
"""Verify that CMake warning flags and .clang-tidy checks are in sync.

Parses cmake/LumaCompilerFlags.cmake for GCC/Clang warning flags and
.clang-tidy for enabled/disabled checks, then reports known
correspondences where one side is enabled and the other is disabled.

Usage:
    python scripts/check_warning_sync.py          # from repo root
    python scripts/check_warning_sync.py --strict  # exit 1 on mismatch

Exit codes:
    0  All known correspondences are consistent (or --strict not set).
    1  At least one mismatch found (--strict mode only).
"""

import re
import sys
from pathlib import Path

from _common import REPO_ROOT

# ── Known correspondences between GCC warning flags and clang-tidy checks ──
# Each entry maps a GCC -W flag to the clang-tidy check(s) that cover the same
# diagnostic. Only flags with a real clang-tidy equivalent are listed: several
# warnings have no clang-tidy check and are deliberately omitted — e.g.
# -Wimplicit-fallthrough and -Wshadow (clang-tidy has no variable-shadowing
# check; shadowing is a compiler-only diagnostic).
#
# Check names must be exact and must actually exist in clang-tidy. A misspelled
# or non-existent name that happens to share a prefix with an enabled group
# (e.g. a bogus "bugprone-foo" under bugprone-*) would be treated as enabled and
# silently pass, defeating the purpose of this cross-check.
CORRESPONDENCES: list[tuple[str, list[str]]] = [
    ("-Wnon-virtual-dtor", ["cppcoreguidelines-virtual-class-destructor"]),
    ("-Wold-style-cast", ["cppcoreguidelines-pro-type-cstyle-cast"]),
    ("-Wdouble-promotion", ["bugprone-narrowing-conversions"]),
    # Null-dereference detection lives in the static analyzer, not a bugprone
    # check; clang-analyzer-* is enabled (and error-escalated) in .clang-tidy.
    ("-Wnull-dereference", ["clang-analyzer-core.NullDereference"]),
]


def parse_cmake_warnings(cmake_path: Path) -> set[str]:
    """Extract GCC/Clang warning flags from the CMake file."""
    text = cmake_path.read_text(encoding="utf-8")

    # Find the LUMA_GCC_WARN_FLAGS block.
    match = re.search(r"set\(LUMA_GCC_WARN_FLAGS\s*(.*?)\)", text, re.DOTALL)
    if not match:
        return set()

    block = match.group(1)
    flags: set[str] = set()
    for token in block.split():
        token = token.strip()
        if token.startswith("-W"):
            # Normalise -Wformat=2 → -Wformat
            base = token.split("=")[0]
            flags.add(base)
    return flags


def parse_clang_tidy_checks(tidy_path: Path) -> tuple[set[str], set[str]]:
    """Return (enabled_checks, disabled_checks) from .clang-tidy.

    Wildcard entries like 'bugprone-*' are included as-is.
    Disabled entries start with '-' in the YAML.
    """
    text = tidy_path.read_text(encoding="utf-8")

    # Capture only the indented lines of the folded block. Using [ \t]+ (rather
    # than \s+) stops at the first blank or unindented line, so a trailing
    # column-0 comment after the block is not swallowed into the last entry.
    match = re.search(r"Checks:\s*>?[ \t]*\n((?:[ \t]+.*\n)*)", text)
    if not match:
        return set(), set()

    raw = match.group(1)
    enabled: set[str] = set()
    disabled: set[str] = set()

    for chunk in raw.split(","):
        name = chunk.strip().rstrip(",")
        if not name:
            continue
        if name.startswith("-"):
            disabled.add(name[1:])
        else:
            enabled.add(name)

    return enabled, disabled


def is_check_enabled(check: str, enabled: set[str], disabled: set[str]) -> bool | None:
    """Determine whether *check* is enabled, disabled, or unknown.

    Honours clang-tidy's glob semantics: an entry ending in ``*`` covers every
    check whose name starts with the text before the ``*`` — so ``bugprone-*``
    covers ``bugprone-implicit-fallthrough`` and the leading ``-*`` covers
    everything. When both an enable and a disable match, the more specific
    (longest-prefix) entry wins, mirroring clang-tidy's last-match-wins ordering
    where a group is enabled and then one of its members is individually
    disabled. Returns ``None`` only when no entry matches at all.
    """

    def best_match(entries: set[str]) -> int | None:
        """Specificity of the most specific matching entry, or ``None``.

        An exact (non-glob) match outscores every glob so it always wins; a glob
        is scored by the length of its fixed prefix (the text before ``*``).
        """
        best: int | None = None
        for entry in entries:
            if entry == check:
                score = len(check) + 1
            elif entry.endswith("*") and check.startswith(entry[:-1]):
                score = len(entry) - 1
            else:
                continue
            if best is None or score > best:
                best = score
        return best

    enabled_score = best_match(enabled)
    disabled_score = best_match(disabled)

    if enabled_score is None and disabled_score is None:
        return None
    if disabled_score is None:
        return True
    if enabled_score is None:
        return False
    return enabled_score >= disabled_score


def main() -> int:
    strict = "--strict" in sys.argv

    cmake_path = REPO_ROOT / "cmake" / "LumaCompilerFlags.cmake"
    tidy_path = REPO_ROOT / ".clang-tidy"

    if not cmake_path.exists():
        print(f"error: {cmake_path} not found", file=sys.stderr)
        return 1
    if not tidy_path.exists():
        print(f"error: {tidy_path} not found", file=sys.stderr)
        return 1

    cmake_flags = parse_cmake_warnings(cmake_path)
    tidy_enabled, tidy_disabled = parse_clang_tidy_checks(tidy_path)

    mismatches: list[str] = []

    for flag, checks in CORRESPONDENCES:
        flag_present = flag in cmake_flags
        for check in checks:
            check_state = is_check_enabled(check, tidy_enabled, tidy_disabled)

            if check_state is None:
                continue

            if flag_present and not check_state:
                mismatches.append(f"  CMake enables {flag} but .clang-tidy disables {check}")
            elif not flag_present and check_state:
                mismatches.append(f"  .clang-tidy enables {check} but CMake lacks {flag}")

    # Summary.
    print(f"CMake GCC warning flags: {len(cmake_flags)}")
    print(f"clang-tidy enabled groups: {len(tidy_enabled)}")
    print(f"clang-tidy disabled checks: {len(tidy_disabled)}")
    print(f"Known correspondences checked: {len(CORRESPONDENCES)}")

    if mismatches:
        print(f"\n{len(mismatches)} mismatch(es) found:")
        for msg in mismatches:
            print(msg)
        if strict:
            return 1
    else:
        print("\nAll known correspondences are consistent.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
