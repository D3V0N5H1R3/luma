"""Shared utilities for Luma build and maintenance scripts.

Provides common constants and helpers used across multiple scripts to
avoid duplicating boilerplate such as version guards, path resolution,
executable and directory discovery, UTF-8 stream setup, and subprocess
wrappers.
"""

import contextlib
import os
import subprocess
import sys
from pathlib import Path


def require_python() -> None:
    """Exit unless the interpreter is Python 3.10 or newer.

    All Luma scripts require Python 3.10+. This runs at import time (below) so
    the check fires before any 3.10+ syntax elsewhere is evaluated; a script
    that uses nothing else from this module can call it explicitly to make that
    dependency visible instead of importing an unused name for its side effect.
    """
    if sys.version_info < (3, 10):
        sys.exit("Error: Python 3.10 or later is required to run this script.")


# ── Python version gate ────────────────────────────────────────────
# Import this module early so the check runs before any 3.10+ syntax is
# evaluated in the importing script.
require_python()

# ── Repository layout ─────────────────────────────────────────────
SCRIPT_DIR: Path = Path(__file__).resolve().parent
REPO_ROOT: Path = SCRIPT_DIR.parent


def run(args: list, **kwargs) -> subprocess.CompletedProcess:
    """Run a command, printing it first for visibility.

    Passes *check=True* by default so the caller gets a
    ``subprocess.CalledProcessError`` on non-zero exit.
    """
    kwargs.setdefault("check", True)
    print(f"$ {' '.join(str(a) for a in args)}")
    return subprocess.run(args, **kwargs)


def _explicit_or_env(explicit: str | None, env_var: str) -> Path | None:
    """Resolve an override path from an explicit argument or environment variable.

    Returns the resolved *explicit* path when given, otherwise the resolved value
    of *env_var* when it is set to a non-empty string, otherwise ``None`` so the
    caller can fall back to its own default.
    """
    if explicit:
        return Path(explicit).resolve()
    value = os.environ.get(env_var)
    if value:
        return Path(value).resolve()
    return None


def find_luma_exe(explicit: str | None) -> Path:
    """Resolve the luma executable path using platform-appropriate defaults.

    Precedence: the *explicit* argument, then the ``LUMA_EXE`` environment
    variable, then the platform's default build output under ``build/``.
    """
    override = _explicit_or_env(explicit, "LUMA_EXE")
    if override is not None:
        return override

    if sys.platform == "win32":
        candidate = REPO_ROOT / "build" / "Release" / "luma.exe"
    else:
        candidate = REPO_ROOT / "build" / "luma"

    return candidate.resolve()


def resolve_dir(explicit: str | None, env_var: str, default_rel: str) -> Path:
    """Resolve a directory: the *explicit* argument, then *env_var*, then *default_rel*.

    *default_rel* is interpreted relative to :data:`REPO_ROOT`.
    """
    override = _explicit_or_env(explicit, env_var)
    if override is not None:
        return override
    return (REPO_ROOT / default_rel).resolve()


def resolve_jobs(explicit: int | None) -> int:
    """Resolve the worker-thread count for parallel subprocess fan-out.

    Returns *explicit* when given, otherwise the machine's CPU count (falling
    back to 1 when that is unavailable). Raises :class:`ValueError` when
    *explicit* is not a positive integer so callers can surface a usage error.
    """
    if explicit is None:
        return os.cpu_count() or 1
    if explicit < 1:
        raise ValueError("jobs must be a positive integer")
    return explicit


def reconfigure_utf8_streams() -> None:
    """Reconfigure stdout/stderr to UTF-8 so reports render on any code page.

    Scripts print box-drawing characters and echo UTF-8 program output; on hosts
    whose console defaults to a non-UTF-8 code page this keeps that output
    readable. Streams that cannot be reconfigured are left untouched.
    """
    for stream in (sys.stdout, sys.stderr):
        with contextlib.suppress(AttributeError, ValueError):
            stream.reconfigure(encoding="utf-8", errors="replace")
