#!/usr/bin/env python3
"""Compare benchmark results against a baseline and fail if regression exceeds threshold.

Most benchmarks are CPU-bound and stable, so a tight default threshold catches
genuine regressions. A handful exercise the filesystem, an embedded key-value
store, subprocess startup, or the concurrency runtime; these have far higher
run-to-run variance, so they use a wider threshold to avoid spurious CI failures.

A relative threshold alone is not enough: many benchmarks run in a few hundred
nanoseconds, and the harness reports per-iteration times quantised to ~0.0001 ms
(100 ns). At that scale a single tick of scheduler jitter turns 0.0003 ms into
0.0005 ms — a spurious "+66%" that trips the relative gate even though the
absolute change is meaningless. To stay robust, a regression must exceed BOTH
the relative threshold AND an absolute minimum-delta floor (``--min-delta``),
so sub-microsecond noise no longer fails CI while genuinely expensive
regressions still do.

The baseline is managed by the Benchmark CI workflow, which caches each run's
results as the baseline for the next run; it is not committed to the repository.
"""

import argparse
import json
import sys
from pathlib import Path

from _common import require_python

# Enforce the shared Python version gate before any 3.10+ syntax runs.
require_python()

# Substrings identifying high-variance (I/O- or scheduler-bound) benchmarks.
# These are compared against a wider threshold than CPU-bound benchmarks.
_HIGH_VARIANCE_MARKERS = (
    "FileSystem",
    "KVS ",
    "Process.run",
    "task_scope",
    "channel",
    "producer/consumer",
    "Task.",
)


def is_high_variance(name: str) -> bool:
    """Return True if the benchmark is I/O- or scheduler-bound (noisy)."""
    return any(marker in name for marker in _HIGH_VARIANCE_MARKERS)


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare benchmark results against a baseline.")
    parser.add_argument("baseline", help="Path to baseline results JSON")
    parser.add_argument("current", help="Path to current results JSON")
    parser.add_argument(
        "--threshold",
        type=float,
        default=10.0,
        help="Regression threshold percentage for CPU-bound benchmarks (default: 10)",
    )
    parser.add_argument(
        "--io-threshold",
        type=float,
        default=50.0,
        help="Regression threshold percentage for high-variance I/O and "
        "concurrency benchmarks (default: 50)",
    )
    parser.add_argument(
        "--min-delta",
        type=float,
        default=0.0,
        help="Absolute minimum per-iteration slowdown, in milliseconds, that a "
        "benchmark must exceed (in addition to the percentage threshold) to "
        "count as a regression. Suppresses spurious failures from timer-"
        "resolution noise on sub-microsecond benchmarks (default: 0, disabled)",
    )
    args = parser.parse_args()

    if not Path(args.baseline).exists():
        print(f"Error: baseline benchmark file not found: {args.baseline}", file=sys.stderr)
        return 1
    if not Path(args.current).exists():
        print(f"Error: current benchmark file not found: {args.current}", file=sys.stderr)
        return 1

    with open(args.baseline, encoding="utf-8") as f:
        baseline = json.load(f)
    with open(args.current, encoding="utf-8") as f:
        current = json.load(f)

    threshold = args.threshold

    regressions = []
    new_benchmarks = []
    for name, base_time in baseline.items():
        if name in current:
            curr_time = current[name]
            if base_time > 0:
                limit = args.io_threshold if is_high_variance(name) else threshold
                change_pct = ((curr_time - base_time) / base_time) * 100
                abs_delta = curr_time - base_time
                if change_pct > limit and abs_delta >= args.min_delta:
                    regressions.append((name, base_time, curr_time, change_pct))
                    print(
                        f"REGRESSION: {name}: {base_time:.6f}ms -> {curr_time:.6f}ms "
                        f"({change_pct:+.1f}%, limit {limit:.0f}%)"
                    )
                elif change_pct > limit:
                    # Over the percentage gate but under the absolute floor: this
                    # is timer-resolution noise on a sub-microsecond benchmark,
                    # not a meaningful slowdown.
                    print(
                        f"OK: {name}: {base_time:.6f}ms -> {curr_time:.6f}ms "
                        f"({change_pct:+.1f}%, +{abs_delta:.6f}ms < {args.min_delta:.6f}ms floor)"
                    )
                else:
                    print(
                        f"OK: {name}: {base_time:.6f}ms -> {curr_time:.6f}ms ({change_pct:+.1f}%)"
                    )

    # Report benchmarks present in current but absent from baseline.
    for name in sorted(current):
        if name not in baseline:
            new_benchmarks.append(name)
            print(f"NEW: {name}: {current[name]:.6f}ms (no baseline)")

    if new_benchmarks:
        print(f"\n{len(new_benchmarks)} new benchmark(s) without baseline data")

    # Report benchmarks present in the baseline but absent from the current run.
    # A dropped benchmark can be an intentional removal/rename, or a sign the
    # benchmark crashed or produced no output. Surface it as a warning rather
    # than a hard failure — mirroring the NEW handling above — so a deliberate
    # removal does not break CI (a rename shows up as both NEW and MISSING).
    missing_benchmarks = [name for name in sorted(baseline) if name not in current]
    for name in missing_benchmarks:
        print(f"MISSING: {name}: present in baseline but absent from current run")

    if missing_benchmarks:
        print(f"\n{len(missing_benchmarks)} benchmark(s) missing from the current run")

    if regressions:
        print(f"\n{len(regressions)} benchmark(s) regressed beyond threshold")
        return 1

    print(f"\nAll benchmarks within threshold (CPU {threshold:.0f}%, I/O {args.io_threshold:.0f}%)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
