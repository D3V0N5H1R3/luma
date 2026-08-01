# ─────────────────────────────────────────────────────────────
# Luma — Dev Container image
#
# Ubuntu 24.04 (Noble) ships the exact toolchain Luma's CI pins, straight from
# the distribution archives — GCC 14, CMake 3.28 (>= 3.21 required),
# clang-format/clang-tidy 18, and libwebkit2gtk-4.1-dev for the GraphicalUi
# backend — so this image needs no external apt repositories or PPAs and builds
# fully offline against the vendored dependencies in external/.
# ─────────────────────────────────────────────────────────────
FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

# Build-time only (an ARG, not a persisted ENV), so debconf stays interactive
# for the user's own apt commands later.
ARG DEBIAN_FRONTEND=noninteractive

# One apt layer installs the complete build / test / lint toolchain:
#   gcc-14 g++-14 ........... primary compiler pair (ci.yml's main Linux job)
#   cmake ninja-build make .. build system + the default (Make) and ninja presets
#   pkg-config .............. GraphicalUi / WebKitGTK discovery
#   libwebkit2gtk-4.1-dev ... enables GraphicalUi (else it compiles to a stub)
#   clang-format-18 ......... C++ formatting gate (Formatting job in ci.yml)
#   clang-tidy-18 ........... C++ static-analysis gate (Static Analysis job)
#   gdb ..................... native debugging of the interpreter (CONTRIBUTING.md §Debugging the C++ Interpreter)
#   lcov ................... the coverage preset (LUMA_FEATURE_COVERAGE)
#   python3 python3-pip pipx  helper scripts under scripts/ and the Ruff CLI
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        gcc-14 \
        g++-14 \
        cmake \
        ninja-build \
        make \
        pkg-config \
        libwebkit2gtk-4.1-dev \
        clang-format-18 \
        clang-tidy-18 \
        gdb \
        lcov \
        python3 \
        python3-pip \
        pipx \
    && rm -rf /var/lib/apt/lists/*

# Make the unversioned tool names resolve to the CI-pinned versions, so `cc` /
# `c++` (used by the presets and CMake Tools) and the `clang-format` /
# `clang-tidy` the lint scripts invoke all match the CI toolchain. Separate
# --install groups (no slaves) avoid clashing with the alternatives the gcc / g++
# packages register for cc / c++.
RUN update-alternatives --install /usr/bin/gcc  gcc  /usr/bin/gcc-14 140 \
    && update-alternatives --install /usr/bin/g++  g++  /usr/bin/g++-14 140 \
    && update-alternatives --install /usr/bin/cc   cc   /usr/bin/gcc-14 140 \
    && update-alternatives --install /usr/bin/c++  c++  /usr/bin/g++-14 140 \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-18 100 \
    && update-alternatives --install /usr/bin/clang-tidy   clang-tidy   /usr/bin/clang-tidy-18 100

# Default the interpreter build to the CI compiler pair. scripts/container-build.sh
# requires CC / CXX to be set, and CMake honours them when a preset names no
# compiler, so `cmake --preset default` picks GCC 14 without a custom kit.
ENV CC=gcc-14 \
    CXX=g++-14
