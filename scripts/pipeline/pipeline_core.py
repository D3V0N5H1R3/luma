"""Side-effecting core of the Luma prompt pipeline.

Every side effect the pipeline performs — invoking the agent CLI, building,
testing, linting, and touching git or the artifact tree — flows through
:class:`Pipeline`. Its ``is_dry_run`` flag gives all of them one uniform preview
path: a dry run prints the exact command each step would run and returns a
neutral result without spawning anything.

The module also holds the pure helpers those steps are built from (agent
argument vectors, Copilot JSONL reduction, the abort rule), so they can be
unit-tested without a process in sight. ``luma_pipeline.py`` is the CLI that
orchestrates the audit, fix, and combined runs on top of this module.

Standard library only; Python 3.10+. Works on Windows, macOS, and Linux.
"""

from __future__ import annotations

import codecs
import contextlib
import ctypes
import functools
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from collections.abc import Callable, Iterator, Mapping, Sequence
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from types import FrameType
from typing import IO, TextIO

AGENTS = ("copilot", "claude")
COPILOT_EFFORTS = ("low", "medium", "high", "xhigh", "max")
AGENT_ENV_OVERRIDES = {"copilot": "LUMA_COPILOT", "claude": "LUMA_CLAUDE"}
CO_AUTHOR_TRAILERS = {
    "copilot": "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>",
    "claude": "Co-authored-by: Claude <noreply@anthropic.com>",
}

MODEL_PROBE_TIMEOUT_SECONDS = 90
MODEL_PROBE_PROMPT = "Reply with the single word: ok. Do not use any tools."
QUOTA_SIGNATURE = "exceeded your monthly quota"
ARTIFACT_BACKUP_NAME = "_pipeline-artifacts.pipeline.bak"
INSTRUCTION_PLACEHOLDER = "<instruction>"

_MODEL_UNAVAILABLE = re.compile(r"model .*is not available", re.IGNORECASE)
_IS_WINDOWS = os.name == "nt"
_CHUNK_SIZE = 65536
# Bound on draining a child's pipes after it exits: a grandchild that inherited
# the pipe handles could otherwise hold them open indefinitely.
_DRAIN_TIMEOUT_SECONDS = 10
# How long the main thread blocks at a time while it supervises a child. Short
# waits keep it responsive to Ctrl+C and termination signals on every platform
# (a blocking pipe read on Windows cannot be interrupted by Ctrl+C).
_POLL_INTERVAL_SECONDS = 0.1
# Grace period for the output readers after the process tree was killed.
_POST_KILL_DRAIN_SECONDS = 2
# Signals that ask the pipeline to stop; each one is routed through the same
# cleanup as Ctrl+C. SIGBREAK is the Windows Ctrl+Break / console-close signal.
TERMINATION_SIGNALS = tuple(
    getattr(signal, name) for name in ("SIGTERM", "SIGHUP", "SIGBREAK") if hasattr(signal, name)
)
_CMAKE_TRUE_VALUES = frozenset({"1", "ON", "TRUE", "YES", "Y"})
# Generators that write compile_commands.json themselves on every configure.
_COMPILE_DATABASE_GENERATORS = ("Ninja", "Makefiles")
# Run inside the vcvars-initialised cmd.exe: dump its environment as ASCII-only
# JSON, so no console code page can garble a value on its way back.
_ENV_DUMP_CODE = "import json,os,sys;sys.stdout.write(json.dumps(dict(os.environ)))"
_VCVARS_VARIABLE = "LUMA_PIPELINE_VCVARS"
_ENV_PYTHON_VARIABLE = "LUMA_PIPELINE_PYTHON"
_ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004
_STD_OUTPUT_HANDLE = -11
_STD_ERROR_HANDLE = -12
_VC_TOOLS_COMPONENT = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
_VS_CMAKE_PARTS = ("Common7", "IDE", "CommonExtensions", "Microsoft", "CMake", "CMake", "bin")


class Status(str, Enum):
    """The outcome of one pipeline phase, as recorded in the run summaries."""

    OK = "ok"
    FAILED = "failed"
    ERROR = "error"
    AGENT_FAILED = "agent-failed"
    GATE_FAILED = "gate-failed"
    COMMIT_FAILED = "commit-failed"

    def __str__(self) -> str:
        return self.value


class PipelineError(RuntimeError):
    """A pipeline step could not run; carries the process exit code to use."""

    def __init__(self, message: str, *, exit_code: int = 1) -> None:
        super().__init__(message)
        self.exit_code = exit_code


class CheckpointError(PipelineError):
    """A git checkpoint was rejected (e.g. by a pre-commit hook)."""


class TerminationRequested(BaseException):
    """Raised in the main thread when the pipeline receives a termination signal.

    Like ``KeyboardInterrupt`` it derives from ``BaseException``, so phase-level
    error handling never swallows it, while every cleanup path — the
    process-tree kill and the artifact restore — still runs as it unwinds.
    """

    def __init__(self, signum: int) -> None:
        self.signum = signum
        super().__init__(f"received {signal.Signals(signum).name}")

    @property
    def exit_code(self) -> int:
        """The conventional ``128 + signal`` exit code."""
        return 128 + self.signum


@contextlib.contextmanager
def termination_handlers() -> Iterator[None]:
    """Turn SIGTERM / SIGHUP / SIGBREAK into :class:`TerminationRequested` for the block.

    Without this, a termination signal ends the pipeline on the spot and skips
    the cleanup that kills the agent's process tree (on POSIX the agent leads its
    own session, so the signal never reaches it). The first signal raises; later
    ones are ignored so they cannot interrupt the cleanup. The previous handlers
    are restored on exit. A no-op outside the main thread, where Python cannot
    install signal handlers.
    """
    if threading.current_thread() is not threading.main_thread():
        yield
        return
    is_raised = False

    def handle(signum: int, _frame: FrameType | None) -> None:
        nonlocal is_raised
        if is_raised:
            return
        is_raised = True
        raise TerminationRequested(signum)

    previous: dict[int, signal.Handlers | Callable[[int, FrameType | None], object] | int] = {}
    try:
        for signum in TERMINATION_SIGNALS:
            installed = signal.signal(signum, handle)
            # None means the old handler was not installed from Python.
            previous[signum] = signal.SIG_DFL if installed is None else installed
        yield
    finally:
        for signum, handler in previous.items():
            signal.signal(signum, handler)


# ── Console ───────────────────────────────────────────────────────


@functools.cache
def _enable_windows_vt(handle_id: int) -> bool:
    """Turn on ANSI escape processing for one Windows console handle.

    Cached because the console mode only needs setting once per handle.

    Args:
        handle_id: ``STD_OUTPUT_HANDLE`` or ``STD_ERROR_HANDLE``.

    Returns:
        True when the console now renders ANSI escapes.
    """
    try:
        kernel32 = ctypes.windll.kernel32  # type: ignore[attr-defined]
        handle = kernel32.GetStdHandle(handle_id)
        mode = ctypes.c_uint32()
        if not kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            return False
        return bool(
            kernel32.SetConsoleMode(handle, mode.value | _ENABLE_VIRTUAL_TERMINAL_PROCESSING)
        )
    except (AttributeError, OSError):
        return False


def _stream_supports_colour(stream: TextIO) -> bool:
    """True when *stream* is a terminal that renders colour and ``NO_COLOR`` is unset."""
    if os.environ.get("NO_COLOR"):
        return False
    isatty = getattr(stream, "isatty", None)
    if isatty is None or not isatty():
        return False
    if not _IS_WINDOWS:
        return True
    try:
        is_stderr = stream.fileno() == 2
    except (AttributeError, OSError, ValueError):
        return False
    return _enable_windows_vt(_STD_ERROR_HANDLE if is_stderr else _STD_OUTPUT_HANDLE)


