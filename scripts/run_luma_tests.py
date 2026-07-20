#!/usr/bin/env python3
"""Run Luma feature tests.

Cross-platform replacement for run_luma_tests.sh / run_luma_tests.ps1.
Discovers all .luma files under a test directory, executes each with the
luma interpreter in --test mode, and reports a summary.

Usage:
    python scripts/run_luma_tests.py [--exe <luma-exe>] [--dir <tests-dir>]
                                     [--jobs <n>]

Environment variables:
    LUMA_EXE       - Path to the luma executable (fallback).
    LUMA_TESTS_DIR - Path to the tests directory (fallback).
"""

import argparse
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from _common import (
    REPO_ROOT,
    find_luma_exe,
    reconfigure_utf8_streams,
    resolve_dir,
    resolve_jobs,
)


def _build_command(luma_exe: Path, test_file: Path) -> list[str]:
    """Construct the luma invocation for one feature-test file.

    Mirrors the canonical CTest invocation (tests/CMakeLists.txt): every feature
    test runs in --strict mode, and the sandbox suite additionally needs --box
    so its "operation is blocked" assertions exercise the real restricted
    environment. Without --box those operations run unsandboxed, which pollutes
    the working tree and even calls Process.exit(0) for real, aborting the run
    with a success code and masking the failures.
    """
    command = [str(luma_exe)]
    if test_file.stem == "sandbox":
        command.append("--box")
    command += ["--strict", "--test", str(test_file)]
    return command


def _run_test_captured(luma_exe: Path, test_file: Path) -> tuple[int, str]:
    """Run one feature-test file, capturing merged stdout/stderr.

    Output is captured rather than inherited so that parallel workers do not
    interleave their console output; the caller replays it in sorted order.
    """
    proc = subprocess.run(
        _build_command(luma_exe, test_file),
        cwd=str(REPO_ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, proc.stdout or ""


def _print_captured(name: str, output: str) -> None:
    """Print a test file's banner followed by its captured output."""
    print(f"\u2500\u2500 {name} \u2500\u2500")
    if output:
        sys.stdout.write(output)
        if not output.endswith("\n"):
            sys.stdout.write("\n")


def main() -> int:
    # The report uses box-drawing characters and tests emit UTF-8; make sure
    # our own stdout/stderr can render them regardless of the host code page.
    reconfigure_utf8_streams()

    parser = argparse.ArgumentParser(description="Run Luma feature tests.")
    parser.add_argument(
        "--exe", default=None, help="Path to the luma executable (default: auto-detect)"
    )
    parser.add_argument(
        "--dir", default=None, help="Path to the tests directory (default: tests/features)"
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=None,
        help="Number of test files to run in parallel (default: CPU count; 1 = serial)",
    )
    args = parser.parse_args()

    try:
        jobs = resolve_jobs(args.jobs)
    except ValueError as exc:
        parser.error(str(exc))

    luma_exe = find_luma_exe(args.exe)
    tests_dir = resolve_dir(args.dir, "LUMA_TESTS_DIR", "tests/features")

    if not luma_exe.exists():
        print(f"Error: luma executable not found at {luma_exe}", file=sys.stderr)
        return 1
    if not tests_dir.is_dir():
        print(f"Error: tests directory not found at {tests_dir}", file=sys.stderr)
        return 1

    # Discover all .luma test files, sorted for deterministic order.
    test_files = sorted(tests_dir.rglob("*.luma"))

    if not test_files:
        print(f"No .luma test files found under {tests_dir}", file=sys.stderr)
        return 1

    passed = 0
    failed = 0

    if jobs == 1:
        # Serial escape hatch: stream each test's output live (inherited stdout)
        # exactly as before, which is the most useful mode for debugging a hang.
        for test_file in test_files:
            print(f"\u2500\u2500 {test_file.name} \u2500\u2500")
            result = subprocess.run(_build_command(luma_exe, test_file), cwd=str(REPO_ROOT))
            if result.returncode == 0:
                passed += 1
            else:
                failed += 1
    else:
        # Fan the per-file runs out across worker threads (subprocess.run releases
        # the GIL while each child runs). Output is captured per test and replayed
        # in the original sorted order, so the console stays deterministic.
        with ThreadPoolExecutor(max_workers=jobs) as executor:
            futures = [executor.submit(_run_test_captured, luma_exe, tf) for tf in test_files]
            for test_file, future in zip(test_files, futures, strict=True):
                returncode, output = future.result()
                _print_captured(test_file.name, output)
                if returncode == 0:
                    passed += 1
                else:
                    failed += 1

    total = passed + failed
    print()
    print("\u2500" * 22)
    print(f"Total: {total} | Passed: {passed} | Failed: {failed}")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
