#!/usr/bin/env python3
"""Execute and verify every Luma example program.

Unlike ``run_luma_tests.py`` (which runs ``@test`` annotations), this script
actually *runs* each example end to end and checks that it completes
successfully — including examples that would normally require user input:

* **Non-interactive** examples are run directly and must exit 0.
* **Console** examples (``guess_the_number``, ``calculator``, ``todo_list``)
  are driven with scripted stdin. Their input is closed afterwards, so prompts
  that fail on EOF make the program quit cleanly.
* **Terminal raw-mode** examples (``process_and_terminal``, ``mouse_draw``,
  ``editor``) are driven through the headless Terminal harness: scripted keys
  are fed to ``read_key`` / ``get_input`` via ``LUMA_TERMINAL_INPUT`` (one key
  per line), so the real input loop runs unattended and exercises its logic.
* **GraphicalUi** examples (``gui_*`` and ``solaris_*``) are run with ``LUMA_GUI_HEADLESS=1`` so
  the interpreter executes the full init / view / subscribe lifecycle without
  opening a window. They must exit 0 and print ``initial render OK``.
* Any example that declares ``@test`` functions is *additionally* run with
  ``luma --test`` so its embedded assertions are verified — not just that
  ``@main`` completes. GUI examples drive their UI through the headless
  ``GraphicalUi.test_*`` interaction API, so button clicks and input are
  simulated and the resulting model is asserted on.
* A couple of examples are intentionally skipped (documented below).

Usage:
    python scripts/run_examples.py [--exe <luma-exe>] [--dir <examples-dir>]
                                   [--filter <substring>] [--list] [--verbose]
                                   [--jobs <n>]

Environment variables:
    LUMA_EXE          - Path to the luma executable (fallback).
    LUMA_EXAMPLES_DIR - Path to the examples directory (fallback).
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

from _common import (
    REPO_ROOT,
    find_luma_exe,
    reconfigure_utf8_streams,
    resolve_dir,
    resolve_jobs,
)

# ── Categories ────────────────────────────────────────────────────────
RUN = "run"  # Run directly, expect exit 0.
STDIN = "stdin"  # Feed scripted stdin, expect exit 0.
GUI = "gui"  # Run headless (LUMA_GUI_HEADLESS=1), expect exit 0 + render marker.
TERMINAL = "terminal"  # Drive a raw-mode TUI via LUMA_TERMINAL_INPUT, expect exit 0.
SKIP = "skip"  # Do not run; report why.

# Marker printed by the headless GraphicalUi lifecycle.
GUI_MARKER = "initial render OK"


@dataclass(frozen=True)
class ExampleSpec:
    """How a single example should be executed and verified."""

    category: str = RUN
    stdin: str = ""
    args: tuple[str, ...] = ()
    env: dict[str, str] = field(default_factory=dict)
    timeout: float = 45.0
    expect_substring: str | None = None
    expect_returncode: int = 0
    reason: str = ""  # For skipped examples.


# Per-example overrides keyed by file basename. Anything not listed here uses
# the default ExampleSpec() (run directly, expect exit 0), except gui_* and
# solaris_* files which are auto-classified as headless GUI runs.
OVERRIDES: dict[str, ExampleSpec] = {
    # ── Terminal raw-mode TUIs (driven via the headless Terminal harness) ──
    # LUMA_TERMINAL_INPUT feeds scripted keys (one per line) to
    # read_key / get_input, so the real input loop runs unattended. Each script
    # exercises some logic and then quits (or drains, which exits on EOF).
    "process_and_terminal.luma": ExampleSpec(
        category=TERMINAL,
        # get_input() ignores read failures, so it only exits on its Ctrl+C
        # break — the script must end with ctrl+c.
        env={"LUMA_TERMINAL_INPUT": "a\nshift+b\nctrl+c\n"},
        expect_substring="Key: Ctrl+c",
    ),
    "mouse_draw.luma": ExampleSpec(
        category=TERMINAL,
        # select a brush, draw with a mouse press, clear, then quit.
        env={"LUMA_TERMINAL_INPUT": "2\nmouse:left_press:5:10\nc\nq\n"},
        expect_substring="Goodbye!",
    ),
    "editor.luma": ExampleSpec(
        category=TERMINAL,
        # move, enter insert mode, type a char, escape, then :q! to force quit.
        env={"LUMA_TERMINAL_INPUT": "j\nl\ni\nX\nescape\n:\nq\n!\nenter\n"},
    ),
    # ── Intentionally skipped ────────────────────────────────────────
    "multi_file_utils.luma": ExampleSpec(
        category=SKIP,
        reason="include-only helper with no @main entry point",
    ),
    # ── Console-interactive (driven via scripted stdin) ──────────────
    "guess_the_number.luma": ExampleSpec(
        category=STDIN,
        stdin="1\n2\n3\n4\n5\n6\n7\nn\n",
    ),
    "calculator.luma": ExampleSpec(
        category=STDIN,
        stdin="2 + 3\nsqrt 144\npi * 2\nquit\n",
    ),
    "todo_list.luma": ExampleSpec(
        category=STDIN,
        stdin="list\nadd Buy milk\ndone 1\nlist\nquit\n",
    ),
    # ── Heavier / debugger demos: allow more time ────────────────────
    "long_loop.luma": ExampleSpec(timeout=90.0),
    # ── By-design unhandled exception (debugger demo): exits non-zero ─
    "exception_unhandled.luma": ExampleSpec(
        expect_returncode=1,
        expect_substring="division by zero",
    ),
}


def spec_for(path: Path) -> ExampleSpec:
    """Return the execution spec for an example file."""
    name = path.name
    if name in OVERRIDES:
        return OVERRIDES[name]
    if name.startswith(("gui_", "solaris_")):
        return ExampleSpec(
            category=GUI,
            env={"LUMA_GUI_HEADLESS": "1"},
            expect_substring=GUI_MARKER,
        )
    return ExampleSpec()


def file_has_tests(path: Path) -> bool:
    """True if the example declares any ``@test`` functions.

    Only a line whose first non-whitespace token is ``@test`` counts; mentions
    of ``@test`` inside ``#`` comments or prose are ignored.
    """
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False
    return any(line.lstrip().startswith("@test") for line in text.splitlines())


def _combined_output(completed: subprocess.CompletedProcess | subprocess.TimeoutExpired) -> str:
    """Join captured stdout and stderr, tolerating either stream being ``None``.

    Works for both a finished ``CompletedProcess`` and a ``TimeoutExpired``,
    which expose the same ``stdout``/``stderr`` attributes.
    """
    return (completed.stdout or "") + (completed.stderr or "")


def run_test_blocks(
    exe: Path, path: Path, env: dict[str, str], timeout: float
) -> tuple[bool, str, str]:
    """Run an example's ``@test`` blocks via ``luma --test``.

    Returns ``(ok, detail, output)``. ``@main`` is not executed in this mode, so
    examples that would otherwise open a window or start a server only run their
    assertions here.
    """
    try:
        proc = subprocess.run(
            [str(exe), "--test", str(path)],
            capture_output=True,
            cwd=str(REPO_ROOT),
            env=env,
            timeout=timeout,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except subprocess.TimeoutExpired as exc:
        return False, f"@test timed out after {timeout:.0f}s", _combined_output(exc)

    output = _combined_output(proc)
    if proc.returncode != 0:
        return False, f"@test failed (exit {proc.returncode})", output
    return True, "", output


@dataclass
class Result:
    path: Path
    spec: ExampleSpec
    status: str  # "pass", "fail", "skip"
    detail: str = ""
    duration: float = 0.0
    output: str = ""


def run_example(exe: Path, path: Path, spec: ExampleSpec) -> Result:
    """Execute a single example and classify the outcome."""
    if spec.category == SKIP:
        return Result(path, spec, "skip", spec.reason)

    env = os.environ.copy()
    env.update(spec.env)

    started = time.perf_counter()
    try:
        proc = subprocess.run(
            [str(exe), str(path), *spec.args],
            input=spec.stdin,
            capture_output=True,
            cwd=str(REPO_ROOT),
            env=env,
            timeout=spec.timeout,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except subprocess.TimeoutExpired as exc:
        duration = time.perf_counter() - started
        return Result(
            path,
            spec,
            "fail",
            f"timed out after {spec.timeout:.0f}s",
            duration,
            _combined_output(exc),
        )

    duration = time.perf_counter() - started
    output = _combined_output(proc)

    if proc.returncode != spec.expect_returncode:
        expected = "0" if spec.expect_returncode == 0 else f"{spec.expect_returncode} (expected)"
        return Result(
            path,
            spec,
            "fail",
            f"exit code {proc.returncode}, wanted {expected}",
            duration,
            output,
        )

    if spec.expect_substring and spec.expect_substring not in output:
        return Result(
            path,
            spec,
            "fail",
            f"missing expected output {spec.expect_substring!r}",
            duration,
            output,
        )

    # The normal run only proves @main completes. If the example embeds @test
    # blocks, run them too (via `luma --test`) so its logic is actually verified.
    if file_has_tests(path):
        ok, detail, test_output = run_test_blocks(exe, path, env, spec.timeout)
        duration = time.perf_counter() - started
        if not ok:
            return Result(path, spec, "fail", detail, duration, test_output)
        return Result(path, spec, "pass", f"{spec.category}+@test", duration, output)

    return Result(path, spec, "pass", spec.category, duration, output)


def render_result(res: Result, rel: str, verbose: bool) -> None:
    """Print a single example's outcome line and any failure or verbose detail."""
    if res.status == "pass":
        icon = "\u2713"  # check mark
    elif res.status == "skip":
        icon = "\u2298"  # circled division slash
    else:
        icon = "\u2717"  # ballot x

    timing = f"{res.duration:5.1f}s" if res.status != "skip" else "  --  "
    print(f"{icon} {timing}  [{res.spec.category:5}] {rel}")
    if res.status == "skip":
        print(f"           skipped: {res.detail}")
    elif res.status == "pass" and "@test" in res.detail:
        print("           +@test blocks passed")
    elif res.status == "fail":
        print(f"           FAILED: {res.detail}")
        tail = "\n".join(res.output.strip().splitlines()[-25:])
        if tail:
            print("           ── output (tail) ──")
            for line in tail.splitlines():
                print(f"           | {line}")
    if verbose and res.output.strip():
        for line in res.output.strip().splitlines():
            print(f"           : {line}")


