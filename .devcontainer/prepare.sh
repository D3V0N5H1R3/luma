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

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

# True when build/ holds a CMake cache created outside this container — e.g. a
# host build in a bind-mounted local Dev Container. CMake refuses to reuse a
# cache from another directory, and the cache belongs to the host, so it is
# reported rather than deleted.
has_foreign_cmake_cache() {
    local cache_file="${repo_root}/build/CMakeCache.txt"
    local cache_dir

    if [[ ! -f "$cache_file" ]]; then
        return 1
    fi

    cache_dir="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$cache_file")"
    [[ "$cache_dir" != "${repo_root}/build" ]]
}

# Bound the job count to the core count: an unbounded `make -j` spawns hundreds
# of compiles and OOM-kills the container (see scripts/container-build.sh).
build_core() {
    printf "prepare: building the interpreter, language server, and debugger (C++)...\n"
    cmake --preset default
    cmake --build build --parallel "$(nproc)"
}

# Runs a step, warning instead of failing so an offline prebuild still yields a
# working interpreter. `set -e` is suspended inside the `if` condition, so each
# step chains its commands with `&&` to stop at the first failure.
run_best_effort() {
    local description="$1"
    shift

    if ! "$@"; then
        printf "warning: %s skipped (network unavailable?) — re-run it manually later\n" \
            "$description" >&2
    fi
}

setup_vscode_extension() {
    (cd extensions/vscode && npm ci && npm run compile)
}

generate_tree_sitter_parser() {
    (cd extensions/zed/grammars/tree-sitter-luma \
        && npm ci \
        && ./node_modules/.bin/tree-sitter generate)
}

build_zed_extension() {
    (cd extensions/zed && cargo build --release --target wasm32-wasip1)
}

# ── C++ core (required) ──
if has_foreign_cmake_cache; then
    printf "warning: build/ holds a CMake cache created outside the container (host build?);\n" >&2
    printf "         skipping the C++ build. Move or delete build/, then re-run:\n" >&2
    printf "         bash .devcontainer/prepare.sh\n" >&2
else
    build_core
fi

# ── VS Code extension + tree-sitter parser (best-effort; need npm) ──
if command -v npm &>/dev/null; then
    printf "prepare: setting up the VS Code extension (npm ci + compile)...\n"
    run_best_effort "VS Code extension setup" setup_vscode_extension

    printf "prepare: generating the tree-sitter parser for the Zed extension...\n"
    run_best_effort "tree-sitter parser generation" generate_tree_sitter_parser
else
    printf "warning: npm not found — skipping VS Code extension and tree-sitter setup\n" >&2
fi

# ── Zed extension WASM (best-effort; needs cargo) ──
if command -v cargo &>/dev/null; then
    printf "prepare: building the Zed extension (wasm32-wasip1)...\n"
    run_best_effort "Zed extension build" build_zed_extension
else
    printf "warning: cargo not found — skipping Zed extension build\n" >&2
fi

printf "prepare: done.\n"
