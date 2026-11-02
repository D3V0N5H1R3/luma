#!/usr/bin/env python3
"""Unit tests for the Luma prompt pipeline (``luma_pipeline.py`` / ``pipeline_core.py``).

Pins the gate and agent result contracts with fake ``cmake`` / ``ctest`` /
``python`` / agent tools: each fake prints output and exits with a chosen code,
and the tests assert the exact value the helper returns. Also covers the pure
helpers (argument vectors, JSONL reduction, the abort rule), process-tree
cleanup on errors, timeouts, Ctrl+C, and termination signals, the clang-tidy
compile-database refresh, Visual Studio discovery, the git checkpoint, revert,
and artifact-protection helpers in scratch repositories, end-to-end ``fix``
runs, and the CLI's dry-run path, which must never spawn a process.

No real agent CLI, build, or test run is ever invoked.

Run from the repository root:
    python -m unittest discover -s scripts/pipeline -p "test_*.py"
"""

from __future__ import annotations

import _thread
import contextlib
import ctypes
import io
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import textwrap
import threading
import time
import unittest
from collections.abc import Callable, Sequence
from pathlib import Path
from unittest import mock

import luma_pipeline
import pipeline_core
from pipeline_core import (
    ARTIFACT_BACKUP_NAME,
    CO_AUTHOR_TRAILERS,
    CheckpointError,
    Console,
    Pipeline,
    PipelineError,
    Status,
    TerminationRequested,
)

HAS_GIT = shutil.which("git") is not None
IS_WINDOWS = os.name == "nt"
_PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
_STILL_ACTIVE = 259


def make_fake_tool(
    directory: Path,
    name: str,
    *,
    exit_code: int = 0,
    lines: Sequence[str] = (),
    read_stdin: bool = False,
    sleep_seconds: float = 0,
    record_file: Path | None = None,
    append_to: str | None = None,
) -> Path:
    """Write an executable stand-in for *name* into *directory*.

    The fake optionally reads stdin, sleeps, appends ``{"args", "stdin",
    "marker"}`` as a JSON line to *record_file* (``marker`` is the value of
    ``LUMA_TEST_MARKER`` in its environment), appends a line to the file
    *append_to* (relative to its working directory), prints *lines*, and exits
    with *exit_code*.
    """
    body = f"""\
        import json
        import os
        import sys
        import time

        stdin_text = sys.stdin.read() if {read_stdin!r} else ""
        time.sleep({sleep_seconds!r})
        record = {str(record_file) if record_file else None!r}
        if record:
            entry = {{
                "args": sys.argv[1:],
                "stdin": stdin_text,
                "marker": os.environ.get("LUMA_TEST_MARKER"),
            }}
            with open(record, "a", encoding="utf-8") as handle:
                handle.write(json.dumps(entry) + "\\n")
        target = {append_to!r}
        if target:
            with open(target, "a", encoding="utf-8") as handle:
                handle.write("changed by the fake agent\\n")
        for line in {list(lines)!r}:
            print(line, flush=True)
        sys.exit({exit_code!r})
        """
    return make_launcher(directory, name, body)


def make_launcher(directory: Path, name: str, body: str) -> Path:
    """Write the Python *body* and an executable launcher for it named *name*.

    The launcher is a ``.cmd`` shim on Windows — exercising the same launch path
    as an npm-installed agent CLI — and a ``/bin/sh`` script elsewhere.
    """
    script = directory / f"{name}_impl.py"
    script.write_text(textwrap.dedent(body), encoding="utf-8")
    if os.name == "nt":
        wrapper = directory / f"{name}.cmd"
        wrapper.write_text(
            f'@"{sys.executable}" "{script}" %*\r\n@exit /b %ERRORLEVEL%\r\n', encoding="utf-8"
        )
    else:
        wrapper = directory / name
        wrapper.write_text(
            f'#!/bin/sh\nexec "{sys.executable}" "{script}" "$@"\n', encoding="utf-8"
        )
        wrapper.chmod(0o755)
    return wrapper


def read_calls(record_file: Path) -> list[dict[str, object]]:
    """Return the invocations a fake tool recorded, oldest first."""
    if not record_file.is_file():
        return []
    return [json.loads(line) for line in record_file.read_text(encoding="utf-8").splitlines()]


def make_tree_agent(
    directory: Path, name: str, pid_file: Path, *, should_exit_early: bool = False
) -> Path:
    """Write a fake agent that starts a long-running child of its own.

    The child's PID goes to *pid_file*, and the child inherits the agent's
    output pipe. The agent then sleeps for a minute, or exits at once when
    *should_exit_early*, leaving the child holding the pipe open.
    """
    return make_launcher(
        directory,
        name,
        f"""\
        import subprocess
        import sys
        import time

        child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        with open({str(pid_file)!r}, "w", encoding="utf-8") as handle:
            handle.write(str(child.pid))
        print("child started", flush=True)
        if not {should_exit_early!r}:
            time.sleep(60)
        """,
    )


