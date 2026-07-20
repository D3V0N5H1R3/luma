#!/usr/bin/env python3
"""Apply every repository auto-formatter (and safe auto-fix) in one command.

The companion to ``lint.py``: where that script *checks*, this one *writes*. It
runs each formatter the project uses — plus the auto-fixable subset of the
linters — so a contributor can bring a working tree into shape before running
``lint.py`` to confirm what remains.

As with ``lint.py``, a formatter whose tool is not installed is reported as
*skipped* rather than failing. A gate that exits non-zero here means the tool
could not fix everything (for example Stylelint or markdownlint hitting an error
that has no auto-fix); the remaining issues are surfaced by ``lint.py``.

Usage:
    python scripts/format.py                # apply every available formatter
    python scripts/format.py --list         # show the formatters and status
    python scripts/format.py --only ruff-format
    python scripts/format.py --skip markdownlint

Exit codes:
    0  Every formatter ran cleanly (or was skipped).
    1  At least one formatter reported unfixable problems.
"""

from __future__ import annotations

import sys

from _common import REPO_ROOT
from _gates import Gate, git_ls, npm_script_gate, npx_tool_installed, run_cli, skip, which

VSCODE_DIR = REPO_ROOT / "extensions" / "vscode"
ZED_DIR = REPO_ROOT / "extensions" / "zed"

# Pinned to match the version the markdown CI workflow runs via `npx --yes`.
MARKDOWNLINT_SPEC = "markdownlint-cli2@0.22.1"

# Directories CI runs clang-format over (ci.yml, mirroring cmake/LumaRunClangTool.cmake):
# the four source trees plus the test and fuzz sources. Deliberately narrower
# than "every tracked .cpp/.hpp" — debugger/tests and language-server/tests are
# not part of the formatting surface, so `-i` must not rewrite them.
CLANG_FORMAT_DIRS = (
    "core",
    "shared",
    "language-server/source",
    "debugger/source",
    "tests",
    "fuzz",
)


def _ruff_gates() -> list[Gate]:
    """Ruff auto-fix (import ordering and friends) followed by the formatter."""
    ruff = which("ruff")
    if ruff is None:
        reason = "ruff not found (pip install ruff)"
        return [
            skip("ruff-fix", "Python auto-fix (Ruff)", reason),
            skip("ruff-format", "Python format (Ruff)", reason),
        ]
    return [
        Gate("ruff-fix", "Python auto-fix (Ruff)", [ruff, "check", "--fix", "."]),
        Gate("ruff-format", "Python format (Ruff)", [ruff, "format", "."]),
    ]


def _clang_format_gate() -> Gate:
    """clang-format in place over tracked C++ (excluding vendored external/)."""
    name, description = "clang-format", "C++ format (clang-format)"
    exe = which("clang-format")
    if exe is None:
        return skip(name, description, "clang-format not found (install LLVM/clang tools)")
    files = [f for f in git_ls(*CLANG_FORMAT_DIRS) if f.endswith((".cpp", ".hpp"))]
    if not files:
        return skip(name, description, "no C++ files tracked")
    # .clang-format-ignore excludes the generated asset header, honoured by clang-format 18+.
    return Gate(name, description, [exe, "-i"], files=files)


def _cargo_fmt_gate() -> Gate:
    """rustfmt in place for the Zed extension."""
    name, description = "cargo-fmt", "Rust format (rustfmt, Zed extension)"
    cargo = which("cargo")
    if cargo is None:
        return skip(name, description, "cargo not found (install Rust via rustup)")
    return Gate(name, description, [cargo, "fmt"], cwd=ZED_DIR)


def _stylelint_gate() -> Gate:
    """Stylelint --fix over first-party CSS.

    Skipped unless Stylelint is already installed locally: its shared config
    (stylelint-config-standard) cannot be supplied by an on-demand npx install.
    """
    name, description = "stylelint", "CSS auto-fix (Stylelint)"
    files = git_ls("*.css", ":!:*.min.css")
    if not files:
        return skip(name, description, "no CSS files tracked")
    if not npx_tool_installed("stylelint"):
        return skip(name, description, "stylelint not installed locally (see CONTRIBUTING.md)")
    return Gate(name, description, [which("npx"), "stylelint", "--fix"], files=files)


def _markdownlint_gate() -> Gate:
    """markdownlint-cli2 --fix over first-party Markdown.

    Globs and ignores come from .markdownlint-cli2.jsonc, so no paths are passed.
    """
    name, description = "markdownlint", "Markdown auto-fix (markdownlint-cli2)"
    npx = which("npx")
    if npx is None:
        return skip(name, description, "npx not found (install Node.js)")
    return Gate(name, description, [npx, "--yes", MARKDOWNLINT_SPEC, "--fix"])


def build_gates() -> list[Gate]:
    """Assemble the ordered list of formatter gates."""
    return [
        *_ruff_gates(),
        _clang_format_gate(),
        npm_script_gate(
            "eslint",
            "TypeScript auto-fix (ESLint, VS Code extension)",
            script="lint:eslint",
            project_dir=VSCODE_DIR,
            missing_hint="run `npm ci` in extensions/vscode first",
            extra_args=("--fix",),
        ),
        npm_script_gate(
            "prettier",
            "TS/JS format (Prettier, VS Code extension)",
            script="format",
            project_dir=VSCODE_DIR,
            missing_hint="run `npm ci` in extensions/vscode first",
        ),
        _cargo_fmt_gate(),
        _stylelint_gate(),
        _markdownlint_gate(),
    ]


def main() -> int:
    return run_cli(
        build_gates(),
        prog="format.py",
        description="Apply the repository auto-formatters and safe auto-fixes.",
    )


if __name__ == "__main__":
    sys.exit(main())
