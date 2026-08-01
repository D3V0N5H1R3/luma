# CMake Modules

Helper modules and templates that support the Luma build. Each `.cmake` file is included from a `CMakeLists.txt` (or invoked directly) to keep the root and per-directory build scripts small and focused.

For the build workflow — presets, sanitizers, coverage, and the full build-options reference — see [build.instructions.md](../instructions/build.instructions.md). For the conventions these modules follow, see [cmake.instructions.md](../instructions/cmake.instructions.md).

## Structure

| File                                                   | Purpose                                                                                         | Included From                                              |
| ------------------------------------------------------ | ----------------------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| [LumaCompilerFlags.cmake](LumaCompilerFlags.cmake)     | Warning, security-hardening, and instrumentation flags; the `luma_project_options` interface target and `luma_set_compile_options()`, plus LTO, sanitizer, coverage, and stack-reservation helpers. | [`CMakeLists.txt`](../CMakeLists.txt)                      |
| [LumaTargetHelpers.cmake](LumaTargetHelpers.cmake)     | `luma_add_executable()`, `luma_add_library()`, and `luma_add_tool_library()` wrappers that create a target and apply the project compile options in one step. | [`CMakeLists.txt`](../CMakeLists.txt)                      |
| [LumaPlatformCodegen.cmake](LumaPlatformCodegen.cmake) | Regenerates the editor-extension platform mappings (TypeScript, Rust) from the shared `platform-map.json` via the `generate_platform_code` target. | [`CMakeLists.txt`](../CMakeLists.txt) |
| [LumaCodeQuality.cmake](LumaCodeQuality.cmake)         | Defines the `format` and `tidy` convenience targets when clang-format / clang-tidy are available. | [`CMakeLists.txt`](../CMakeLists.txt) |
| [LumaPackaging.cmake](LumaPackaging.cmake)             | CPack metadata, install components, and the per-platform generators (ZIP/TGZ everywhere, DEB+RPM on Linux, NSIS on Windows). | [`CMakeLists.txt`](../CMakeLists.txt) |
| [LumaWebView.cmake](LumaWebView.cmake)                 | Detects the platform WebView backend and exposes it as the `luma_webview` interface target for the `GraphicalUi` module. | [`core/runtime/CMakeLists.txt`](../core/runtime/CMakeLists.txt) |
| [LumaMbedTLS.cmake](LumaMbedTLS.cmake)                 | Builds the vendored Mbed TLS sources as the `mbedtls_lib` static target (HTTPS support).        | [`core/runtime/CMakeLists.txt`](../core/runtime/CMakeLists.txt) |
| [LumaMiniz.cmake](LumaMiniz.cmake)                     | Builds the vendored miniz sources as the `miniz_lib` static target (deflate / gzip).            | [`core/runtime/CMakeLists.txt`](../core/runtime/CMakeLists.txt) |
| [LumaRunClangTool.cmake](LumaRunClangTool.cmake)       | Build-time driver that discovers all sources and runs clang-format or clang-tidy over them.      | `cmake -P`, via the `format` and `tidy` targets            |
| [LumaConfig.cmake.in](LumaConfig.cmake.in)             | Package configuration template providing the `Luma::luma`, `Luma::luma_lsp`, and `Luma::luma_dap` imported targets to `find_package(Luma)`. | Configured at install time by [`CMakeLists.txt`](../CMakeLists.txt) |
| [PRESETS.md](PRESETS.md)                               | Reference for the presets defined in [`CMakePresets.json`](../CMakePresets.json).               | Documentation only                                         |

## Design Rationale

The directory uses **one module per concern** rather than a few large files. Each `.cmake` file exists because it has a distinct responsibility, a different consumer, or a different execution mode:

- **Independent change frequency.** WebView backend detection (`LumaWebView`) changes for entirely different reasons than compiler warning flags (`LumaCompilerFlags`). Separate files mean unrelated changes never produce diffs in the same module.
- **Different inclusion points.** The vendored-library modules are included from `core/runtime/CMakeLists.txt` (close to the linking target), while build policy modules are included from the root. A monolith would need internal `if()` guards to replicate this scoping.
- **Different execution modes.** `LumaRunClangTool.cmake` runs as a standalone script (`cmake -P`), not via `include()` — it cannot be merged with the others.
- **Optional/conditional presence.** Code-quality targets are only defined when the tools are found. Keeping them isolated means the core build never sees dead code paths.
- **Discoverability.** A developer asking "how are compiler flags set?" navigates directly to `LumaCompilerFlags.cmake` instead of searching inside a multi-hundred-line monolith.

## How the Build Uses These Modules

The root [`CMakeLists.txt`](../CMakeLists.txt) includes `LumaCompilerFlags.cmake` and `LumaTargetHelpers.cmake` early, so every target created afterwards through the `luma_add_*` wrappers inherits the shared warning, security, and language-standard settings without re-applying them by hand. `LumaTargetHelpers.cmake` also provides `luma_configure_vendored_c_target()`, which applies the shared vendored-C policy (build to C99, suppress the project's strict warnings) so `LumaMiniz.cmake` and `LumaMbedTLS.cmake` do not repeat it.

The leaf concerns that trail the build wiring — `LumaPlatformCodegen.cmake` (the `generate_platform_code` target), `LumaCodeQuality.cmake` (the `format` and `tidy` targets), and `LumaPackaging.cmake` (CPack) — are each extracted into their own module and pulled in with a one-line `include()` at the point they previously occupied. `include()` runs in the including file's directory scope, so every `CMAKE_*`, `PROJECT_*`, and `CPACK_*` variable resolves exactly as it did inline; the split keeps the root file focused on build wiring.

The vendored third-party libraries (`LumaMbedTLS.cmake`, `LumaMiniz.cmake`) and the WebView backend detection (`LumaWebView.cmake`) are included from [`core/runtime/CMakeLists.txt`](../core/runtime/CMakeLists.txt), close to the target that links them, so the dependency wiring lives next to its consumer.

`LumaRunClangTool.cmake` is not included like the others — it is run as a standalone script (`cmake -P`) by the `format` and `tidy` convenience targets, discovering sources at build time so newly added files are covered without reconfiguring. `LumaConfig.cmake.in` is the install-time package config template, expanded by `configure_package_config_file()` into the `LumaConfig.cmake` that downstream projects consume via `find_package(Luma)`.

## Related Documentation

- [PRESETS.md](PRESETS.md) — The CMake presets that bundle generator, build type, and options.
- [cmake.instructions.md](../instructions/cmake.instructions.md) — Conventions for writing `CMakeLists.txt` and these helper modules.
- [build.instructions.md](../instructions/build.instructions.md) — Build workflow, manual commands, and the full build-options reference.
- [`CMakeLists.txt`](../CMakeLists.txt) — The root build script that wires these modules together.