def read_pid(pid_file: Path) -> int | None:
    """Return the PID a tree agent recorded, or None while it is not written yet."""
    try:
        return int(pid_file.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return None


def interrupt_when_started(pid_file: Path, signum: int = signal.SIGINT) -> threading.Thread:
    """Deliver *signum* to the main thread once a tree agent has written its PID.

    ``_thread.interrupt_main`` runs the main thread's Python handler for the
    signal, which works the same on every platform. Nothing is sent when the
    agent never starts within 30 seconds.
    """

    def wait_then_interrupt() -> None:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if read_pid(pid_file) is not None:
                _thread.interrupt_main(signum)
                return
            time.sleep(0.05)

    thread = threading.Thread(target=wait_then_interrupt, daemon=True)
    thread.start()
    return thread


def forbid_spawning() -> contextlib.AbstractContextManager[object]:
    """Patch ``subprocess.Popen`` (which ``subprocess.run`` uses) to fail loudly."""
    return mock.patch.object(
        subprocess, "Popen", side_effect=AssertionError("a dry run spawned a process")
    )


def is_process_alive(pid: int) -> bool:
    """True while the process *pid* is running (a reaped zombie counts as gone)."""
    if os.name == "nt":
        kernel32 = ctypes.windll.kernel32  # type: ignore[attr-defined]
        handle = kernel32.OpenProcess(_PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not handle:
            return False
        try:
            exit_code = ctypes.c_ulong()
            kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code))
            return exit_code.value == _STILL_ACTIVE
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def wait_for_exit(pid: int, *, timeout: float) -> bool:
    """Poll until *pid* is gone; True when it exited within *timeout* seconds."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not is_process_alive(pid):
            return True
        time.sleep(0.1)
    return not is_process_alive(pid)


def _kill_if_alive(pid: int) -> None:
    """Best-effort cleanup for a process a failing test may have leaked."""
    if not is_process_alive(pid):
        return
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True, check=False)
    else:
        with contextlib.suppress(OSError):
            os.kill(pid, signal.SIGKILL)


class FakeToolTestCase(unittest.TestCase):
    """Scratch directory, fake tools first on PATH, and a capturing console."""

    def setUp(self) -> None:
        scratch = tempfile.TemporaryDirectory(
            prefix="luma-pipeline-test-", ignore_cleanup_errors=True
        )
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name).resolve()
        self.tool_dir = self.root / "bin"
        self.tool_dir.mkdir()
        self.repo_root = self.root / "repo"
        self.repo_root.mkdir()
        self.calls = self.root / "calls.jsonl"

        path = f"{self.tool_dir}{os.pathsep}{os.environ.get('PATH', '')}"
        environment = mock.patch.dict(os.environ, {"PATH": path})
        environment.start()
        self.addCleanup(environment.stop)
        for variable in ("LUMA_COPILOT", "LUMA_CLAUDE"):
            os.environ.pop(variable, None)

        self.out = io.StringIO()
        self.err = io.StringIO()
        self.console = Console(out=self.out, err=self.err, use_colour=False)
        self.pipeline = self.make_pipeline()

    def make_pipeline(self, **kwargs: object) -> Pipeline:
        """A pipeline over the scratch repository, snapshotting the current environment."""
        return Pipeline(self.repo_root, console=self.console, **kwargs)  # type: ignore[arg-type]

    def fake_tool(self, name: str, **kwargs: object) -> Path:
        return make_fake_tool(self.tool_dir, name, **kwargs)  # type: ignore[arg-type]

    def assert_child_was_killed(self, pid_file: Path) -> None:
        """Assert the child a tree agent started is gone (and never leak it)."""
        pid = read_pid(pid_file)
        self.assertIsNotNone(pid, "the fake agent never started its child")
        assert pid is not None
        self.addCleanup(_kill_if_alive, pid)
        self.assertTrue(
            wait_for_exit(pid, timeout=10), "the agent's child process was left running"
        )


# ── Gate contracts ────────────────────────────────────────────────


class BuildAndTestTests(FakeToolTestCase):
    def test_returns_false_when_ctest_fails_after_printing_output(self) -> None:
        self.fake_tool("cmake", lines=["cmake build output"])
        self.fake_tool("ctest", exit_code=1, lines=["1 test failed"])

        self.assertIs(self.pipeline.build_and_test(), False)
        self.assertIn("Tests failed.", self.err.getvalue())

    def test_returns_false_when_the_build_fails_after_printing_output(self) -> None:
        self.fake_tool("cmake", exit_code=2, lines=["compile error"])
        self.fake_tool("ctest", lines=["all tests passed"], record_file=self.calls)

        self.assertIs(self.pipeline.build_and_test(), False)
        self.assertEqual(read_calls(self.calls), [], "ctest must not run after a failed build")

    def test_returns_true_when_build_and_tests_pass_after_printing_output(self) -> None:
        self.fake_tool("cmake", lines=["cmake build output"], record_file=self.calls)
        self.fake_tool("ctest", lines=["all tests passed"], record_file=self.calls)

        self.assertIs(self.pipeline.build_and_test(), True)
        self.assertEqual(
            [call["args"] for call in read_calls(self.calls)],
            [["--preset", "default"], ["--build", "--preset", "default"], ["--preset", "default"]],
        )

    def test_configures_before_every_build_even_when_a_build_tree_exists(self) -> None:
        # The Visual Studio generator never re-runs CMake by itself, so a gate that
        # skipped configure for an existing tree built stale project files.
        (self.repo_root / "build").mkdir()
        (self.repo_root / "build" / "CMakeCache.txt").write_text("", encoding="utf-8")
        self.fake_tool("cmake", record_file=self.calls)
        self.fake_tool("ctest")

        self.assertIs(self.pipeline.build_and_test(preset="debug", skip_test=True), True)
        self.assertIs(self.pipeline.build_and_test(preset="debug", skip_test=True), True)
        self.assertEqual(
            [call["args"] for call in read_calls(self.calls)],
            [["--preset", "debug"], ["--build", "--preset", "debug"]] * 2,
        )

    def test_skipping_the_build_skips_the_configure_too(self) -> None:
        self.fake_tool("cmake", record_file=self.calls)
        self.fake_tool("ctest", record_file=self.calls)

        self.assertIs(self.pipeline.build_and_test(skip_build=True), True)
        self.assertEqual(
            [call["args"] for call in read_calls(self.calls)], [["--preset", "default"]]
        )

    def test_tools_run_with_the_pipeline_environment_not_the_process_one(self) -> None:
        self.fake_tool("cmake", record_file=self.calls)
        self.fake_tool("ctest", record_file=self.calls)
        environment = {**os.environ, "LUMA_TEST_MARKER": "pipeline-only"}
        pipeline = self.make_pipeline(env=environment)

        self.assertIs(pipeline.build_and_test(), True)
        self.assertEqual({call["marker"] for call in read_calls(self.calls)}, {"pipeline-only"})
        self.assertNotIn("LUMA_TEST_MARKER", os.environ)

    def test_dry_run_prints_the_gate_and_spawns_nothing(self) -> None:
        pipeline = Pipeline(self.repo_root, is_dry_run=True, console=self.console)
        with forbid_spawning():
            self.assertIs(pipeline.build_and_test(), True)
        self.assertIn("[dry-run] cmake --preset default", self.out.getvalue())
        self.assertIn("[dry-run] cmake --build --preset default", self.out.getvalue())
        self.assertIn("[dry-run] ctest --preset default", self.out.getvalue())


class LintAndFormatTests(FakeToolTestCase):
    def setUp(self) -> None:
        super().setUp()
        # Keep the gate hermetic: no MSVC import, no compile-database configure.
        for name in ("_import_msvc_environment", "_ensure_compile_database"):
            patcher = mock.patch.object(Pipeline, name, return_value=True)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_returns_false_when_lint_fails_after_printing_output(self) -> None:
        python = self.fake_tool("luma-fake-python", exit_code=1, lines=["lint finding"])
        self.pipeline.python_executable = str(python)

        self.assertIs(self.pipeline.lint_and_format(check_only=True), False)

    def test_returns_true_when_format_and_lint_pass_after_printing_output(self) -> None:
        python = self.fake_tool(
            "luma-fake-python", lines=["all checks passed"], record_file=self.calls
        )
        self.pipeline.python_executable = str(python)

        self.assertIs(self.pipeline.lint_and_format(), True)
        self.assertEqual(
            [call["args"] for call in read_calls(self.calls)],
            [["scripts/format.py"], ["scripts/lint.py"]],
        )

    def test_check_only_never_runs_the_formatter(self) -> None:
        python = self.fake_tool("luma-fake-python", record_file=self.calls)
        self.pipeline.python_executable = str(python)

        self.assertIs(self.pipeline.lint_and_format(check_only=True), True)
        self.assertEqual([call["args"] for call in read_calls(self.calls)], [["scripts/lint.py"]])


class CompileDatabaseTests(FakeToolTestCase):
    """``build/compile_commands.json`` is regenerated when missing or stale."""

    def setUp(self) -> None:
        super().setUp()
        self.fake_tool("clang-tidy")
        # A fake cmake that writes a compile database into its -B directory.
        make_launcher(
            self.tool_dir,
            "cmake",
            f"""\
            import json
            import pathlib
            import sys

            with open({str(self.calls)!r}, "a", encoding="utf-8") as handle:
                handle.write(json.dumps({{"args": sys.argv[1:]}}) + "\\n")
            build_dir = pathlib.Path(sys.argv[sys.argv.index("-B") + 1])
            build_dir.mkdir(parents=True, exist_ok=True)
            (build_dir / "compile_commands.json").write_text("[]", encoding="utf-8")
            """,
        )
        self.build_db = self.repo_root / "build" / "compile_commands.json"
        self.build_db.parent.mkdir()
        self.cmake_lists = self.repo_root / "CMakeLists.txt"
        self.cmake_lists.write_text("project(test)\n", encoding="utf-8")
        self.cache = self.repo_root / "build" / "CMakeCache.txt"

    def write_database(self, *, modified: float) -> None:
        self.build_db.write_text("old", encoding="utf-8")
        os.utime(self.build_db, (modified, modified))

    def touch(self, path: Path, *, modified: float, text: str = "") -> None:
        if not path.exists() or text:
            path.write_text(text, encoding="utf-8")
        os.utime(path, (modified, modified))

    def configure_runs(self) -> int:
        return len(read_calls(self.calls))

    def test_generates_a_missing_database(self) -> None:
        self.pipeline._ensure_compile_database()

        self.assertEqual(self.configure_runs(), 1)
        self.assertEqual(self.build_db.read_text(encoding="utf-8"), "[]")

    def test_keeps_a_database_newer_than_every_input(self) -> None:
        self.touch(self.cmake_lists, modified=1000)
        self.touch(self.cache, modified=1000)
        self.write_database(modified=2000)

        self.pipeline._ensure_compile_database()

        self.assertEqual(self.configure_runs(), 0)
        self.assertEqual(self.build_db.read_text(encoding="utf-8"), "old")

    def test_refreshes_a_database_older_than_a_cmake_lists_file(self) -> None:
        # A phase added a source to CMakeLists.txt; clang-tidy must see it.
        self.write_database(modified=1000)
        self.touch(self.cmake_lists, modified=2000)

        self.pipeline._ensure_compile_database()

        self.assertEqual(self.configure_runs(), 1)
        self.assertEqual(self.build_db.read_text(encoding="utf-8"), "[]")

    def test_refreshes_a_database_older_than_the_last_configure(self) -> None:
        self.touch(self.cmake_lists, modified=1000)
        self.write_database(modified=2000)
        self.touch(
            self.cache, modified=3000, text="CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\n"
        )

        self.pipeline._ensure_compile_database()

        self.assertEqual(self.configure_runs(), 1)

    def maintained_cache(self, *, modified: float) -> None:
        self.touch(
            self.cache,
            modified=modified,
            text="CMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON\nCMAKE_GENERATOR:INTERNAL=Unix Makefiles\n",
        )

    def test_trusts_a_maintained_database_after_the_gate_configured_build(self) -> None:
        # CMake rewrites the database only when its content changes, so its
        # timestamp may predate the configure that refreshed it.
        self.touch(self.cmake_lists, modified=1000)
        self.write_database(modified=1000)
        self.maintained_cache(modified=3000)
        self.pipeline._configured_build_cache_mtime = 3000

        self.pipeline._ensure_compile_database()

        self.assertEqual(self.configure_runs(), 0)

    def test_reconfigures_a_maintained_build_the_gate_did_not_configure(self) -> None:
        # A gate on another preset (build-debug/, …) or with --skip-build never
        # touched build/, whose database may predate a phase's new source.
        self.touch(self.cmake_lists, modified=1000)
        self.write_database(modified=1000)
        self.maintained_cache(modified=3000)

        self.pipeline._ensure_compile_database()

        calls = read_calls(self.calls)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0]["args"][-2:], ["-B", str(self.repo_root / "build")])
        self.assertNotIn("-G", calls[0]["args"], "build/ keeps its own generator")

        # The in-place configure counts as the gate's own: no second refresh.
        self.pipeline._ensure_compile_database()
        self.assertEqual(self.configure_runs(), 1)

    def test_reconfigures_a_maintained_build_when_cmake_lists_changed_after_the_gate(
        self,
    ) -> None:
        self.write_database(modified=1000)
        self.maintained_cache(modified=3000)
        self.pipeline._configured_build_cache_mtime = 3000
        self.touch(self.cmake_lists, modified=4000)

        self.pipeline._ensure_compile_database()

        self.assertEqual(self.configure_runs(), 1)

    def test_gate_configure_of_build_marks_the_database_current(self) -> None:
        # A fake cmake whose configure rewrites build/CMakeCache.txt, as the
        # default preset (binaryDir build/) does.
        make_launcher(
            self.tool_dir,
            "cmake",
            f"""\
            import json
            import pathlib
            import sys

            with open({str(self.calls)!r}, "a", encoding="utf-8") as handle:
                handle.write(json.dumps({{"args": sys.argv[1:]}}) + "\\n")
            if sys.argv[1:2] == ["--preset"]:
                pathlib.Path({str(self.cache)!r}).write_text(
                    "CMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON\\n"
                    "CMAKE_GENERATOR:INTERNAL=Unix Makefiles\\n",
                    encoding="utf-8",
                )
            """,
        )
        self.fake_tool("ctest")
        self.touch(self.cmake_lists, modified=1000)
        self.write_database(modified=1000)
        self.maintained_cache(modified=2000)

        self.assertIs(self.pipeline.build_and_test(skip_test=True), True)
        self.pipeline._ensure_compile_database()

        self.assertEqual(
            [call["args"] for call in read_calls(self.calls)],
            [["--preset", "default"], ["--build", "--preset", "default"]],
        )

    def test_does_nothing_without_clang_tidy(self) -> None:
        with mock.patch.object(Pipeline, "_which", return_value=None):
            self.pipeline._ensure_compile_database()

        self.assertEqual(self.configure_runs(), 0)
        self.assertFalse(self.build_db.exists())


class VisualStudioDiscoveryTests(FakeToolTestCase):
    """vswhere lists every installation; each one is searched, newest first."""

    def setUp(self) -> None:
        super().setUp()
        program_files = self.root / "pf86"
        vswhere = program_files / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        vswhere.parent.mkdir(parents=True)
        vswhere.write_bytes(b"")
        self.newest = self.root / "vs-preview"
        self.older = self.root / "vs-2022"
        empty_path = self.root / "empty-path"
        empty_path.mkdir()
        self.pipeline = self.make_pipeline(
            env={"PATH": str(empty_path), "PROGRAMFILES(X86)": str(program_files)}
        )
        self.commands: list[list[str]] = []

        def fake_vswhere(_pipeline: Pipeline, args: Sequence[str]) -> object:
            self.commands.append(list(args))
            stdout = f"{self.newest}\n{self.older}\n"
            return subprocess.CompletedProcess(list(args), 0, stdout=stdout, stderr="")

        for patcher in (
            mock.patch.object(pipeline_core, "_IS_WINDOWS", True),
            mock.patch.object(Pipeline, "_capture", autospec=True, side_effect=fake_vswhere),
        ):
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_finds_the_bundled_cmake_in_an_older_installation(self) -> None:
        cmake = self.older.joinpath(*pipeline_core._VS_CMAKE_PARTS, "cmake.exe")
        cmake.parent.mkdir(parents=True)
        cmake.write_bytes(b"")

        self.assertEqual(self.pipeline._find_cmake(), str(cmake))
        self.assertTrue(self.pipeline._search_path.startswith(str(cmake.parent)))
        (command,) = self.commands
        for flag in ("-sort", "-prerelease", "-utf8"):
            self.assertIn(flag, command)
        self.assertNotIn("-latest", command)

    def test_finds_vcvars_in_an_older_installation(self) -> None:
        vcvars = self.older / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
        vcvars.parent.mkdir(parents=True)
        vcvars.write_bytes(b"")

        self.assertEqual(self.pipeline._find_vcvars(), vcvars)


@unittest.skipUnless(IS_WINDOWS, "vcvars runs only on Windows")
class MsvcEnvironmentTests(FakeToolTestCase):
    def test_imports_the_vcvars_environment_without_code_page_damage(self) -> None:
        # Characters outside every OEM code page: `set` output decoded as
        # "oem" turned them into '?'; the JSON round trip keeps them exact.
        self.fake_tool("clang-tidy")
        vcvars = self.root / "fake vcvars64.bat"
        vcvars.write_text("@set LUMA_VCVARS_TEST=from vcvars\r\n", encoding="ascii")
        system_root = os.environ["SYSTEMROOT"]
        pipeline = self.make_pipeline(
            env={
                "PATH": f"{self.tool_dir}{os.pathsep}{Path(system_root, 'System32')}",
                "SYSTEMROOT": system_root,
                "COMSPEC": os.environ.get("COMSPEC", str(Path(system_root, "System32", "cmd.exe"))),
                "LUMA_TEST_UNICODE": "café \u6f22\u5b57",
            }
        )

        with mock.patch.object(Pipeline, "_find_vcvars", return_value=vcvars):
            self.assertIs(pipeline._import_msvc_environment(), True)

        self.assertEqual(pipeline._env["LUMA_VCVARS_TEST"], "from vcvars")
        self.assertEqual(pipeline._env["LUMA_TEST_UNICODE"], "café \u6f22\u5b57")
        self.assertNotIn(pipeline_core._VCVARS_VARIABLE, pipeline._env)


class ConsoleTests(unittest.TestCase):
    def test_raw_output_keeps_a_utf8_sequence_split_across_chunks(self) -> None:
        out = io.StringIO()
        console = Console(out=out, use_colour=False)

        console.write_raw(b"caf\xc3")
        console.write_raw(b"\xa9!")
        console.end_raw()

        self.assertEqual(out.getvalue(), "café!")

    def test_a_truncated_sequence_at_the_end_prints_a_replacement_character(self) -> None:
        out = io.StringIO()
        console = Console(out=out, use_colour=False)

        console.write_raw(b"ok\xc3")
        console.end_raw()
        console.write_raw(b"next")

        self.assertEqual(out.getvalue(), "ok\ufffdnext")


class TerminationHandlerTests(unittest.TestCase):
    @staticmethod
    def wait_for_handler() -> None:
        """Give the main thread a few bytecode boundaries to run a pending handler."""
        for _ in range(20):
            time.sleep(0.05)

    def test_a_termination_signal_raises_and_the_old_handler_is_restored(self) -> None:
        before = signal.getsignal(signal.SIGTERM)

        with (
            self.assertRaises(TerminationRequested) as caught,
            pipeline_core.termination_handlers(),
        ):
            _thread.interrupt_main(signal.SIGTERM)
            self.wait_for_handler()

        self.assertEqual(caught.exception.exit_code, 128 + signal.SIGTERM)
        self.assertEqual(signal.getsignal(signal.SIGTERM), before)

    def test_a_second_signal_cannot_interrupt_the_cleanup(self) -> None:
        with pipeline_core.termination_handlers():
            with self.assertRaises(TerminationRequested):
                _thread.interrupt_main(signal.SIGTERM)
                self.wait_for_handler()
            # Cleanup is running now; a repeated signal is ignored.
            _thread.interrupt_main(signal.SIGTERM)
            self.wait_for_handler()


# A leader that starts a long-running child detached from its output, prints
# the child's PID, and exits.
_LEADER_SCRIPT = (
    "import subprocess, sys\n"
    "child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'],"
    " stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)\n"
    "print(child.pid, flush=True)\n"
)


class ProcessTreeTests(unittest.TestCase):
    """The tree kill reaches a grandchild whose parent already exited."""

    def start_leader(self) -> tuple[subprocess.Popen[bytes], object, int]:
        """Start the leader through the pipeline's launcher; return it, its job, and the child."""
        leader, job = pipeline_core._start_process(
            [sys.executable, "-c", _LEADER_SCRIPT], stdout=subprocess.PIPE
        )
        if job is not None:
            self.addCleanup(job.close)
        assert leader.stdout is not None
        with leader.stdout:
            grandchild = int(leader.stdout.readline())
        self.addCleanup(_kill_if_alive, grandchild)
        leader.wait(timeout=30)
        self.assertTrue(is_process_alive(grandchild))
        return leader, job, grandchild

    def test_kills_the_tree_even_after_its_leader_exited(self) -> None:
        leader, job, grandchild = self.start_leader()

        pipeline_core._kill_process_tree(leader, job)  # type: ignore[arg-type]

        self.assertTrue(wait_for_exit(grandchild, timeout=10), "the orphaned child survived")

    @unittest.skipUnless(IS_WINDOWS, "Job Objects are Windows-only")
    def test_every_child_starts_inside_a_job(self) -> None:
        # A silent fallback to taskkill would bring back the orphan gap.
        _, job, _ = self.start_leader()

        self.assertIsNotNone(job)

    @unittest.skipUnless(IS_WINDOWS, "Job Objects are Windows-only")
    def test_closing_the_job_kills_the_tree(self) -> None:
        # What the OS does when the pipeline itself dies without cleaning up.
        _, job, grandchild = self.start_leader()
        assert isinstance(job, pipeline_core._WindowsJob)

        job.close()

        self.assertTrue(wait_for_exit(grandchild, timeout=10), "the orphaned child survived")


