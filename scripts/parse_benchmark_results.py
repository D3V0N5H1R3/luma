#!/usr/bin/env python3
"""Parse Luma benchmark text output into JSON for comparison.

Reads the text output produced by benchmarks/suite.luma and extracts
per-benchmark timing data into a JSON object mapping benchmark names
to their per-iteration time in milliseconds.

Usage:
    python3 parse_benchmark_results.py benchmark-results.txt -o results.json
    cat benchmark-results.txt | python3 parse_benchmark_results.py -o results.json
"""

import argparse
import json
import re
import sys

from _common import require_python

# Enforce the shared Python version gate before any 3.10+ syntax runs.
require_python()

# Matches lines like: "integer arithmetic | 50000 iterations | 123 ms | 0.002 ms/iter"
_RESULT_RE = re.compile(
    r"^(?P<name>.+?)\s*\|\s*\d+\s+iterations\s*\|\s*[\d.]+\s+ms\s*\|\s*(?P<per_iter>[\d.]+)\s+ms/iter$"
)


def parse_results(text: str) -> dict[str, float]:
    """Extract benchmark results from text output."""
    results: dict[str, float] = {}
    for line in text.splitlines():
        m = _RESULT_RE.match(line.strip())
        if m:
            results[m.group("name")] = float(m.group("per_iter"))
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description="Parse Luma benchmark output to JSON.")
    parser.add_argument(
        "input", nargs="?", default="-", help="Path to benchmark text output (default: stdin)"
    )
    parser.add_argument(
        "-o", "--output", default="-", help="Path to write JSON results (default: stdout)"
    )
    args = parser.parse_args()

    if args.input == "-":
        text = sys.stdin.read()
    else:
        with open(args.input, encoding="utf-8") as f:
            text = f.read()

    results = parse_results(text)

    if not results:
        print("warning: no benchmark results found in input", file=sys.stderr)

    output = json.dumps(results, indent=2, sort_keys=True) + "\n"

    if args.output == "-":
        sys.stdout.write(output)
    else:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(output)

    print(f"Parsed {len(results)} benchmark(s)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
