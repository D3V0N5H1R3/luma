#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────
# Luma — Dev Container prepare (updateContentCommand)
#
# Builds the C++ core (interpreter, language server, debugger) and sets up the
# VS Code and Zed extensions so the container opens ready to develop every
# component. Runs during Codespaces prebuilds and re-runs incrementally when
# sources change.
#
# The C++ build is required and builds offline against the vendored dependencies
# in external/. The extension setup needs the network (npm registry / crates.io)
# and is therefore best-effort: a warning — not a failure — keeps an offline
# prebuild usable, and the step can be re-run by hand later.
# ─────────────────────────────────────────────────────────────
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

# ── C++ core (required) ──
# Bound the job count to the core count: an unbounded `make -j` spawns hundreds
# of compiles and OOM-kills the container (see scripts/container-build.sh).
echo "prepare: building the interpreter, language server, and debugger (C++)..."
cmake --preset default
cmake --build build --parallel "$(nproc)"

# ── Best-effort helper ──
# Runs a step, warning instead of failing so an offline prebuild still yields a
# working interpreter. The extension steps need network access for npm/crates.
setup() {
    description="$1"
    shift
    if "$@"; then
        return 0
    fi
    echo "warning: ${description} skipped (network unavailable?) — re-run it manually later" >&2
}

# ── VS Code extension + tree-sitter parser (best-effort; need npm) ──
if command -v npm >/dev/null 2>&1; then
    echo "prepare: setting up the VS Code extension (npm ci + compile)..."
    setup "VS Code extension setup" \
        bash -c 'cd extensions/vscode && npm ci && npm run compile'

    echo "prepare: generating the tree-sitter parser for the Zed extension..."
    setup "tree-sitter parser generation" \
        bash -c 'cd extensions/zed/grammars/tree-sitter-luma && npm ci && ./node_modules/.bin/tree-sitter generate'
else
    echo "warning: npm not found — skipping VS Code extension and tree-sitter setup" >&2
fi

# ── Zed extension WASM (best-effort; needs cargo) ──
if command -v cargo >/dev/null 2>&1; then
    echo "prepare: building the Zed extension (wasm32-wasip1)..."
    setup "Zed extension build" \
        bash -c 'cd extensions/zed && cargo build --release --target wasm32-wasip1'
else
    echo "warning: cargo not found — skipping Zed extension build" >&2
fi

echo "prepare: done — interpreter and extensions ready."