class SupervisedRunTests(FakeToolTestCase):
    def test_a_child_left_running_without_the_output_survives_a_clean_exit(self) -> None:
        # Only a failure, interruption, or a held output pipe kills the tree;
        # a clean exit releases it on every platform.
        exit_code, output = self.pipeline._run_bounded(
            [sys.executable, "-c", _LEADER_SCRIPT], stdin_text="", timeout=30
        )

        self.assertEqual(exit_code, 0, output)
        grandchild = int(output.strip())
        self.addCleanup(_kill_if_alive, grandchild)
        time.sleep(0.5)
        self.assertTrue(is_process_alive(grandchild))


# ── Agent contracts ───────────────────────────────────────────────


class AgentModeTests(FakeToolTestCase):
    def setUp(self) -> None:
        super().setUp()
        os.environ["LUMA_COPILOT"] = "luma-fake-copilot"
        os.environ["LUMA_CLAUDE"] = "luma-fake-claude"
        self.log_file = self.root / "agent.log"

    def run_agent(self, agent: str = "copilot") -> pipeline_core.AgentOutcome:
        return self.pipeline.invoke_agent_phase(
            agent=agent, mode="agent", instruction="do the work", log_file=self.log_file
        )

    def test_returns_one_outcome_and_tees_the_transcript_for_a_zero_exit(self) -> None:
        self.fake_tool(
            "luma-fake-copilot",
            read_stdin=True,
            lines=["agent transcript line"],
            record_file=self.calls,
        )

        outcome = self.run_agent()

        self.assertIsInstance(outcome, pipeline_core.AgentOutcome)
        self.assertTrue(outcome.success)
        self.assertEqual(outcome.exit_code, 0)
        self.assertIn("agent transcript line", self.log_file.read_text(encoding="utf-8"))
        self.assertIn("agent transcript line", self.out.getvalue())
        (call,) = read_calls(self.calls)
        self.assertEqual(call["stdin"], "do the work", "copilot takes its prompt on stdin")
        self.assertNotIn("-p", call["args"])
        self.assertIn("--deny-tool=shell(git push)", call["args"])

    def test_reports_the_agent_exit_code_on_failure(self) -> None:
        self.fake_tool("luma-fake-copilot", exit_code=3, read_stdin=True, lines=["agent gave up"])

        outcome = self.run_agent()

        self.assertFalse(outcome.success)
        self.assertEqual(outcome.exit_code, 3)

    def test_fails_a_zero_exit_phase_whose_transcript_shows_quota_exhaustion(self) -> None:
        self.fake_tool(
            "luma-fake-copilot", read_stdin=True, lines=["You have exceeded your monthly quota"]
        )

        outcome = self.run_agent()

        self.assertFalse(outcome.success)
        self.assertEqual(outcome.exit_code, 1)
        self.assertIn("quota", self.err.getvalue())

    def test_claude_takes_its_prompt_in_argv_or_on_stdin_through_a_shim(self) -> None:
        self.fake_tool("luma-fake-claude", read_stdin=True, record_file=self.calls)

        self.assertTrue(self.run_agent("claude").success)
        (call,) = read_calls(self.calls)
        if os.name == "nt":
            # The fake is a .cmd shim, which cannot carry a multi-line argument.
            self.assertEqual(call["args"][:2], ["-p", "--output-format"])
            self.assertEqual(call["stdin"], "do the work")
        else:
            self.assertEqual(call["args"][:2], ["-p", "do the work"])
            self.assertEqual(call["stdin"], "")

    def test_raises_with_exit_code_127_when_the_agent_is_missing(self) -> None:
        os.environ["LUMA_COPILOT"] = "luma-no-such-agent"

        with self.assertRaises(PipelineError) as caught:
            self.run_agent()
        self.assertEqual(caught.exception.exit_code, 127)

    def test_dry_run_renders_the_command_and_spawns_nothing(self) -> None:
        pipeline = Pipeline(self.repo_root, is_dry_run=True, console=self.console)
        with forbid_spawning():
            outcome = pipeline.invoke_agent_phase(
                agent="claude", mode="agent", instruction="multi\nline", log_file=self.log_file
            )
        self.assertTrue(outcome.is_dry_run)
        self.assertIn("[dry-run] claude -p '<instruction>'", self.out.getvalue())
        self.assertFalse(self.log_file.exists())

    def test_an_error_while_streaming_kills_the_agent_and_its_children(self) -> None:
        # The agent starts a long-running child of its own, then prints a line;
        # the console then fails, which must take the whole process tree down.
        pid_file = self.root / "grandchild.pid"
        make_tree_agent(self.tool_dir, "luma-fake-copilot", pid_file)

        class BrokenConsole(Console):
            def write_raw(self, chunk: bytes) -> None:
                raise RuntimeError("console broke")

        self.pipeline.console = BrokenConsole(out=self.out, err=self.err, use_colour=False)
        with self.assertRaisesRegex(RuntimeError, "console broke"):
            self.run_agent()

        self.assert_child_was_killed(pid_file)

    def test_ctrl_c_kills_the_agent_and_its_children(self) -> None:
        pid_file = self.root / "grandchild.pid"
        make_tree_agent(self.tool_dir, "luma-fake-copilot", pid_file)

        interrupter = interrupt_when_started(pid_file)
        with self.assertRaises(KeyboardInterrupt):
            self.run_agent()
        interrupter.join()

        self.assert_child_was_killed(pid_file)

    def test_a_termination_signal_kills_the_agent_and_its_children(self) -> None:
        pid_file = self.root / "grandchild.pid"
        make_tree_agent(self.tool_dir, "luma-fake-copilot", pid_file)

        interrupter = interrupt_when_started(pid_file, signal.SIGTERM)
        with self.assertRaises(TerminationRequested), pipeline_core.termination_handlers():
            self.run_agent()
        interrupter.join()

        self.assert_child_was_killed(pid_file)

    def test_a_child_holding_the_output_open_does_not_hang_the_phase(self) -> None:
        # The agent exits at once, but its child inherited the output pipe and
        # keeps it open; the drain is bounded instead of waiting for the child.
        pid_file = self.root / "grandchild.pid"
        make_tree_agent(self.tool_dir, "luma-fake-copilot", pid_file, should_exit_early=True)

        started = time.monotonic()
        with mock.patch.object(pipeline_core, "_DRAIN_TIMEOUT_SECONDS", 1):
            outcome = self.run_agent()
        elapsed = time.monotonic() - started

        pid = read_pid(pid_file)
        self.assertIsNotNone(pid)
        assert pid is not None
        self.addCleanup(_kill_if_alive, pid)
        self.assertTrue(outcome.success)
        self.assertLess(elapsed, 30)
        self.assertIn("child started", self.log_file.read_text(encoding="utf-8"))
        # The process group (POSIX) or job (Windows) is killed even though the
        # agent itself already exited.
        self.assertTrue(wait_for_exit(pid, timeout=10), "the agent's child was left running")
        self.assertNotIn("still holds its output open", self.err.getvalue())

    @unittest.skipUnless(IS_WINDOWS, "Job Objects are Windows-only")
    def test_without_a_job_ctrl_c_falls_back_to_taskkill(self) -> None:
        # E.g. nested-job restrictions on old Windows: the live tree is still killed.
        pid_file = self.root / "grandchild.pid"
        make_tree_agent(self.tool_dir, "luma-fake-copilot", pid_file)

        interrupter = interrupt_when_started(pid_file)
        with (
            mock.patch.object(pipeline_core._WindowsJob, "create", return_value=None),
            self.assertRaises(KeyboardInterrupt),
        ):
            self.run_agent()
        interrupter.join()

        self.assert_child_was_killed(pid_file)


