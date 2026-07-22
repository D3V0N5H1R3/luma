#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────
# Luma — Dev Container bootstrap (onCreateCommand)
#
# Installs the developer-only CLI tooling that is not part of the apt image and
# runs once when the container is created (and during Codespaces prebuilds).
# Kept idempotent so a prebuild content refresh is safe to re-run.
# ─────────────────────────────────────────────────────────────
set -euo pipefail

# Ruff CLI — pinned to the version the Python CI gate (ci-python.yml) runs, so
# `scripts/lint.py` / `scripts/format.py` reproduce the CI result locally. The
# VS Code Ruff extension bundles its own copy; this is for the command line.
# Fall back to the latest release, then to a no-op, so a transient install
# failure never aborts container creation.
RUFF_VERSION="0.15.17"
if command -v pipx >/dev/null 2>&1; then
    pipx install --force "ruff==${RUFF_VERSION}" \
        || pipx install --force ruff \
        || echo "warning: could not install the Ruff CLI; the VS Code Ruff extension still works" >&2
    # Ensure ~/.local/bin is on PATH for interactive shells (remoteEnv covers
    # the non-interactive lifecycle commands).
    pipx ensurepath >/dev/null 2>&1 || true
fi

echo "bootstrap: developer tooling ready"