def main() -> int:
    # The report uses box-drawing characters and examples emit UTF-8; make sure
    # our own stdout/stderr can render them regardless of the host code page.
    reconfigure_utf8_streams()

    parser = argparse.ArgumentParser(description="Execute and verify all Luma examples.")
    parser.add_argument("--exe", default=None, help="Path to the luma executable")
    parser.add_argument("--dir", default=None, help="Path to the examples directory")
    parser.add_argument(
        "--filter", default=None, help="Only run examples whose path contains this substring"
    )
    parser.add_argument(
        "--list", action="store_true", help="List examples and their categories, then exit"
    )
    parser.add_argument(
        "--verbose", action="store_true", help="Print captured output for every example"
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=None,
        help="Number of examples to run in parallel (default: CPU count; 1 = serial)",
    )
    args = parser.parse_args()

    exe = find_luma_exe(args.exe)
    examples_dir = resolve_dir(args.dir, "LUMA_EXAMPLES_DIR", "examples")

    if not examples_dir.is_dir():
        print(f"Error: examples directory not found at {examples_dir}", file=sys.stderr)
        return 1

    files = sorted(examples_dir.rglob("*.luma"))
    if args.filter:
        files = [f for f in files if args.filter in f.as_posix()]

    if not files:
        print(f"No .luma examples found under {examples_dir}", file=sys.stderr)
        return 1

    if args.list:
        for f in files:
            spec = spec_for(f)
            rel = f.relative_to(examples_dir).as_posix()
            note = f" — {spec.reason}" if spec.reason else ""
            print(f"{spec.category:5}  {rel}{note}")
        return 0

    if not exe.exists():
        print(f"Error: luma executable not found at {exe}", file=sys.stderr)
        return 1

    print(f"luma:     {exe}")
    print(f"examples: {examples_dir}")
    print(f"count:    {len(files)}")
    print("─" * 60)

    try:
        jobs = resolve_jobs(args.jobs)
    except ValueError as exc:
        parser.error(str(exc))

    results: list[Result] = []

    def record(res: Result) -> None:
        results.append(res)
        render_result(res, res.path.relative_to(examples_dir).as_posix(), args.verbose)

    if jobs == 1:
        for f in files:
            record(run_example(exe, f, spec_for(f)))
    else:
        # run_example is pure and captures all output into its Result, so the
        # runs fan out across worker threads (subprocess.run releases the GIL
        # while each child runs) and their Results are rendered back in the
        # original sorted order for deterministic console output.
        with ThreadPoolExecutor(max_workers=jobs) as executor:
            futures = [executor.submit(run_example, exe, f, spec_for(f)) for f in files]
            for future in futures:
                record(future.result())

    passed = sum(1 for r in results if r.status == "pass")
    failed = sum(1 for r in results if r.status == "fail")
    skipped = sum(1 for r in results if r.status == "skip")

    print("─" * 60)
    print(f"Total: {len(results)} | Passed: {passed} | Failed: {failed} | Skipped: {skipped}")

    if failed:
        print("\nFailed examples:")
        for r in results:
            if r.status == "fail":
                print(f"  - {r.path.relative_to(examples_dir).as_posix()}: {r.detail}")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