class PlanModeTests(FakeToolTestCase):
    def setUp(self) -> None:
        super().setUp()
        os.environ["LUMA_COPILOT"] = "luma-fake-copilot"
        os.environ["LUMA_CLAUDE"] = "luma-fake-claude"
        self.report = self.root / "reports" / "phase.md"

    def run_plan(self, agent: str = "copilot") -> pipeline_core.AgentOutcome:
        return self.pipeline.invoke_agent_phase(
            agent=agent, mode="plan", instruction="audit it", output_file=self.report
        )

    def test_copilot_report_is_the_last_assistant_message_with_text(self) -> None:
        events = [
            {"type": "assistant.message", "data": {"content": "interim narration"}},
            {"type": "assistant.message", "data": {"toolRequests": []}},
            {"type": "tool.execution", "data": {"content": "ignored"}},
            {"type": "assistant.message", "data": {"content": "# Final report\n\nbody\n\n"}},
            {"type": "assistant.message", "data": {"content": "   "}},
        ]
        self.fake_tool(
            "luma-fake-copilot", read_stdin=True, lines=[json.dumps(event) for event in events]
        )

        outcome = self.run_plan()

        self.assertTrue(outcome.success)
        self.assertEqual(self.report.read_text(encoding="utf-8"), "# Final report\n\nbody\n")

    def test_copilot_output_without_a_report_keeps_the_raw_transcript_and_fails(self) -> None:
        self.fake_tool("luma-fake-copilot", read_stdin=True, lines=["not json at all"])

        outcome = self.run_plan()

        self.assertEqual(outcome.exit_code, 1)
        self.assertIn("not json at all", self.report.read_text(encoding="utf-8"))
        self.assertIn("raw JSONL transcript", self.err.getvalue())

    def test_empty_copilot_output_writes_a_diagnostic_report_and_fails(self) -> None:
        self.fake_tool("luma-fake-copilot", read_stdin=True)

        outcome = self.run_plan()

        self.assertEqual(outcome.exit_code, 1)
        self.assertTrue(
            self.report.read_text(encoding="utf-8").startswith("# Phase produced no report")
        )

    def test_claude_report_is_its_stdout(self) -> None:
        self.fake_tool("luma-fake-claude", lines=["# Claude report"])

        self.assertTrue(self.run_plan("claude").success)
        self.assertEqual(self.report.read_text(encoding="utf-8"), "# Claude report\n")

    def test_empty_claude_output_fails_instead_of_a_hollow_success(self) -> None:
        self.fake_tool("luma-fake-claude")

        self.assertEqual(self.run_plan("claude").exit_code, 1)

    def test_ctrl_c_kills_the_audit_agent_and_its_children(self) -> None:
        # Audits run through the same supervised launch as fix phases: their
        # own process group and a tree kill on any interruption.
        pid_file = self.root / "grandchild.pid"
        make_tree_agent(self.tool_dir, "luma-fake-copilot", pid_file)

        interrupter = interrupt_when_started(pid_file)
        with self.assertRaises(KeyboardInterrupt):
            self.run_plan()
        interrupter.join()

        self.assert_child_was_killed(pid_file)

    def test_claude_stderr_is_kept_separate_from_the_report(self) -> None:
        make_launcher(
            self.tool_dir,
            "luma-fake-claude",
            """\
            import sys

            print("# Report", flush=True)
            print("a warning", file=sys.stderr, flush=True)
            """,
        )

        self.assertTrue(self.run_plan("claude").success)
        self.assertEqual(self.report.read_text(encoding="utf-8"), "# Report\n")
        self.assertIn("a warning", self.err.getvalue())


