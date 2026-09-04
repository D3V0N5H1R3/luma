#!/usr/bin/env python3
"""Shared infrastructure for the aggregate lint and format runners.

Defines the :class:`Gate` abstraction — a single external quality tool with a
name, a resolved command to run (or ``None`` when the tool is unavailable and
the gate should be skipped), and the directory to run it in — plus the driver
that runs a selection of gates and prints a summary. Imported by
``lint.py`` and ``format.py``; not run directly.

Gates are built eagerly by the caller: tool availability is resolved with
:func:`which` (which returns a full path so Windows ``.cmd`` shims are invoked
correctly), and file lists are discovered with :func:`git_ls`. A gate whose
command is ``None`` is reported as skipped, with a reason, rather than failing.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from dataclasses import dataclass
from enum import Enum
from pathlib import Path

from _common import REPO_ROOT, reconfigure_utf8_streams

_RULE_WIDTH = 72


class Status(Enum):
    """Outcome of running (or not running) a single gate."""

    PASSED = "passed"
    FAILED = "failed"
    SKIPPED = "skipped"


@dataclass
class Gate:
    """A single external quality tool wired into the aggregate runner.

    *argv* is the fully resolved command line (with an absolute executable
    path) or ``None`` when the underlying tool is unavailable, in which case
    *skip_reason* explains why and the gate is reported as skipped.

    When *files* is set it holds the file arguments to append to *argv*. The
    driver batches them across several invocations when the combined command
    line would be too long (see :func:`_batch_files`), so a file-list gate stays
    within the Windows ``CreateProcess`` limit instead of crashing on a big tree.
    """

    name: str
    description: str
    argv: list[str] | None
    cwd: Path = REPO_ROOT
    skip_reason: str = ""
    files: list[str] | None = None


def skip(name: str, description: str, reason: str) -> Gate:
    """Build a gate that will be reported as skipped with *reason*."""
    return Gate(name=name, description=description, argv=None, skip_reason=reason)


def npm_script_gate(
    name: str,
    description: str,
    *,
    script: str,
    project_dir: Path,
    missing_hint: str,
    extra_args: tuple[str, ...] = (),
) -> Gate:
    """Build a gate that runs an npm *script* in *project_dir*.

    Skipped when ``npm`` is absent or the project's ``node_modules`` has not been
    installed. Any *extra_args* are forwarded to the underlying script after the
    npm ``--`` separator (for example ``--fix``).
    """
    npm = which("npm")
    if npm is None:
        return skip(name, description, "npm not found (install Node.js)")
    if not (project_dir / "node_modules").is_dir():
        return skip(name, description, missing_hint)
    argv = [npm, "run", script]
    if extra_args:
        argv += ["--", *extra_args]
    return Gate(name, description, argv, cwd=project_dir)


def which(tool: str) -> str | None:
    """Return the absolute path to *tool* on ``PATH``, or ``None`` if absent.

    Resolving to a full path (rather than passing the bare name to
    ``subprocess``) is what makes Windows ``npm.cmd`` / ``npx.cmd`` shims
    launch correctly, since ``subprocess`` does not consult ``PATHEXT`` for a
    bare command name.
    """
    return shutil.which(tool)


def git_ls(*patterns: str) -> list[str]:
    """Return tracked files matching the given git pathspecs, repo-relative.

    Runs ``git ls-files`` from :data:`REPO_ROOT`, so the returned paths are
    usable as arguments to a tool invoked with ``cwd=REPO_ROOT``.
    """
    result = subprocess.run(
        ["git", "ls-files", *patterns],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    return [line for line in result.stdout.splitlines() if line]


def _rule(label: str = "") -> str:
    """Return a box-drawing rule, optionally captioned with *label*."""
    if not label:
        return "─" * _RULE_WIDTH
    prefix = f"── {label} "
    return prefix + "─" * max(0, _RULE_WIDTH - len(prefix))


def _select(gates: list[Gate], only: str | None, skip_names: str | None) -> list[Gate]:
    """Filter *gates* by the ``--only`` / ``--skip`` selectors.

    Raises :class:`SystemExit` with a usage message when a selector names a
    gate that does not exist, so a typo fails loudly instead of silently
    running nothing.
    """
    known = {gate.name for gate in gates}

    def parse(value: str | None) -> set[str]:
        names = {item.strip() for item in value.split(",") if item.strip()}
        unknown = names - known
        if unknown:
            available = ", ".join(sorted(known))
            sys.exit(
                f"Error: unknown gate(s): {', '.join(sorted(unknown))}.\n"
                f"Available gates: {available}"
            )
        return names

    selected = gates
    if only:
        wanted = parse(only)
        selected = [gate for gate in selected if gate.name in wanted]
    if skip_names:
        unwanted = parse(skip_names)
        selected = [gate for gate in selected if gate.name not in unwanted]
    return selected


def _print_list(gates: list[Gate]) -> None:
    """Print the available gates, their status, and what each one does."""
    width = max((len(gate.name) for gate in gates), default=0)
    print(_rule("gates"))
    for gate in gates:
        state = "available" if gate.argv is not None else f"skip: {gate.skip_reason}"
        print(f"  {gate.name:<{width}}  {gate.description}")
        print(f"  {'':<{width}}  [{state}]")
    print(_rule())


# Conservative budget for a single command line, kept well under the Windows
# CreateProcess hard limit of 32,767 characters. File-list gates (clang-format,
# clang-tidy, cmakelint, shellcheck) can exceed that on a large tree,
# so their file arguments are split into batches — the same way CI drives the
# tools through `find … -exec … {} +`.
_MAX_CMDLINE = 30_000


def _batch_files(base_argv: list[str], files: list[str]) -> list[list[str]]:
    """Split *files* across *base_argv* invocations that respect the cmdline budget.

    Each returned command is ``base_argv`` followed by as many files as fit
    under :data:`_MAX_CMDLINE`. At least one command is always returned, and a
    single file whose own length exceeds the budget still gets its own command
    rather than being dropped.
    """
    base_len = len(subprocess.list2cmdline(base_argv))
    commands: list[list[str]] = []
    current: list[str] = []
    current_len = base_len
    for file in files:
        addition = len(subprocess.list2cmdline([file])) + 1  # +1 for the separating space
        if current and current_len + addition > _MAX_CMDLINE:
            commands.append([*base_argv, *current])
            current = []
            current_len = base_len
        current.append(file)
        current_len += addition
    commands.append([*base_argv, *current])
    return commands


def _plan_commands(gate: Gate) -> list[list[str]]:
    """Return the concrete command(s) for *gate*, batching its files if it lists any."""
    assert gate.argv is not None  # callers guard against the skipped (argv is None) case
    if gate.files is None:
        return [gate.argv]
    return _batch_files(gate.argv, gate.files)


def _echo(gate: Gate, command: list[str], index: int, total: int, location: str) -> None:
    """Print the ``$ …`` command line, abbreviating a long batched file list."""
    if gate.files is None:
        print(f"$ {' '.join(command)}{location}", flush=True)
        return
    assert gate.argv is not None
    file_count = len(command) - len(gate.argv)
    batch = f" [batch {index}/{total}]" if total > 1 else ""
    print(f"$ {' '.join(gate.argv)} … ({file_count} files){batch}{location}", flush=True)


def _run_gate(gate: Gate, location: str) -> Status:
    """Run a gate's command(s) in order; return FAILED if any invocation fails.

    A tool that cannot be launched at all (an :class:`OSError`, such as a command
    line still too long for the platform) is recorded as a failure rather than
    being allowed to abort the whole run.
    """
    commands = _plan_commands(gate)
    status = Status.PASSED
    for index, command in enumerate(commands, start=1):
        _echo(gate, command, index, len(commands), location)
        try:
            completed = subprocess.run(command, cwd=gate.cwd, check=False)
        except OSError as error:
            print(f"  could not run {gate.name}: {error}")
            status = Status.FAILED
            continue
        if completed.returncode != 0:
            status = Status.FAILED
    return status


def _run(gates: list[Gate]) -> int:
    """Run each gate in order, stream its output, and print a summary.

    Returns ``0`` when every gate passed or was skipped, and ``1`` when at
    least one gate failed. Gates are never aborted early: the whole selection
    runs so the summary reports every result in one pass.
    """
    results: list[tuple[Gate, Status]] = []

    for gate in gates:
        if gate.argv is None:
            print(f"{_rule(gate.name)}\n  skipped — {gate.skip_reason}\n")
            results.append((gate, Status.SKIPPED))
            continue

        location = "" if gate.cwd == REPO_ROOT else f" (in {gate.cwd.relative_to(REPO_ROOT)})"
        print(_rule(gate.name))
        results.append((gate, _run_gate(gate, location)))
        print()

    return _summarise(results)


def _summarise(results: list[tuple[Gate, Status]]) -> int:
    """Print the per-gate summary table and return the process exit code."""
    marks = {Status.PASSED: "✔", Status.FAILED: "✘", Status.SKIPPED: "·"}
    width = max((len(gate.name) for gate, _ in results), default=0)

    print(_rule("summary"))
    for gate, status in results:
        print(f"  {marks[status]} {gate.name:<{width}}  {status.value}")

    passed = sum(1 for _, status in results if status is Status.PASSED)
    failed = sum(1 for _, status in results if status is Status.FAILED)
    skipped = sum(1 for _, status in results if status is Status.SKIPPED)
    print(_rule())
    print(f"  {passed} passed, {failed} failed, {skipped} skipped")

    if failed:
        names = ", ".join(gate.name for gate, status in results if status is Status.FAILED)
        print(f"  failed: {names}")
        return 1
    return 0


def run_cli(gates: list[Gate], *, prog: str, description: str) -> int:
    """Parse shared arguments, run the selected gates, and return an exit code.

    Provides the common ``--list`` / ``--only`` / ``--skip`` interface used by
    both ``lint.py`` and ``format.py`` so the two scripts stay in lockstep.
    """
    reconfigure_utf8_streams()

    parser = argparse.ArgumentParser(prog=prog, description=description)
    parser.add_argument(
        "--list",
        action="store_true",
        help="List the gates (and their availability) without running them.",
    )
    parser.add_argument(
        "--only",
        metavar="NAMES",
        help="Comma-separated gate names to run exclusively (e.g. --only ruff,shellcheck).",
    )
    parser.add_argument(
        "--skip",
        metavar="NAMES",
        help="Comma-separated gate names to skip (e.g. --skip clang-tidy).",
    )
    args = parser.parse_args()

    if args.list:
        _print_list(gates)
        return 0

    return _run(_select(gates, args.only, args.skip))
