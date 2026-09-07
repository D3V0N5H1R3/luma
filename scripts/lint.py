#!/usr/bin/env python3
"""Run every repository lint/format check gate, as CI does, in one command.

The project enforces a linter or formatter for each language it contains, split
across a dozen CI workflows. This script mirrors that surface locally: it runs
each check the way its workflow does and prints a single pass/fail summary, so a
contributor can reproduce the CI lint result before pushing.

Gates whose tool is not installed (or whose prerequisite is missing — a
configured ``build/`` for clang-tidy, ``extensions/vscode/node_modules`` for the
extension gates) are reported as *skipped* rather than failing, so the script is
useful whether you have the full toolchain or only part of it. Nothing is
modified — use ``format.py`` to apply the auto-fixable subset.

Usage:
    python scripts/lint.py                 # run every available gate
    python scripts/lint.py --list          # show the gates and their status
    python scripts/lint.py --only ruff     # run a subset
    python scripts/lint.py --skip clang-tidy   # everything but the slow gate

Exit codes:
    0  Every gate passed or was skipped.
    1  At least one gate failed.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

from _common import REPO_ROOT
from _gates import Gate, git_ls, npm_script_gate, run_cli, skip, which

VSCODE_DIR = REPO_ROOT / "extensions" / "vscode"
ZED_DIR = REPO_ROOT / "extensions" / "zed"
BUILD_DIR = REPO_ROOT / "build"

# Pinned to match the version the markdown CI workflow runs via `npx --yes`.
MARKDOWNLINT_SPEC = "markdownlint-cli2@0.22.1"
WASM_TARGET = "wasm32-wasip1"

# Directories CI runs clang-format over (ci.yml, mirroring cmake/LumaRunClangTool.cmake):
# the four source trees plus the test and fuzz sources. Deliberately narrower
# than "every tracked .cpp/.hpp" — debugger/tests and language-server/tests are
# not part of the formatting surface.
CLANG_FORMAT_DIRS = (
    "core",
    "shared",
    "language-server/source",
    "debugger/source",
    "tests",
    "fuzz",
)


def _ruff_gates() -> list[Gate]:
    """Ruff lint and format-check, mirroring ci-python.yml."""
    ruff = which("ruff")
    if ruff is None:
        reason = "ruff not found (pip install ruff)"
        return [
            skip("ruff", "Python lint (Ruff)", reason),
            skip("ruff-format", "Python format check (Ruff)", reason),
        ]
    return [
        Gate("ruff", "Python lint (Ruff)", [ruff, "check", "."]),
        Gate(
            "ruff-format", "Python format check (Ruff)", [ruff, "format", "--check", "--diff", "."]
        ),
    ]


def _clang_format_gate() -> Gate:
    """clang-format dry-run over tracked C++, mirroring the ci.yml formatting job."""
    name, description = "clang-format", "C++ formatting (clang-format)"
    exe = which("clang-format")
    if exe is None:
        return skip(name, description, "clang-format not found (install LLVM/clang tools)")
    files = [f for f in git_ls(*CLANG_FORMAT_DIRS) if f.endswith((".cpp", ".hpp"))]
    if not files:
        return skip(name, description, "no C++ files tracked")
    # .clang-format-ignore excludes the generated asset header, honoured by clang-format 18+.
    return Gate(name, description, [exe, "--dry-run", "--Werror"], files=files)


def _clang_tidy_gate() -> Gate:
    """clang-tidy over the compiled sources, mirroring the ci.yml static-analysis job.

    Requires a configured build for its compilation database; skipped when that
    is absent so the script still runs without a build tree. This is the slowest
    gate — pass ``--skip clang-tidy`` to leave it out.
    """
    name, description = "clang-tidy", "C++ static analysis (clang-tidy)"
    exe = which("clang-tidy")
    if exe is None:
        return skip(name, description, "clang-tidy not found (install LLVM/clang tools)")
    compile_db = BUILD_DIR / "compile_commands.json"
    if not compile_db.exists():
        return skip(
            name, description, "no build/compile_commands.json (run cmake --preset default)"
        )
    # Restrict to files that actually have an entry in this platform's compile
    # database. ci.yml explicitly excludes *_win32.cpp when it runs clang-tidy
    # on Linux, because those files aren't part of the Linux build and would be
    # parsed with a synthesized command line that fails on their unconditional
    # <windows.h>/<io.h> includes. The mirror image happens locally on Windows:
    # *_posix.cpp files (termios.h, sys/ioctl.h, sys/select.h, poll.h, ...)
    # aren't part of the Windows build either. Rather than hard-coding a
    # platform-specific filename pattern, filter generically against whatever
    # compile_commands.json actually contains — the same set of sources CMake
    # compiled for this build, on any platform.
    compiled_files = {
        Path(entry["file"]).resolve()
        for entry in json.loads(compile_db.read_text(encoding="utf-8"))
        if "file" in entry
    }
    files = [
        f
        for f in git_ls("core", "language-server/source", "debugger/source", "shared")
        if f.endswith(".cpp") and (REPO_ROOT / f).resolve() in compiled_files
    ]
    if not files:
        return skip(name, description, "no C++ sources tracked")
    return Gate(name, description, [exe, "-p", str(BUILD_DIR)], files=files)


def _warning_sync_gate() -> Gate:
    """Compiler-flag / clang-tidy sync check, mirroring the ci.yml warning-sync job."""
    return Gate(
        "warning-sync",
        "Compiler-flag / clang-tidy sync",
        [sys.executable, "scripts/check_warning_sync.py", "--strict"],
    )


def _cmakelint_gate() -> Gate:
    """cmakelint over first-party CMake files, mirroring ci-cmake.yml."""
    name, description = "cmakelint", "CMake lint (cmakelint)"
    exe = which("cmakelint")
    if exe is None:
        return skip(name, description, "cmakelint not found (pip install cmakelint)")
    files = git_ls("CMakeLists.txt", "**/CMakeLists.txt", "*.cmake", ":!:external/**")
    if not files:
        return skip(name, description, "no CMake files tracked")
    return Gate(name, description, [exe, "--config=.cmakelintrc"], files=files)


def _shellcheck_gate() -> Gate:
    """ShellCheck over shell scripts and the pre-commit hook, mirroring ci-shell.yml."""
    name, description = "shellcheck", "Shell lint (ShellCheck)"
    exe = which("shellcheck")
    if exe is None:
        return skip(name, description, "shellcheck not found (apt-get install shellcheck)")
    files = git_ls("*.sh", "*.bash", ":!:external/**")
    if (REPO_ROOT / "scripts" / "git-hooks" / "pre-commit").is_file():
        files.append("scripts/git-hooks/pre-commit")
    if not files:
        return skip(name, description, "no shell scripts tracked")
    return Gate(name, description, [exe], files=files)


def _markdownlint_gate() -> Gate:
    """markdownlint-cli2 over first-party Markdown, mirroring ci-markdown.yml.

    Globs and ignores come from .markdownlint-cli2.jsonc, so no paths are passed.
    Uses ``npx --yes`` with the pinned spec, which fetches it on first run.
    """
    name, description = "markdownlint", "Markdown lint (markdownlint-cli2)"
    npx = which("npx")
    if npx is None:
        return skip(name, description, "npx not found (install Node.js)")
    return Gate(name, description, [npx, "--yes", MARKDOWNLINT_SPEC])


def _vscode_npm_gate(name: str, description: str, script: str) -> Gate:
    """Build a gate that runs an npm script in extensions/vscode."""
    return npm_script_gate(
        name,
        description,
        script=script,
        project_dir=VSCODE_DIR,
        missing_hint="run `npm ci` in extensions/vscode first",
    )


def _cargo_fmt_gate() -> Gate:
    """rustfmt check for the Zed extension, mirroring the local ci-zed guidance."""
    name, description = "cargo-fmt", "Rust formatting (rustfmt, Zed extension)"
    cargo = which("cargo")
    if cargo is None:
        return skip(name, description, "cargo not found (install Rust via rustup)")
    return Gate(name, description, [cargo, "fmt", "--check"], cwd=ZED_DIR)


def _clippy_gate() -> Gate:
    """Clippy for the Zed extension against the wasm target used by CI."""
    name, description = "clippy", "Rust lint (Clippy, Zed extension)"
    cargo = which("cargo")
    if cargo is None:
        return skip(name, description, "cargo not found (install Rust via rustup)")
    if not _wasm_target_available():
        return skip(name, description, f"rustup target add {WASM_TARGET}")
    argv = [cargo, "clippy", "--target", WASM_TARGET, "--", "-D", "warnings"]
    return Gate(name, description, argv, cwd=ZED_DIR)


def _wasm_target_available() -> bool:
    """Report whether the Clippy wasm target is installed (assume yes without rustup)."""
    rustup = which("rustup")
    if rustup is None:
        return True
    probe = subprocess.run(
        [rustup, "target", "list", "--installed"],
        capture_output=True,
        text=True,
        check=False,
    )
    return WASM_TARGET in probe.stdout.split()


def _powershell_gate() -> Gate:
    """PSScriptAnalyzer via the existing wrapper, mirroring ci-powershell.yml."""
    name, description = "powershell", "PowerShell lint (PSScriptAnalyzer)"
    pwsh = which("pwsh")
    if pwsh is None:
        return skip(name, description, "pwsh not found (install PowerShell 7+)")
    return Gate(name, description, [pwsh, "-File", "scripts/run_psscriptanalyzer.ps1"])


def build_gates() -> list[Gate]:
    """Assemble the ordered list of lint gates."""
    return [
        *_ruff_gates(),
        _warning_sync_gate(),
        _clang_format_gate(),
        _clang_tidy_gate(),
        _cmakelint_gate(),
        _shellcheck_gate(),
        _markdownlint_gate(),
        _vscode_npm_gate("eslint", "TypeScript lint (ESLint, VS Code extension)", "lint:eslint"),
        _vscode_npm_gate(
            "prettier", "TS/JS format check (Prettier, VS Code extension)", "format:check"
        ),
        _vscode_npm_gate("tsc", "TypeScript type check (tsc, VS Code extension)", "lint:types"),
        _cargo_fmt_gate(),
        _clippy_gate(),
        _powershell_gate(),
    ]


def main() -> int:
    return run_cli(
        build_gates(),
        prog="lint.py",
        description="Run the repository lint/format checks, mirroring CI.",
    )


if __name__ == "__main__":
    sys.exit(main())