class ResolveCopilotModelTests(FakeToolTestCase):
    def setUp(self) -> None:
        super().setUp()
        os.environ["LUMA_COPILOT"] = "luma-fake-copilot"

    def test_falls_back_to_auto_when_the_cli_reports_the_model_is_not_available(self) -> None:
        self.fake_tool(
            "luma-fake-copilot",
            exit_code=1,
            read_stdin=True,
            lines=["Error: Model claude-opus-4.6 from --model flag is not available"],
        )

        self.assertEqual(self.pipeline.resolve_copilot_model("copilot", "claude-opus-4.6"), "auto")
        self.assertIn("Falling back to 'auto'", self.err.getvalue())

    def test_keeps_the_requested_model_when_the_cli_accepts_it(self) -> None:
        self.fake_tool("luma-fake-copilot", read_stdin=True, lines=["ok"])

        self.assertEqual(
            self.pipeline.resolve_copilot_model("copilot", "claude-opus-4.6"), "claude-opus-4.6"
        )

    def test_keeps_the_requested_model_when_the_probe_exceeds_its_timeout(self) -> None:
        self.fake_tool("luma-fake-copilot", sleep_seconds=30)

        started = time.monotonic()
        model = self.pipeline.resolve_copilot_model("copilot", "claude-opus-4.6", timeout=1)
        elapsed = time.monotonic() - started

        self.assertEqual(model, "claude-opus-4.6")
        self.assertLess(elapsed, 15)

    def test_a_timed_out_probe_kills_the_whole_process_tree(self) -> None:
        pid_file = self.root / "grandchild.pid"
        make_tree_agent(self.tool_dir, "luma-fake-copilot", pid_file)

        model = self.pipeline.resolve_copilot_model("copilot", "claude-opus-4.6", timeout=8)

        self.assertEqual(model, "claude-opus-4.6")
        self.assert_child_was_killed(pid_file)

    def test_probes_each_model_once(self) -> None:
        self.fake_tool("luma-fake-copilot", read_stdin=True, lines=["ok"])
        self.pipeline.resolve_copilot_model("copilot", "gpt-5.4")

        with forbid_spawning():
            self.assertEqual(self.pipeline.resolve_copilot_model("copilot", "gpt-5.4"), "gpt-5.4")

    def test_never_probes_claude_auto_an_empty_model_or_a_dry_run(self) -> None:
        dry = Pipeline(self.repo_root, is_dry_run=True, console=self.console)
        with forbid_spawning():
            self.assertEqual(self.pipeline.resolve_copilot_model("claude", "opus"), "opus")
            self.assertEqual(self.pipeline.resolve_copilot_model("copilot", "auto"), "auto")
            self.assertEqual(self.pipeline.resolve_copilot_model("copilot", ""), "")
            self.assertEqual(dry.resolve_copilot_model("copilot", "gpt-5.4"), "gpt-5.4")


# ── Git helpers ───────────────────────────────────────────────────


@unittest.skipUnless(HAS_GIT, "git is not installed")
class GitTestCase(FakeToolTestCase):
    """A scratch repository with one commit and a hermetic git configuration."""

    def setUp(self) -> None:
        super().setUp()
        empty_config = self.root / "gitconfig"
        empty_config.write_text("", encoding="utf-8")
        os.environ.update(
            {
                "GIT_CONFIG_GLOBAL": str(empty_config),
                "GIT_CONFIG_NOSYSTEM": "1",
                "GIT_AUTHOR_NAME": "Pipeline Test",
                "GIT_AUTHOR_EMAIL": "pipeline@example.invalid",
                "GIT_COMMITTER_NAME": "Pipeline Test",
                "GIT_COMMITTER_EMAIL": "pipeline@example.invalid",
                "GIT_CEILING_DIRECTORIES": str(self.root),
            }
        )
        self.git("init", "--quiet")
        (self.repo_root / ".gitignore").write_text(
            "build/\npipeline-artifacts/\n", encoding="utf-8"
        )
        (self.repo_root / "README.md").write_text("initial\n", encoding="utf-8")
        self.git("add", "-A")
        self.git("commit", "--quiet", "-m", "initial")
        # The pipeline snapshots the environment, so build it after the git setup.
        self.pipeline = self.make_pipeline()

    def git(self, *args: str) -> str:
        return subprocess.run(
            ["git", "-C", str(self.repo_root), *args],
            capture_output=True,
            text=True,
            check=True,
        ).stdout


class CleanWorkingTreeTests(GitTestCase):
    def test_raises_when_the_directory_is_not_a_git_working_tree(self) -> None:
        not_a_repo = self.root / "not-a-repo"
        not_a_repo.mkdir()

        with self.assertRaisesRegex(PipelineError, "git status failed"):
            Pipeline(not_a_repo, console=self.console).is_clean_working_tree()

    def test_distinguishes_a_clean_tree_from_a_dirty_one(self) -> None:
        self.assertIs(self.pipeline.is_clean_working_tree(), True)

        (self.repo_root / "new.txt").write_text("change\n", encoding="utf-8")
        self.assertIs(self.pipeline.is_clean_working_tree(), False)


class GitCheckpointTests(GitTestCase):
    def checkpoint(self, artifact_root: Path | None = None) -> str | None:
        return self.pipeline.git_checkpoint(
            subject="chore(pipeline): bug-fix-core", agent="claude", artifact_root=artifact_root
        )

    def test_commits_changes_with_the_agent_trailer(self) -> None:
        (self.repo_root / "README.md").write_text("changed\n", encoding="utf-8")

        sha = self.checkpoint()

        self.assertTrue(sha)
        message = self.git("log", "-1", "--format=%B")
        self.assertIn("chore(pipeline): bug-fix-core", message)
        self.assertIn(CO_AUTHOR_TRAILERS["claude"], message)

    def test_returns_none_when_nothing_changed(self) -> None:
        self.assertIsNone(self.checkpoint())

    def test_keeps_an_in_tree_artifact_root_out_of_the_commit(self) -> None:
        artifacts = self.repo_root / "custom-artifacts"
        artifacts.mkdir()
        (artifacts / "log.txt").write_text("transcript\n", encoding="utf-8")
        (self.repo_root / "README.md").write_text("changed\n", encoding="utf-8")

        self.checkpoint(artifacts)

        committed = self.git("show", "--name-only", "--format=", "HEAD").split()
        self.assertEqual(committed, ["README.md"])

    def test_a_rejected_commit_raises_checkpoint_error(self) -> None:
        hooks = self.root / "hooks"
        hooks.mkdir()
        hook = hooks / "pre-commit"
        hook.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8", newline="\n")
        hook.chmod(0o755)
        self.git("config", "core.hooksPath", str(hooks))
        (self.repo_root / "README.md").write_text("changed\n", encoding="utf-8")

        with self.assertRaises(CheckpointError):
            self.checkpoint()

    def test_refuses_to_commit_when_the_artifact_root_cannot_be_unstaged(self) -> None:
        artifacts = self.repo_root / "custom-artifacts"
        artifacts.mkdir()
        (artifacts / "log.txt").write_text("transcript\n", encoding="utf-8")
        (self.repo_root / "README.md").write_text("changed\n", encoding="utf-8")
        original_run = Pipeline._run

        def failing_reset(pipeline: Pipeline, args: Sequence[str], **kwargs: object) -> int:
            if "reset" in args:
                return 128
            return original_run(pipeline, args, **kwargs)  # type: ignore[arg-type]

        with (
            mock.patch.object(Pipeline, "_run", autospec=True, side_effect=failing_reset),
            self.assertRaisesRegex(CheckpointError, "could not unstage"),
        ):
            self.checkpoint(artifacts)

        self.assertEqual(self.git("rev-list", "--count", "HEAD").strip(), "1")

    def test_dry_run_prints_the_checkpoint_and_spawns_nothing(self) -> None:
        pipeline = Pipeline(self.repo_root, is_dry_run=True, console=self.console)
        with forbid_spawning():
            sha = pipeline.git_checkpoint(
                subject="chore(pipeline): x", agent="copilot", artifact_root=None
            )
        self.assertIsNone(sha)
        self.assertIn("[dry-run] git commit -m 'chore(pipeline): x'", self.out.getvalue())


