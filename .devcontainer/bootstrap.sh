#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────
# Luma — Dev Container bootstrap (onCreateCommand)
#
# Installs the developer-only CLI tooling that is not part of the apt image and
# runs once when the container is created (and during Codespaces prebuilds).
# Kept idempotent so a prebuild content refresh is safe to re-run.
# ─────────────────────────────────────────────────────────────
set -euo pipefail

# Pinned to the versions the CI gates run (ci-python.yml, ci-cmake.yml), so
# `scripts/lint.py` / `scripts/format.py` reproduce the CI result locally. The
# VS Code Ruff extension bundles its own copy; these are for the command line.
readonly RUFF_VERSION="0.15.17"
readonly CMAKELINT_VERSION="1.4.3"

# Installs an exact version of a Python CLI with pipx. A failure only warns, so
# a transient network error never aborts container creation — lint.py then
# reports the gate as skipped rather than running an unpinned version.
install_pinned_tool() {
    local package="$1"
    local version="$2"

    if ! pipx install --force "${package}==${version}"; then
        printf "warning: could not install %s %s; scripts/lint.py will skip its gate\n" \
            "$package" "$version" >&2
    fi
}

if ! command -v pipx &>/dev/null; then
    printf "warning: pipx not found; skipping the Ruff and cmakelint CLIs\n" >&2
    exit 0
fi

install_pinned_tool ruff "$RUFF_VERSION"
install_pinned_tool cmakelint "$CMAKELINT_VERSION"

# Put ~/.local/bin on PATH for interactive shells (remoteEnv covers the
# non-interactive lifecycle commands).
pipx ensurepath &>/dev/null || true

printf "bootstrap: developer tooling ready\n"
