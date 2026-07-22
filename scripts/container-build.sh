#!/bin/sh
# ─────────────────────────────────────────────────────────────
# Luma — Container build & test helper
#
# Configures, builds, and runs the full CTest suite (C++ unit
# tests and Luma feature tests) using the compilers named by the
# CC and CXX environment variables.
#
# Kept POSIX-sh compatible (no bashisms) so it runs unmodified
# under the default shell of every container image used in CI
# (Debian, Kali, Fedora, Arch, and the ARM64 Debian image).
# ─────────────────────────────────────────────────────────────
set -eu

: "${CC:?CC must be set (C compiler)}"
: "${CXX:?CXX must be set (C++ compiler)}"

cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="${CC}" \
  -DCMAKE_CXX_COMPILER="${CXX}"

# An explicit job count is required: `--parallel` with no number becomes
# `make -j` (unlimited) under the Makefiles generator, which spawns hundreds of
# compiles and OOM-kills the container. Bound it to the detected core count.
cmake --build build --parallel "$(nproc)"

ctest --test-dir build --output-on-failure -C Release
