# Luma — Test Suite Organization

This document maps out where the Luma project's tests live, how they are grouped, and how to run them. For conventions on *writing* tests — the framework, assertion macros, fixtures, and feature-test layout — see [testing.instructions.md](../instructions/testing.instructions.md), which is the authoritative guide.

## Table of Contents

1. [Test Categories](#1-test-categories)
2. [C++ Tests](#2-c-tests)
3. [Luma Feature Tests](#3-luma-feature-tests)
4. [Running Tests](#4-running-tests)
5. [Adding New Tests](#5-adding-new-tests)
6. [Further Reading](#6-further-reading)

## 1. Test Categories

Luma has two testing layers: **C++ tests** that exercise interpreter internals, and **Luma feature tests** written in the language itself. They are organised as follows:

| Location | Layer | Covers |
| --- | --- | --- |
| `tests/analysis/` | C++ | Front-end: lexer, parser, type checker, resolver, linter, diagnostics. |
| `tests/runtime/` | C++ | Back-end: compiler, VM, stdlib modules, concurrency, CLI, REPL. |
| `tests/integration/` | C++ | Full pipeline, from source to execution. |
| `tests/platform/` | C++ | Platform-specific behaviour (`safe_getenv`, Win32 UTF-8). |
| `tests/features/language/` | Luma | Core language features (control flow, types, closures, generics). |
| `tests/features/stdlib/` | Luma | Standard library modules (`*_functions.luma`). |

Two further C++ suites live next to the components they test:

- `language-server/tests/` — LSP language server (completion, navigation, features, protocol). Label: `lsp`.
- `debugger/tests/` — DAP debugger (breakpoints, stepping, variables, expression evaluation), including an end-to-end `dap_integration_test`. Label: `dap`.

All of these are registered with CTest from `tests/CMakeLists.txt`.

## 2. C++ Tests

- **Purpose:** test individual C++ classes and functions (`analysis`, `runtime`, `platform`) and verify that the whole pipeline works together (`integration`).
- **Framework:** a lightweight custom framework in `tests/test_framework.hpp` (no GTest or Catch2). Tests register functions with `RUN(...)` and return `SUMMARY()` from `main()`.
- **Shared helpers:** each suite includes the specific helper header its layer needs. The core framework (`test_framework.hpp`) provides `RUN`, the `ASSERT_*` macros, and `SUMMARY`; the layer helpers add the rest — `shared_eval.hpp` for full-pipeline evaluators (`eval()`, `eval_checked()`, `ASSERT_EVAL_INT`, …), `lex_parse_util.hpp` for a front-end-only lex→parse, `analysis/test_parse_helper.hpp` for parser tests, `analysis/type_checker_test_helpers.hpp` for type-checker tests (`check()`, `check_warnings()`, `fails()`), `runtime/compiler_test_helpers.hpp` for compiler opcode inspection, and `runtime/stdlib_test_helpers.hpp` for end-to-end stdlib tests.
- **Naming:** one executable per file, named `<component>_test.cpp`. Exceptions: stdlib tests use `stdlib_test_<module>.cpp`, type-checker tests use `type_checker_test_<aspect>.cpp`, and the LSP and DAP suites use `lsp_test_<area>.cpp` / `dap_test_<area>.cpp`.
- **Running:** via CTest — see [§4](#4-running-tests).

## 3. Luma Feature Tests

- **Purpose:** test the language from a user's perspective. These tests are written in Luma, use the `@test` annotation with `assert()`, and act as a living specification of the language's behaviour.
- **Location:** `tests/features/`, split into `language/` and `stdlib/`. Each `.luma` file is one self-contained suite.
- **Execution:** every suite runs through the `luma` interpreter with `--strict --test`. The `sandbox.luma` suite additionally runs under `--box`, so its "operation blocked" assertions exercise the real restricted environment.
- **CTest registration:** each suite is registered as `luma_<name>` with a 30-second timeout (configurable via the `LUMA_FEATURE_TEST_TIMEOUT` cache variable) and the labels `feature` plus `feature_language` or `feature_stdlib`.

## 4. Running Tests

Configure, build, and test with the CMake presets:

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

`ctest --preset default` runs the entire suite — C++ unit, integration, platform, LSP, DAP, and Luma feature tests — with `--output-on-failure` already enabled by the preset.

> **Note:** Without presets, run the equivalent from the build directory: `cd build && ctest -C Release --output-on-failure`.

### Running a Subset by Label

CTest labels allow targeted runs. The `-L` flag takes a regular expression:

```bash
ctest --preset default -L feature_stdlib   # only stdlib feature tests
ctest --preset default -L analysis         # all front-end unit tests
ctest --preset default -L vm               # just the VM tests
ctest --preset default -L "lsp|dap"        # language server and debugger
```

| Label | Scope |
| --- | --- |
| `analysis` | Front-end unit tests; sub-labels `lexer`, `parser`, `types`, `resolver`, `linter`. |
| `runtime` | Back-end unit tests; sub-labels `stdlib`, `compiler`, `vm`, `concurrency`. |
| `platform` | Platform-specific tests. |
| `integration` | Full-pipeline and DAP end-to-end tests. |
| `lsp` | Language server tests. |
| `dap` | Debugger tests. |
| `feature` | All Luma feature suites; subsets `feature_language` and `feature_stdlib`. |

You can also select tests by name with `-R`, for example `ctest --preset default -R luma_arrays`.

### Running a Single Feature Suite Directly

```bash
build/Release/luma --strict --test tests/features/language/arrays.luma
```

On Windows the executable is `build\Release\luma.exe`.

### Running All Feature Suites Without CMake

A cross-platform Python script mirrors the CTest invocation and is handy while iterating:

```bash
python scripts/run_luma_tests.py
```

It discovers every `.luma` file under `tests/features/`, runs each with `--strict --test` (adding `--box` for `sandbox.luma`), and prints a pass/fail summary. Override the executable or directory with `--exe` / `--dir`, or the `LUMA_EXE` / `LUMA_TESTS_DIR` environment variables.

## 5. Adding New Tests

### New C++ Test

1. Create `<component>_test.cpp` in `tests/analysis/`, `tests/runtime/`, `tests/integration/`, or `tests/platform/`, following the structure in [testing.instructions.md](../instructions/testing.instructions.md).
2. Register it in `tests/CMakeLists.txt`: add the name to the relevant list (`LUMA_ANALYSIS_TESTS`, `LUMA_RUNTIME_TESTS`, …), or call the `luma_add_test()` helper directly for special cases such as a custom working directory, library, or timeout.
3. Labels are derived automatically from the filename prefix via the label maps; add a prefix entry there if you introduce a new component.
4. Build and run: `cmake --build --preset default && ctest --preset default`.

### New Luma Feature Test

1. Add `@test` functions to an existing `.luma` file in `tests/features/language/` or `tests/features/stdlib/`, or create a new file when introducing a new topic.
2. Verify behaviour with `assert(condition)` or `assert(condition, "message")`; do not add a `@main` function.
3. Register the file in `tests/CMakeLists.txt` by adding its name to `LUMA_FEATURE_TESTS_LANGUAGE` or `LUMA_FEATURE_TESTS_STDLIB`; it is then run with `--strict --test` on the next configure. (Files are listed explicitly rather than glob-discovered so the Visual Studio generator does not trigger racing mid-build reconfigures.)

## 6. Further Reading

- [testing.instructions.md](../instructions/testing.instructions.md) — authoritative guide to the framework, assertion macros, fixtures, sandbox and resource-limit testing, and fuzzing.
- [Luma_Software_Architecture.md](../documents/Luma_Software_Architecture.md) — the compilation pipeline these tests exercise.
- `fuzz/` — libFuzzer targets for the front-end and back-end (see the fuzzing section of the testing guide).
