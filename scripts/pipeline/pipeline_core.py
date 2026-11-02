"""Side-effecting core of the Luma prompt pipeline.

Every side effect the pipeline performs — invoking the agent CLI, building,
testing, linting, and touching git or the artifact tree — flows through
:class:`Pipeline`. Its ``dry_run`` flag gives all of them one uniform preview
path: a dry run prints the exact command each step would run and returns a
neutral result without spawning anything.

The module also holds the pure helpers those steps are built from (agent
argument vectors, Copilot JSONL reduction, the abort rule), so they can be
unit-tested without a process in sight. ``luma_pipeline.py`` is the CLI that
orchestrates the audit, fix, and combined runs on top of this module.

Standard library only; Python 3.10+. Works on Windows, macOS, and Linux.
"""

from __future__ import annotations

import contextlib
import ctypes
import functools
import io
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
from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
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
_ENV_ASSIGNMENT = re.compile(r"^([^=]+)=(.*)$")
_IS_WINDOWS = os.name == "nt"
_CHUNK_SIZE = 65536
# Bound on draining a child's pipes after it exits: a grandchild that inherited
# the pipe handles could otherwise hold them open indefinitely.
_DRAIN_TIMEOUT_SECONDS = 10
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
            self.out.write(chunk.decode("utf-8", "replace"))
            self.out.flush()
            return
        buffer.write(chunk)
        buffer.flush()


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


def _kill_process_tree(process: subprocess.Popen[bytes]) -> None:
    """Kill *process* and its descendants (e.g. node behind a ``.cmd`` shim).

    On POSIX this relies on the child leading its own session (see
    ``start_new_session`` where the agent processes are started), so its
    process group holds exactly the tree to kill.
    """
    if process.poll() is not None:
        return
    if _IS_WINDOWS:
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
    with contextlib.suppress(OSError):
        process.kill()
    # Reap it, so no zombie outlives the pipeline's handle on it.
    with contextlib.suppress(subprocess.TimeoutExpired):
        process.wait(timeout=_DRAIN_TIMEOUT_SECONDS)


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


