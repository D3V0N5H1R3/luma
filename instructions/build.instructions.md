---
description: "Use when building, configuring, or troubleshooting the Luma build system. Covers manual commands, CMake presets, sanitizers, coverage, fuzz testing, and cross-platform considerations."
applyTo: "**/{CMakeLists.txt,CMakePresets.json}"
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

- `--config <type>` — required for multi-config generators (Visual Studio, Xcode).
- `--parallel` — uses all available cores.

### Running Tests

```bash
cd build && ctest --output-on-failure -C Release
```

---

## 3 — CMake Presets

`CMakePresets.json` defines ready-made configurations so common builds are a single command. Available presets:

| Configure Preset   | Build Preset       | Purpose                               |
| ------------------ | ------------------ | ------------------------------------- |
| `default`          | `default`          | Release build (all platforms)         |
| `debug`            | `debug`            | Debug build with full symbols         |
| `msvc`             | `msvc-release`     | Visual Studio generator (Windows)     |
| `msvc`             | `msvc-debug`       | Visual Studio debug build (Windows)   |
| `xcode`            | `xcode-release`    | Xcode generator (macOS)               |
| `xcode`            | `xcode-debug`      | Xcode debug build (macOS)             |
| `sanitize`         | `sanitize`         | Debug + ASan + UBSan (Clang/GCC)      |
| `relwithdebinfo`   | `relwithdebinfo`   | Optimised build with debug symbols    |
| `coverage`         | `coverage`         | Debug + lcov coverage instrumentation |
| `lto`              | `lto`              | Release + link-time optimisation      |
| `ninja`            | `ninja`            | Ninja generator (Release)             |
| `arm64-cross`      | `arm64-cross`      | ARM64 cross-compilation               |
| `release-no-tests` | `release-no-tests` | Release build without tests           |
| `windows-arm64`    | `windows-arm64`    | Windows ARM64 build                   |
| `macos-universal`  | `macos-universal`  | macOS universal binary                |

Usage pattern:

```bash
cmake --preset <configure-preset>
cmake --build --preset <build-preset>
ctest --preset <configure-preset>
```

---

## 4 — Build Types

- **`Release`** — default. Optimised for performance, minimal debug information.
- **`Debug`** — full debug symbols, no optimisation. Use with a debugger.
- **`RelWithDebInfo`** — optimised build that retains debug symbols for profiling.

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

- **Windows (MSVC)** — use the `msvc` / `msvc-release` presets with Visual Studio or VS Code. Always pass `--config Release` (or `Debug`) with `cmake --build` for multi-config generators.
- **macOS (Xcode)** — use the `xcode` / `xcode-release` presets. The `--config` flag is required here too.
- **Linux** — GCC and Clang are both fully supported. The `default` preset works out of the box.

---

## 9 — Build Options Reference

| Option                    | Default | Purpose                                           |
| ------------------------- | ------- | ------------------------------------------------- |
| `LUMA_BUILD_TESTS`        | `ON`    | Build and register C++ unit and integration tests |
| `LUMA_FEATURE_TLS`        | `ON`    | Enable HTTPS support via Mbed TLS                 |
| `LUMA_FEATURE_WEBVIEW`    | `ON`    | Enable the GraphicalUi module via the platform WebView backend |
| `LUMA_BUILD_FUZZ`         | `OFF`   | Build LibFuzzer fuzz targets (Clang only)         |
| `LUMA_FEATURE_SANITIZERS` | `OFF`   | Enable ASan + UBSan (Clang/GCC only)              |
| `LUMA_FEATURE_COVERAGE`   | `OFF`   | Enable code coverage instrumentation (Clang/GCC)  |
| `LUMA_FEATURE_LTO`        | `OFF`   | Enable link-time optimisation for Release builds  |

---

## 10 — Checklist

- [ ] The project configures cleanly from a fresh build directory.
- [ ] `cmake --build` completes with no errors.
- [ ] `ctest --output-on-failure` reports zero failures.
- [ ] Luma feature tests pass: `build/Release/luma --test tests/features/language/<file>.luma`.
- [ ] Multi-config generators use the `--config` flag.
- [ ] Sanitizer or coverage builds use the appropriate preset or flags.
