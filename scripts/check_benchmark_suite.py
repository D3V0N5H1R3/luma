#!/usr/bin/env python3
"""Verify that benchmarks/suite.luma wires in every benchmark module.

Luma cannot glob includes, so suite.luma maintains two hand-written lists:

  1. the ``include "bench_<topic>.luma"`` list at the top of the file, and
  2. the ``st = run_<topic>_benchmarks(st)`` call list inside run_benchmarks().

Forgetting the second list silently drops a whole module from the run without
any compile error. This guard turns that silent omission into a failing check
by asserting that every ``bench_*.luma`` module is both included and has one of
its ``run_*_benchmarks`` entry functions called from suite.luma.

Usage:
    python scripts/check_benchmark_suite.py          # from repo root

Exit codes:
    0  Every benchmark module is included and run.
    1  A module is missing from a list (or a list references a missing file).
"""

import re
import sys

from _common import REPO_ROOT

BENCH_DIR = REPO_ROOT / "benchmarks"
SUITE = BENCH_DIR / "suite.luma"

_INCLUDE_RE = re.compile(r'include\s+"([^"]+)"')
_RUN_NAME_RE = re.compile(r"\brun_\w+_benchmarks\b")
_RUN_DEF_RE = re.compile(r"function\s+BenchResult\s+(run_\w+_benchmarks)\s*\(")


def _strip_comments(text: str) -> str:
    """Drop Luma line comments so commented-out includes/calls are not counted.

    Luma comments run from ``#`` to end of line. None of the lines this guard
    scans (includes, run-call assignments, run-function definitions) carry a
    ``#`` inside a string, so a plain first-``#`` cut is safe here.
    """
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def main() -> int:
    if not SUITE.exists():
        print(f"error: {SUITE} not found", file=sys.stderr)
        return 1

    suite_text = _strip_comments(SUITE.read_text(encoding="utf-8"))
    includes = set(_INCLUDE_RE.findall(suite_text))
    # suite.luma never *defines* a run_*_benchmarks function (its entry point is
    # run_benchmarks, which this pattern excludes), so every match is a call.
    suite_calls = set(_RUN_NAME_RE.findall(suite_text))

    bench_files = sorted(BENCH_DIR.glob("bench_*.luma"))
    # Cache each module's defined entry functions (comments stripped).
    defined_by_file = {
        path: set(_RUN_DEF_RE.findall(_strip_comments(path.read_text(encoding="utf-8"))))
        for path in bench_files
    }

    problems: list[str] = []

    # (1) Every bench_*.luma file must be included by suite.luma.
    for path in bench_files:
        if path.name not in includes:
            problems.append(f"  {path.name} exists but is not included in suite.luma")

    # (2) Every included bench_*.luma must reference a file that exists.
    for name in sorted(includes):
        if name.startswith("bench_") and not (BENCH_DIR / name).exists():
            problems.append(f'  include "{name}" in suite.luma references a missing file')

    # (3) Each bench_*.luma must define a run_*_benchmarks entry function, and at
    #     least one entry function it defines must be called from suite.luma.
    for path in bench_files:
        defined = defined_by_file[path]
        if not defined:
            problems.append(f"  {path.name} defines no run_*_benchmarks entry function")
        elif not (defined & suite_calls):
            names = ", ".join(sorted(defined))
            problems.append(
                f"  {path.name} is included but none of its entry functions "
                f"({names}) are called in suite.luma's run_benchmarks()"
            )

    # (4) Every run_*_benchmarks called in suite.luma must be defined somewhere.
    all_defined: set[str] = set().union(*defined_by_file.values()) if defined_by_file else set()
    for name in sorted(suite_calls - all_defined):
        problems.append(f"  suite.luma calls {name}(), which no bench_*.luma defines")

    print(f"Benchmark modules on disk: {len(bench_files)}")
    print(f"Includes in suite.luma:    {sum(1 for n in includes if n.startswith('bench_'))}")
    print(f"Run calls in suite.luma:   {len(suite_calls)}")

    if problems:
        print(f"\n{len(problems)} problem(s) found:")
        for msg in problems:
            print(msg)
        print("\nUpdate benchmarks/suite.luma so both lists stay in sync.")
        return 1

    print("\nAll benchmark modules are included and run.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