class RevertTests(GitTestCase):
    def setUp(self) -> None:
        super().setUp()
        (self.repo_root / "README.md").write_text("changed by the phase\n", encoding="utf-8")
        (self.repo_root / "new-by-phase.txt").write_text("leaked?\n", encoding="utf-8")
        ignored = self.repo_root / "pipeline-artifacts"
        ignored.mkdir()
        (ignored / "run.log").write_text("transcript\n", encoding="utf-8")

    def test_removes_the_files_a_failed_phase_created_but_keeps_ignored_ones(self) -> None:
        self.assertIs(self.pipeline.revert_to(remove_untracked=True), True)

        self.assertEqual((self.repo_root / "README.md").read_text(encoding="utf-8"), "initial\n")
        self.assertFalse((self.repo_root / "new-by-phase.txt").exists())
        self.assertTrue((self.repo_root / "pipeline-artifacts" / "run.log").is_file())
        self.assertIs(self.pipeline.is_clean_working_tree(), True)

    def test_keeps_an_in_tree_artifact_root_that_is_not_ignored(self) -> None:
        artifacts = self.repo_root / "custom-artifacts"
        artifacts.mkdir()
        (artifacts / "run.log").write_text("transcript\n", encoding="utf-8")

        self.assertIs(self.pipeline.revert_to(remove_untracked=True, keep=artifacts), True)

        self.assertTrue((artifacts / "run.log").is_file())
        self.assertFalse((self.repo_root / "new-by-phase.txt").exists())

    def test_without_removal_only_tracked_files_are_reset(self) -> None:
        self.assertIs(self.pipeline.revert_to(remove_untracked=False), True)

        self.assertEqual((self.repo_root / "README.md").read_text(encoding="utf-8"), "initial\n")
        self.assertTrue((self.repo_root / "new-by-phase.txt").is_file())

    def test_dry_run_prints_the_reset_and_clean_and_spawns_nothing(self) -> None:
        pipeline = self.make_pipeline(is_dry_run=True)
        with forbid_spawning():
            self.assertIs(pipeline.revert_to(remove_untracked=True), True)
        self.assertIn("[dry-run] git reset --hard HEAD", self.out.getvalue())
        self.assertIn("[dry-run] git clean -fd", self.out.getvalue())
        self.assertTrue((self.repo_root / "new-by-phase.txt").is_file())