def _drain(stream: IO[bytes], sink: list[bytes]) -> None:
    """Read *stream* to the end into *sink*; used from a reader thread."""
    with contextlib.suppress(OSError, ValueError):
        sink.append(stream.read())


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
        dry_run: bool = False,
        console: Console | None = None,
        python_executable: str = sys.executable,
        model_cache: dict[tuple[str, str], str] | None = None,
        env: Mapping[str, str] | None = None,
    ) -> None:
        """Create a pipeline rooted at *repo_root*.

        Args:
            repo_root: Repository root; every tool runs from here.
            dry_run: Print each command instead of running it.
            console: Where progress, dry-run lines, and warnings go.
            python_executable: Interpreter for ``scripts/format.py`` / ``lint.py``.
            model_cache: Resolved Copilot models, shared across stages of one run.
            env: Environment for child processes (default: a copy of ``os.environ``).
        """
        self.repo_root = repo_root
        self.dry_run = dry_run
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

    def _run_bounded(
        self, args: Sequence[str], *, stdin_text: str, timeout: float
    ) -> tuple[int | None, str]:
        """Run with a deadline, killing the whole process tree on expiry.

        Returns:
            The exit code (``None`` when the deadline passed) and the merged
            stdout/stderr output collected so far.
        """
        process = subprocess.Popen(
            list(args),
            cwd=self.repo_root,
            env=self._env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=not _IS_WINDOWS,
        )
        chunks: list[bytes] = []
        reader = threading.Thread(target=_drain, args=(process.stdout, chunks), daemon=True)
        reader.start()
        _feed_stdin(process, stdin_text)
        exit_code: int | None
        try:
            exit_code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            _kill_process_tree(process)
            exit_code = None
        except KeyboardInterrupt:
            _kill_process_tree(process)
            raise
        reader.join(_DRAIN_TIMEOUT_SECONDS)
        # Close the pipe only once drained; a grandchild may still hold it open.
        if not reader.is_alive() and process.stdout is not None:
            process.stdout.close()
        return exit_code, b"".join(chunks).decode("utf-8", "replace")

    # -- file system -------------------------------------------------------

    def make_dirs(self, path: Path) -> None:
        """Create *path* and its parents (skipped in a dry run)."""
        if not self.dry_run:
            path.mkdir(parents=True, exist_ok=True)

    def write_text(self, path: Path, text: str) -> None:
        """Write a UTF-8 text file with LF line endings (skipped in a dry run)."""
        if not self.dry_run:
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
        if agent != "copilot" or self.dry_run or not model or model == "auto":
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

        if self.dry_run:
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
        self.console.flush()
        completed = subprocess.run(
            command,
            cwd=self.repo_root,
            env=self._env,
            input=stdin_text.encode("utf-8"),
            capture_output=True,
            check=False,
        )
        stdout = completed.stdout.decode("utf-8", "replace")
        stderr = completed.stderr.decode("utf-8", "replace")
        exit_code = completed.returncode
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
        self.console.flush()
        # Its own session on POSIX, so the agent's whole process tree can be
        # killed as one group if anything goes wrong while it runs.
        process = subprocess.Popen(
            command,
            cwd=self.repo_root,
            env=self._env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=not _IS_WINDOWS,
        )
        # Feed stdin from a thread so a prompt larger than the pipe buffer cannot
        # deadlock against the output we are reading here.
        writer = threading.Thread(target=_feed_stdin, args=(process, stdin_text), daemon=True)
        writer.start()
        output = process.stdout
        try:
            if not isinstance(output, io.BufferedReader):
                raise PipelineError("could not open the agent's output stream.")
            with output, log_file.open("wb") as log:
                while chunk := output.read1(_CHUNK_SIZE):
                    log.write(chunk)
                    log.flush()
                    self.console.write_raw(chunk)
            exit_code = process.wait()
        except BaseException:
            # Covers Ctrl+C as well: never leave an agent running unattended.
            _kill_process_tree(process)
            raise
        writer.join(_DRAIN_TIMEOUT_SECONDS)

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
        if self.dry_run:
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
        if self.dry_run:
            if not check_only:
                self.console.dry("python scripts/format.py")
            self.console.dry("generate build/compile_commands.json for clang-tidy (if missing)")
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

    def _find_visual_studio(self, *, requires: str | None = None) -> Path | None:
        """Return the newest Visual Studio installation that vswhere reports.

        Args:
            requires: A workload component the installation must have.

        Returns:
            The installation directory, or None when there is none (or no vswhere).
        """
        program_files = self._env.get("PROGRAMFILES(X86)")
        if not program_files:
            return None
        vswhere = Path(program_files, "Microsoft Visual Studio", "Installer", "vswhere.exe")
        if not vswhere.is_file():
            return None
        command = [str(vswhere), "-latest", "-products", "*"]
        if requires is not None:
            command += ["-requires", requires]
        command += ["-property", "installationPath"]
        completed = self._capture(command)
        lines = completed.stdout.strip().splitlines()
        if completed.returncode != 0 or not lines:
            return None
        return Path(lines[0].strip())

    def _find_cmake(self) -> str | None:
        """Return cmake, adding a well-known Windows install to the pipeline's ``PATH``.

        Visual Studio bundles a CMake that is on ``PATH`` only inside a Developer
        prompt, and a standalone install may be missing from a fresh shell. The
        install directory is prepended to the pipeline's own environment, so
        ctest beside it and every child process find it; ``os.environ`` is left
        alone.
        """
        cmake = self._which("cmake")
        if cmake is not None or not _IS_WINDOWS:
            return cmake
        candidates: list[Path] = []
        visual_studio = self._find_visual_studio()
        if visual_studio is not None:
            candidates.append(visual_studio.joinpath(*_VS_CMAKE_PARTS, "cmake.exe"))
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
        visual_studio = self._find_visual_studio(requires=_VC_TOOLS_COMPONENT)
        if visual_studio is None:
            return None
        vcvars = visual_studio / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
        return vcvars if vcvars.is_file() else None

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
        # A batch file can only run under cmd.exe; a generated wrapper keeps the
        # vcvars path out of a hand-quoted command line.
        with tempfile.TemporaryDirectory(prefix="luma-vcvars-") as scratch:
            wrapper = Path(scratch, "capture-env.cmd")
            wrapper.write_text(f'@call "{vcvars}" >nul 2>&1\r\n@set\r\n', encoding="utf-8")
            completed = subprocess.run(
                [str(wrapper)],
                env=self._env,
                capture_output=True,
                encoding="oem",
                errors="replace",
                check=False,
            )
        if completed.returncode != 0:
            return False
        for line in completed.stdout.splitlines():
            match = _ENV_ASSIGNMENT.match(line)
            if match:
                self._env[match.group(1).upper()] = match.group(2)
        self._is_msvc_ready = True
        return True

    def _ensure_compile_database(self) -> None:
        """Emit ``build/compile_commands.json`` for clang-tidy when it is missing.

        The Visual Studio and Xcode generators do not write one, so configure a
        database-capable generator (Ninja, else NMake / Unix Makefiles) into
        ``build-compiledb/`` and copy the result into ``build/``. Best-effort: a
        missing tool only leaves clang-tidy skipped by ``lint.py``.
        """
        build_db = self.repo_root / "build" / "compile_commands.json"
        if build_db.is_file() or not self._which("clang-tidy"):
            return
        cmake = self._find_cmake()
        if cmake is None:
            self.console.warn(
                "cmake not found; cannot generate compile_commands.json "
                "(clang-tidy will be skipped)."
            )
            return
        if self._which("ninja"):
            generator = "Ninja"
        else:
            generator = "NMake Makefiles" if _IS_WINDOWS else "Unix Makefiles"
        db_dir = self.repo_root / "build-compiledb"
        self.console.line(
            f'    Generating compile_commands.json for clang-tidy (cmake -G "{generator}")'
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
                "could not configure a compile database; clang-tidy will be skipped this run."
            )
            return
        generated = db_dir / "compile_commands.json"
        if not generated.is_file():
            self.console.warn(
                "configure produced no compile_commands.json; clang-tidy will be skipped."
            )
            return
        build_db.parent.mkdir(parents=True, exist_ok=True)
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
        if self.dry_run:
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
        if self.dry_run:
            self.console.dry(render_command("git", ["switch", "-c", name]))
            return True
        return self._run(self._git("switch", "-c", name)) == 0

    def revert_to_head(self, *, remove_untracked: bool, keep: Path | None = None) -> bool:
        """Discard a failed phase's changes by resetting tracked files to HEAD.

        Args:
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
        commands = [["reset", "--hard", "HEAD"]]
        if remove_untracked:
            clean = ["clean", "-fd"]
            if keep is not None and is_within(keep, self.repo_root):
                relative = keep.resolve().relative_to(self.repo_root.resolve()).as_posix()
                clean += ["-e", f"/{relative}/"]
            commands.append(clean)
        if self.dry_run:
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
        if self.dry_run:
            self.console.dry(render_command("git", ["add", "-A"]))
            if exclude is not None:
                self.console.dry(render_command("git", ["reset", "-q", "--", str(exclude)]))
            self.console.dry(render_command("git", commit))
            return None

        if self._run(self._git("add", "-A")) != 0:
            raise CheckpointError("git add failed.")
        if exclude is not None:
            self._run(self._git("reset", "-q", "--", str(exclude)), should_discard_output=True)
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
        """
        if self.dry_run:
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
            shutil.rmtree(backup, ignore_errors=True)
        shutil.move(str(artifact_root), str(backup))
        return backup

    def restore_artifacts(self, backup: Path | None, artifact_root: Path) -> None:
        """Move a saved artifact root back, merging into any recreated root."""
        if self.dry_run or backup is None or not backup.is_dir():
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
        if self.dry_run:
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
                self.console.warn(f"failed to restore {artifact_root} (backup at {backup}): {err}")
            log_dir.mkdir(parents=True, exist_ok=True)
            for entry in temp_log_dir.iterdir():
                _replace_with(entry, log_dir / entry.name)
            shutil.rmtree(temp_log_dir, ignore_errors=True)
