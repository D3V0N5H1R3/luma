---
description: "Use when building, configuring, or troubleshooting the Luma build system. Covers manual commands, CMake presets, sanitizers, coverage, fuzz testing, and cross-platform considerations."
applyTo: "**/{CMakeLists.txt,CMakePresets.json}"
priority: reference
---

# Building and Testing Luma

How to configure, build, and test the Luma interpreter. This document covers the **user-facing build workflow** — what commands to run and what options are available. For instructions on how to _write_ `CMakeLists.txt` files, see [cmake.instructions.md](cmake.instructions.md).

---

## Table of Contents

1. [Quick Start](#1--quick-start)
2. [Manual Build Commands](#2--manual-build-commands)
3. [CMake Presets](#3--cmake-presets)
4. [Build Types](#4--build-types)
5. [Sanitizer Configuration](#5--sanitizer-configuration)
6. [Coverage Measurement](#6--coverage-measurement)
7. [Fuzz Testing Setup](#7--fuzz-testing-setup)
8. [Cross-Platform Considerations](#8--cross-platform-considerations)
9. [Build Options Reference](#9--build-options-reference)
10. [Checklist](#10--checklist)

---

## 1 — Quick Start

The fastest way to build and test uses CMake presets defined in `CMakePresets.json`:

```bash
# Configure + build + test (Release)
cmake --preset default
cmake --build --preset default
ctest --preset default
```

For a debug build:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The executables land at the root of the build directory on every platform: `build/luma`, `build/luma_lsp`, and `build/luma_dap` (with `.exe` on Windows), or `build-debug/…` for the `debug` preset.

---

## 2 — Manual Build Commands

When presets are not available or you need custom flags, use the two-step configure-and-build flow.

### Configuration

```bash
# Release (optimised)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Debug (full symbols, no optimisation)
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
```

- `-B <dir>` — build output directory.
- `-DCMAKE_BUILD_TYPE` — `Release`, `Debug`, or `RelWithDebInfo`.

### Building

```bash
cmake --build build --config Release --parallel
```

- `--config <type>` — required for multi-config generators (Visual Studio, Xcode): they ignore `CMAKE_BUILD_TYPE` when building and default to Debug, so a bare `cmake --build build` there builds Debug into `<build>/Debug/` and leaves the Release binaries at `build/` stale. Ninja Multi-Config defaults to the tree's primary configuration instead. See [cmake/PRESETS.md](../cmake/PRESETS.md#build-directories-and-binaries) for `LUMA_PRIMARY_CONFIG`, which picks the configuration placed at the build-tree root.
- `--parallel` — uses all available cores.

### Running Tests

```bash
cd build && ctest --output-on-failure -C Release
```

---

## 3 — CMake Presets

`CMakePresets.json` defines ready-made configurations (`default`, `debug`, `relwithdebinfo`, `sanitize`, `coverage`, `lto`, `ninja`, and IDE- and platform-specific ones). [cmake/PRESETS.md](../cmake/PRESETS.md) is the single reference for what each preset does, its platforms, and its build directory; `python scripts/configure.py` lists the presets available on the current host.

```bash
cmake --preset <name>
cmake --build --preset <name>
ctest --preset <name>
```

---

## 4 — Build Types

- `Release` — default. Optimised for performance, minimal debug information.
- `Debug` — full debug symbols, no optimisation. Use with a debugger.
- `RelWithDebInfo` — optimised build that retains debug symbols for profiling.

---

## 5 — Sanitizer Configuration

Sanitizers detect runtime errors such as buffer overflows, use-after-free, and undefined behaviour. Luma supports AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan). These require Clang or GCC.

### Using the Preset

```bash
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

### Manual Configuration

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DLUMA_FEATURE_SANITIZERS=ON

cmake --build build-asan
cd build-asan && ctest --output-on-failure
```

---

## 6 — Coverage Measurement

Code coverage shows which parts of the code are exercised by tests. Requires GCC or Clang.

### Using the Preset

```bash
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
```

### Manual Configuration

```bash
cmake -B build-coverage -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -DLUMA_FEATURE_COVERAGE=ON

cmake --build build-coverage
cd build-coverage && ctest
```

After running tests, generate a report with `gcovr` or `lcov`:

```bash
gcovr --root ../ --html --html-details -o coverage.html
```

---

## 7 — Fuzz Testing Setup

Fuzz testing finds bugs by feeding random inputs to the program. Luma uses LibFuzzer, which requires Clang.

```bash
cmake -B build-fuzz -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DLUMA_BUILD_FUZZ=ON

cmake --build build-fuzz
```

Run a fuzzer with an optional corpus directory:

```bash
./build-fuzz/fuzz_parser fuzz/corpus/ -dict=fuzz/dictionary.txt -max_total_time=300
```

---

## 8 — Cross-Platform Considerations

- **Windows (MSVC)** — the `default` and `debug` presets use the newest installed Visual Studio generator; the `msvc` preset pins Visual Studio 2022 for use in the IDE. When building without a build preset, always pass `--config Release` (or `Debug`) with `cmake --build` for multi-config generators — Visual Studio otherwise builds Debug. `python scripts/configure.py msvc --build` runs the `msvc-release` build preset (`--config Debug` selects `msvc-debug`). The C/C++ runtime is linked statically (`/MT`, `/MTd` for Debug; `CMAKE_MSVC_RUNTIME_LIBRARY` in the root `CMakeLists.txt`), so the executables need no Visual C++ Redistributable; an AddressSanitizer build (`-fsanitize=address` / `/fsanitize=address` in the global flags) keeps the DLL runtime, and an explicit `-DCMAKE_MSVC_RUNTIME_LIBRARY=...` always wins.
- **macOS (Xcode)** — the `default` preset uses Makefiles; use the `xcode` / `xcode-release` presets to work in Xcode. The `--config` flag is required there too.
- **Binary location** — every generator places the executables at the root of the build directory (`build/luma`, `build\luma.exe`); in a multi-config tree that is the primary configuration (`LUMA_PRIMARY_CONFIG`) and any other goes to `<build>/<Config>/`. See [cmake/PRESETS.md](../cmake/PRESETS.md#build-directories-and-binaries).
- **Linux** — GCC and Clang are both fully supported. The `default` preset works out of the box. The minimum is **GCC 13** (or Clang 15): Luma uses `std::format` and `std::chrono::clock_cast`, which libstdc++ ships only from GCC 13, so GCC 12 and earlier — such as the default `g++` on Raspberry Pi OS and Debian 12 "bookworm" — cannot build the project. Install GCC 13+ and pass `-DCMAKE_CXX_COMPILER=g++-13` when configuring.

---

## 9 — Build Options Reference

| Option                    | Default | Purpose                                           |
| ------------------------- | ------- | ------------------------------------------------- |
| `LUMA_BUILD_TESTS`        | `ON`    | Build and register C++ unit and integration tests |
| `LUMA_FEATURE_TLS`        | `ON`    | Enable HTTPS support via Mbed TLS                 |
| `LUMA_BUILD_FUZZ`         | `OFF`   | Build LibFuzzer fuzz targets (Clang only)         |
| `LUMA_FEATURE_SANITIZERS` | `OFF`   | Enable ASan + UBSan (Clang/GCC only)              |
| `LUMA_FEATURE_COVERAGE`   | `OFF`   | Enable code coverage instrumentation (Clang/GCC)  |
| `LUMA_FEATURE_LTO`        | `OFF`   | Enable link-time optimisation for Release builds  |
| `LUMA_PRIMARY_CONFIG`     | Release | Multi-config: configuration placed at build root  |

`LUMA_PRIMARY_CONFIG` defaults to `CMAKE_DEFAULT_BUILD_TYPE`, else `CMAKE_BUILD_TYPE`, else Release, and must be one of `CMAKE_CONFIGURATION_TYPES`; single-config generators ignore it.

---

## 10 — Checklist

- [ ] The project configures cleanly from a fresh build directory.
- [ ] `cmake --build` completes with no errors.
- [ ] `ctest --output-on-failure` reports zero failures.
- [ ] Luma feature tests pass: `build/luma --strict --test tests/features/language/<file>.luma`.
- [ ] Multi-config generators use the `--config` flag.
- [ ] Sanitizer or coverage builds use the appropriate preset or flags.
