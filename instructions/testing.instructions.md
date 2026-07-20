---
description: "Use when writing, reviewing, or modifying C++ or Luma test files. Covers the custom test framework, assertion macros, helper functions, Luma feature tests, and test conventions."
applyTo: "tests/**"
---

# Working with Tests

Guidelines for writing, reviewing, and maintaining tests for the Luma interpreter. This project has two testing layers: **C++ unit tests** (testing interpreter internals) and **Luma feature tests** (testing language behaviour end-to-end). Both layers must be kept in sync with any changes to the interpreter or standard library.

---

## Table of Contents

1. [Test Philosophy](#1--test-philosophy)
2. [C++ Unit Tests](#2--c-unit-tests)
3. [Luma Feature Tests](#3--luma-feature-tests)
4. [Test Naming](#4--test-naming)
5. [Running Tests](#5--running-tests)
6. [Security and Sandbox Testing](#6--security-and-sandbox-testing)
7. [Fuzz Testing](#7--fuzz-testing)
8. [Checklist](#8--checklist)

---

## 1 — Test Philosophy

1. **Every observable behaviour has a test.** If a user can trigger it from Luma source code, a Luma feature test should cover it. If it is an internal API, a C++ unit test should cover it.
2. **Tests are documentation.** A reader should be able to understand what a feature does by reading its tests.
3. **One assertion per logical concept.** A test function may contain multiple assertions, but they should all verify the same logical behaviour.
4. **Fast and deterministic.** Tests must not depend on wall-clock time, network access, or random state. Tests that exercise `Random` or `DateTime` should use controlled inputs.

---

## 2 — C++ Unit Tests

C++ unit tests live in `tests/analysis/` and `tests/runtime/` and are registered with CTest via `CMakeLists.txt`. Each test file is a standalone executable with its own `main()`.

### Test Framework

The project uses a lightweight custom framework in `tests/test_framework.hpp` (namespace `luma::test`) — no external dependencies (no GTest, Catch2, etc.). Every test file includes `test_framework.hpp` and uses these macros:

```cpp
#include "test_framework.hpp"

void test_something() {
    ASSERT_EQ(1, 1);
}

int main() {
    RUN(test_something);
    return SUMMARY();
}
```

Available assertion macros:

- `ASSERT_EQ(a, b)` — fails if `a != b`.
- `ASSERT_NE(a, b)` — fails if `a == b`.
- `ASSERT_TRUE(cond)` — fails if `cond` is false.
- `ASSERT_FALSE(cond)` — fails if `cond` is true.
- `ASSERT_THROWS(expr)` — fails if `expr` does not throw.
- `ASSERT_LT(a, b)` — fails if `a >= b`.
- `ASSERT_LE(a, b)` — fails if `a > b`.
- `ASSERT_GT(a, b)` — fails if `a <= b`.
- `ASSERT_GE(a, b)` — fails if `a < b`.
- `ASSERT_NEAR(a, b, epsilon)` — fails if `|a - b| > epsilon`.
- `ASSERT_SNAPSHOT(name, actual, __FILE__)` — compares against `.expected` file in `snapshots/` directory.

Stdlib test convenience macros (from `shared_eval.hpp`):

- `ASSERT_EVAL_INT(code, expected)` — evaluates Luma expression `code` and asserts the result is integer `expected`.
- `ASSERT_EVAL_STR(code, expected)` — evaluates and asserts the result is string `expected`.
- `ASSERT_EVAL_BOOL(code, expected)` — evaluates and asserts the result is boolean `expected`.
- `ASSERT_EVAL_NUM(code, expected)` — evaluates and asserts the result is number (double) near `expected`.
- `ASSERT_EVAL_FAILURE(code)` — evaluates and asserts the result is an error.
- `ASSERT_RESULT_SUCCESS(value)` — asserts `value` is a `result` in the success state (checks `is_result()` first).
- `ASSERT_RESULT_FAILURE(value)` — asserts `value` is a `result` in the failure state (checks `is_result()` first).

Prefer `ASSERT_EVAL_*` for the common single-scalar shape (evaluate → assert success → compare one value) and `ASSERT_RESULT_SUCCESS`/`ASSERT_RESULT_FAILURE` over hand-written `ASSERT_TRUE(v.as_result()->is_success)`, which skips the `is_result()` guard.

All throw `std::runtime_error` on failure, which `run_test` catches and reports. Enum values are auto-formatted to their underlying integer type for assertion messages.

### Temporary Files and Directories

For tests that touch the real filesystem, `test_framework.hpp` provides RAII helpers that clean up on destruction — even if an assertion throws — and use unique names so parallel `ctest -j` runs never collide:

- `TempFile{content}` — writes a uniquely named `.luma` file in the system temp directory.
- `TempFile{path, content}` — writes `content` to the given path, creating parent directories as needed.
- `TempDir{}` — creates a fresh, unique temporary directory and removes it recursively.

Both expose `.path()` (a `std::filesystem::path`) and `.path_string()`. Never hand-roll a fixed-name temporary file or directory: reusing a shared name races under parallel test execution and can leak artifacts into the working tree.

### Test Fixtures

Derive from `TestFixture` and override `set_up()` / `tear_down()`:

```cpp
class MyFixture : public TestFixture {
public:
    int value{};
    void set_up() override { value = 42; }
};

TEST_F(MyFixture, test_uses_value) { ASSERT_EQ(fixture.value, 42); }

int main() { RUN(test_uses_value); return SUMMARY(); }
```

### Benchmarks

Use the `BENCHMARK` macro for simple micro-benchmarks:

```cpp
BENCHMARK(my_benchmark, 10000) {
    // code to benchmark
}
RUN(my_benchmark); // in main — prints ns/iter after warmup
```

### Test File Structure

Follow this layout for every C++ test file:

1. **File comment** — single line describing the scope (e.g., `// Lexer unit tests.`).
2. **Includes** — standard library first, then project headers, then `test_framework.hpp`.
3. **`using namespace luma;`** — permitted in test files only.
4. **Helper functions** — pipeline shortcuts like `lex()`, `parse()`, `eval()`, `run()`.
5. **Test functions** — one `static void` function per test, named `test_<feature>`.
6. **`main()`** — registers tests with `RUN(test_name)`, returns `SUMMARY()`. Alternatively, use `LUMA_TEST(name)` for auto-registration and `LUMA_RUN_ALL()` in `main()`.

### Auto-Registration (Alternative)

Use `LUMA_TEST` to avoid listing every test in `main()`:

```cpp
#include "test_framework.hpp"

LUMA_TEST(something) {
    ASSERT_EQ(1, 1);
}

int main() {
    LUMA_RUN_ALL();
}
```

Both approaches (`RUN`/`SUMMARY` and `LUMA_TEST`/`LUMA_RUN_ALL`) are supported. Existing tests use the manual `RUN()` approach; new tests may use either.

### Helper Functions

Each test file defines helpers that set up the interpreter pipeline for the layer under test:

| Test file                                 | Helper                        | Purpose                                                                                        |
| ----------------------------------------- | ----------------------------- | ---------------------------------------------------------------------------------------------- |
| `cli_test.cpp`                            | _(direct)_                    | Tests exit codes, `levenshtein()`, and `suggest_flag()` directly.                              |
| `concurrency_test.cpp`                    | _(direct)_                    | Tests Channel, Task, TaskScope, CancellationToken, and ThreadPool APIs directly (no pipeline). |
| `include_resolver_test.cpp`               | `count_declarations(prog, k)` | Counts declarations of a specific kind in a `Program`.                                         |
| `include_resolver_test.cpp`               | `load_and_resolve(path, sm)`  | Lexes, parses, and resolves includes from a file, returns `Program`.                           |
| `lexer_test.cpp`                          | `lex(src)`                    | Tokenises `src`, returns `std::vector<Token>`.                                                 |
| `parser_test.cpp`                         | `parse(src)`                  | Lexes and parses `src`, returns `Program`.                                                     |
| `repl_test.cpp`                           | _(direct)_                    | Tests `compute_brace_depth_delta` directly (no pipeline).                                      |
| `stdlib_test_helpers.hpp` consumers       | `eval(src)`                   | Evaluates expressions that call stdlib functions, returns `Value`.                             |
| `vm_test.cpp`                             | `eval(src)`                   | Runs the full pipeline for expression-level code via the VM, returns `Value`.                  |
| `type_checker_test_helpers.hpp` consumers | `check_warnings(src)`         | Lexes, parses, and type-checks `src`, returns `std::vector<Diagnostic>`.                       |
| `type_checker_test_helpers.hpp` consumers | `check(src)`                  | Lexes, parses, and type-checks `src`, returns `std::vector<Diagnostic>`.                       |
| `type_checker_test_helpers.hpp` consumers | `fails(src)`                  | Returns `true` if `src` produces type errors.                                                  |
| `type_checker_test_helpers.hpp` consumers | `has_warnings(src)`           | Returns `true` if `src` produces warnings.                                                     |
| `type_checker_test_helpers.hpp` consumers | `passes(src)`                 | Returns `true` if `src` produces no type errors.                                               |

**When to use which:**

- `eval()` — for testing expressions or statements that produce a value (arithmetic, string ops, variable declarations). The result is the value of the last statement. Available in the `tests/runtime/stdlib_test_*.cpp` files via `stdlib_test_helpers.hpp`, and in `vm_test.cpp`.
- Direct API calls — for testing internal state (e.g., verifying that specific functions are registered in the environment).

### Adding a New C++ Test

1. Write a `static void test_<feature>()` function in the appropriate test file.
2. Add a `RUN(test_<feature>)` call in `main()`.
3. Build and run: `cmake --build --preset default && ctest --preset default`.

### Adding a New Test Executable

1. Create a test file in the appropriate directory (`tests/analysis/`, `tests/runtime/`, or `tests/integration/`) following the standard structure.
2. Add `add_executable`, `target_link_libraries`, and `add_test` to `CMakeLists.txt`.
3. List source files explicitly — no `file(GLOB)`.

### Testing Error Conditions

Use `ASSERT_THROWS` for verifying that code throws:

```cpp
static void test_division_by_zero() {
    ASSERT_THROWS(eval("1 / 0"));
}
```

For more specific exception type checking:

```cpp
static void test_division_by_zero() {
    bool threw = false;

    try {
        eval("1 / 0");
    } catch (const RuntimeError&) {
        threw = true;
    }

    ASSERT_TRUE(threw);
}
```

---

## 3 — Luma Feature Tests

Luma feature tests live in `tests/features/`, organised into `language/` (core language features) and `stdlib/` (standard library modules), and are run with the `--test` flag. Each file is a self-contained test suite for a specific language feature or standard library module.

### Structure

Every Luma test file follows this layout:

```luma
# ═══════════════════════════════════════════════════════════
# Luma v1.0 — Feature Tests — <Topic>
#
# Feature Tests are based on Luma_Initial_Concept.md and
# Luma_User_Manual.md.
# ═══════════════════════════════════════════════════════════

@test
function void test_<feature_a>() {
    # Arrange

    # Act

    # Assert
    assert(<condition>)
    assert(<condition>, "<message>")
}

@test
function void test_<feature_b>() {
    # ...
}
```

### Conventions

- **Annotation:** every test function must be annotated with `@test`.
- **Naming:** `test_<feature_under_test>` in `snake_case`.
- **Assertions:** use `assert(condition)` or `assert(condition, "message")`. The message should describe what was expected.
- **No `@main`:** test files are run with `--test`, not as regular programs. Do not include a `@main` function.
- **One file per topic:** group related tests in a single file (e.g., `arrays.luma`, `string_functions.luma`, `control_flow.luma`).
- **Self-contained:** each test function should set up its own state. Do not rely on execution order or shared mutable state between tests.
- **Header references:** `language/` tests cite `Luma_User_Manual.md`; `stdlib/` tests cite `Luma_Standard_Library_Reference.md` (both alongside `Luma_Initial_Concept.md`).

### Running Luma Tests

```bash
# Run a single test suite
build/Release/luma --test tests/features/language/arrays.luma
```

```powershell
# Run all test suites (PowerShell)
foreach ($f in Get-ChildItem tests\features\*\*.luma) {
    Write-Host "── $($f.Name) ──"
    & build/Release/luma.exe --test $f.FullName
}
```

### Adding a New Luma Test

1. Open the appropriate file in `tests/features/language/` or `tests/features/stdlib/` (or create a new one if the topic is not yet covered).
2. Write a `@test` annotated function with descriptive assertions.
3. Run with `--test` to verify.

---

## 4 — Test Naming

Use descriptive names that state what is being tested, not how:

```text
# Good
test_array_safe_access
test_division_by_zero_throws
test_mutable_assignment
test_string_upper_lower

# Bad
test_bug_fix_123
test_it_works
test1
```

---

## 5 — Running Tests

| Command                                                         | What it runs                      |
| --------------------------------------------------------------- | --------------------------------- |
| `build/Release/luma --test tests/features/language/<file>.luma` | A single Luma feature test suite. |
| `ctest --preset default`                                        | All C++ unit tests via CTest.     |

Always run both C++ and Luma tests before submitting changes. CTest must report zero failures, and all Luma test suites must pass.

---

## 6 — Security and Sandbox Testing

### Sandbox Tests

The interpreter supports `--box` (sandbox) mode which disables OS-accessing modules. Sandbox behaviour is tested at two levels:

- **C++ tests** (`tests/runtime/stdlib_test_*.cpp`) — verify that sandbox-blocked functions are not registered, that sandbox-specific error messages are produced for blocked modules, and that file-I/O functions in otherwise-safe modules (`Log.set_output`, `Compression.gzip_file`, etc.) are correctly gated.
- **Luma tests** (`tests/features/language/sandbox.luma`) — verify that safe modules (`String`, `Array`, `Math`, `Hash`, `Compression`, `Log`, `Json`, `Converter`, `Result`) remain fully functional when run with `--box --test`.

When adding or modifying a standard library module, update the sandbox tests:

1. If the module is entirely OS-dependent, add an assertion to `test_sandbox_disables_dangerous_modules`.
2. If only some functions perform file I/O, add assertions to `test_sandbox_gates_file_io_in_safe_modules` and `test_sandbox_non_sandbox_has_file_io`.
3. Add a corresponding `@test` function to `tests/features/language/sandbox.luma` to verify the safe functions work in sandbox mode.

### Resource Limit Tests

Resource limits are defined in `resource_limits.hpp` and enforced at runtime. When adding a new limit:

1. Add the constant to `ResourceLimits`.
2. Add a C++ test that triggers the limit and verifies the expected error.
3. Document the limit in the User Manual and Coding Guidelines.

The `test_thread_pool_queue_limit` test in `tests/runtime/stdlib_test_core.cpp` demonstrates how to test a resource limit by blocking a worker thread and flooding the queue.

---

## 7 — Fuzz Testing

Fuzz tests live in `fuzz/` and use LLVM's libFuzzer to find crashes, hangs, and undefined behaviour in the interpreter's front-end and back-end.

### Fuzz Targets

| Target                       | Tests                                        |
| ---------------------------- | -------------------------------------------- |
| `fuzz_lexer`                 | Tokenisation of arbitrary input              |
| `fuzz_parser`                | AST construction from token stream           |
| `fuzz_resolver`              | Name resolution and slot assignment          |
| `fuzz_type_checker`          | Static type checking                         |
| `fuzz_linter`                | Post-type-check lint pass                    |
| `fuzz_compiler`              | Bytecode compilation from valid ASTs         |
| `fuzz_optimizer`             | Bytecode optimisation pass                   |
| `fuzz_include_resolver`      | Include path resolution                      |
| `fuzz_vm`                    | VM execution of compiled bytecode            |
| `fuzz_structured`            | Records, choices, and pattern matching       |
| `fuzz_bytecode_deserializer` | Bytecode decode with round-trip oracle       |
| `fuzz_json`                  | JSON-RPC body parse with re-serialise oracle |
| `fuzz_json_stdlib`           | `Json.deserialize` with re-serialise oracle  |
| `fuzz_csv`                   | RFC 4180 CSV parse with round-trip oracle    |
| `fuzz_xml`                   | `Xml.deserialize` with round-trip oracle     |
| `fuzz_datetime`              | ISO-8601 parse with round-trip oracle        |
| `fuzz_protocol`              | Content-Length message framing               |
| `fuzz_compression`           | deflate/gzip/RLE decode round-trip oracle    |
| `fuzz_encoder`               | Base64/URL decode round-trip oracle          |
| `fuzz_graphicalui_css`       | Stylesheet sanitiser monotonicity oracle     |
| `fuzz_keyvaluestore`         | `.kv` parse and glob round-trip oracle       |
| `fuzz_hash`                  | CRC32 and hex round-trip / known-answer      |
| `fuzz_path`                  | Sandbox path validation agreement oracle     |
| `fuzz_random`                | Bounded-integer sampling in-range oracle     |
| `fuzz_http`                  | URL parse reconstruct-converge oracle        |
| `fuzz_process`               | argv tokenisation re-quote oracle            |
| `fuzz_regex`                 | Nested-quantifier ReDoS known-answers        |
| `fuzz_string`                | UTF-8 codepoint helpers round-trip oracles   |
| `fuzz_terminal`              | Key/UTF-8/ANSI decode non-empty oracle       |

### Building

Fuzz targets require Clang with AddressSanitizer and libFuzzer:

```bash
cmake -B build-fuzz -DLUMA_BUILD_FUZZ=ON \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-fuzz
```

### Running

Run a target with the shared corpus and dictionary:

```bash
./build-fuzz/fuzz_parser fuzz/corpus/ -dict=fuzz/dictionary.txt -max_total_time=300
```

Key libFuzzer flags:

- `-max_total_time=N` — stop after N seconds
- `-max_len=4096` — cap input size (default for this project)
- `-dict=fuzz/dictionary.txt` — use project dictionary for token-aware mutations
- `-jobs=N` — run N parallel fuzzing jobs

### Corpus

Seed inputs live in `fuzz/corpus/` with subdirectories per component (e.g., `fuzz/corpus/compiler/`, `fuzz/corpus/linter/`). libFuzzer automatically discovers and uses all files in the corpus directory.

When adding a new language feature, add a minimal seed file demonstrating its syntax to the appropriate corpus subdirectory.

### Investigating Crashes

When a fuzzer finds a crash:

1. The crashing input is saved as `crash-<hash>` in the working directory.
2. Reproduce: `./build-fuzz/fuzz_parser crash-<hash>`
3. Minimise: `./build-fuzz/fuzz_parser -minimize_crash=1 crash-<hash>`
4. Fix the bug, then add the minimised input to the corpus as a regression test.

### Adding a New Fuzz Target

1. Create `fuzz/fuzz_<component>.cpp` implementing `LLVMFuzzerTestOneInput`.
2. Add the target to `fuzz/CMakeLists.txt` using the `luma_add_fuzz_target` helper.
3. Add the target name to `.github/workflows/fuzz.yml`.
4. Optionally add seed inputs to `fuzz/corpus/`.

---

## 8 — Checklist

Before submitting any change that touches the interpreter, standard library, or tests, verify:

- [ ] Interpreter or stdlib behaviour changes are covered by **both** a C++ unit test and a Luma `@test` feature test where applicable.
- [ ] Test and `@test` function names follow the conventions in §4 (descriptive `test_*` names, no `test1`).
- [ ] `ctest --preset default` reports zero failures.
- [ ] All affected Luma feature suites pass via `build/Release/luma --test <file>.luma`.
- [ ] New or modified stdlib modules update the sandbox tests (§6) at both the C++ and Luma layers.
- [ ] New resource limits add a C++ test that triggers the limit and verifies the error (§6).
- [ ] New language features add a minimal seed file to the appropriate `fuzz/corpus/` subdirectory (§7).
- [ ] New fuzz targets are registered in `fuzz/CMakeLists.txt` and `.github/workflows/fuzz.yml`.