class ArtifactProtectionTests(GitTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.artifact_root = self.repo_root / "pipeline-artifacts"
        self.log_dir = self.artifact_root / "fix-1" / "logs"
        self.log_dir.mkdir(parents=True)
        (self.artifact_root / "audit-1.md").write_text("report\n", encoding="utf-8")
        self.backup = Path(
            self.git("rev-parse", "--absolute-git-dir").strip(), ARTIFACT_BACKUP_NAME
        )

    def test_sets_the_root_aside_and_folds_the_transcript_back(self) -> None:
        with self.pipeline.protect_artifacts(self.artifact_root, self.log_dir) as phase_log_dir:
            self.assertFalse(self.artifact_root.exists())
            self.assertTrue(self.backup.is_dir())
            self.assertFalse(pipeline_core.is_within(phase_log_dir, self.repo_root))
            (phase_log_dir / "release-verification.log").write_text("log\n", encoding="utf-8")

        self.assertTrue((self.artifact_root / "audit-1.md").is_file())
        self.assertTrue((self.log_dir / "release-verification.log").is_file())
        self.assertFalse(self.backup.exists())

    def test_restores_the_root_even_when_the_phase_raises(self) -> None:
        with (
            self.assertRaises(RuntimeError),
            self.pipeline.protect_artifacts(self.artifact_root, self.log_dir),
        ):
            raise RuntimeError("phase blew up")

        self.assertTrue((self.artifact_root / "audit-1.md").is_file())

    def test_leaves_an_artifact_root_outside_the_tree_alone(self) -> None:
        outside = self.root / "outside-artifacts"
        outside.mkdir()

        self.assertIsNone(self.pipeline.save_artifacts(outside))
        self.assertTrue(outside.is_dir())

    def test_never_deletes_a_backup_left_by_an_earlier_failed_restore(self) -> None:
        self.backup.mkdir()
        (self.backup / "recover-me.md").write_text("only copy\n", encoding="utf-8")

        with self.assertRaisesRegex(PipelineError, "still exists") as caught:
            self.pipeline.save_artifacts(self.artifact_root)

        self.assertIn(str(self.backup), str(caught.exception))
        self.assertTrue((self.backup / "recover-me.md").is_file())
        self.assertTrue((self.artifact_root / "audit-1.md").is_file())

    def test_a_failed_restore_keeps_the_backup_and_says_where_it_is(self) -> None:
        with (
            mock.patch.object(Pipeline, "restore_artifacts", side_effect=OSError("disk full")),
            self.pipeline.protect_artifacts(self.artifact_root, self.log_dir) as phase_log_dir,
        ):
            (phase_log_dir / "release-verification.log").write_text("log\n", encoding="utf-8")

        self.assertIn(str(self.backup), self.err.getvalue())
        self.assertTrue((self.backup / "audit-1.md").is_file())
        self.assertTrue((self.log_dir / "release-verification.log").is_file())
        # The next protected phase refuses to run rather than delete the backup.
        with (
            self.assertRaises(PipelineError),
            self.pipeline.protect_artifacts(self.artifact_root, self.log_dir),
        ):
            self.fail("a protected phase ran over an unrecovered backup")
        self.assertTrue((self.backup / "audit-1.md").is_file())


# ── Pure helpers ──────────────────────────────────────────────────


class ReportExtractionTests(unittest.TestCase):
    def test_skips_malformed_null_and_non_object_lines(self) -> None:
        lines = [
            "",
            "null",
            "[]",
            "{not json",
            json.dumps({"type": "assistant.message", "data": None}),
            json.dumps({"type": "assistant.message", "data": {"content": "the report"}}),
            json.dumps({"type": "assistant.message", "data": {"content": 42}}),
        ]
        self.assertEqual(pipeline_core.extract_report_from_jsonl("\n".join(lines)), "the report")

    def test_keeps_unicode_line_separators_inside_a_message(self) -> None:
        content = "first\u2028second"
        line = json.dumps(
            {"type": "assistant.message", "data": {"content": content}}, ensure_ascii=False
        )
        self.assertEqual(pipeline_core.extract_report_from_jsonl(line + "\r\n"), content)

    def test_returns_empty_when_there_is_no_text_message(self) -> None:
        self.assertEqual(pipeline_core.extract_report_from_jsonl("garbage\n"), "")


class AgentArgumentTests(unittest.TestCase):
    def test_copilot_plan_mode_is_read_only_json(self) -> None:
        args = pipeline_core.build_copilot_args("plan", log_dir=Path("logs"))
        self.assertIn("--output-format=json", args)
        self.assertIn("--available-tools=view,grep,glob", args)
        self.assertIn(f"--log-dir={Path('logs')}", args)
        self.assertNotIn("--deny-tool=shell(git push)", args)

    def test_copilot_agent_mode_denies_push(self) -> None:
        args = pipeline_core.build_copilot_args("agent")
        self.assertIn("--deny-tool=shell(git push)", args)
        self.assertNotIn("-p", args)

    def test_copilot_omits_effort_for_auto_or_no_model(self) -> None:
        self.assertNotIn("--effort=high", pipeline_core.build_copilot_args("agent", effort="high"))
        auto = pipeline_core.build_copilot_args("agent", model="auto", effort="high")
        self.assertNotIn("--effort=high", auto)
        explicit = pipeline_core.build_copilot_args("agent", model="gpt-5.4", effort="high")
        self.assertEqual(explicit[-2:], ["--model=gpt-5.4", "--effort=high"])

    def test_claude_modes(self) -> None:
        plan = pipeline_core.build_claude_args("go", "plan", model="opus", effort="max")
        self.assertEqual(
            plan,
            ["-p", "go", "--output-format", "text", "--permission-mode", "plan"]
            + ["--model", "opus", "--effort", "max"],
        )
        agent = pipeline_core.build_claude_args("go", "agent")
        self.assertIn("acceptEdits", agent)
        self.assertEqual(agent[-2:], ["--disallowedTools", "Bash(git push *)"])
        piped = pipeline_core.build_claude_args(None, "plan")
        self.assertEqual(piped[:3], ["-p", "--output-format", "text"])

    def test_only_cmd_and_bat_files_are_batch_shims(self) -> None:
        self.assertTrue(pipeline_core.is_batch_shim(r"C:\npm\claude.CMD"))
        self.assertTrue(pipeline_core.is_batch_shim("tool.bat"))
        self.assertFalse(pipeline_core.is_batch_shim("/usr/local/bin/claude"))
        self.assertFalse(pipeline_core.is_batch_shim(None))

    def test_render_command_elides_the_instruction_and_quotes_like_a_shell(self) -> None:
        rendered = pipeline_core.render_command(
            "claude", ["-p", "line one\nline two", "--x", "a b"], instruction="line one\nline two"
        )
        self.assertEqual(rendered, "claude -p '<instruction>' --x 'a b'")

    def test_quota_detection_is_case_insensitive(self) -> None:
        self.assertTrue(pipeline_core.is_quota_exhausted("You have EXCEEDED your monthly quota."))
        self.assertFalse(pipeline_core.is_quota_exhausted("all good"))

    def test_status_prints_as_its_summary_value(self) -> None:
        self.assertEqual(str(Status.COMMIT_FAILED), "commit-failed")
        self.assertEqual(f"{Status.GATE_FAILED}", "gate-failed")


class ShouldAbortTests(unittest.TestCase):
    FAILURES = (Status.ERROR, Status.AGENT_FAILED, Status.GATE_FAILED, Status.COMMIT_FAILED)

    def check(self, status: Status, *, keep_going: bool, reverts_cleanly: bool) -> bool:
        return pipeline_core.should_abort(
            status, continue_on_failure=keep_going, reverts_cleanly=reverts_cleanly
        )

    def test_ok_never_aborts(self) -> None:
        self.assertFalse(self.check(Status.OK, keep_going=False, reverts_cleanly=False))

    def test_any_failure_aborts_without_continue_on_failure(self) -> None:
        for status in self.FAILURES:
            self.assertTrue(self.check(status, keep_going=False, reverts_cleanly=True), status)

    def test_continue_on_failure_skips_per_phase_failures(self) -> None:
        for status in (Status.ERROR, Status.AGENT_FAILED, Status.GATE_FAILED):
            self.assertFalse(self.check(status, keep_going=True, reverts_cleanly=False), status)

    def test_a_commit_failure_stops_the_run_unless_reverted_to_a_clean_tree(self) -> None:
        self.assertTrue(self.check(Status.COMMIT_FAILED, keep_going=True, reverts_cleanly=False))
        self.assertFalse(self.check(Status.COMMIT_FAILED, keep_going=True, reverts_cleanly=True))


class PhaseTests(unittest.TestCase):
    def test_selects_by_case_insensitive_substring_and_commas(self) -> None:
        selected = luma_pipeline.select_phases(luma_pipeline.FIX_PHASES, ["BUG-FIX-core, Refactor"])
        self.assertEqual([phase.phase_id for phase in selected], ["bug-fix-core", "refactor"])

    def test_no_filters_selects_everything_and_no_match_selects_nothing(self) -> None:
        phases = luma_pipeline.AUDIT_PHASES
        self.assertEqual(luma_pipeline.select_phases(phases, []), list(phases))
        self.assertEqual(luma_pipeline.select_phases(phases, ["no-such-phase"]), [])

    def test_fix_instruction_adds_report_lint_and_pass_clauses(self) -> None:
        phase = next(p for p in luma_pipeline.FIX_PHASES if p.phase_id == "iterative-improvement")
        text = luma_pipeline.build_fix_instruction(
            phase, report_path=Path("r.md"), enforce_lint=True, max_passes=4
        )
        self.assertIn(f"    {Path('r.md')}", text)
        self.assertIn("python scripts/lint.py", text)
        self.assertIn("at most 4 full improvement passes", text)
        self.assertTrue(text.endswith("unless the prompt says otherwise."))

    def test_audit_instruction_uses_the_scope(self) -> None:
        phase = luma_pipeline.AUDIT_PHASES[0]
        self.assertIn("scope: core/vm/.", luma_pipeline.build_audit_instruction(phase, "core/vm/"))
        self.assertIn("default scope", luma_pipeline.build_audit_instruction(phase, ""))


# ── Command line ──────────────────────────────────────────────────


class CommandLineTests(unittest.TestCase):
    def run_main(self, *argv: str) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = luma_pipeline.main(list(argv))
        return code, out.getvalue()

    def test_list_prints_every_phase(self) -> None:
        code, output = self.run_main("fix", "--list")
        self.assertEqual(code, 0)
        for phase in luma_pipeline.FIX_PHASES:
            self.assertIn(phase.phase_id, output)

    def test_every_dry_run_succeeds_without_spawning_a_process(self) -> None:
        for command in ("audit", "fix", "all"):
            for agent in pipeline_core.AGENTS:
                with self.subTest(command=command, agent=agent), forbid_spawning():
                    code, output = self.run_main(command, "--dry-run", "--agent", agent)
                    self.assertEqual(code, 0, output)
                    self.assertIn(f"[dry-run] {agent} ", output)

    def test_all_forwards_stage_arguments_and_passthrough(self) -> None:
        with forbid_spawning():
            code, output = self.run_main(
                "all", "--dry-run", "--fix-arg", "--no-commit", "--", "--phase", "refactor"
            )
        self.assertEqual(code, 0, output)
        self.assertIn("Refactor audit", output)
        self.assertIn("Prompt: refactor.prompt.md", output)
        self.assertNotIn("git commit", output)

    def test_all_rejects_a_bad_stage_argument_before_running_anything(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            self.run_main("all", "--dry-run", "--fix-arg", "--bogus")
        self.assertEqual(caught.exception.code, 2)

    def test_invalid_copilot_effort_is_a_usage_error(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            self.run_main("audit", "--effort", "extreme")
        self.assertEqual(caught.exception.code, 2)

    def test_unmatched_phase_filter_exits_2(self) -> None:
        code, output = self.run_main("audit", "--dry-run", "--phase", "no-such-phase")
        self.assertEqual(code, 2)
        self.assertIn("no audit phase matched", output)

    def test_allow_dirty_with_revert_on_failure_is_a_usage_error(self) -> None:
        # The revert's git reset --hard would destroy the uncommitted changes.
        for argv in (
            ("fix", "--dry-run", "--allow-dirty", "--revert-on-failure"),
            ("all", "--dry-run", "--fix-arg", "--allow-dirty", "--fix-arg", "--revert-on-failure"),
        ):
            with self.subTest(argv=argv), forbid_spawning():
                out = io.StringIO()
                with (
                    self.assertRaises(SystemExit) as caught,
                    contextlib.redirect_stdout(out),
                    contextlib.redirect_stderr(out),
                ):
                    luma_pipeline.main(list(argv))
                self.assertEqual(caught.exception.code, 2)
                self.assertIn("cannot be combined with --allow-dirty", out.getvalue())

    def test_an_empty_model_or_effort_for_all_means_the_agent_default(self) -> None:
        with forbid_spawning():
            code, output = self.run_main("all", "--dry-run", "--model=", "--effort=")
        self.assertEqual(code, 0, output)
        self.assertNotIn("--model", output)
        self.assertNotIn("--effort", output)

    def test_missing_agent_exits_127_before_any_phase(self) -> None:
        with mock.patch.dict(os.environ, {"LUMA_COPILOT": "luma-no-such-agent"}), forbid_spawning():
            code, output = self.run_main("audit")
        self.assertEqual(code, 127)
        self.assertIn("luma-no-such-agent", output)


@unittest.skipUnless(HAS_GIT, "git is not installed")
class FixRunTests(GitTestCase):
    """Drive ``fix`` end to end against a scratch repository and fake tools."""

    def setUp(self) -> None:
        super().setUp()
        os.environ["LUMA_COPILOT"] = "luma-fake-copilot"
        self.fake_tool("luma-fake-copilot", read_stdin=True, append_to="README.md")
        self.fake_tool("cmake")
        self.ctest = self.fake_tool("ctest")
        self.artifacts = self.root / "artifacts"
        patcher = mock.patch.object(luma_pipeline, "find_repo_root", return_value=self.repo_root)
        patcher.start()
        self.addCleanup(patcher.stop)

    def run_fix(self, *extra: str, phases: str = "bug-fix-core") -> tuple[int, str]:
        out = io.StringIO()
        argv = ["fix", "--phase", phases, "--skip-lint-format"]
        argv += ["--artifact-root", str(self.artifacts), *extra]
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = luma_pipeline.main(argv)
        return code, out.getvalue()

    def summary(self) -> str:
        (summary,) = self.artifacts.glob("fix-*/SUMMARY.md")
        return summary.read_text(encoding="utf-8")

    def break_tests_after_agent(
        self,
        *,
        on_calls: Sequence[int],
        create_file: str | None = None,
        then: Callable[[int], None] | None = None,
    ) -> contextlib.AbstractContextManager[object]:
        """Make ctest fail after the listed agent calls (1-based) and pass after the others.

        Optionally the failing phase also creates *create_file*, as an agent
        adding a new source file would, and *then* runs after every agent call
        with its 1-based number.
        """
        failing = make_fake_tool(self.root, "ctest-red", exit_code=1)
        passing = make_fake_tool(self.root, "ctest-green")
        original = Pipeline.invoke_agent_phase
        call_count = 0

        def agent_then_set_tests(
            pipeline: Pipeline, **kwargs: object
        ) -> pipeline_core.AgentOutcome:
            nonlocal call_count
            call_count += 1
            outcome = original(pipeline, **kwargs)  # type: ignore[arg-type]
            is_failing_call = call_count in on_calls
            if is_failing_call and create_file is not None:
                (self.repo_root / create_file).write_text("new\n", encoding="utf-8")
            shutil.copyfile(failing if is_failing_call else passing, self.ctest)
            if then is not None:
                then(call_count)
            return outcome

        return mock.patch.object(Pipeline, "invoke_agent_phase", agent_then_set_tests)

    def make_committing_agent(self) -> None:
        """Replace the fake agent with one that commits its own change."""
        make_launcher(
            self.tool_dir,
            "luma-fake-copilot",
            """\
            import subprocess
            import sys

            sys.stdin.read()
            with open("README.md", "a", encoding="utf-8") as handle:
                handle.write("committed by the agent\\n")
            subprocess.run(["git", "commit", "-q", "-am", "agent commit"], check=True)
            """,
        )

    def reject_commits_containing(self, name: str) -> None:
        """Install a pre-commit hook that rejects any commit while *name* exists."""
        hooks = self.root / "hooks"
        hooks.mkdir()
        hook = hooks / "pre-commit"
        hook.write_text(f"#!/bin/sh\ntest ! -e {name}\n", encoding="utf-8", newline="\n")
        hook.chmod(0o755)
        self.git("config", "core.hooksPath", str(hooks))

    def head(self) -> str:
        return self.git("rev-parse", "HEAD").strip()

    def test_a_green_phase_is_committed_on_a_new_branch(self) -> None:
        code, output = self.run_fix()

        self.assertEqual(code, 0, output)
        self.assertTrue(self.git("branch", "--show-current").startswith("pipeline/fix-"))
        self.assertIn(CO_AUTHOR_TRAILERS["copilot"], self.git("log", "-1", "--format=%B"))
        self.assertRegex(
            self.summary(), r"\| bug-fix-core \| .* \| ok \| [0-9a-f]+ \| \[bug-fix-core\.log\]"
        )

    def test_a_red_gate_fails_the_run_and_reverts_when_asked(self) -> None:
        with self.break_tests_after_agent(on_calls=[1]):
            code, output = self.run_fix("--revert-on-failure")

        self.assertEqual(code, 1, output)
        self.assertIn("gate-failed", self.summary())
        self.assertEqual((self.repo_root / "README.md").read_text(encoding="utf-8"), "initial\n")

    def test_a_failed_phase_does_not_leak_its_new_files_into_the_next_checkpoint(self) -> None:
        with self.break_tests_after_agent(on_calls=[1], create_file="leaked.txt"):
            code, output = self.run_fix(
                "--revert-on-failure",
                "--continue-on-failure",
                phases="bug-fix-core,bug-fix-debugger",
            )

        self.assertEqual(code, 1, output)
        self.assertFalse((self.repo_root / "leaked.txt").exists())
        committed = self.git("show", "--name-only", "--format=", "HEAD").split()
        self.assertEqual(committed, ["README.md"])
        self.assertRegex(self.summary(), r"\| bug-fix-debugger \| .* \| ok \| [0-9a-f]+ \|")

    def test_allow_dirty_with_revert_on_failure_is_refused_before_touching_the_tree(
        self,
    ) -> None:
        (self.repo_root / "README.md").write_text("my uncommitted edit\n", encoding="utf-8")
        (self.repo_root / "notes.txt").write_text("mine\n", encoding="utf-8")
        branch = self.git("branch", "--show-current").strip()

        with self.assertRaises(SystemExit) as caught:
            self.run_fix("--allow-dirty", "--revert-on-failure")

        self.assertEqual(caught.exception.code, 2)
        self.assertEqual(
            (self.repo_root / "README.md").read_text(encoding="utf-8"), "my uncommitted edit\n"
        )
        self.assertTrue((self.repo_root / "notes.txt").is_file())
        self.assertEqual(self.git("branch", "--show-current").strip(), branch)

    def test_a_rejected_commit_is_reverted_and_the_run_continues(self) -> None:
        self.reject_commits_containing("reject.txt")

        def create_rejected_file(call: int) -> None:
            if call == 1:
                (self.repo_root / "reject.txt").write_text("blocked\n", encoding="utf-8")

        with self.break_tests_after_agent(on_calls=[], then=create_rejected_file):
            code, output = self.run_fix(
                "--revert-on-failure",
                "--continue-on-failure",
                phases="bug-fix-core,bug-fix-debugger",
            )

        self.assertEqual(code, 1, output)
        self.assertFalse((self.repo_root / "reject.txt").exists())
        self.assertRegex(self.summary(), r"\| bug-fix-core \| .* \| commit-failed \| - \|")
        self.assertRegex(self.summary(), r"\| bug-fix-debugger \| .* \| ok \| [0-9a-f]+ \|")
        self.assertEqual(
            (self.repo_root / "README.md").read_text(encoding="utf-8"),
            "initial\nchanged by the fake agent\n",
        )
        self.assertEqual(
            self.git("show", "--name-only", "--format=", "HEAD").split(), ["README.md"]
        )

    def test_a_rejected_commit_stops_the_run_without_a_revert(self) -> None:
        self.reject_commits_containing("README.md")

        code, output = self.run_fix("--continue-on-failure", phases="bug-fix-core,bug-fix-debugger")

        self.assertEqual(code, 1, output)
        self.assertIn("cannot be skipped", output)
        self.assertIn("commit-failed", self.summary())
        self.assertNotIn("bug-fix-debugger", self.summary())

    def test_a_failed_revert_stops_the_run_even_with_continue_on_failure(self) -> None:
        git_dir = Path(self.git("rev-parse", "--absolute-git-dir").strip())

        def lock_the_index(call: int) -> None:
            # A stale index.lock makes git reset --hard fail.
            if call == 1:
                (git_dir / "index.lock").write_text("", encoding="utf-8")

        with self.break_tests_after_agent(on_calls=[1], then=lock_the_index):
            code, output = self.run_fix(
                "--revert-on-failure",
                "--continue-on-failure",
                phases="bug-fix-core,bug-fix-debugger",
            )

        self.assertEqual(code, 1, output)
        self.assertIn("the revert failed", output)
        self.assertIn("stopped early", self.summary())
        self.assertNotIn("bug-fix-debugger", self.summary())

    def test_a_revert_drops_commits_the_agent_made_during_the_failed_phase(self) -> None:
        self.make_committing_agent()
        start = self.head()

        with self.break_tests_after_agent(on_calls=[1]):
            code, output = self.run_fix("--revert-on-failure")

        self.assertEqual(code, 1, output)
        self.assertEqual(self.head(), start)
        self.assertEqual((self.repo_root / "README.md").read_text(encoding="utf-8"), "initial\n")
        self.assertIn("made 1 commit(s) of its own", output)
        self.assertIn("git branch <name>", output)

    def test_commits_the_agent_made_in_a_green_phase_are_reported(self) -> None:
        self.make_committing_agent()
        start = self.head()

        code, output = self.run_fix()

        self.assertEqual(code, 0, output)
        self.assertIn("made 1 commit(s) of its own", output)
        self.assertEqual(self.git("rev-list", "--count", f"{start}..HEAD").strip(), "1")
        short_head = self.git("rev-parse", "--short", "HEAD").strip()
        self.assertRegex(self.summary(), rf"\| bug-fix-core \| .* \| ok \| {short_head} \|")

    def test_a_termination_signal_stops_the_run_and_kills_the_agent_tree(self) -> None:
        pid_file = self.root / "grandchild.pid"
        make_tree_agent(self.tool_dir, "luma-fake-copilot", pid_file)
        before = signal.getsignal(signal.SIGTERM)

        interrupter = interrupt_when_started(pid_file, signal.SIGTERM)
        code, output = self.run_fix()
        interrupter.join()

        self.assertEqual(code, 128 + signal.SIGTERM, output)
        self.assertIn("terminated", output)
        self.assert_child_was_killed(pid_file)
        self.assertEqual(signal.getsignal(signal.SIGTERM), before)

    def test_ctrl_c_stops_the_run_with_exit_code_130(self) -> None:
        pid_file = self.root / "grandchild.pid"
        make_tree_agent(self.tool_dir, "luma-fake-copilot", pid_file)

        interrupter = interrupt_when_started(pid_file)
        code, output = self.run_fix()
        interrupter.join()

        self.assertEqual(code, 130, output)
        self.assert_child_was_killed(pid_file)

    def test_a_red_baseline_says_how_to_leave_the_run_branch(self) -> None:
        shutil.copyfile(make_fake_tool(self.root, "ctest-red", exit_code=1), self.ctest)

        code, output = self.run_fix()

        self.assertEqual(code, 1, output)
        self.assertIn("baseline build + test is not green", output)
        self.assertIn("git switch -", output)
        self.assertTrue(self.git("branch", "--show-current").startswith("pipeline/fix-"))

    def test_a_dirty_tree_is_refused(self) -> None:
        (self.repo_root / "README.md").write_text("dirty\n", encoding="utf-8")

        code, output = self.run_fix()

        self.assertEqual(code, 1)
        self.assertIn("working tree is not clean", output)


if __name__ == "__main__":
    unittest.main()
