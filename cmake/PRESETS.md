# CMake Presets

This document describes the CMake presets defined in [`CMakePresets.json`](../CMakePresets.json). Presets bundle a generator, build type, and option choices into a single name so that common builds are a single command.

For the wider build workflow — manual commands, sanitizers, coverage, fuzz testing, and the full build-options reference — see [build.instructions.md](../instructions/build.instructions.md).

> **Note:** The preset file uses schema version 3 and therefore requires CMake 3.21 or later.

## Table of Contents

1. [Configure Presets](#configure-presets)
2. [Build Presets](#build-presets)
3. [Test Presets](#test-presets)
4. [Build Directories](#build-directories)
5. [Usage Examples](#usage-examples)
6. [When to Use Each Preset](#when-to-use-each-preset)
7. [Related Documentation](#related-documentation)

## Configure Presets

A configure preset selects the generator, build type, and feature options. Platform-specific presets carry a `condition`, so they are hidden on incompatible hosts.

| Preset             | Description                                                          | Platform    | Build Type     |
| ------------------ | ------------------------------------------------------------------- | ----------- | -------------- |
| `default`          | Standard release build for all platforms.                           | All         | Release        |
| `debug`            | Debug build with full symbols.                                      | All         | Debug          |
| `msvc`             | Visual Studio 17 2022 multi-config generator.                       | Windows     | Multi          |
| `xcode`            | Xcode multi-config generator.                                       | macOS       | Multi          |
| `sanitize`         | Debug build with AddressSanitizer and UndefinedBehaviorSanitizer.   | Linux/macOS | Debug          |
| `relwithdebinfo`   | Optimised build that retains debug symbols for profiling.           | All         | RelWithDebInfo |
| `coverage`         | Debug build with lcov code-coverage instrumentation.                | Linux/macOS | Debug          |
| `lto`              | Release build with link-time optimisation.                          | All         | Release        |
| `ninja`            | Ninja generator for fast parallel builds.                           | All         | Release        |
| `arm64-cross`      | Cross-compile for ARM64 (`aarch64-linux-gnu`).                      | Linux       | Release        |
| `release-no-tests` | Release build with `LUMA_BUILD_TESTS=OFF` for production deployment. | All         | Release        |
| `windows-arm64`    | MSVC build targeting ARM64 on Windows.                              | Windows     | Release        |
| `macos-universal`  | Xcode build producing a universal binary (x86_64 + arm64).          | macOS       | Release        |

The `msvc` and `xcode` generators are multi-config: their build type is chosen at build time by the corresponding build preset (see below) rather than at configure time.

### Prerequisites

- `sanitize` and `coverage` require GCC or Clang and are unavailable on Windows (MSVC).
- `arm64-cross` requires the `aarch64-linux-gnu` cross-compilation toolchain.
- `windows-arm64` requires the MSVC ARM64 toolchain; `macos-universal` requires an Xcode toolchain able to target both `x86_64` and `arm64`.

## Build Presets

Each build preset references a configure preset and selects the configuration to build. Every build preset sets `jobs: 0`, which uses all available cores for parallel compilation.

| Preset             | Configure Preset   | Configuration  |
| ------------------ | ------------------ | -------------- |
| `default`          | `default`          | Release        |
| `debug`            | `debug`            | Debug          |
| `msvc-release`     | `msvc`             | Release        |
| `msvc-debug`       | `msvc`             | Debug          |
| `xcode-release`    | `xcode`            | Release        |
| `xcode-debug`      | `xcode`            | Debug          |
| `sanitize`         | `sanitize`         | Debug          |
| `relwithdebinfo`   | `relwithdebinfo`   | RelWithDebInfo |
| `coverage`         | `coverage`         | Debug          |
| `lto`              | `lto`              | Release        |
| `ninja`            | `ninja`            | Release        |
| `arm64-cross`      | `arm64-cross`      | Release        |
| `release-no-tests` | `release-no-tests` | Release        |
| `windows-arm64`    | `windows-arm64`    | Release        |
| `macos-universal`  | `macos-universal`  | Release        |

The multi-config configure presets (`msvc`, `xcode`) expose two build presets each — one for `Release` and one for `Debug` — because the configuration is selected here rather than at configure time.

## Test Presets

Each test preset runs the CTest suite for its configure preset. All test presets enable `outputOnFailure`, so output from failing tests is shown automatically.

| Preset           | Configure Preset | Notes                                |
| ---------------- | ---------------- | ------------------------------------ |
| `default`        | `default`        | Standard release tests.              |
| `debug`          | `debug`          | Debug tests.                         |
| `msvc`           | `msvc`           | MSVC release tests.                  |
| `xcode`          | `xcode`          | Xcode release tests.                 |
| `sanitize`       | `sanitize`       | Tests under sanitizers.              |
| `relwithdebinfo` | `relwithdebinfo` | RelWithDebInfo tests.                |
| `coverage`       | `coverage`       | Tests with coverage instrumentation. |
| `lto`            | `lto`            | Tests with LTO.                      |
| `ninja`          | `ninja`          | Tests with the Ninja generator.      |

The remaining presets are configure-and-build only and have no test preset: the cross-compilation and multi-architecture presets (`arm64-cross`, `windows-arm64`, `macos-universal`) produce binaries that may not run on the build host, and `release-no-tests` builds with tests disabled.

## Build Directories

Each preset writes to its own directory so that configurations never clash. For single-config generators the binary is placed directly in the directory; for the multi-config `msvc` and `xcode` generators it is placed in a per-configuration subdirectory (for example `build-msvc/Release/`).

| Configure Preset   | Build Directory           |
| ------------------ | ------------------------- |
| `default`          | `build/`                  |
| `debug`            | `build-debug/`            |
| `msvc`             | `build-msvc/`             |
| `xcode`            | `build-xcode/`            |
| `sanitize`         | `build-sanitize/`         |
| `relwithdebinfo`   | `build-relwithdebinfo/`   |
| `coverage`         | `build-coverage/`         |
| `lto`              | `build-lto/`              |
| `ninja`            | `build-ninja/`            |
| `arm64-cross`      | `build-arm64/`            |
| `release-no-tests` | `build-release-no-tests/` |
| `windows-arm64`    | `build-windows-arm64/`    |
| `macos-universal`  | `build-macos-universal/`  |

## Usage Examples

```bash
# Configure and build with the default (Release) preset
cmake --preset default
cmake --build --preset default

# Run tests
ctest --preset default

# Debug build with sanitizers (Linux/macOS only)
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize

# Release build with LTO
cmake --preset lto
cmake --build --preset lto
ctest --preset lto

# MSVC on Windows (multi-config: pick the configuration in the build/test step)
cmake --preset msvc
cmake --build --preset msvc-release
ctest --preset msvc

# Fast parallel builds with Ninja
cmake --preset ninja
cmake --build --preset ninja
ctest --preset ninja

# Coverage analysis (Linux/macOS only)
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
```

## When to Use Each Preset

- **`default`** — Everyday development and CI release builds.
- **`debug`** — Step-through debugging with full symbols.
- **`sanitize`** — Diagnosing memory errors or undefined behaviour.
- **`coverage`** — Measuring test coverage with lcov/gcov.
- **`lto`** — Production-quality builds with cross-module optimisation.
- **`relwithdebinfo`** — Profiling optimised code while retaining symbols.
- **`ninja`** — Faster incremental builds when Ninja is available.
- **`release-no-tests`** — Minimal production build without test overhead.
- **`msvc` / `xcode`** — IDE-native multi-config builds on Windows/macOS.
- **`arm64-cross` / `windows-arm64` / `macos-universal`** — Cross-compilation and multi-architecture builds.

## Related Documentation

- [build.instructions.md](../instructions/build.instructions.md) — User-facing build workflow, manual commands, and the full build-options reference.
- [cmake.instructions.md](../instructions/cmake.instructions.md) — Conventions for writing `CMakeLists.txt` and preset files.
- [`CMakePresets.json`](../CMakePresets.json) — The authoritative preset definitions described here.
