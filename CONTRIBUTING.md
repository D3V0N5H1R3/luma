# Contributing to Luma

Thank you for your interest in contributing to the Luma programming language.

This project and everyone participating in it is governed by the Luma
[Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to
uphold this code.

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [Required Knowledge](#required-knowledge)
- [Project Documentation](#project-documentation)
- [Getting Started](#getting-started)
    - [Code Quality and Linting](#code-quality-and-linting)
    - [Linters and Formatters](#linters-and-formatters)
    - [Git Hooks](#git-hooks)
    - [Benchmarks](#benchmarks)
    - [Fuzz Testing](#fuzz-testing)
    - [Security Considerations](#security-considerations)
    - [Debugging the C++ Interpreter](#debugging-the-c-interpreter)
    - [Editor Integration](#editor-integration)
- [Branch Naming](#branch-naming)
- [Commit Messages](#commit-messages)
- [Pull Request Workflow](#pull-request-workflow)
- [Releasing](#releasing)
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
third-party library is vendored and built from source. These versions are hard
minimums: Luma relies on the C++20 library features `std::format` and
`std::chrono::clock_cast`, which libstdc++ provides only from **GCC 13** onward,
so GCC 12 and earlier cannot build the project (for example, the default `g++`
on Raspberry Pi OS and Debian 12 "bookworm" is GCC 12 — install GCC 13 or newer
and configure with `-DCMAKE_CXX_COMPILER=g++-13`). **Python 3.10 or later** is
additionally required to run the helper scripts under `scripts/` (test runners,
the Git hook installer, and coverage).

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

## Required Knowledge

The project spans several technologies. You don't need all of them — only the
ones relevant to the area you're working on. The matrix below maps each area to
the languages, tools, and concepts you should be comfortable with before
contributing to it.

### By Area

| Area | Languages | Key Technologies & Concepts |
| ---- | --------- | --------------------------- |
| **Interpreter core** (`core/`) | C++20 | Lexers and tokenisation, recursive-descent parsing, ASTs, static type checking, bytecode compilation, stack-based virtual machines, RAII and smart pointers (`unique_ptr`, `shared_ptr`), value semantics, closures (upvalue capture) |
| **Concurrency runtime** (`core/runtime/concurrency/`) | C++20 | Threads (`std::thread`), mutexes, condition variables, atomics, cooperative cancellation, channel/CSP patterns, thread pools |
| **Standard library** (`core/runtime/stdlib/`) | C++20 | The Luma language itself (to design API surfaces), platform APIs (sockets, file I/O, processes), data format specifications (JSON, CSV, XML), IEEE-754 floating-point, regular expressions, cryptographic hashing |
| **Language server** (`language-server/`) | C++20 | [Language Server Protocol (LSP)](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/), JSON-RPC, incremental parsing, red-green trees, semantic tokens, UTF-16 position encoding |
| **Debugger** (`debugger/`) | C++20 | [Debug Adapter Protocol (DAP)](https://microsoft.github.io/debug-adapter-protocol/specification), breakpoint management, call stacks and stack frames, expression evaluation, thread-safe state machines, mutex lock ordering |
| **VS Code extension** (`extensions/vscode/`) | TypeScript | VS Code Extension API (`vscode` module), extension activation and lifecycle, language client (`vscode-languageclient`), debug adapter integration, webviews, TextMate grammars |
| **Zed extension** (`extensions/zed/`) | Rust | Zed Extension API (`zed_extension_api` crate), WebAssembly (`wasm32-wasip1` target), Tree-sitter grammars and queries, WASI |
| **Build system** (`CMakeLists.txt`, `cmake/`) | CMake | CMake presets, target-based configuration, cross-platform builds (Windows/macOS/Linux), compiler feature detection, CTest |
| **CI / CD** (`.github/workflows/`) | YAML | GitHub Actions, matrix builds, caching, artifact management, release automation |
| **Test infrastructure** (`tests/`, `fuzz/`) | C++20, Luma, Python | Custom C++ test framework (`test_framework.hpp`), Luma `@test` annotations and `assert()`, snapshot testing, LibFuzzer, `pytest` (for helper scripts) |
| **Scripts & tooling** (`scripts/`) | Python, PowerShell, Shell | Test runners, Git hooks, coverage reporting, cross-platform scripting |
| **Documentation** (`documents/`) | Markdown | Technical writing, API reference conventions |

### Cross-Cutting Concepts

These apply regardless of which area you work on:

- **Git** — branching (`feature/`, `fix/`, `docs/`), conventional commits
  (`feat:`, `fix:`, `refactor:`, `test:`, `docs:`, `chore:`), rebasing, and
  squash-merging.
- **The Luma language** — you should be able to read and write basic Luma
  programs (see the [User Manual](documents/Luma_User_Manual.md)) since feature
  tests, examples, and stdlib API design all require it.
- **The interpreter pipeline** — understanding the flow
  `Source → Lexer → Parser → Include Resolver → Type Checker → Linter → Compiler → VM`
  helps even when working on tooling, because the language server and debugger
  reuse the same front-end and runtime.

---

## Project Documentation

Before making significant changes, skim the design and reference documents in
[`documents/`](documents/):

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
python scripts/run_luma_examples.py
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

See [scripts/DIRECTORY.md](scripts/DIRECTORY.md#linting-and-formatting) for the full
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

The repository ships two hooks (in `scripts/git-hooks/`):

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
python scripts/install_git_hooks.py
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

The `fuzz/` directory contains LibFuzzer targets for each stage of the interpreter pipeline. Fuzz testing requires **Clang** with AddressSanitizer support and is not part of the normal build. See [fuzz/DIRECTORY.md](fuzz/DIRECTORY.md) for full details.

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
| `fuzz_decimal`               | `Decimal` base-10 parser        |
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

### Debugging the C++ Interpreter

To debug the interpreter itself, build a `Debug` configuration and launch it under a native debugger:

```bash
# Configure a Debug build
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel

# GDB
gdb --args build-debug/luma examples/language-features/hello.luma

# LLDB
lldb -- build-debug/luma examples/language-features/hello.luma
```

Each editor can drive these debuggers through its own interface — see [Editor Integration](#editor-integration) below. For debugging **Luma programs** (rather than the interpreter), use the Debug Adapter Protocol integration described per editor, and see [Luma_Debugger.md](documents/Luma_Debugger.md) for the debugger's design.

### Editor Integration

The Luma project ships first-class integration for two editors. Each provides syntax highlighting, Language Server Protocol (LSP) support, and Debug Adapter Protocol (DAP) debugging for `.luma` files. For the design of these tools, see [Luma_Language_Server.md](documents/Luma_Language_Server.md), [Luma_Debugger.md](documents/Luma_Debugger.md), and [Luma_Syntax_Highlighting.md](documents/Luma_Syntax_Highlighting.md).

#### Visual Studio Code

**Recommended extensions** (all listed in `.vscode/extensions.json`):

- **C/C++ Extension Pack** (`ms-vscode.cpptools-extension-pack`): IntelliSense, debugging, CMake Tools integration.
- **Luma** (`D3V0N5H1R3.luma-language`): Syntax highlighting, LSP, and DAP for `.luma` files.
- **GitHub Actions** (`github.vscode-github-actions`): Workflow authoring and validation.
- **GitHub Copilot** (`github.copilot`) and **Copilot Chat** (`github.copilot-chat`): AI code completion and chat.
- **Claude Code** (`anthropic.claude-code`): Anthropic's agentic coding assistant.
- **GitHub Pull Requests** (`github.vscode-pull-request-github`): PR review in the editor.

**Configuration:** When you first open the project, CMake Tools prompts you to select a kit. Choose a C++20-compliant compiler. CMake Tools then configures automatically; if not, run **CMake: Configure** from the Command Palette.

The Luma extension resolves `luma_lsp`, `luma`, and `luma_dap` binaries automatically (bundled download, then `PATH`). To use a locally-built binary, set `luma.lsp.path`, `luma.path`, and `luma.dap.path` in your **user** settings.

**Building, running, and testing:**

- **Build:** Press `F7` or run **CMake: Build**.
- **Run:** Set the launch target via the status bar, then `Ctrl+F5` (run) or `F5` (debug).
- **Test:** Use **CTest: Run All Tests** or the Test Explorer for C++ tests.

**Debugging the C++ interpreter:** The project ships `.vscode/launch.json` with pre-configured targets for the Debug build (`build-debug/`). Open the "Run and Debug" view (`Ctrl+Shift+D`) and select one of:

- **C++: Interpreter (MSVC / GDB / LLDB)** — Launch the interpreter under the debugger.
- **C++: REPL (MSVC / GDB)** — Launch the REPL under the debugger.
- **C++: Language Server** / **C++: DAP Debugger** — Debug `luma_lsp` or `luma_dap`.

**Debugging Luma programs:** Set a breakpoint in a `.luma` file and use a `"type": "luma"` launch configuration:

```json
{
    "type": "luma",
    "request": "launch",
    "name": "Debug Current File",
    "program": "${file}",
    "stopOnEntry": false
}
```

**Formatting:** The C/C++ extension formats on save using the repository's `.clang-format`, pre-configured in `.vscode/settings.json`.

#### Zed

**Install the Luma extension:** Command Palette → **zed: extensions** → search **Luma** → **Install**. The extension automatically downloads `luma_lsp` from GitHub Releases on first use. For manual installation:

```bash
cp -r extensions/zed ~/.local/share/zed/extensions/luma
```

**Building, running, and testing:** Zed has no built-in CMake integration; use the integrated terminal and the commands in [Build](#3-build) and [Test Your Changes](#4-test-your-changes).

**Debugging Luma programs:** Open a `.luma` file, set breakpoints, then Command Palette → **debugger: start** → select the **Luma** adapter and the **Launch** request. (The extension also registers a locator, so the ▶ run affordance on a `@main` function can start a debug session too.) Ensure `luma_dap` is on your `PATH`, or let the extension download it. See [extensions/zed/DIRECTORY.md](extensions/zed/DIRECTORY.md#debugger-setup) for a `debug.json` example.

**Formatting:** Configure clang-format as an external formatter in your Zed `settings.json`:

```json
{
    "languages": {
        "C++": {
            "formatter": {
                "external": {
                    "command": "clang-format",
                    "arguments": ["--assume-filename={buffer_path}"]
                }
            },
            "format_on_save": "on"
        }
    }
}
```

**LSP features:** Once the language server is available, completions, hover, diagnostics, inlay hints, and code snippets work automatically.

#### Keeping Editor Configurations in Sync

| Surface                      | VS Code               | Zed               |
| ---------------------------- | --------------------- | ----------------- |
| Tasks (configure/build/test) | `.vscode/tasks.json`  | `.zed/tasks.json` |
| Run / test current file      | `.vscode/tasks.json`  | `.zed/tasks.json` |
| C++ debug launch             | `.vscode/launch.json` | `.zed/debug.json` |

Both editors drive the same CMake presets (`default`, `debug`, `relwithdebinfo`, `sanitize`, `coverage`). When you add or rename a preset, update both editor configurations together.

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

## Releasing

Luma produces builds in two distinct places. Knowing which is which saves a lot of confusion:

| | **Actions build artifacts** | **GitHub Releases** |
| --- | --- | --- |
| Produced by | [`ci.yml`](.github/workflows/ci.yml) on every build | [`release.yml`](.github/workflows/release.yml) when a version tag is pushed |
| Lifetime | **Temporary** — auto-deleted after 7 days (`retention-days: 7`; the coverage report keeps 14) | **Permanent** — kept until you delete the release |
| Where | An Actions run's **Artifacts** section (`https://github.com/D3V0N5H1R3/luma/actions`), or `gh run download <run-id>` | The repository's **Releases** page (`https://github.com/D3V0N5H1R3/luma/releases`), or `gh release download <tag>` |
| Named | `luma-<os>-<compiler>` (e.g. `luma-ubuntu-latest-gcc`) | `luma-<os>-<arch>.tar.gz` / `.zip` per platform, plus a `SHA256SUMS` manifest |
| Use it for | Debugging or smoke-testing the build from one specific commit | Distributing a version to users — the canonical download |

Because [`ci.yml`](.github/workflows/ci.yml) is path-filtered, a docs- or workflow-only
commit does not trigger a build, so the newest artifacts may sit on an earlier
commit than `HEAD`. Release assets never expire, so they are what users should download.

### Cutting a release

Releases are **tag-triggered**: pushing a tag that matches `v*.*.*` (that is,
`vMAJOR.MINOR.PATCH`) runs [`release.yml`](.github/workflows/release.yml), which builds
every platform (Linux x86_64, Linux aarch64 / Raspberry Pi, macOS, Windows), validates
on the Linux distros, packages the VS Code `.vsix`, generates a changelog from the commit
log since the previous tag, and publishes a GitHub Release with all binaries and a
`SHA256SUMS` manifest attached.

1. Update the [`VERSION`](VERSION) file — the single source of truth for the project
   version — on `main`, and commit it:

    ```bash
    # e.g. bump 0.11.0 -> 0.11.0
    git switch main && git pull
    # edit VERSION, then:
    git commit -am "chore: bump version to 0.11.0"
    git push
    ```

2. Create the `v`-prefixed tag that matches `VERSION`. Use **either** option:

    **A. Automated (recommended) — [`tag-release.yml`](.github/workflows/tag-release.yml).**
    From the repository's **Actions → Tag Release** page, run the workflow (`workflow_dispatch`)
    and enter the new version in the `confirm_version` input. It reads [`VERSION`](VERSION),
    validates it is a strict `MAJOR.MINOR.PATCH` version, confirms your input matches the file,
    checks the `v<VERSION>` tag does not already exist, then creates and pushes the annotated
    tag for you — which triggers [`release.yml`](.github/workflows/release.yml). Tick `dry_run`
    first to validate without pushing. This requires the one-time `RELEASE_PAT` secret setup
    described below.

    **B. Manual.** Create an **annotated** tag whose version matches `VERSION`, prefixed with
    `v`, and push it:

    ```bash
    git tag -a v0.11.0 -m "Release version 0.11.0"
    git push origin v0.11.0
    ```

3. Watch the **Release** workflow run under
   [Actions](https://github.com/D3V0N5H1R3/luma/actions). When it finishes, the new
   release (with its binaries) appears on the
   [Releases](https://github.com/D3V0N5H1R3/luma/releases) page. It publishes
   immediately — there is no manual draft step.

The two editor extensions publish from their own tag prefixes —
`vscode-v*.*.*` ([`release-vscode.yml`](.github/workflows/release-vscode.yml), to the
Visual Studio Marketplace) and `zed-v*.*.*`
([`release-zed.yml`](.github/workflows/release-zed.yml)) — so they can be versioned
independently of the interpreter. See
[.github/workflows/DIRECTORY.md](.github/workflows/DIRECTORY.md) (§6 Releases) for the
workflow index and [Git tag conventions](instructions/git.instructions.md) for the tag
commands.

#### One-time setup: the `RELEASE_PAT` secret

The automated [`tag-release.yml`](.github/workflows/tag-release.yml) workflow needs a
`RELEASE_PAT` repository secret. This is required because a tag pushed with the default
`GITHUB_TOKEN` does **not** trigger another workflow (GitHub deliberately blocks recursive
workflow runs), so the tag must be pushed with a separate token for
[`release.yml`](.github/workflows/release.yml) to fire. The workflow fails fast with an
explanatory message if the secret is missing, so it never pushes a tag that would silently
never release.

Configure it once (a repository **admin** with a Personal Access Token, or a GitHub App
token, is needed):

1. Create a **fine-grained Personal Access Token**: GitHub → your **Settings** →
   **Developer settings** → **Personal access tokens** → **Fine-grained tokens** →
   **Generate new token**.
2. Scope it to the `D3V0N5H1R3/luma` repository (**Only select repositories**), set a short
   expiry, and under **Repository permissions** grant **Contents: Read and write** (this
   covers pushing tags). Leave everything else at **No access**.
3. **Generate token** and copy the value — it is shown only once.
4. In the `D3V0N5H1R3/luma` repository, go to **Settings → Secrets and variables → Actions
   → New repository secret**, name it exactly `RELEASE_PAT`, paste the token, and save.
5. Rotate the token before it expires and update the secret; if it lapses, the workflow's
   first step reports that `RELEASE_PAT` is not set.

> A classic PAT with the `repo` scope works too, but a fine-grained token limited to this
> repository with only `Contents: write` is the least-privilege choice.

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
