---
description: "Build the Luma interpreter from source and run the test suite"
agent: "agent"
---

# Build and Test

Build the Luma interpreter and run all tests. See [build.instructions.md](../../instructions/build.instructions.md) for the full build reference (presets, sanitizers, coverage, fuzzing).

> **Scope:** This is the quick inner-loop check — build plus C++ unit tests and
> Luma feature tests. For a complete run that also covers fuzz targets,
> benchmarks, and examples, use [full-test-sweep.prompt.md](full-test-sweep.prompt.md).
> For a clean-room rebuild of all binaries and editor extensions before a
> release, use [release-verification.prompt.md](release-verification.prompt.md).

1. Configure and build (Release) with CMake presets:
    - `cmake --preset default`
    - `cmake --build --preset default`
2. Run the C++ unit tests: `ctest --preset default`
3. Run the Luma feature tests: `python scripts/run_luma_tests.py`
4. Report any failures with details.
