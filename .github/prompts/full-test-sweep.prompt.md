---
description: "Run the complete test suite including unit tests, fuzz tests, benchmarks, and example validation"
agent: "agent"
version: 1
lastUpdated: "2026-08-01"
---

# Full Test Sweep

Run every category of test in the project and fix any failures. Start by building the project in Release mode.

> **Scope:** This runs every test category — C++ unit tests, Luma feature tests
> (strict), fuzz smoke tests, benchmarks, and examples. For a faster inner-loop
> check, use [build-and-test.prompt.md](build-and-test.prompt.md). For a
> clean-room release check that also rebuilds the editor extensions, use
> [release-verification.prompt.md](release-verification.prompt.md).

## 1. Build

```bash
cmake --preset default
cmake --build --preset default
```

If there are build errors or warnings, fix them before proceeding.

## 2. C++ Unit Tests

Run all CTest targets:

```bash
ctest --preset default
```

Fix any test failures. Do not disable or skip tests — fix the underlying issue.

## 3. Luma Feature Tests

Run all Luma test suites in `tests/features/language/` and `tests/features/stdlib/` in strict mode:

```bash
python scripts/run_luma_tests.py
```

Fix any failures or warnings. The runner invokes the interpreter with `--strict` for each test, so all warnings are treated as errors.

## 4. Fuzz Tests

1. Build the fuzz targets (requires Clang — LibFuzzer is bundled with it):

    ```bash
    cmake -B build-fuzz -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address" \
      -DLUMA_BUILD_FUZZ=ON
    cmake --build build-fuzz --parallel
    ```

2. Smoke-test every fuzz target (each runs for `LUMA_FUZZ_QUICK_TIME`, default 10s, against its seed corpus):

    ```bash
    ctest --test-dir build-fuzz -R _quick --output-on-failure
    ```

3. Fix any immediate crashes or assertion failures found during the fuzz run.

## 5. Benchmarks

Run the benchmark suite to verify all benchmarks execute without errors:

```bash
build/Release/luma benchmarks/suite.luma
```

Fix any runtime errors. Do not optimise for performance — just ensure correctness.

## 6. Examples

Build (parse and type-check) all example programs in `examples/` in strict mode to verify they are valid:

On Linux/macOS (bash):

```bash
find examples -name '*.luma' -print0 | xargs -0 -n1 build/Release/luma --check --strict
```

On Windows (PowerShell):

```powershell
Get-ChildItem -Path examples -Filter *.luma | ForEach-Object { build/Release/luma.exe --check --strict $_.FullName }
```

Fix any type errors, syntax errors, or references to deprecated/removed API.

## 7. Report

Summarise the results:

- Total tests run per category.
- Any failures that were fixed (what and how).
- Any remaining issues that need manual attention.
