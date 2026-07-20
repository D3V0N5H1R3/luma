# Contributing to Luma

Thank you for your interest in contributing to the Luma programming language.

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [Project Documentation](#project-documentation)
- [Getting Started](#getting-started)
    - [Code Quality and Linting](#code-quality-and-linting)
    - [Linters and Formatters](#linters-and-formatters)
    - [Git Hooks](#git-hooks)
    - [Benchmarks](#benchmarks)
    - [Fuzz Testing](#fuzz-testing)
    - [Security Considerations](#security-considerations)
- [Branch Naming](#branch-naming)
- [Commit Messages](#commit-messages)
- [Pull Request Workflow](#pull-request-workflow)
- [Code Style](#code-style)
- [Error Handling](#error-handling)
- [Reporting Issues](#reporting-issues)
- [License](#license)

---

## Prerequisites

| Platform | Compiler Requirement | Build System |
| -------- | -------------------- | ------------ |
| Linux    | GCC 13 or later      | CMake 3.21+  |
| macOS    | Clang 15 or later    | CMake 3.21+  |
| Windows  | MSVC 2022 or later   | CMake 3.21+  |

All compilers must support C++20. Building the interpreter, language server, and
debugger needs only a C++20 toolchain and CMake 3.21 or later — every
third-party library is vendored and built from source. **Python 3.10 or later**
is additionally required to run the helper scripts under `scripts/` (test
runners, the Git hook installer, and coverage).

Some optional components need extra tooling:

- **`GraphicalUi` on Linux** — the WebKitGTK development headers and `pkg-config`
  (e.g. `sudo apt-get install libwebkit2gtk-4.1-dev pkg-config`). Without them
  the build succeeds but the module is disabled (compiled as a stub); pass
  `-DLUMA_FEATURE_WEBVIEW=OFF` to disable it deliberately. Windows (WebView2) and
  macOS (WebKit) need no extra package.
- **VS Code extension and the Node-based linters** (markdownlint, Stylelint,
  ESLint, Prettier) — **Node.js 20 or later** (CI builds with Node 22).
- **Zed extension** — **Rust** via [rustup](https://rustup.rs/) with the
  `wasm32-wasip1` target (`rustup target add wasm32-wasip1`).

---

## Project Documentation

Before making significant changes, skim the design and reference documents in
[`documents/`](documents/):

- [Luma_Setup.md](documents/Luma_Setup.md) — toolchain setup and build details.
- [Luma_Software_Architecture.md](documents/Luma_Software_Architecture.md) — interpreter pipeline and module design.
- [Luma_User_Manual.md](documents/Luma_User_Manual.md) — complete language reference.
- [Luma_Standard_Library_Reference.md](documents/Luma_Standard_Library_Reference.md) — standard library and built-in functions.
- [Luma_Coding_Guidelines.md](documents/Luma_Coding_Guidelines.md) — Luma coding style and conventions.
- [Luma_Error_Handling.md](documents/Luma_Error_Handling.md) — error categories and `result`/`optional` handling.

Per-language coding standards (C++, Python, Rust, TypeScript, CMake, and more)
live in [`instructions/`](instructions/) as `*.instructions.md` files.

---

## Getting Started

### 1. Clone the Repository

```bash
git clone https://github.com/d3v0n5h1r3/luma.git
cd luma
```

### 2. Create a Feature Branch

```bash
git switch -c feature/short-description
```

### 3. Build

The CMake presets are the canonical way to configure and build. The `default` preset is a Release build that writes to `build/`:

```bash
cmake --preset default
cmake --build --preset default
```

Alternatively, configure and build manually without presets:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The binary is produced at `build/luma` (Unix) or `build\Release\luma.exe` (Windows). The language server (`luma_lsp`) and DAP debugger (`luma_dap`) are built alongside it.

### 4. Test Your Changes

Run a Luma program:

```bash
build/luma examples/language-features/hello.luma
```

Run C++ unit tests with the matching test preset:

```bash
ctest --preset default
```

Or invoke CTest directly in the build directory:

```bash
cd build && ctest --output-on-failure -C Release
```

Run Luma test suites (cross-platform):

```bash
python scripts/run_luma_tests.py
```

Or run them manually (Linux and macOS):

```bash
for f in tests/features/language/*.luma tests/features/stdlib/*.luma; do build/luma --test "$f"; done
```

Or on Windows:

```powershell
foreach ($f in Get-ChildItem tests\features\*\*.luma) { build\Release\luma.exe --test $f.FullName }
```

Run sandbox tests (verify safe modules work in `--box` mode):

```bash
build/luma --box --test tests/features/language/sandbox.luma
```

Run **and verify every example** end to end — including ones that need user input. Console examples are driven with scripted stdin, Terminal/TUI examples through the headless Terminal harness, and `GraphicalUi` examples in headless mode; any example with `@test` blocks also has its assertions checked:

```bash
python scripts/run_examples.py
```

On Windows, replace `build/luma` with `build\Release\luma.exe` in the commands above.

### Code Quality and Linting

`clang-tidy` provides static analysis for the C++ sources. The repository ships a
`.clang-tidy` file that defines the enforced check set, so run clang-tidy with
that configuration — rather than a custom `--checks` override — so local results
match what CI enforces. clang-tidy 18 or later is required.

clang-tidy reads a `compile_commands.json` compilation database, which the CMake
presets generate automatically (`CMAKE_EXPORT_COMPILE_COMMANDS` is on). Configure
once, then analyse the files you changed:

```bash
cmake --preset default
clang-tidy -p build core/analysis/lexer/lexer.cpp
```

To analyse the whole project as it compiles, point CMake at clang-tidy instead:

```bash
cmake -B build-tidy -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_CLANG_TIDY=clang-tidy
cmake --build build-tidy
```

Either way the analysis uses the checks defined in `.clang-tidy`, and findings
are printed to the console.

### Linters and Formatters

Beyond `clang-tidy`, every language in the repository has a dedicated linter or
formatter enforced by its own CI workflow. Configuration is checked in at the
repository root so local runs and CI stay in sync.

The fastest way to reproduce the CI lint result locally is the aggregate runner,
which runs every gate below the way its workflow does and prints a single
pass/fail summary, skipping any tool you do not have installed:

```bash
python scripts/lint.py       # run every check gate (add --list to preview, --skip clang-tidy to omit the slow one)
python scripts/format.py     # apply every auto-formatter and safe auto-fix
```

See [scripts/README.md](scripts/README.md#linting-and-formatting) for the full
options. The individual gates can also be run by hand — install the tool noted
below, then invoke it as CI does:

| Scope                       | Tool                 | Configuration              | Workflow              |
| --------------------------- | -------------------- | -------------------------- | --------------------- |
| C++ formatting              | clang-format         | `.clang-format`            | `ci.yml`              |
| C++ static analysis         | clang-tidy           | `.clang-tidy`              | `ci.yml`              |
| Compiler-flag / tidy sync   | `check_warning_sync` | `scripts/`                 | `ci.yml`              |
| Python                      | Ruff                 | `ruff.toml`                | `ci-python.yml`       |
| Rust (Zed extension)        | rustfmt + Clippy     | `extensions/zed/`          | `ci-zed.yml`          |
| TypeScript / JavaScript     | ESLint + Prettier    | `extensions/vscode/`       | `ci-vscode.yml`       |
| Shell                       | ShellCheck           | auto-discovered            | `ci-shell.yml`        |
| PowerShell                  | PSScriptAnalyzer     | `PSScriptAnalyzerSettings` | `ci-powershell.yml`   |
| CMake                       | cmakelint            | `.cmakelintrc`             | `ci-cmake.yml`        |
| CSS                         | Stylelint            | `stylelint.config.mjs`     | `ci-css.yml`          |
| Markdown                    | markdownlint-cli2    | `.markdownlint-cli2.jsonc` | `ci-markdown.yml`     |

The exact commands each gate runs (with the pinned tool versions) live in each
workflow; the equivalents below are what `scripts/lint.py` invokes under the
hood:

```bash
# C++ formatting — clang-format >= 18 (apt-get install clang-format)
find core shared language-server/source debugger/source tests fuzz \( -name '*.cpp' -o -name '*.hpp' \) -exec clang-format --dry-run --Werror {} +

# C++ static analysis — clang-tidy >= 18 (apt-get install clang-tidy); needs a configured build/
clang-tidy -p build core/analysis/lexer/lexer.cpp

# Compiler-flag / clang-tidy sync — Python >= 3.10
python scripts/check_warning_sync.py --strict

# Python — Ruff (pip install ruff)
ruff check . && ruff format --check --diff .

# CMake — cmakelint (pip install cmakelint)
cmakelint --config=.cmakelintrc $(git ls-files 'CMakeLists.txt' '**/CMakeLists.txt' '*.cmake' ':!:external/**')

# Rust (Zed extension) — rustfmt + Clippy (rustup component add rustfmt clippy; needs the wasm target)
(cd extensions/zed && cargo fmt --check && cargo clippy --target wasm32-wasip1 -- -D warnings)

# TypeScript / JavaScript — ESLint + Prettier (run `npm ci` in extensions/vscode first)
npm --prefix extensions/vscode run lint:eslint
npm --prefix extensions/vscode run format:check

# Shell — ShellCheck (apt-get install shellcheck)
shellcheck $(git ls-files '*.sh' '*.bash' ':!:external/**')

# PowerShell — PSScriptAnalyzer (pwsh: Install-Module PSScriptAnalyzer)
pwsh -c "Invoke-ScriptAnalyzer -Path . -Recurse -Settings ./PSScriptAnalyzerSettings.psd1"

# Markdown and CSS — markdownlint-cli2 and Stylelint (require Node.js)
npx markdownlint-cli2
npx stylelint external/gui-framework/gui-overrides.css
```

### Git Hooks

The repository ships two hooks (in `scripts/hooks/`):

- **`pre-commit`** runs `clang-format` — and `clang-tidy` when
  `build/compile_commands.json` exists — on staged C++ files, blocking the
  commit if they are not clean.
- **`commit-msg`** checks that the commit subject follows the
  [Conventional Commits](#commit-messages) format (one of `chore`, `docs`,
  `feat`, `fix`, `refactor`, `test`, with an optional scope and `!`), blocking
  the commit if it does not. It also warns — without blocking — when the
  subject exceeds 72 characters. Merge, revert, and autosquash
  (`fixup!`/`squash!`) messages are exempt.

Enable them once per clone by pointing Git at the tracked hooks directory:

```bash
python scripts/install_hooks.py
```

This sets a repository-local `core.hooksPath`, so the hooks stay
version-controlled and update automatically on `git pull`. Running
`python scripts/configure.py <preset>` enables the hooks for you as part of
configuring a build.

### Benchmarks

The `benchmarks/` directory contains a Luma-language performance suite that times arithmetic, strings, arrays, dictionaries, function calls, control flow, and pipe chains:

```bash
# Unix/macOS
build/luma benchmarks/suite.luma

# Windows
build\Release\luma.exe benchmarks/suite.luma
```

Results are printed as a formatted table with total time and per-iteration cost.

### Fuzz Testing

The `fuzz/` directory contains LibFuzzer targets for each stage of the interpreter pipeline. Fuzz testing requires **Clang** with AddressSanitizer support and is not part of the normal build. See [fuzz/README.md](fuzz/README.md) for full details.

```bash
# Configure with fuzzing flags (Clang only)
cmake -B build-fuzz -DLUMA_BUILD_FUZZ=ON \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++

# Build
cmake --build build-fuzz --parallel

# Run (e.g. fuzz the lexer for 5 minutes)
./build-fuzz/fuzz_lexer fuzz/corpus/lexer/ -dict=fuzz/dictionary.txt -max_total_time=300
./build-fuzz/fuzz_parser fuzz/corpus/parser/ -dict=fuzz/dictionary.txt -max_total_time=300
```

Available fuzz targets:

| Target                       | Component                       |
| ---------------------------- | ------------------------------- |
| `fuzz_lexer`                 | Lexer                           |
| `fuzz_parser`                | Parser                          |
| `fuzz_resolver`              | Name Resolver                   |
| `fuzz_type_checker`          | Type Checker                    |
| `fuzz_linter`                | Linter                          |
| `fuzz_compiler`              | Compiler                        |
| `fuzz_optimizer`             | Optimizer                       |
| `fuzz_include_resolver`      | Include Resolver                |
| `fuzz_vm`                    | VM                              |
| `fuzz_structured`            | Structured input                |
| `fuzz_bytecode_deserializer` | `.lumc` deserializer            |
| `fuzz_json`                  | `shared/json` parser            |
| `fuzz_json_stdlib`           | `Json` stdlib parser            |
| `fuzz_csv`                   | `Csv` codec                     |
| `fuzz_xml`                   | `Xml` parser                    |
| `fuzz_datetime`              | `DateTime` ISO-8601 codec       |
| `fuzz_protocol`              | `shared/protocol` transport     |
| `fuzz_compression`           | `Compression` codec             |
| `fuzz_encoder`               | `Encoder` Base64 / URL codecs   |
| `fuzz_graphicalui_css`       | `GraphicalUi` CSS sanitiser     |
| `fuzz_keyvaluestore`         | `KeyValueStore` `.kv` codec     |
| `fuzz_hash`                  | `Hash` CRC32 / hex codec        |
| `fuzz_path`                  | `FileSystem` path validator     |
| `fuzz_random`                | `Random` bounded integers       |
| `fuzz_http`                  | `Http` URL parser               |
| `fuzz_process`               | `Process` command tokenizer     |
| `fuzz_regex`                 | `RegularExpression` ReDoS guard |
| `fuzz_string`                | UTF-8 string codec              |
| `fuzz_terminal`              | `Terminal` key decoder          |

Seed inputs live in `fuzz/corpus/`. Any crash found by the fuzzer is a real bug — expected parse errors are caught internally.

### Security Considerations

When adding or modifying standard library modules:

- **Sandbox mode:** any function that performs file I/O, network access, or process execution must be gated behind the `sandbox` flag in the module's registration function. If the module is entirely OS-dependent, skip its registration in `stdlib_registry.hpp` when `sandbox` is true. If only individual functions access the filesystem, wrap those `define_native` calls in `if (!sandbox) { ... }`.
- **Resource limits:** new unbounded resources (queues, pools, caches) must have an upper bound defined in `resource_limits.hpp`.
- **Input validation:** validate all inputs at the module boundary. Return `result<T>` on failure. Reject path traversal, CRLF injection, and other injection vectors.

---

## Branch Naming

Use lowercase, hyphen-separated names with a category prefix:

| Prefix      | Purpose                                 |
| ----------- | --------------------------------------- |
| `chore/`    | Tooling, config, dependencies           |
| `docs/`     | Documentation changes                   |
| `feature/`  | New feature                             |
| `fix/`      | Bug fix                                 |
| `refactor/` | Code restructuring, no behaviour change |
| `test/`     | Adding or updating tests                |

Examples: `docs/update-readme`, `feature/add-match-expression`, `fix/lexer-string-escape`.

---

## Commit Messages

Follow the [Conventional Commits](https://www.conventionalcommits.org/) format:

```text
<type>: <short summary>
```

| Type        | Purpose                                 |
| ----------- | --------------------------------------- |
| `chore:`    | Tooling, config, dependencies           |
| `docs:`     | Documentation only                      |
| `feat:`     | New feature                             |
| `fix:`      | Bug fix                                 |
| `refactor:` | Code restructuring, no behaviour change |
| `test:`     | Adding or updating tests                |

**Rules:**

- Use the imperative mood: `feat: add match expression` not `feat: added match expression`.
- Keep the first line under 72 characters.
- One logical change per commit.

---

## Pull Request Workflow

1. Push your feature branch:

    ```bash
    git push --set-upstream origin feature/short-description
    ```

2. Open a pull request targeting `main`.

3. Ensure CI passes on all platforms (Ubuntu, macOS, Windows). CI also runs sanitizers, static analysis (clang-tidy), and code coverage. Running the [Code Quality and Linting](#code-quality-and-linting) checks locally first catches most failures before they reach CI.

4. Request a review.

5. Once approved, the PR will be merged with a merge commit (`--no-ff`).

---

## Code Style

| Element             | Convention   | Example                  |
| ------------------- | ------------ | ------------------------ |
| C++ namespace       | `luma`       | `namespace luma { ... }` |
| Classes / structs   | `PascalCase` | `Interpreter`, `Token`   |
| Constants           | `snake_case` | `luma_version`           |
| Directories         | `snake_case` | `core_builtins`          |
| Functions / methods | `snake_case` | `tokenize`, `check_type` |
| Source files        | `snake_case` | `type_checker.cpp`       |

- Use C++20 features.
- Header-only files use the `.hpp` extension.
- Implementation files use the `.cpp` extension.
- Every header file must have `#ifndef` / `#define` / `#endif` include guards.

---

## Error Handling

The project uses a layered error handling policy. Each layer of the interpreter, shared libraries, and editor extensions uses the error model best suited to its domain. The full specification lives in [Luma_Error_Handling.md §9 — Interpreter Implementation Policy](documents/Luma_Error_Handling.md#9--interpreter-implementation-policy).

### Summary by Layer

| Layer                             | Error Model                                                                              |
| --------------------------------- | ---------------------------------------------------------------------------------------- |
| **Analysis** (lexer, parser, type checker, linter) | Emit `Diagnostic` objects. Do not throw for user-facing errors.               |
| **Runtime** (VM, stdlib)          | `RuntimeError` for bugs. `result<T>` for expected failures.                              |
| **Protocol** (LSP, DAP)          | Typed error responses for protocol errors. `std::runtime_error` for I/O failures.        |
| **Shared modules** (`shared/`)   | `std::runtime_error` for malformed input. `std::optional` for expected absence.          |
| **Extensions** (`extensions/`)   | Follow host-language idioms (TypeScript exceptions, Rust `Result<T>`).                   |

### Key Rules

1. **Do not mix error categories.** Domain failures (expected conditions) use `result<T>` or `std::optional`. Programmer errors (bugs) use exceptions.
2. **Do not use `Result<T, E>` outside the runtime.** `Result<T, E>` is a Luma runtime concept. Shared C++ code uses `std::optional` and exceptions.
3. **Do not silently swallow errors.** If a function encounters an unexpected state, throw or propagate — do not return a default.
4. **Extensions follow their ecosystem.** TypeScript extensions use `try`/`catch` and `Promise` rejection. Rust extensions use `Result<T>` and `?`.

For detailed per-module guidelines, see [Luma_Error_Handling.md](documents/Luma_Error_Handling.md).

---

## Reporting Issues

> **Security vulnerabilities:** Do **not** open a public issue. Follow the
> private reporting process in [SECURITY.md](SECURITY.md) instead.

When filing a bug report, include:

1. **Platform** — OS, compiler, and compiler version.
2. **Steps to reproduce** — a minimal `.luma` file and the command used.
3. **Expected behaviour** — what you expected to happen.
4. **Actual behaviour** — what actually happened, including the full error message.

---

## License

By contributing, you agree that your contributions will be licensed under the same license as the project.