class Console:
    """User-facing progress output with minimal, opt-out colour.

    Colour is used only when the stream is a terminal and ``NO_COLOR`` is unset.
    Warnings and errors go to stderr; everything else to stdout. The streams are
    looked up at call time unless given explicitly, so redirection works.
    """

    _CYAN = "\033[36m"
    _YELLOW = "\033[33m"
    _RED = "\033[31m"
    _GREEN = "\033[32m"
    _GREY = "\033[90m"
    _RESET = "\033[0m"

    def __init__(
        self,
        *,
        out: TextIO | None = None,
        err: TextIO | None = None,
        use_colour: bool | None = None,
    ) -> None:
        """Create a console.

        Args:
            out: Stream for progress output (default: ``sys.stdout`` at call time).
            err: Stream for warnings and errors (default: ``sys.stderr`` at call time).
            use_colour: Force colour on or off; ``None`` detects it per stream.
        """
        self._out = out
        self._err = err
        self._use_colour = use_colour
        # Decodes raw child output for a stream without a byte buffer; being
        # incremental, it keeps a UTF-8 sequence split across chunks intact.
        self._raw_decoder = codecs.getincrementaldecoder("utf-8")("replace")

    @property
    def out(self) -> TextIO:
        """The stream progress output goes to."""
        return self._out if self._out is not None else sys.stdout

    @property
    def err(self) -> TextIO:
        """The stream warnings and errors go to."""
        return self._err if self._err is not None else sys.stderr

    def _paint(self, text: str, colour: str, stream: TextIO) -> str:
        use_colour = self._use_colour
        if use_colour is None:
            use_colour = _stream_supports_colour(stream)
        return f"{colour}{text}{self._RESET}" if use_colour else text

    def line(self, text: str = "") -> None:
        """Print one line of progress output."""
        print(text, file=self.out, flush=True)

    def banner(self, title: str, subtitle: str = "") -> None:
        """Print a ruled section heading with an optional subtitle."""
        rule = self._paint("=" * 72, self._CYAN, self.out)
        self.line()
        self.line(rule)
        self.line(f"  {title}")
        if subtitle:
            self.line(f"  {subtitle}")
        self.line(rule)

    def ok(self, message: str) -> None:
        """Print a success message."""
        self.line(self._paint(message, self._GREEN, self.out))

    def note(self, message: str) -> None:
        """Print a message that deserves attention but is not a warning."""
        self.line(self._paint(message, self._YELLOW, self.out))

    def dim(self, message: str) -> None:
        """Print a low-importance message."""
        self.line(self._paint(message, self._GREY, self.out))

    def dry(self, message: str) -> None:
        """Print the command a dry run would have executed."""
        self.dim(f"    [dry-run] {message}")

    def warn(self, message: str) -> None:
        """Print a warning to the error stream."""
        self.out.flush()
        print(
            f"{self._paint('Warning:', self._YELLOW, self.err)} {message}",
            file=self.err,
            flush=True,
        )

    def error(self, message: str) -> None:
        """Print an error to the error stream."""
        self.out.flush()
        print(f"{self._paint('Error:', self._RED, self.err)} {message}", file=self.err, flush=True)

    def flush(self) -> None:
        """Flush both streams, so a child process's output lands after ours."""
        self.out.flush()
        self.err.flush()

    def write_raw(self, chunk: bytes) -> None:
        """Echo a child's raw output, bypassing text encoding when possible."""
        buffer = getattr(self.out, "buffer", None)
        self.out.flush()
        if buffer is None:
            self.out.write(self._raw_decoder.decode(chunk))
            self.out.flush()
            return
        buffer.write(chunk)
        buffer.flush()

    def end_raw(self) -> None:
        """Finish one child's raw output; a truncated UTF-8 sequence prints as U+FFFD."""
        tail = self._raw_decoder.decode(b"", final=True)
        self._raw_decoder.reset()
        if tail:
            self.out.write(tail)
            self.out.flush()


# ── Pure helpers ──────────────────────────────────────────────────


def render_command(label: str, args: Sequence[str], *, instruction: str | None = None) -> str:
    """Render a command for display, quoted with POSIX shell rules.

    The quoting (``shlex.join``) also reads correctly in PowerShell for these
    arguments; the agent instruction is replaced by a placeholder.

    Args:
        label: The program name to show first.
        args: The program's arguments.
        instruction: An argument to elide as ``<instruction>``.

    Returns:
        The command line as one string.
    """
    shown = [
        INSTRUCTION_PLACEHOLDER if instruction is not None and arg == instruction else arg
        for arg in args
    ]
    return shlex.join([label, *shown])


def build_copilot_args(
    mode: str,
    *,
    model: str = "",
    effort: str = "",
    log_dir: Path | None = None,
) -> list[str]:
    """Return the Copilot CLI arguments for one phase (the prompt goes on stdin).

    The prompt is never passed as ``-p <text>``: a multi-line argv element is
    mangled whenever the CLI is launched across a boundary that cannot carry
    newlines (an npm ``copilot.cmd`` shim, WSL interop). With ``-p`` omitted and
    stdin not a TTY, Copilot reads the piped prompt and runs non-interactively.

    Args:
        mode: ``plan`` (read-only audit) or ``agent`` (mutating fix).
        model: Value for ``--model``; empty to let Copilot choose.
        effort: Value for ``--effort``; forwarded only with an explicit model.
        log_dir: Directory for Copilot's own logs, if any.

    Returns:
        The argument vector, without the executable.
    """
    args = ["--allow-all-tools", "--no-ask-user", "--no-color"]
    if mode == "plan":
        # Read-only audit. Copilot's interactive --plan mode delivers its result
        # through exit_plan_mode / plan.md rather than stdout, so restrict the
        # tools instead. JSONL output lets the caller keep only the final message
        # (text mode interleaves narration and parallel sub-agent transcripts).
        args += ["--output-format=json", "--available-tools=view,grep,glob"]
    else:
        args.append("--deny-tool=shell(git push)")
    if log_dir is not None:
        args.append(f"--log-dir={log_dir}")
    if model:
        args.append(f"--model={model}")
    # Copilot rejects reasoning effort for 'auto' (and an omitted model resolves
    # to 'auto'), so only forward it for an explicit, non-auto model.
    if effort and model and model != "auto":
        args.append(f"--effort={effort}")
    return args


def build_claude_args(
    instruction: str | None,
    mode: str,
    *,
    model: str = "",
    effort: str = "",
) -> list[str]:
    """Return the Claude Code CLI arguments for one phase.

    Args:
        instruction: The prompt, passed as the positional argument after ``-p``
            (print mode); ``None`` omits it so Claude reads it from stdin.
        mode: ``plan`` (read-only audit) or ``agent`` (mutating fix).
        model: Value for ``--model``; empty to let Claude choose.
        effort: Value for ``--effort``; empty to omit.

    Returns:
        The argument vector, without the executable.
    """
    args = ["-p"] if instruction is None else ["-p", instruction]
    args += ["--output-format", "text"]
    if mode == "plan":
        args += ["--permission-mode", "plan"]
    else:
        # Deny rules beat allow rules, so the push ban holds despite Bash access.
        args += [
            "--permission-mode",
            "acceptEdits",
            "--allowedTools",
            "Bash",
            "Edit",
            "Write",
            "--disallowedTools",
            "Bash(git push *)",
        ]
    if model:
        args += ["--model", model]
    if effort:
        args += ["--effort", effort]
    return args


def extract_report_from_jsonl(text: str) -> str:
    """Reduce a Copilot ``--output-format json`` event stream to the final report.

    The report is the content of the last ``assistant.message`` event carrying
    text: tool-call-only steps have empty content, and background sub-agents
    finish before the main agent's closing synthesis. Malformed or unexpected
    lines are skipped. Splits on ``\\n`` only, because U+2028/U+2029 are legal
    unescaped inside JSON strings.

    Args:
        text: The captured JSONL output.

    Returns:
        The report, or ``""`` when no message carries text.
    """
    report = ""
    for raw_line in text.split("\n"):
        line = raw_line.strip()
        if not line:
            continue
        try:
            event = json.loads(line)
        except (ValueError, RecursionError):
            continue
        if not isinstance(event, dict) or event.get("type") != "assistant.message":
            continue
        data = event.get("data")
        if not isinstance(data, dict):
            continue
        content = data.get("content")
        if isinstance(content, str) and content.strip():
            report = content
    return report


def is_quota_exhausted(transcript: str) -> bool:
    """True when a Copilot transcript shows monthly-quota exhaustion.

    Copilot prints the quota message and then exits 0 without doing any work.
    """
    return QUOTA_SIGNATURE in transcript.lower()


def should_abort(status: Status, *, continue_on_failure: bool, reverts_cleanly: bool) -> bool:
    """Decide whether a phase's status stops the fix run.

    Any failure stops the run unless *continue_on_failure*. A ``commit-failed``
    is systemic — the hook keeps rejecting the commit and ``git add -A``
    re-stages the change into every later checkpoint — so it stops the run even
    then, unless the failed phase is reverted to a clean tree.

    Args:
        status: The phase's outcome.
        continue_on_failure: The user asked to keep going past failures.
        reverts_cleanly: A failed phase is reverted *and* its untracked files
            removed, which needs ``--revert-on-failure`` and a run that started
            from a clean tree.

    Returns:
        True when the run must stop.
    """
    if status is Status.OK:
        return False
    if not continue_on_failure:
        return True
    return status is Status.COMMIT_FAILED and not reverts_cleanly


def is_within(path: Path, root: Path) -> bool:
    """True when *path* is *root* itself or lies inside it."""
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return True


# ── Executable discovery ──────────────────────────────────────────


def find_agent_executable(agent: str, *, search_path: str | None = None) -> str | None:
    """Resolve the agent CLI, honouring ``LUMA_COPILOT`` / ``LUMA_CLAUDE``.

    ``shutil.which`` applies ``PATHEXT`` on Windows, so npm ``.cmd`` shims are
    found and returned as a full path that ``subprocess`` can launch.

    Args:
        agent: ``copilot`` or ``claude``.
        search_path: ``PATH`` to search (default: the process ``PATH``).

    Returns:
        The executable's full path, or None when it is not installed.
    """
    name = os.environ.get(AGENT_ENV_OVERRIDES[agent]) or agent
    return shutil.which(name, path=search_path)


