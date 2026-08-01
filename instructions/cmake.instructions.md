---
description: "Use when writing, reviewing, or modifying CMake files (CMakeLists.txt and .cmake modules). Covers target-based configuration, dependency management, compiler warnings, and project structure conventions."
applyTo: "**/{CMakeLists.txt,*.cmake}"
priority: reference
---

# Working with CMake

Every decision should favour **simplicity, readability, safety, and modern idioms**. When in doubt, prefer the approach that is easiest for a human to read six months later.

## Table of Contents

1. [Minimum Version & Project Declaration](#1--minimum-version--project-declaration)
2. [Formatting and Whitespace](#2--formatting-and-whitespace)
3. [Naming Conventions](#3--naming-conventions)
4. [Golden Rules](#4--golden-rules)
5. [Creating Targets](#5--creating-targets)
6. [Compiler Warnings & Safety Flags](#6--compiler-warnings--safety-flags)
7. [Finding & Linking Dependencies](#7--finding--linking-dependencies)
8. [Project Options](#8--project-options)
9. [Subdirectory Structure](#9--subdirectory-structure)
10. [Testing with CTest](#10--testing-with-ctest)
11. [Installing & Exporting](#11--installing--exporting)
12. [Presets](#12--presets)
13. [Sanitizers](#13--sanitizers)
14. [Code Coverage](#14--code-coverage)
15. [Anti-Patterns](#15--anti-patterns)
16. [Quick Reference: Build Commands](#16--quick-reference-build-commands)
17. [Checklist Before Committing a CMakeLists.txt](#17--checklist-before-committing-a-cmakeliststxt)

---

## 1 — Minimum Version & Project Declaration

Always start a root `CMakeLists.txt` with an explicit minimum version and a project declaration that includes languages.

```cmake
cmake_minimum_required(VERSION 3.21)
project(luma
    VERSION 1.0.0
    LANGUAGES CXX
)
```

**Why 3.21+:** it is widely available, supports presets, and avoids legacy pitfalls. Raise the minimum only when a newer feature is genuinely needed. Never omit `cmake_minimum_required` — it controls policy behaviour.

---

## 2 — Formatting and Whitespace

Consistent formatting makes `CMakeLists.txt` files as scannable as any other source code.

### Indentation

Use 4 spaces per indentation level. Do not use tabs. Indent the body of `if()` / `else()` / `endif()`, `foreach()` / `endforeach()`, `function()` / `endfunction()`, and `macro()` / `endmacro()` blocks.

```cmake
if(LUMA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

### Argument Lists

When a command has more than two or three arguments, place each argument on its own line, indented one level from the command. This keeps diffs clean and lines short.

```cmake
target_link_libraries(my_app
    PRIVATE
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
)
```

### Blank Lines

Use one blank line to separate logical sections — between the `project()` declaration and options, between `add_executable` / `add_library` and the subsequent `target_*` calls, and between unrelated target definitions. Do not use multiple consecutive blank lines.

### Line Length

Aim for a maximum of 100 characters per line. Break long generator expressions or paths at logical boundaries.

### Comments

Start comments with `#` followed by a single space. Use comments to explain **why**, not **what**. Use comment headers to separate major sections in longer files.

```cmake
# ── Dependencies ──

find_package(Threads REQUIRED)

# ── Main executable ──

add_executable(my_app
    src/main.cpp
)
```

---

## 3 — Naming Conventions

| Entity                    | Convention                                | Examples                                      |
| ------------------------- | ----------------------------------------- | --------------------------------------------- |
| Cache variables / options | `UPPER_SNAKE_CASE`, project-prefixed      | `LUMA_BUILD_TESTS`, `LUMA_FEATURE_SANITIZERS` |
| Functions / macros        | `snake_case`                              | `set_project_warnings`, `add_luma_test`       |
| Imported targets          | `PascalCase::Component` (follow upstream) | `Threads::Threads`, `GTest::gtest`            |
| Local variables           | `snake_case`                              | `source_files`, `test_name`                   |
| Targets                   | `snake_case`                              | `my_app`, `luma`, `lexer_test`                |

- Choose descriptive names. `luma_test` — good. `t1` — bad.
- Prefix all cache variables and options with the project name to avoid collisions in super-builds.
- Use the conventions of upstream packages for their imported targets — do not rename them.

---

## 4 — Golden Rules

1. **Targets, not variables.** Never set global `CMAKE_CXX_FLAGS`, `INCLUDE_DIRECTORIES`, or `LINK_LIBRARIES`. Attach properties to targets instead.
2. **`PRIVATE` / `PUBLIC` / `INTERFACE` always.** Every `target_*` call must specify a visibility keyword.
3. **No `file(GLOB)` for sources.** List source files explicitly so that CMake re-runs when the list changes.
4. **Out-of-source builds only.** Never generate build files inside the source tree.
5. **Avoid `add_definitions`, `include_directories`, `link_directories`.** These are directory-scoped legacy commands — use the target-scoped equivalents.

---

## 5 — Creating Targets

### 5.1 Executables

```cmake
add_executable(my_app
    src/main.cpp
    src/app.cpp
)

target_include_directories(my_app PRIVATE src)

target_compile_features(my_app PRIVATE cxx_std_20)
```

### 5.2 Libraries

Default to `STATIC` unless the project explicitly needs a shared library.

```cmake
add_library(my_lib STATIC
    src/my_lib.cpp
    src/utils.cpp
)

target_include_directories(my_lib
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE src
)

target_compile_features(my_lib PUBLIC cxx_std_20)
```

The `BUILD_INTERFACE` / `INSTALL_INTERFACE` generator expressions keep include paths correct both during the build and after installation.

### 5.3 Header-Only Libraries

```cmake
add_library(my_header_lib INTERFACE)

target_include_directories(my_header_lib
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
              $<INSTALL_INTERFACE:include>
)

target_compile_features(my_header_lib INTERFACE cxx_std_20)
```

---

## 6 — Compiler Warnings & Safety Flags

Define a reusable function and call it on every target. Do not apply warnings globally.

```cmake
function(set_project_warnings target_name)
    target_compile_options(${target_name} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:
            -Wall -Wextra -Wpedantic
            -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Wold-style-cast
            -Woverloaded-virtual -Wnull-dereference
        >
    )
endfunction()
```

Usage:

```cmake
set_project_warnings(my_app)
set_project_warnings(my_lib)
```

---

## 7 — Finding & Linking Dependencies

### 7.1 System / Pre-installed Packages

Use `find_package` with `REQUIRED` so configuration fails immediately if the dependency is missing.

```cmake
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

target_link_libraries(my_app
    PRIVATE
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
)
```

Always use the namespaced imported target (`Library::Component`) — never raw variable names like `${OPENSSL_LIBRARIES}`.

> Luma's own third-party dependencies are vendored, not found on the system (see §7.2). The
> example above illustrates the generic `find_package` pattern; in this project reserve it for
> packages genuinely provided by the system or toolchain, such as `Threads`.

### 7.2 Vendored Dependencies (No Configure-Time Downloads)

Luma keeps every third-party dependency **vendored** under `external/` and builds it
from source as part of the normal CMake configure. Dependencies are never downloaded
at configure or build time, so the project always builds offline and reproducibly. See
`cpp.instructions.md` §7.1 for the policy and the current list of vendored libraries
(miniz, Mbed TLS, webview).

Wrap each vendored library in a static target that points at the sources under
`external/`, then link it with a namespaced alias:

```cmake
# cmake/LumaMbedTLS.cmake — vendored Mbed TLS, built as a static target.
set(MBEDTLS_DIR ${PROJECT_SOURCE_DIR}/external/mbedtls/library)

add_library(mbedtls_lib STATIC
    ${MBEDTLS_DIR}/aes.c
    ${MBEDTLS_DIR}/sha256.c
    # ... remaining sources listed explicitly (no file(GLOB))
)

target_include_directories(mbedtls_lib
    PUBLIC ${PROJECT_SOURCE_DIR}/external/mbedtls/include
)

target_link_libraries(luma_core PUBLIC mbedtls_lib)
```

Guidelines for vendored dependencies:

- **List sources explicitly** — never `file(GLOB)` a vendored tree.
- **Record provenance** — note the upstream release tag and vendoring date in the
  module that builds the library (see `cmake/LumaMbedTLS.cmake`).
- **Guard optional dependencies behind an option** (for example `LUMA_FEATURE_TLS`) so
  builds that do not need them stay lean.
- Do **not** use `FetchContent` or `ExternalProject_Add`; configure-time downloads break
  hermetic, offline builds and are intentionally avoided here.

---

## 8 — Project Options

Expose every user-facing knob with `option()` or `set(... CACHE ...)`, grouped near the top of the root `CMakeLists.txt`.

```cmake
option(LUMA_BUILD_TESTS "Build and register unit tests"     ON)
option(LUMA_BUILD_FUZZ  "Build LibFuzzer fuzz targets"      OFF)
option(LUMA_FEATURE_TLS "Enable HTTPS support via Mbed TLS" ON)
```

Prefix every option with the project name to avoid collisions in super-builds.

---

## 9 — Subdirectory Structure

A clean multi-directory layout keeps concerns separate.

```text
project/
├── CMakeLists.txt     # root: project(), options, add_subdirectory()
├── CMakePresets.json  # build presets
│
├── cmake/
│   └── ...            # custom Find modules or helper scripts
│
├── include/
│   └── luma/
│       └── ...
│
├── src/
│   ├── CMakeLists.txt # library and/or executable targets
│   └── ...
│
└── tests/
    ├── CMakeLists.txt # test targets
    └── ...
```

Root file delegates immediately:

```cmake
add_subdirectory(src)

if(LUMA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

---

## 10 — Testing with CTest

Each test executable has its own `main()` and is registered as a single CTest case with `add_test`. This avoids external test-framework dependencies.

```cmake
# Basic pattern — one executable per test file.
add_executable(lexer_test
    tests/analysis/lexer_test.cpp
)

target_link_libraries(lexer_test PRIVATE my_lib)

add_test(NAME lexer_test COMMAND lexer_test)
```

When the project has many test executables, reduce boilerplate with a helper function:

```cmake
function(my_add_test)
    cmake_parse_arguments(ARG "" "NAME;WORKING_DIR" "SOURCES;LIBS" ${ARGN})

    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE ${ARG_LIBS})

    if(ARG_WORKING_DIR)
        add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME}
            WORKING_DIRECTORY "${ARG_WORKING_DIR}")
    else()
        add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME})
    endif()
endfunction()

# Usage:
my_add_test(NAME lexer_test
    SOURCES tests/analysis/lexer_test.cpp
    LIBS    my_lib)
```

---

## 11 — Installing & Exporting

Provide an install step so downstream projects can `find_package(Luma)`.

```cmake
include(GNUInstallDirs)

install(TARGETS luma
    EXPORT LumaTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT LumaTargets
    FILE        LumaTargets.cmake
    NAMESPACE   Luma::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Luma
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    cmake/LumaConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/LumaConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Luma
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/LumaConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/LumaConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/LumaConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Luma
)
```

---

## 12 — Presets

Provide a `CMakePresets.json` so that common configurations are one command away.

```json
{
    "version": 3,
    "cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
    "configurePresets": [
        {
            "name": "dev",
            "displayName": "Development",
            "binaryDir": "${sourceDir}/build/dev",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
                "LUMA_BUILD_TESTS": "ON"
            }
        },
        {
            "name": "release",
            "displayName": "Release",
            "binaryDir": "${sourceDir}/build/release",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release"
            }
        }
    ],
    "buildPresets": [
        { "name": "dev", "configurePreset": "dev" },
        { "name": "release", "configurePreset": "release" }
    ]
}
```

Usage:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

---

## 13 — Sanitizers

Guard sanitizers behind an option and apply them per-target or as a project-wide preset.

```cmake
if(LUMA_FEATURE_SANITIZERS)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
```

Prefer a dedicated preset for sanitizer builds rather than manually passing flags.

---

## 14 — Code Coverage

Guard coverage instrumentation behind an option. Use `--coverage` (GCC) or equivalent flags and generate reports with `lcov` or `llvm-cov`.

```cmake
option(LUMA_FEATURE_COVERAGE "Enable code coverage instrumentation" OFF)

if(LUMA_FEATURE_COVERAGE)
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
endif()
```

---

## 15 — Anti-Patterns

| Anti-Pattern                                           | Correct Alternative                             |
| ------------------------------------------------------ | ----------------------------------------------- |
| `add_definitions(-DFOO)`                               | `target_compile_definitions(t PRIVATE FOO)`     |
| `file(GLOB SOURCES "src/*.cpp")`                       | List source files explicitly                    |
| `include_directories(...)`                             | `target_include_directories(t ...)`             |
| `link_libraries(...)`                                  | `target_link_libraries(t ...)`                  |
| `set(CMAKE_CXX_STANDARD 20)` at global scope           | `target_compile_features(t PUBLIC cxx_std_20)`  |
| Bare library names in `target_link_libraries`          | Namespaced imported targets (`Lib::Lib`)        |
| Building inside the source tree                        | Use a `build/` directory or presets             |
| Checking `CMAKE_BUILD_TYPE` in multi-config generators | Use generator expressions (`$<CONFIG:Release>`) |

---

## 16 — Quick Reference: Build Commands

```bash
# Configure (single-config generator like Makefiles/Ninja)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Run tests
ctest --test-dir build --output-on-failure

# Install
cmake --install build --prefix /usr/local
```

---

## 17 — Checklist Before Committing a CMakeLists.txt

- [ ] `cmake_minimum_required` is present and set to the lowest version that works.
- [ ] Every target uses `target_*` commands with explicit visibility keywords.
- [ ] No `file(GLOB)` for source files.
- [ ] All external dependencies use namespaced imported targets.
- [ ] Compiler warnings are enabled on all project targets.
- [ ] Options are prefixed with the project name.
- [ ] The project configures and builds cleanly from a fresh build directory.
- [ ] `CMAKE_EXPORT_COMPILE_COMMANDS` is enabled in development presets (for IDE and tooling support).
- [ ] Indentation uses 4 spaces consistently. Blank lines separate logical sections.
- [ ] Names follow the conventions: `snake_case` targets/functions, `UPPER_SNAKE_CASE` cache variables/options.
