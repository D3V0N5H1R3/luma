#!/usr/bin/env python3
"""Generate a code coverage report for the Luma project.

Configures a coverage build, compiles, runs tests, and produces an HTML
report using lcov + genhtml (Linux/macOS) or a summary via gcovr.

Usage:
    python scripts/generate_coverage.py [--open]

Options:
    --open  Open the HTML report in the default browser after generation.

Requirements:
    - GCC or Clang (MSVC is not supported for coverage).
    - lcov and genhtml (preferred), or gcovr as a fallback.
"""

import shutil
import sys
import webbrowser

from _common import REPO_ROOT, run

BUILD_DIR = REPO_ROOT / "build-coverage"
REPORT_DIR = BUILD_DIR / "coverage-report"


def has_tool(name: str) -> bool:
    """Return True if *name* is on PATH."""
    return shutil.which(name) is not None


def main() -> int:
    open_report = "--open" in sys.argv

    if sys.platform == "win32":
        print("Error: code coverage is not supported on Windows (MSVC).", file=sys.stderr)
        return 1

    # ── Configure ──
    print("\n=== Configuring coverage build ===\n")
    run(
        [
            "cmake",
            "-B",
            str(BUILD_DIR),
            "-S",
            str(REPO_ROOT),
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DLUMA_FEATURE_COVERAGE=ON",
        ]
    )

    # ── Build ──
    print("\n=== Building ===\n")
    run(["cmake", "--build", str(BUILD_DIR), "--parallel"])

    # ── Run tests ──
    print("\n=== Running tests ===\n")
    run(["ctest", "--test-dir", str(BUILD_DIR), "--output-on-failure"])

    # ── Generate report ──
    if has_tool("lcov") and has_tool("genhtml"):
        print("\n=== Generating HTML report with lcov + genhtml ===\n")
        info_file = BUILD_DIR / "coverage.info"

        run(
            [
                "lcov",
                "--capture",
                "--directory",
                str(BUILD_DIR),
                "--output-file",
                str(info_file),
                "--ignore-errors",
                "mismatch",
            ]
        )

        # Remove external/test coverage data.
        run(
            [
                "lcov",
                "--remove",
                str(info_file),
                "*/external/*",
                "*/tests/*",
                "/usr/*",
                "--output-file",
                str(info_file),
                "--ignore-errors",
                "unused",
            ]
        )

        REPORT_DIR.mkdir(parents=True, exist_ok=True)
        run(
            [
                "genhtml",
                str(info_file),
                "--output-directory",
                str(REPORT_DIR),
                "--title",
                "Luma Code Coverage",
            ]
        )

        index = REPORT_DIR / "index.html"
        print(f"\nCoverage report: {index}")

        if open_report and index.exists():
            webbrowser.open(index.as_uri())

    elif has_tool("gcovr"):
        print("\n=== Generating report with gcovr ===\n")
        REPORT_DIR.mkdir(parents=True, exist_ok=True)
        index = REPORT_DIR / "index.html"

        run(
            [
                "gcovr",
                "--root",
                str(REPO_ROOT),
                "--filter",
                "core/",
                "--filter",
                "shared/",
                "--filter",
                "language-server/",
                "--filter",
                "debugger/",
                "--html-details",
                str(index),
                str(BUILD_DIR),
            ]
        )

        print(f"\nCoverage report: {index}")

        if open_report and index.exists():
            webbrowser.open(index.as_uri())

    else:
        print("\nWarning: neither lcov+genhtml nor gcovr found.", file=sys.stderr)
        print("Install one of them to generate an HTML coverage report.", file=sys.stderr)
        print("Tests were run with coverage instrumentation; .gcda files are in the build dir.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