def is_batch_shim(executable: str | None) -> bool:
    """True when *executable* is a Windows ``.cmd`` / ``.bat`` shim run by cmd.exe."""
    return executable is not None and Path(executable).suffix.lower() in (".cmd", ".bat")


def agent_needs_missing_node(executable: str, *, search_path: str | None = None) -> bool:
    """True when *executable* is an npm shim that needs ``node`` and none is on PATH.

    A native binary (e.g. the standalone ``copilot.exe``) runs without node, so
    only ``.cmd`` / ``.bat`` shims and ``#!`` scripts that mention node count.
    """
    if shutil.which("node", path=search_path):
        return False
    path = Path(executable)
    suffix = path.suffix.lower()
    if suffix == ".exe":
        return False
    try:
        with path.open("rb") as handle:
            head = handle.read(4096)
    except OSError:
        return False
    if suffix in (".cmd", ".bat"):
        return b"node" in head.lower()
    first_line = head.split(b"\n", 1)[0]
    return first_line.startswith(b"#!") and b"node" in first_line


def require_agent(agent: str, *, search_path: str | None = None) -> str:
    """Return the agent executable, or raise when it cannot run.

    Args:
        agent: ``copilot`` or ``claude``.
        search_path: ``PATH`` to search (default: the process ``PATH``).

    Returns:
        The executable's full path.

    Raises:
        PipelineError: With exit code 127 when the CLI (or the node it needs)
            is missing.
    """
    executable = find_agent_executable(agent, search_path=search_path)
    env_name = AGENT_ENV_OVERRIDES[agent]
    if executable is None:
        name = os.environ.get(env_name) or agent
        raise PipelineError(
            f"the {agent} CLI ('{name}') was not found on PATH (install it, or set {env_name}).",
            exit_code=127,
        )
    if agent == "copilot" and agent_needs_missing_node(executable, search_path=search_path):
        raise PipelineError(
            "the copilot CLI requires node, but node is not on PATH. "
            "Install Node.js and put its bin directory on PATH.",
            exit_code=127,
        )
    return executable


def find_repo_root(start: Path) -> Path:
    """Resolve the repository root from the pipeline directory.

    The pipeline lives in ``scripts/pipeline/``, so the root is two levels up;
    ``git rev-parse --show-toplevel`` is the fallback when the marker is absent.

    Raises:
        PipelineError: When neither finds the root.
    """
    candidate = start.resolve().parents[1]
    if (candidate / "CMakePresets.json").is_file():
        return candidate
    git = shutil.which("git")
    if git is not None:
        completed = subprocess.run(
            [git, "-C", str(start), "rev-parse", "--show-toplevel"],
            capture_output=True,
            text=True,
            check=False,
        )
        if completed.returncode == 0 and completed.stdout.strip():
            return Path(completed.stdout.strip())
    raise PipelineError(f"could not resolve the Luma repository root from '{start}'.")


# ── Process plumbing ──────────────────────────────────────────────


# -- Windows Job Objects -------------------------------------------------------
#
# Windows has no process groups, and ``taskkill /T`` walks the tree from a live
# parent, so the descendants of a child that already exited are unreachable.
# Every supervised child therefore runs in a Job Object: the job contains the
# child and everything it spawns, however deep, and terminating the job kills
# them all — even after the direct child exited.
#
# The child is started with CREATE_SUSPENDED, assigned to the job, and only then
# resumed, so it cannot spawn anything before it is in the job (assigning a
# running child races with its first CreateProcess). Popen closes the primary
# thread handle, so the whole process is resumed with NtResumeProcess.
#
# The job is created with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, so if the pipeline
# itself dies abruptly the OS closes the handle and the agent's tree dies with
# it. After a normal exit the flag is cleared before the handle is closed, so a
# process the child deliberately left running survives, as on POSIX. When the
# job cannot be set up (e.g. nested-job restrictions before Windows 8), the
# tree kill falls back to ``taskkill /T``.

_CREATE_SUSPENDED = 0x00000004
_JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000
_JOB_OBJECT_EXTENDED_LIMIT_INFORMATION_CLASS = 9
_PROCESS_TERMINATE = 0x0001
_PROCESS_SET_QUOTA = 0x0100
_PROCESS_SUSPEND_RESUME = 0x0800


class _IoCounters(ctypes.Structure):
    _fields_ = [
        (name, ctypes.c_uint64)
        for name in (
            "ReadOperationCount",
            "WriteOperationCount",
            "OtherOperationCount",
            "ReadTransferCount",
            "WriteTransferCount",
            "OtherTransferCount",
        )
    ]


class _BasicLimitInformation(ctypes.Structure):
    _fields_ = [
        ("PerProcessUserTimeLimit", ctypes.c_int64),
        ("PerJobUserTimeLimit", ctypes.c_int64),
        ("LimitFlags", ctypes.c_uint32),
        ("MinimumWorkingSetSize", ctypes.c_size_t),
        ("MaximumWorkingSetSize", ctypes.c_size_t),
        ("ActiveProcessLimit", ctypes.c_uint32),
        ("Affinity", ctypes.c_size_t),
        ("PriorityClass", ctypes.c_uint32),
        ("SchedulingClass", ctypes.c_uint32),
    ]


class _ExtendedLimitInformation(ctypes.Structure):
    _fields_ = [
        ("BasicLimitInformation", _BasicLimitInformation),
        ("IoInfo", _IoCounters),
        ("ProcessMemoryLimit", ctypes.c_size_t),
        ("JobMemoryLimit", ctypes.c_size_t),
        ("PeakProcessMemoryUsed", ctypes.c_size_t),
        ("PeakJobMemoryUsed", ctypes.c_size_t),
    ]


@functools.cache
def _job_api() -> tuple[ctypes.CDLL, ctypes.CDLL]:
    """Return kernel32 and ntdll with the prototypes the job code needs.

    Private ``WinDLL`` instances, so these prototypes never clash with other
    users of ``ctypes.windll``.
    """
    from ctypes import wintypes  # Windows-only, so imported on first use

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)  # type: ignore[attr-defined]
    ntdll = ctypes.WinDLL("ntdll")  # type: ignore[attr-defined]
    prototypes: dict[str, tuple[object, list[object]]] = {
        "CreateJobObjectW": (wintypes.HANDLE, [ctypes.c_void_p, wintypes.LPCWSTR]),
        "SetInformationJobObject": (
            wintypes.BOOL,
            [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD],
        ),
        "AssignProcessToJobObject": (wintypes.BOOL, [wintypes.HANDLE, wintypes.HANDLE]),
        "TerminateJobObject": (wintypes.BOOL, [wintypes.HANDLE, wintypes.UINT]),
        "OpenProcess": (wintypes.HANDLE, [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]),
        "CloseHandle": (wintypes.BOOL, [wintypes.HANDLE]),
    }
    for name, (restype, argtypes) in prototypes.items():
        function = getattr(kernel32, name)
        function.restype = restype
        function.argtypes = argtypes
    ntdll.NtResumeProcess.restype = ctypes.c_long
    ntdll.NtResumeProcess.argtypes = [wintypes.HANDLE]
    return kernel32, ntdll


class _WindowsJob:
    """A kill-on-close Job Object holding one supervised child's process tree."""

    def __init__(self, handle: int) -> None:
        self._handle: int | None = handle

    @classmethod
    def create(cls) -> _WindowsJob | None:
        """Create the job; None when the API is unavailable or refuses."""
        try:
            kernel32, _ = _job_api()
        except (AttributeError, OSError):
            return None
        handle = kernel32.CreateJobObjectW(None, None)
        if not handle:
            return None
        job = cls(handle)
        if not job._set_kill_on_close(is_enabled=True):
            job._close_handle()
            return None
        return job

    def _set_kill_on_close(self, *, is_enabled: bool) -> bool:
        kernel32, _ = _job_api()
        info = _ExtendedLimitInformation()
        if is_enabled:
            info.BasicLimitInformation.LimitFlags = _JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        return bool(
            kernel32.SetInformationJobObject(
                self._handle,
                _JOB_OBJECT_EXTENDED_LIMIT_INFORMATION_CLASS,
                ctypes.byref(info),
                ctypes.sizeof(info),
            )
        )

    def adopt(self, pid: int) -> bool:
        """Assign the suspended process *pid* to the job, then resume it.

        Returns:
            True when the process is in the job; False when the assignment was
            refused (it is resumed regardless).

        Raises:
            OSError: When the process cannot be opened or resumed; the caller
                must kill it.
        """
        kernel32, ntdll = _job_api()
        access = _PROCESS_SET_QUOTA | _PROCESS_TERMINATE | _PROCESS_SUSPEND_RESUME
        process_handle = kernel32.OpenProcess(access, False, pid)
        if not process_handle:
            raise ctypes.WinError(ctypes.get_last_error())  # type: ignore[attr-defined]
        try:
            is_assigned = bool(kernel32.AssignProcessToJobObject(self._handle, process_handle))
            status = ntdll.NtResumeProcess(process_handle)
        finally:
            kernel32.CloseHandle(process_handle)
        if status < 0:
            raise OSError(f"could not resume the child process (NTSTATUS {status & 0xFFFFFFFF:#x})")
        return is_assigned

    def terminate(self) -> None:
        """Kill every process in the job, including orphaned descendants."""
        if self._handle is not None:
            kernel32, _ = _job_api()
            kernel32.TerminateJobObject(self._handle, 1)

    def release(self) -> None:
        """Close the job without killing what is left in it."""
        if self._handle is not None:
            self._set_kill_on_close(is_enabled=False)
            self._close_handle()

    def close(self) -> None:
        """Close the job; kill-on-close ends every process still in it."""
        self._close_handle()

    def _close_handle(self) -> None:
        if self._handle is not None:
            kernel32, _ = _job_api()
            kernel32.CloseHandle(self._handle)
            self._handle = None


