#!/usr/bin/env python3
"""Enable the Luma Git hooks for this repository.

Points the repository's local ``core.hooksPath`` at the version-controlled
``scripts/git-hooks`` directory, so the tracked hooks run on every commit. The
tracked hooks are a ``pre-commit`` hook that checks staged C++ files with
clang-format and clang-tidy, and a ``commit-msg`` hook that enforces the
project's Conventional Commits message format.

A tracked hooks directory is used in preference to copying a generated script
into ``.git/hooks`` because:

* The hooks are version-controlled and update automatically on ``git pull`` —
  there is no generated copy to keep in sync.
* A repository-local ``core.hooksPath`` overrides any global ``core.hooksPath``
  (for example a shared template directory), so the hooks are reliably picked
  up rather than silently bypassed.

Usage:
    python scripts/install_git_hooks.py
"""

import subprocess
import sys

from _common import REPO_ROOT

# Hooks directory, relative to the working-tree root. Git resolves a relative
# core.hooksPath against the directory a hook runs in, which for a non-bare
# repository is the top level of the working tree.
HOOKS_DIR = "scripts/git-hooks"

# Tracked hooks expected to live in HOOKS_DIR. Pointing core.hooksPath at the
# directory activates every hook it contains; these are listed only so the
# installer can sanity-check that each source is present and executable before
# enabling them.
HOOKS = ("pre-commit", "commit-msg")


def hook_index_mode(name: str) -> str | None:
    """Return the Git index mode of a tracked hook (e.g. ``"100755"``).

    Git decides whether a hook is executable from the mode recorded in its index
    (``100755`` vs ``100644``), not the working-tree permission bits — and
    restores that bit on checkout. A hook committed as ``100644`` is therefore
    silently skipped on Linux/macOS even though it runs fine on Windows. The
    index is consulted rather than ``os.access`` because the latter is
    unreliable on Windows (it does not model the POSIX execute bit) and would
    miss exactly this cross-platform footgun.

    Returns ``None`` when the mode cannot be determined (git unavailable, or the
    hook is not tracked yet); callers treat that as "nothing to verify" since a
    missing source is already reported separately.
    """
    rel_path = f"{HOOKS_DIR}/{name}"
    try:
        result = subprocess.run(
            ["git", "ls-files", "--stage", "--", rel_path],
            cwd=str(REPO_ROOT),
            check=True,
            capture_output=True,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None

    line = result.stdout.strip()
    if not line:
        return None

    return line.split(maxsplit=1)[0]


def enable_hooks() -> int:
    """Point the repository's local ``core.hooksPath`` at ``scripts/git-hooks``.

    Returns 0 on success and a non-zero exit code on failure.
    """
    if not (REPO_ROOT / ".git").exists():
        print(f"Error: {REPO_ROOT} is not a git repository.", file=sys.stderr)
        return 1

    for name in HOOKS:
        hook = REPO_ROOT / HOOKS_DIR / name
        if not hook.is_file():
            print(f"Error: hook source {hook} is missing.", file=sys.stderr)
            return 1

        mode = hook_index_mode(name)
        if mode is not None and mode != "100755":
            print(
                f"Error: hook {hook} is not executable in the Git index "
                f"(mode {mode}); Git would skip it on Linux/macOS.",
                file=sys.stderr,
            )
            print(
                f"Fix it with: git update-index --chmod=+x {HOOKS_DIR}/{name}",
                file=sys.stderr,
            )
            return 1

    try:
        subprocess.run(
            ["git", "config", "--local", "core.hooksPath", HOOKS_DIR],
            cwd=str(REPO_ROOT),
            check=True,
        )
    except FileNotFoundError:
        print("Error: git executable not found on PATH.", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as exc:
        print(f"Failed to set core.hooksPath: {exc}", file=sys.stderr)
        return 1

    print(f"Git hooks enabled: core.hooksPath -> {HOOKS_DIR}")
    return 0


def main() -> int:
    return enable_hooks()


if __name__ == "__main__":
    raise SystemExit(main())
