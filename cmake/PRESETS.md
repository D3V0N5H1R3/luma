# CMake Presets

[`CMakePresets.json`](../CMakePresets.json) bundles a generator, build type, and option choices into a single name, so a common build is three commands. This page is the one place the presets are described; to see the presets available on your machine, run `python scripts/configure.py` or `cmake --list-presets`.

For the wider build workflow — manual commands, sanitizers, coverage, fuzz testing, and the build-options reference — see [build.instructions.md](../instructions/build.instructions.md).

> **Note:** The preset file uses schema version 3 and therefore requires CMake 3.21 or later.

## Usage

```bash
cmake --preset <name>           # configure
cmake --build --preset <name>   # build
ctest --preset <name>           # test
```

`python scripts/configure.py <name> --build` configures and builds in one step and explains when a preset is unavailable on the current host. It runs the build preset of the same name; for `msvc` and `xcode`, which have one build preset per configuration, it runs `<name>-release` unless you pass `--config Debug` (which selects `<name>-debug`).

## Presets

| Preset           | Purpose                                                      | Platforms   | Build presets                  | Test preset |
| ---------------- | ------------------------------------------------------------ | ----------- | ------------------------------ | ----------- |
| `default`        | Release build; everyday development and CI.                  | All         | `default`                      | `default`   |
| `debug`          | Debug build with full symbols, for step-through debugging.   | All         | `debug`                        | `debug`     |
| `relwithdebinfo` | Optimised build that keeps debug symbols, for profiling.     | All         | `relwithdebinfo`               | Yes         |
| `sanitize`       | Debug build with AddressSanitizer and UBSan.                 | Linux/macOS | `sanitize`                     | Yes         |
| `coverage`       | Debug build with lcov/gcov coverage instrumentation.         | Linux/macOS | `coverage`                     | Yes         |
| `lto`            | Release build with link-time optimisation.                   | All         | `lto`                          | Yes         |
| `ninja`          | Release build with the Ninja generator (fast incremental).   | All         | `ninja`                        | Yes         |
| `msvc`           | Visual Studio 17 2022 multi-config build, for the IDE.       | Windows     | `msvc-release`, `msvc-debug`   | `msvc`      |
| `xcode`          | Xcode multi-config build, for the IDE.                       | macOS       | `xcode-release`, `xcode-debug` | `xcode`     |
| `windows-arm64`  | Native ARM64 binaries for Windows on ARM (build only).       | Windows     | `windows-arm64`                | No          |

"Yes" means the test preset has the same name as the configure preset. `windows-arm64` has no test preset because its binaries may not run on the build host.

Platform-specific presets carry a `condition`, so CMake hides them on other hosts. `sanitize` and `coverage` need GCC or Clang; `windows-arm64` needs the MSVC ARM64 toolchain.

There is deliberately no preset for a build without tests or for Linux ARM64: add `-DLUMA_BUILD_TESTS=OFF` to any configure command to skip the tests, and build ARM64 Linux binaries natively on an ARM64 host, as CI does (see [`build-raspberry-pi.yml`](../.github/workflows/build-raspberry-pi.yml)).

## Build Directories and Binaries

Each preset builds in its own directory, so configurations never clash: `default` uses `build/`, and every other preset uses `build-<name>/` (for example `build-debug/`, `build-sanitize/`, `build-msvc/`).

The executables — `luma`, `luma_lsp`, `luma_dap`, and the test executables — are placed at the root of that directory on every platform and generator: `build/luma`, or `build\luma.exe` on Windows.

In a multi-config tree (`msvc`, `xcode`, `windows-arm64`, any Visual Studio build, or a Ninja Multi-Config build) the root holds the tree's *primary configuration* and any other configuration built in the same tree goes to `<build>/<Config>/`. The primary configuration is the `LUMA_PRIMARY_CONFIG` cache variable, which defaults to `CMAKE_DEFAULT_BUILD_TYPE`, else `CMAKE_BUILD_TYPE` (set by `default` and `debug`), else Release; set it at configure time to choose another, e.g. `-DLUMA_PRIMARY_CONFIG=Debug`.

The build presets always name the configuration, so `cmake --build --preset <name>` refreshes the root. Without a preset:

- **Visual Studio and Xcode** build Debug unless told otherwise, so always pass `--config`, e.g. `cmake --build build --config Release` (and `ctest -C Release`). A bare `cmake --build build` builds Debug into `<build>/Debug/` (unless Debug is the primary configuration) and leaves the binaries at the root untouched and possibly stale.
- **Ninja Multi-Config** builds the primary configuration by default: the build sets `CMAKE_DEFAULT_BUILD_TYPE` to it unless you set that variable yourself.

## Build Parallelism

Every build preset sets `jobs: 0`, which is equivalent to a bare `cmake --build --parallel`: the native tool's default parallelism. That means `/m` (one project per core) for Visual Studio and Ninja's own default (about cores + 2), but an **unbounded** `make -j` for the Makefiles generator, which is the default on Linux and macOS. That can exhaust memory on large or instrumented builds. Cap it there by passing an explicit count, e.g. `cmake --build --preset default --parallel "$(nproc)"`. The preset's `jobs` value overrides `CMAKE_BUILD_PARALLEL_LEVEL`.

## Related Documentation

- [build.instructions.md](../instructions/build.instructions.md) — Build workflow, manual commands, and the full build-options reference.
- [cmake.instructions.md](../instructions/cmake.instructions.md) — Conventions for writing `CMakeLists.txt` and preset files.