def _start_process(
    args: Sequence[str], **popen_options: object
) -> tuple[subprocess.Popen[bytes], _WindowsJob | None]:
    """Start a child that can later be killed together with all its descendants.

    POSIX: the child leads its own session, so its process group is the tree.
    Windows: the child runs in a :class:`_WindowsJob` (None when the job could
    not be set up, in which case the tree kill falls back to ``taskkill /T``).

    Args:
        args: The command line.
        popen_options: Further ``subprocess.Popen`` arguments.

    Returns:
        The process and its job.
    """
    job = _WindowsJob.create() if _IS_WINDOWS else None
    process: subprocess.Popen[bytes] = subprocess.Popen(  # type: ignore[call-overload]
        list(args),
        start_new_session=not _IS_WINDOWS,
        creationflags=_CREATE_SUSPENDED if job is not None else 0,
        **popen_options,
    )
    if job is None:
        return process, None
    try:
        is_assigned = job.adopt(process.pid)
    except BaseException:
        with contextlib.suppress(OSError):
            process.kill()
        job.close()
        raise
    if not is_assigned:
        job.release()
        return process, None
    return process, job


def _kill_process_tree(process: subprocess.Popen[bytes], job: _WindowsJob | None = None) -> None:
    """Kill *process* and its descendants (e.g. node behind a ``.cmd`` shim).

    POSIX: the child leads its own session (see :func:`_start_process`), so its
    process group holds exactly the tree. The group is signalled even when the
    leader has already exited: a grandchild that outlived it is still in the
    group, and may be holding the leader's output pipe open.

    Windows: terminating the child's *job* kills every process in it, including
    the descendants of a child that already exited. Without a job,
    ``taskkill /T`` walks the tree from the live child, which cannot reach the
    descendants of an exited one.
    """
    if _IS_WINDOWS:
        if job is not None:
            job.terminate()
        elif process.poll() is None:
            taskkill = shutil.which("taskkill")
            if taskkill is not None:
                subprocess.run(
                    [taskkill, "/T", "/F", "/PID", str(process.pid)],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False,
                )
    else:
        with contextlib.suppress(ProcessLookupError, PermissionError):
            os.killpg(process.pid, signal.SIGKILL)
    if process.poll() is None:
        with contextlib.suppress(OSError):
            process.kill()
    # Reap it, so no zombie outlives the pipeline's handle on it. Polled rather
    # than wait(timeout), which can hang on Windows if Ctrl+C lands mid-wait.
    deadline = time.monotonic() + _DRAIN_TIMEOUT_SECONDS
    while process.poll() is None and time.monotonic() < deadline:
        time.sleep(_POLL_INTERVAL_SECONDS)


def _feed_stdin(process: subprocess.Popen[bytes], text: str) -> None:
    """Write *text* to the child's stdin and close it; tolerate an early exit."""
    if process.stdin is None:
        return
    # The CLI may exit before reading its prompt (e.g. it rejected --model at
    # start-up); its output is still collected by the caller.
    with contextlib.suppress(OSError):
        process.stdin.write(text.encode("utf-8"))
    with contextlib.suppress(OSError):
        process.stdin.close()


class _OutputPump:
    """Copy a child's output stream to a callback on a daemon reader thread.

    Reading off the main thread lets the main thread wait with short timeouts,
    so it notices Ctrl+C, termination signals, and deadlines even while a
    grandchild holds the pipe open. A callback failure is stored and re-raised
    in the main thread by :meth:`raise_if_failed`; after it, or after
    :meth:`detach`, output is still read (so the child never blocks on a full
    pipe) but discarded.
    """

    def __init__(self, stream: IO[bytes], on_chunk: Callable[[bytes], None]) -> None:
        self._stream = stream
        self._on_chunk = on_chunk
        self._lock = threading.Lock()
        self._is_detached = False
        self._error: BaseException | None = None
        self._thread = threading.Thread(target=self._pump, daemon=True)

    def start(self) -> None:
        """Start reading."""
        self._thread.start()

    def _pump(self) -> None:
        read = getattr(self._stream, "read1", self._stream.read)
        try:
            while True:
                try:
                    chunk = read(_CHUNK_SIZE)
                except (OSError, ValueError):
                    return
                if not chunk:
                    return
                with self._lock:
                    if self._is_detached:
                        continue
                    try:
                        self._on_chunk(chunk)
                    except BaseException as err:
                        self._error = err
                        self._is_detached = True
        finally:
            # Closed here, at end of file, because a reader the main thread gave
            # up on may still be blocked on the stream.
            with contextlib.suppress(OSError):
                self._stream.close()

    def is_alive(self) -> bool:
        """True while the stream has not reached end of file."""
        return self._thread.is_alive()

    def join(self, timeout: float) -> None:
        """Wait up to *timeout* seconds for end of file (no-op if never started)."""
        if self._thread.ident is not None:
            self._thread.join(timeout)

    def detach(self) -> None:
        """Stop forwarding output, waiting (bounded) for an in-flight callback."""
        is_locked = self._lock.acquire(timeout=_POST_KILL_DRAIN_SECONDS)
        self._is_detached = True
        if is_locked:
            self._lock.release()

    def raise_if_failed(self) -> None:
        """Re-raise a callback failure in the calling thread."""
        if self._error is not None:
            raise self._error


def _join_pumps(pumps: Sequence[_OutputPump], timeout: float) -> bool:
    """Wait, in short interruptible slices, until every pump reached end of file.

    Returns:
        True when they all finished within *timeout* seconds.

    Raises:
        BaseException: A pump's callback failure, re-raised here.
    """
    deadline = time.monotonic() + timeout
    for pump in pumps:
        while pump.is_alive():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            pump.join(min(remaining, _POLL_INTERVAL_SECONDS))
            pump.raise_if_failed()
    return True


def _write_text(path: Path, text: str) -> None:
    """Write UTF-8 text with LF line endings, creating parent directories."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def _no_report_body(exit_code: int, stderr: str) -> str:
    """Build the report written when an audit phase produced no output."""
    captured = stderr.rstrip("\r\n") or "(none)"
    lines = [
        "# Phase produced no report",
        "",
        f"The agent exited with code {exit_code} and wrote nothing to standard output, "
        "so no report could be produced.",
        "",
        "Captured standard error:",
        "",
        "```",
        captured,
        "```",
    ]
    return "\n".join(lines) + "\n"


def _replace_with(source: Path, destination: Path) -> None:
    """Move *source* to *destination*, replacing whatever is there."""
    if destination.is_dir() and not destination.is_symlink():
        shutil.rmtree(destination, ignore_errors=True)
    elif destination.exists():
        destination.unlink()
    shutil.move(str(source), str(destination))


@dataclass(frozen=True)
class AgentOutcome:
    """The result of one agent phase."""

    mode: str
    exit_code: int
    is_dry_run: bool = False

    @property
    def success(self) -> bool:
        """True when the agent exited with code 0."""
        return self.exit_code == 0


# ── Pipeline: the single home of every side effect ────────────────


class Pipeline:
    """Runs every side effect of the pipeline, with one uniform dry-run path.

    The pipeline works on its own copy of the environment. Tool discovery and
    the MSVC developer environment extend that copy — never ``os.environ`` —
    and every child process receives it.
    """

    def __init__(
        self,
        repo_root: Path,
        *,
        is_dry_run: bool = False,
        console: Console | None = None,
        python_executable: str = sys.executable,
        model_cache: dict[tuple[str, str], str] | None = None,
        env: Mapping[str, str] | None = None,
    ) -> None:
        """Create a pipeline rooted at *repo_root*.

        Args:
            repo_root: Repository root; every tool runs from here.
            is_dry_run: Print each command instead of running it.
            console: Where progress, dry-run lines, and warnings go.
            python_executable: Interpreter for ``scripts/format.py`` / ``lint.py``.
            model_cache: Resolved Copilot models, shared across stages of one run.
            env: Environment for child processes (default: a copy of ``os.environ``).
        """
        self.repo_root = repo_root
        self.is_dry_run = is_dry_run
        self.console = console if console is not None else Console()
        self.python_executable = python_executable
        self.model_cache = model_cache if model_cache is not None else {}
        source = os.environ if env is None else env
        # Windows environment names are case-insensitive; normalise them so
        # lookups and the variables vcvars reports agree.
        self._env = {
            (name.upper() if _IS_WINDOWS else name): value for name, value in source.items()
        }
        self._is_msvc_ready = False

    # -- low-level runners -------------------------------------------------

    @property
    def _search_path(self) -> str:
        return self._env.get("PATH", "")

    def _which(self, name: str) -> str | None:
        """Resolve *name* on the pipeline's own ``PATH``."""
        return shutil.which(name, path=self._search_path)

    def _require_tool(self, name: str) -> str:
        """Resolve *name* on the pipeline's ``PATH``, or raise ``PipelineError``."""
        path = self._which(name)
        if path is None:
            raise PipelineError(f"{name} was not found on PATH.")
        return path

    def _run(self, args: Sequence[str], *, should_discard_output: bool = False) -> int:
        """Run a command from the repo root, streaming its output; return its code."""
        self.console.flush()
        target = subprocess.DEVNULL if should_discard_output else None
        completed = subprocess.run(
            list(args),
            cwd=self.repo_root,
            env=self._env,
            stdout=target,
            stderr=target,
            check=False,
        )
        return completed.returncode

    def _capture(self, args: Sequence[str]) -> subprocess.CompletedProcess[str]:
        """Run a command from the repo root and capture its text output."""
        return subprocess.run(
            list(args),
            cwd=self.repo_root,
            env=self._env,
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )

    def _run_supervised(
        self,
        args: Sequence[str],
        *,
        stdin_text: str,
        on_stdout: Callable[[bytes], None],
        on_stderr: Callable[[bytes], None] | None = None,
        timeout: float | None = None,
    ) -> int | None:
        """Run a child in its own process group and pump its output on reader threads.

        The main thread only waits, in short slices, so Ctrl+C, a termination
        signal, a reader failure, or the deadline is acted on promptly. Any of
        them kills the whole process tree; an exception then propagates. After
        the child exits, its pipes are drained for a bounded time: a grandchild
        still holding them open is then killed with the rest of the tree.

        Args:
            args: The command line.
            stdin_text: Text for the child's stdin, which is then closed.
            on_stdout: Receives each chunk of standard output (called on a
                reader thread).
            on_stderr: Receives standard error; ``None`` merges it into stdout.
            timeout: Seconds before the tree is killed; ``None`` waits forever.

        Returns:
            The exit code, or ``None`` when the deadline passed.
        """
        self.console.flush()
        # Its own process group (POSIX) or job (Windows), so the whole process
        # tree can be killed if anything goes wrong while it runs.
        process, job = _start_process(
            args,
            cwd=self.repo_root,
            env=self._env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT if on_stderr is None else subprocess.PIPE,
        )
        streams = [(process.stdout, on_stdout), (process.stderr, on_stderr)]
        pumps = [
            _OutputPump(stream, callback)
            for stream, callback in streams
            if stream is not None and callback is not None
        ]
        # Feed stdin from a thread so a prompt larger than the pipe buffer cannot
        # deadlock against the output being read.
        writer = threading.Thread(target=_feed_stdin, args=(process, stdin_text), daemon=True)
        deadline = None if timeout is None else time.monotonic() + timeout
        try:
            for pump in pumps:
                pump.start()
            writer.start()
            # poll() + sleep() rather than wait(timeout): a KeyboardInterrupt
            # that lands inside Popen.wait(timeout) after the timeout elapsed
            # makes its SIGINT grace period negative, which Windows treats as an
            # infinite wait for the child.
            while (exit_code := process.poll()) is None:
                for pump in pumps:
                    pump.raise_if_failed()
                if deadline is not None and time.monotonic() >= deadline:
                    _kill_process_tree(process, job)
                    break
                time.sleep(_POLL_INTERVAL_SECONDS)
            if not _join_pumps(pumps, _DRAIN_TIMEOUT_SECONDS):
                # A descendant still holds the output open: take it down too.
                _kill_process_tree(process, job)
                if not _join_pumps(pumps, _POST_KILL_DRAIN_SECONDS):
                    self.console.warn(
                        "a process the child started still holds its output open; "
                        "no longer reading it."
                    )
            for pump in pumps:
                pump.raise_if_failed()
        except BaseException:
            # Covers Ctrl+C and termination signals: never leave a child running.
            _kill_process_tree(process, job)
            raise
        finally:
            for pump in pumps:
                pump.detach()
            if job is not None:
                # Everything was killed or drained; anything the child left
                # running on purpose (and not holding its output) survives.
                job.release()
        writer.join(_POLL_INTERVAL_SECONDS)
        return exit_code

    def _run_bounded(
        self, args: Sequence[str], *, stdin_text: str, timeout: float
    ) -> tuple[int | None, str]:
        """Run with a deadline, killing the whole process tree on expiry.

        Returns:
            The exit code (``None`` when the deadline passed) and the merged
            stdout/stderr output collected so far.
        """
        chunks: list[bytes] = []
        exit_code = self._run_supervised(
            args, stdin_text=stdin_text, on_stdout=chunks.append, timeout=timeout
        )
        return exit_code, b"".join(chunks).decode("utf-8", "replace")

    # -- file system -------------------------------------------------------

    def make_dirs(self, path: Path) -> None:
        """Create *path* and its parents (skipped in a dry run)."""
        if not self.is_dry_run:
            path.mkdir(parents=True, exist_ok=True)

    def write_text(self, path: Path, text: str) -> None:
        """Write a UTF-8 text file with LF line endings (skipped in a dry run)."""
        if not self.is_dry_run:
            _write_text(path, text)

    # -- agent ---------------------------------------------------------------

    def resolve_copilot_model(
        self,
        agent: str,
        model: str,
        *,
        timeout: float = MODEL_PROBE_TIMEOUT_SECONDS,
    ) -> str:
        """Return the model to use, falling back to ``auto`` when Copilot rejects it.

        The Copilot CLI validates ``--model`` against its own entitlement, which on
        some org-managed accounts rejects every explicit model. One bounded probe
        detects that so the run still works. Fails open (keeps *model*) for
        claude, an empty/``auto`` model, a dry run, a missing CLI, or a timeout.

        Args:
            agent: ``copilot`` or ``claude``.
            model: The requested model.
            timeout: Seconds before the probe is abandoned.

        Returns:
            *model*, or ``auto`` when Copilot cannot select it.
        """
        if agent != "copilot" or self.is_dry_run or not model or model == "auto":
            return model
        cached = self.model_cache.get((agent, model))
        if cached is not None:
            return cached
        executable = find_agent_executable("copilot", search_path=self._search_path)
        if executable is None:
            return model
        probe = [
            executable,
            "--model",
            model,
            "--output-format=json",
            "--available-tools=view",
            "--allow-all-tools",
            "--no-ask-user",
            "--no-color",
        ]
        resolved = model
        try:
            exit_code, output = self._run_bounded(
                probe, stdin_text=MODEL_PROBE_PROMPT, timeout=timeout
            )
        except OSError:
            exit_code, output = None, ""
        # A timeout means the CLI accepted the model and is mid-turn: keep it.
        if exit_code is not None and _MODEL_UNAVAILABLE.search(output):
            self.console.warn(
                f"Copilot CLI cannot select model '{model}' for this account; model access "
                "is governed by your org/enterprise Copilot policy. Falling back to 'auto'."
            )
            resolved = "auto"
        self.model_cache[(agent, model)] = resolved
        return resolved

    def invoke_agent_phase(
        self,
        *,
        agent: str,
        mode: str,
        instruction: str,
        output_file: Path | None = None,
        log_file: Path | None = None,
        log_dir: Path | None = None,
        model: str = "",
        effort: str = "",
    ) -> AgentOutcome:
        """Run one agent phase in read-only ``plan`` or mutating ``agent`` mode.

        ``plan`` captures the final report to *output_file* (Copilot's JSONL is
        reduced to its last message); ``agent`` streams the transcript to the
        console and tees it to *log_file*.

        Args:
            agent: ``copilot`` or ``claude``.
            mode: ``plan`` or ``agent``.
            instruction: The prompt for the agent.
            output_file: Where a ``plan`` phase's report goes (required for it).
            log_file: Where an ``agent`` phase's transcript goes (required for it).
            log_dir: Directory for Copilot's own logs (``--log-dir``; Copilot only).
            model: Model to request; empty to let the agent choose.
            effort: Reasoning effort; empty to omit.

        Returns:
            The phase outcome.

        Raises:
            PipelineError: When the agent CLI is missing.
            ValueError: When the sink file for *mode* is missing.
        """
        sink = output_file if mode == "plan" else log_file
        if sink is None:
            raise ValueError(
                f"{mode} mode requires {'output_file' if mode == 'plan' else 'log_file'}"
            )

        executable = find_agent_executable(agent, search_path=self._search_path)
        # cmd.exe cannot carry a multi-line argument, so a prompt goes on stdin
        # for Copilot always and for Claude when it is launched through a shim.
        should_pipe_prompt = agent == "copilot" or is_batch_shim(executable)
        if agent == "claude":
            argv_prompt = None if should_pipe_prompt else instruction
            args = build_claude_args(argv_prompt, mode, model=model, effort=effort)
        else:
            args = build_copilot_args(mode, model=model, effort=effort, log_dir=log_dir)
        stdin_text = instruction if should_pipe_prompt else ""

        if self.is_dry_run:
            self.console.dry(render_command(agent, args, instruction=instruction))
            if should_pipe_prompt:
                self.console.dry(f"stdin  <- {INSTRUCTION_PLACEHOLDER}")
            self.console.dry(f"{'stdout ->' if mode == 'plan' else 'tee    ->'} {sink}")
            return AgentOutcome(mode, 0, is_dry_run=True)

        command = [require_agent(agent, search_path=self._search_path), *args]
        if mode == "plan":
            exit_code = self._run_plan_phase(agent, command, stdin_text, sink)
        else:
            exit_code = self._run_agent_phase(agent, command, stdin_text, sink)
        return AgentOutcome(mode, exit_code)

    def _run_plan_phase(
        self, agent: str, command: list[str], stdin_text: str, output_file: Path
    ) -> int:
        """Capture a read-only phase's report; never record a hollow success."""
        stdout_chunks: list[bytes] = []
        stderr_chunks: list[bytes] = []
        returned = self._run_supervised(
            command,
            stdin_text=stdin_text,
            on_stdout=stdout_chunks.append,
            on_stderr=stderr_chunks.append,
        )
        # Without a deadline the child always reports an exit code.
        exit_code = 1 if returned is None else returned
        stdout = b"".join(stdout_chunks).decode("utf-8", "replace")
        stderr = b"".join(stderr_chunks).decode("utf-8", "replace")
        report = extract_report_from_jsonl(stdout) if agent == "copilot" else stdout

        if report.strip():
            _write_text(output_file, report.rstrip("\r\n") + "\n")
            if agent == "claude" and stderr.strip():
                self.console.err.write(stderr)
            return exit_code

        if stdout.strip():
            # Output but no extractable final message: keep the raw transcript.
            self.console.warn(
                "could not extract a final report from Copilot output; "
                "keeping the raw JSONL transcript."
            )
            if stderr.strip():
                self.console.warn(f"agent stderr: {stderr[:500].rstrip()}")
            _write_text(output_file, stdout)
            return exit_code or 1

        self.console.warn(
            "the agent produced no report for this phase (empty output); "
            "see the captured error in the report."
        )
        _write_text(output_file, _no_report_body(exit_code, stderr))
        return exit_code or 1

    def _run_agent_phase(
        self, agent: str, command: list[str], stdin_text: str, log_file: Path
    ) -> int:
        """Stream a mutating phase to the console and tee it to *log_file*."""
        log_file.parent.mkdir(parents=True, exist_ok=True)
        with log_file.open("wb") as log:

            def tee(chunk: bytes) -> None:
                log.write(chunk)
                log.flush()
                self.console.write_raw(chunk)

            try:
                returned = self._run_supervised(command, stdin_text=stdin_text, on_stdout=tee)
            finally:
                self.console.end_raw()
        exit_code = 1 if returned is None else returned

        if agent == "copilot" and exit_code == 0:
            transcript = log_file.read_bytes().decode("utf-8", "replace")
            if is_quota_exhausted(transcript):
                self.console.warn(
                    "Copilot monthly quota exceeded; this phase did no work. Wait for the "
                    "quota to reset (or switch account/model), then re-run."
                )
                exit_code = 1
        return exit_code

    # -- build, test, lint -----------------------------------------------------

    def build_and_test(
        self,
        *,
        preset: str = "default",
        skip_build: bool = False,
        skip_test: bool = False,
    ) -> bool:
        """Run the authoritative gate: configure, build, then ctest.

        Configure runs before every build, not only for a fresh tree. The
        Visual Studio generator never re-runs CMake on its own (the root
        ``CMakeLists.txt`` suppresses regeneration), so a phase that adds a source
        or test file to a ``CMakeLists.txt`` would otherwise be built — and
        gated — against stale project files. Configuring an up-to-date tree is
        cheap, and it also targets the preset's own build directory. Skipping
        the build skips the configure too.

        Args:
            preset: The CMake configure, build, and test preset.
            skip_build: Skip configure and build.
            skip_test: Skip ctest.

        Returns:
            True when every step that ran succeeded.
        """
        configure_args = ["--preset", preset]
        build_args = ["--build", "--preset", preset]
        test_args = ["--preset", preset]
        if self.is_dry_run:
            if not skip_build:
                self.console.dry(render_command("cmake", configure_args))
                self.console.dry(render_command("cmake", build_args))
            if not skip_test:
                self.console.dry(render_command("ctest", test_args))
            return True

        # Distinguish a missing toolchain from a broken build, so a red baseline
        # is not misattributed to the code.
        cmake = self._find_cmake()
        if cmake is None:
            self.console.warn(
                "cmake is not on PATH; the build + test gate cannot run. Install CMake and "
                "a C++ toolchain, or pass --skip-baseline to skip the gate."
            )
            return False
        ctest = self._which("ctest")
        if not skip_test and ctest is None:
            self.console.warn(
                "ctest is not on PATH; the test gate cannot run. Install CMake and a C++ "
                "toolchain, or pass --skip-baseline / --skip-test."
            )
            return False

        steps: list[tuple[str, list[str], str]] = []
        if not skip_build:
            steps.append(("Configuring", [cmake, *configure_args], "CMake configure failed."))
            steps.append(("Building", [cmake, *build_args], "Build failed."))
        if not skip_test and ctest is not None:
            steps.append(("Testing", [ctest, *test_args], "Tests failed."))
        for label, command, failure in steps:
            self.console.line(f"    {label}: {render_command(Path(command[0]).stem, command[1:])}")
            if self._run(command) != 0:
                self.console.warn(failure)
                return False
        return True

    def lint_and_format(self, *, check_only: bool = False) -> bool:
        """Run ``scripts/format.py`` (unless *check_only*), then ``scripts/lint.py``.

        ``lint.py`` is read-only and the authority: the gate passes only when it
        exits 0. clang-tidy needs ``build/compile_commands.json`` (and, on
        Windows, the MSVC header environment); both are provided best-effort.

        Args:
            check_only: Verify without running the formatter.

        Returns:
            True when ``lint.py`` passed.
        """
        if self.is_dry_run:
            if not check_only:
                self.console.dry("python scripts/format.py")
            self.console.dry(
                "refresh build/compile_commands.json for clang-tidy (if missing or stale)"
            )
            self.console.dry("python scripts/lint.py")
            return True

        if not self._import_msvc_environment():
            self.console.warn(
                "could not load the MSVC developer environment; clang-tidy may be skipped. "
                "Run from a Developer prompt, or make sure vswhere can find Visual Studio."
            )
        self._ensure_compile_database()

        if not check_only:
            self.console.line("    Formatting: python scripts/format.py")
            if self._run([self.python_executable, "scripts/format.py"]) != 0:
                # lint.py surfaces the same problem and decides pass/fail.
                self.console.warn(
                    "formatter reported problems it could not auto-fix; see the lint output below."
                )
        self.console.line("    Linting: python scripts/lint.py")
        if self._run([self.python_executable, "scripts/lint.py"]) != 0:
            self.console.warn("Lint checks failed.")
            return False
        return True

    def _visual_studio_installations(self, *, requires: str | None = None) -> list[Path]:
        """List the Visual Studio installations vswhere reports, newest first.

        ``-sort`` lists every installation (``-latest`` would return only the
        newest, which may lack what the caller needs), ``-prerelease`` includes
        Preview builds, and ``-utf8`` keeps a non-ASCII path intact. Mirrors
        ``find_visual_studio_llvm_tool`` in ``scripts/_common.py``.

        Args:
            requires: A workload component the installation must have.

        Returns:
            The installation directories; empty when there are none (or no vswhere).
        """
        program_files = self._env.get("PROGRAMFILES(X86)")
        if not program_files:
            return []
        vswhere = Path(program_files, "Microsoft Visual Studio", "Installer", "vswhere.exe")
        if not vswhere.is_file():
            return []
        command = [str(vswhere), "-sort", "-prerelease", "-products", "*", "-utf8"]
        if requires is not None:
            command += ["-requires", requires]
        command += ["-property", "installationPath"]
        try:
            completed = self._capture(command)
        except OSError:
            return []
        if completed.returncode != 0:
            return []
        return [Path(line.strip()) for line in completed.stdout.splitlines() if line.strip()]

    def _find_cmake(self) -> str | None:
        """Return cmake, adding a well-known Windows install to the pipeline's ``PATH``.

        Visual Studio bundles a CMake that is on ``PATH`` only inside a Developer
        prompt, and a standalone install may be missing from a fresh shell. Every
        Visual Studio installation is checked, newest first, then the standalone
        install. The install directory is prepended to the pipeline's own
        environment, so ctest beside it and every child process find it;
        ``os.environ`` is left alone.
        """
        cmake = self._which("cmake")
        if cmake is not None or not _IS_WINDOWS:
            return cmake
        candidates = [
            installation.joinpath(*_VS_CMAKE_PARTS, "cmake.exe")
            for installation in self._visual_studio_installations()
        ]
        for variable in ("PROGRAMFILES", "PROGRAMFILES(X86)"):
            root = self._env.get(variable)
            if root:
                candidates.append(Path(root, "CMake", "bin", "cmake.exe"))
        for candidate in candidates:
            if candidate.is_file():
                self._env["PATH"] = f"{candidate.parent}{os.pathsep}{self._search_path}"
                return str(candidate)
        return None

    def _find_vcvars(self) -> Path | None:
        """Return ``vcvars64.bat`` of the newest Visual Studio with the C++ tools."""
        for installation in self._visual_studio_installations(requires=_VC_TOOLS_COMPONENT):
            vcvars = installation / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
            if vcvars.is_file():
                return vcvars
        return None

    def _import_msvc_environment(self) -> bool:
        """On Windows, load vcvars64's INCLUDE/LIB/PATH so clang-tidy finds headers.

        The variables go into the pipeline's own environment, which every later
        child process receives. No-op (True) off Windows, without clang-tidy, or
        when a developer environment is already active.

        Returns:
            False only when vcvars cannot be found or fails.
        """
        if self._is_msvc_ready or not _IS_WINDOWS:
            return True
        if self._env.get("VCINSTALLDIR") or self._which("cl") or not self._which("clang-tidy"):
            self._is_msvc_ready = True
            return True
        vcvars = self._find_vcvars()
        if vcvars is None:
            return False
        imported = self._capture_vcvars_environment(vcvars)
        if imported is None:
            return False
        for name, value in imported.items():
            self._env[name.upper()] = value
        self._is_msvc_ready = True
        return True

    def _capture_vcvars_environment(self, vcvars: Path) -> dict[str, str] | None:
        """Run *vcvars* under cmd.exe and return the environment it leaves behind.

        A batch file can only run under cmd.exe. The generated wrapper is pure
        ASCII: the vcvars and Python paths reach it through environment
        variables, so a non-ASCII path survives whatever code page cmd.exe reads
        the file in. The environment comes back as ASCII-only JSON from Python
        rather than as ``set`` output, whose encoding follows the console code
        page (and is garbled after ``chcp 65001``).

        Returns:
            The captured environment, or None when the wrapper failed.
        """
        environment = {
            **self._env,
            _VCVARS_VARIABLE: str(vcvars),
            _ENV_PYTHON_VARIABLE: sys.executable,
        }
        script = (
            f'@call "%{_VCVARS_VARIABLE}%" >nul 2>&1\r\n'
            f'@"%{_ENV_PYTHON_VARIABLE}%" -c "{_ENV_DUMP_CODE}"\r\n'
        )
        with tempfile.TemporaryDirectory(prefix="luma-vcvars-") as scratch:
            wrapper = Path(scratch, "capture-env.cmd")
            wrapper.write_text(script, encoding="ascii", newline="")
            try:
                completed = subprocess.run(
                    [str(wrapper)],
                    env=environment,
                    capture_output=True,
                    check=False,
                )
            except OSError:
                return None
        if completed.returncode != 0:
            return None
        lines = completed.stdout.decode("utf-8", "replace").strip().splitlines()
        try:
            captured = json.loads(lines[-1]) if lines else None
        except ValueError:
            return None
        if not isinstance(captured, dict):
            return None
        return {
            str(name): str(value)
            for name, value in captured.items()
            if str(name).upper() not in (_VCVARS_VARIABLE, _ENV_PYTHON_VARIABLE)
        }

    def _cmake_inputs(self) -> list[Path]:
        """Return the build-system files whose change can alter the compile database.

        ``CMakeLists.txt`` and ``*.cmake`` files are listed through git, tracked
        or untracked but not ignored, so build trees are never walked; without
        git, only the root ``CMakeLists.txt`` counts.
        """
        inputs = [self.repo_root / "CMakeLists.txt"]
        git = self._which("git")
        if git is None:
            return inputs
        completed = self._capture(
            [
                git,
                "-C",
                str(self.repo_root),
                "ls-files",
                "--cached",
                "--others",
                "--exclude-standard",
                "--",
                "*CMakeLists.txt",
                "*.cmake",
            ]
        )
        if completed.returncode != 0:
            return inputs
        listed = [self.repo_root / line for line in completed.stdout.splitlines() if line]
        return inputs + listed

    def _is_compile_database_current(self, build_db: Path) -> bool:
        """True when *build_db* is newer than the last configure and every CMake input.

        ``build/CMakeCache.txt`` is rewritten by every configure, which the gate
        runs before each build, so after a configure the copied database counts
        as stale and is regenerated; a new source picked up by a glob is never
        missed.
        """
        if not build_db.is_file():
            return False
        built = build_db.stat().st_mtime
        inputs = [self.repo_root / "build" / "CMakeCache.txt", *self._cmake_inputs()]
        for path in inputs:
            with contextlib.suppress(OSError):
                if path.stat().st_mtime > built:
                    return False
        return True

    def _is_compile_database_maintained_by_cmake(self) -> bool:
        """True when ``build/`` uses a generator that writes the database on every configure.

        Ninja and Makefile generators with ``CMAKE_EXPORT_COMPILE_COMMANDS``
        refresh ``build/compile_commands.json`` as part of the gate's configure;
        Visual Studio and Xcode never write one.
        """
        cache = self.repo_root / "build" / "CMakeCache.txt"
        try:
            lines = cache.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            return False
        values: dict[str, str] = {}
        for line in lines:
            key, separator, value = line.partition("=")
            if separator:
                values[key.split(":", 1)[0].strip()] = value.strip()
        generator = values.get("CMAKE_GENERATOR", "")
        is_exported = values.get("CMAKE_EXPORT_COMPILE_COMMANDS", "").upper() in (
            _CMAKE_TRUE_VALUES
        )
        return is_exported and any(name in generator for name in _COMPILE_DATABASE_GENERATORS)

    def _ensure_compile_database(self) -> None:
        """Keep ``build/compile_commands.json`` current for clang-tidy.

        ``lint.py`` runs clang-tidy only on sources listed in the database, so a
        stale one silently skips every file a phase added. When ``build/``
        uses a generator that maintains the database itself, the gate's
        configure already refreshed it. Otherwise (Visual Studio, Xcode) a
        missing or stale database — older than the last configure or any
        ``CMakeLists.txt`` / ``*.cmake`` — is regenerated: a database-capable
        generator (Ninja, else NMake / Unix Makefiles) is configured into
        ``build-compiledb/`` and the result copied into ``build/``. Best-effort:
        a missing tool only leaves clang-tidy skipped (or on the old database).
        """
        build_db = self.repo_root / "build" / "compile_commands.json"
        if not self._which("clang-tidy"):
            return
        if build_db.is_file() and (
            self._is_compile_database_maintained_by_cmake()
            or self._is_compile_database_current(build_db)
        ):
            return
        cmake = self._find_cmake()
        if cmake is None:
            self.console.warn(
                "cmake not found; cannot generate compile_commands.json "
                "(clang-tidy will be skipped or use an outdated database)."
            )
            return
        if self._which("ninja"):
            generator = "Ninja"
        else:
            generator = "NMake Makefiles" if _IS_WINDOWS else "Unix Makefiles"
        db_dir = self.repo_root / "build-compiledb"
        verb = "Refreshing" if build_db.is_file() else "Generating"
        self.console.line(
            f'    {verb} compile_commands.json for clang-tidy (cmake -G "{generator}")'
        )
        configure = [
            cmake,
            "-S",
            str(self.repo_root),
            "-B",
            str(db_dir),
            "-G",
            generator,
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            "-DLUMA_BUILD_TESTS=OFF",
        ]
        if self._run(configure) != 0:
            self.console.warn(
                "could not configure a compile database; clang-tidy will be skipped "
                "(or use an outdated database) this run."
            )
            return
        generated = db_dir / "compile_commands.json"
        if not generated.is_file():
            self.console.warn(
                "configure produced no compile_commands.json; clang-tidy will be skipped "
                "(or use an outdated database)."
            )
            return
        build_db.parent.mkdir(parents=True, exist_ok=True)
        # copyfile stamps the copy with the current time, which marks it current.
        shutil.copyfile(generated, build_db)

    # -- git -------------------------------------------------------------------

    def _git(self, *args: str) -> list[str]:
        """Return a git command line that runs against the repository root."""
        return [self._require_tool("git"), "-C", str(self.repo_root), *args]

    def is_clean_working_tree(self) -> bool:
        """True when git reports no staged, unstaged, or untracked changes.

        Raises:
            PipelineError: When ``git status`` fails (e.g. not a git working tree).
        """
        if self.is_dry_run:
            self.console.dry(f"{render_command('git', ['status', '--porcelain'])} (assume clean)")
            return True
        completed = self._capture(self._git("status", "--porcelain"))
        if completed.returncode != 0:
            raise PipelineError(
                f"git status failed in '{self.repo_root}' (exit {completed.returncode}); "
                "is it a git working tree?"
            )
        return not completed.stdout.strip()

    def create_branch(self, name: str) -> bool:
        """Create branch *name* from HEAD and switch to it; True on success."""
        if self.is_dry_run:
            self.console.dry(render_command("git", ["switch", "-c", name]))
            return True
        return self._run(self._git("switch", "-c", name)) == 0

    def head_commit(self) -> str | None:
        """Return the full SHA of HEAD, or None in a dry run.

        Raises:
            PipelineError: When git cannot resolve HEAD.
        """
        if self.is_dry_run:
            return None
        completed = self._capture(self._git("rev-parse", "--verify", "HEAD"))
        sha = completed.stdout.strip()
        if completed.returncode != 0 or not sha:
            raise PipelineError(f"git rev-parse HEAD failed (exit {completed.returncode}).")
        return sha

    def short_sha(self, sha: str) -> str:
        """Return git's abbreviation of *sha* (its first 7 characters if git fails)."""
        completed = self._capture(self._git("rev-parse", "--short", sha))
        short = completed.stdout.strip()
        return short if completed.returncode == 0 and short else sha[:7]

    def count_commits(self, base: str, head: str) -> int | None:
        """Count the commits *head* adds on top of *base*.

        Returns:
            The count, or None when *base* is not an ancestor of *head* — the
            history was rewritten (amended, rebased, or reset).
        """
        is_ancestor = self._run(
            self._git("merge-base", "--is-ancestor", base, head), should_discard_output=True
        )
        if is_ancestor != 0:
            return None
        completed = self._capture(self._git("rev-list", "--count", f"{base}..{head}"))
        try:
            return int(completed.stdout.strip())
        except ValueError:
            return None

    def revert_to(
        self, target: str = "HEAD", *, remove_untracked: bool, keep: Path | None = None
    ) -> bool:
        """Discard a failed phase's changes by resetting the tree and branch to *target*.

        Resetting to the commit the phase started from (rather than to HEAD)
        also drops any commit the agent made during the phase.

        Args:
            target: The commit to reset to — the phase's starting commit.
            remove_untracked: Also delete untracked files (``git clean -fd``), so
                the next checkpoint's ``git add -A`` cannot commit files the
                failed phase created. Ignored files always survive. Pass it only
                when the run started from a clean tree, or the user's own
                untracked files are deleted too.
            keep: A directory to preserve even if it is untracked and not
                ignored — the run's artifact root.

        Returns:
            True when every git command succeeded.
        """
        commands = [["reset", "--hard", target]]
        if remove_untracked:
            clean = ["clean", "-fd"]
            if keep is not None and is_within(keep, self.repo_root):
                relative = keep.resolve().relative_to(self.repo_root.resolve()).as_posix()
                clean += ["-e", f"/{relative}/"]
            commands.append(clean)
        if self.is_dry_run:
            for command in commands:
                self.console.dry(render_command("git", command))
            return True
        return all(self._run(self._git(*command)) == 0 for command in commands)

    def git_checkpoint(self, *, subject: str, agent: str, artifact_root: Path | None) -> str | None:
        """Stage everything and commit.

        An artifact root inside the working tree is unstaged first, so run
        artifacts never land in a commit even when they are not git-ignored. The
        ``Co-authored-by`` trailer names the agent that did the work.

        Args:
            subject: The commit subject.
            agent: The agent whose trailer to add.
            artifact_root: The run's artifact root, kept out of the commit.

        Returns:
            The short SHA, or None when nothing changed (or in a dry run).

        Raises:
            CheckpointError: When git (or a hook) rejects the commit.
        """
        trailer = CO_AUTHOR_TRAILERS[agent]
        exclude = (
            artifact_root if artifact_root and is_within(artifact_root, self.repo_root) else None
        )
        commit = ["commit", "-m", subject, "-m", trailer]
        if self.is_dry_run:
            self.console.dry(render_command("git", ["add", "-A"]))
            if exclude is not None:
                self.console.dry(render_command("git", ["reset", "-q", "--", str(exclude)]))
            self.console.dry(render_command("git", commit))
            return None

        if self._run(self._git("add", "-A")) != 0:
            raise CheckpointError("git add failed.")
        if exclude is not None:
            unstaged = self._run(
                self._git("reset", "-q", "--", str(exclude)), should_discard_output=True
            )
            if unstaged != 0:
                raise CheckpointError(
                    f"could not unstage the artifact root '{exclude}' (git reset exit "
                    f"{unstaged}); refusing to commit run artifacts."
                )
        staged = self._run(self._git("diff", "--cached", "--quiet"))
        if staged == 0:
            self.console.dim("    No changes to commit for this phase.")
            return None
        if staged != 1:
            raise CheckpointError(f"git diff --cached failed (exit {staged}).")
        if self._run(self._git(*commit)) != 0:
            raise CheckpointError("git commit failed (a pre-commit hook or git rejected it).")
        sha = self._capture(self._git("rev-parse", "--short", "HEAD")).stdout.strip()
        self.console.ok(f"    Checkpoint {sha}: {subject}")
        return sha

    # -- artifact protection ---------------------------------------------------

    def save_artifacts(self, artifact_root: Path) -> Path | None:
        """Move an in-tree artifact root into ``.git/`` so ``git clean`` cannot reach it.

        Returns:
            The backup path, or None when there is nothing to protect (no root,
            or a root outside the working tree that ``git clean`` never sees).

        Raises:
            PipelineError: When a backup from an earlier run still exists — a
                failed restore asked the user to recover it, so it is never
                deleted or overwritten.
        """
        if self.is_dry_run:
            self.console.dry(f'move "{artifact_root}" aside into .git/ across the clean')
            return None
        if not artifact_root.is_dir() or not is_within(artifact_root, self.repo_root):
            return None
        completed = self._capture(self._git("rev-parse", "--absolute-git-dir"))
        git_dir = completed.stdout.strip()
        if completed.returncode != 0 or not git_dir:
            return None
        backup = Path(git_dir, ARTIFACT_BACKUP_NAME)
        if backup.exists():
            raise PipelineError(
                f"an artifact backup from an earlier run still exists at '{backup}'. Recover "
                f"anything you need from it into '{artifact_root}', delete it, and re-run."
            )
        shutil.move(str(artifact_root), str(backup))
        return backup

    def restore_artifacts(self, backup: Path | None, artifact_root: Path) -> None:
        """Move a saved artifact root back, merging into any recreated root."""
        if self.is_dry_run or backup is None or not backup.is_dir():
            return
        if not artifact_root.exists():
            artifact_root.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(backup), str(artifact_root))
            return
        for entry in backup.iterdir():
            _replace_with(entry, artifact_root / entry.name)
        shutil.rmtree(backup, ignore_errors=True)

    @contextlib.contextmanager
    def protect_artifacts(self, artifact_root: Path, log_dir: Path) -> Iterator[Path]:
        """Shield the artifact tree from a phase that runs ``git clean -Xdf``.

        Moves the artifact root into ``.git/`` for the duration and yields a
        transcript directory *outside* it (on Windows a directory cannot be moved
        while a file inside it is open). Afterwards — even on error — the root is
        restored and the transcripts are folded back into *log_dir*.
        """
        if self.is_dry_run:
            self.save_artifacts(artifact_root)
            yield log_dir
            return
        temp_log_dir = Path(tempfile.mkdtemp(prefix="luma-relverify-"))
        backup: Path | None = None
        try:
            backup = self.save_artifacts(artifact_root)
            yield temp_log_dir
        finally:
            try:
                self.restore_artifacts(backup, artifact_root)
            except OSError as err:
                self.console.warn(
                    f"failed to restore {artifact_root}: {err}. Its contents are kept at "
                    f"{backup}; move them back by hand (the next protected phase refuses to "
                    "run until that backup is gone)."
                )
            log_dir.mkdir(parents=True, exist_ok=True)
            for entry in temp_log_dir.iterdir():
                _replace_with(entry, log_dir / entry.name)
            shutil.rmtree(temp_log_dir, ignore_errors=True)
