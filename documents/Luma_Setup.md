# Luma — Development Environment Setup

This guide explains how to set up a development environment for working on the Luma project. The shared steps — cloning, building, running, testing, formatting, and debugging the C++ interpreter — are the same for every editor. Editor-specific integration for Visual Studio Code and Zed is covered in [Editor Integration](#8--editor-integration).

## Table of Contents

1. [Prerequisites](#1--prerequisites)
2. [Clone the Repository](#2--clone-the-repository)
3. [Building](#3--building)
4. [Running](#4--running)
5. [Testing](#5--testing)
6. [Code Formatting and Linting](#6--code-formatting-and-linting)
7. [Debugging the C++ Interpreter](#7--debugging-the-c-interpreter)
8. [Editor Integration](#8--editor-integration)
    - [Visual Studio Code](#visual-studio-code)
    - [Zed](#zed)
    - [Keeping Editor Configurations in Sync](#keeping-editor-configurations-in-sync)

- [See Also](#see-also)

---

## 1 — Prerequisites

The following are required to build and run the project on Windows, Linux, or macOS:

- A C++20-compliant compiler (GCC 13+, Clang 15+, or MSVC 2022+). These are hard minimums: Luma uses the C++20 library features `std::format` and `std::chrono::clock_cast`, which libstdc++ provides only from GCC 13 onward, so GCC 12 and earlier cannot build the project. On distributions whose default `g++` is still GCC 12 — notably Raspberry Pi OS and Debian 12 ("bookworm") — install GCC 13 or newer and select it with `-DCMAKE_CXX_COMPILER=g++-13`.
- CMake ≥ 3.21.
- Python 3.10 or later (for the helper scripts under `scripts/`, including the Luma feature-test runner `scripts/run_luma_tests.py`).

The interpreter, language server, and debugger build from those tools alone — every third-party library is vendored. One optional capability needs an extra system package:

- **`GraphicalUi` on Linux** requires the WebKitGTK development headers and `pkg-config` (for example `sudo apt-get install libwebkit2gtk-4.1-dev pkg-config` on Debian/Ubuntu). Without them the build still succeeds but the module is disabled (compiled as a stub); pass `-DLUMA_FEATURE_WEBVIEW=OFF` to disable it deliberately. Windows (WebView2) and macOS (WebKit) need no extra package.

Each editor has additional, optional requirements (extensions or plugins) listed under its section in [Editor Integration](#8--editor-integration). Building an editor extension from source needs its own toolchain — Node.js for Visual Studio Code, Rust (with the `wasm32-wasip1` target) for Zed — as documented in each extension's README.

---

## 2 — Clone the Repository

```bash
git clone https://github.com/d3v0n5h1r3/luma.git
cd luma
```

---

## 3 — Building

Configure once, then build. The interpreter and tooling are produced under `build/`:

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all targets
cmake --build build --config Release --parallel
```

To build a single target — for example the language server or the debug adapter — pass `--target`:

```bash
cmake --build build --config Release --target luma_lsp --parallel
cmake --build build --config Release --target luma_dap --parallel
```

> **Note:** The output directory varies by generator. Single-config generators (Make, Ninja) place binaries in `build/`; multi-config generators (Visual Studio) place them in `build/Release/`. Adjust the paths in the following sections accordingly.

---

## 4 — Running

Run a Luma program by passing its path to the interpreter:

```bash
build/Release/luma examples/language-features/hello.luma
```

Start the interactive REPL by running the interpreter with no arguments:

```bash
build/Release/luma
```

See the [REPL Guide](Luma_REPL_Guide.md) for an overview of REPL commands and behaviour.

---

## 5 — Testing

Run the C++ unit tests with CTest:

```bash
cd build && ctest --output-on-failure
```

Run the Luma feature tests (the `@test` suites under `examples/` and `tests/features/`) with the Python runner:

```bash
python scripts/run_luma_tests.py
```

See [Testing](Luma_User_Manual.md#24--testing-with-test) in the User Manual for how to write `@test` functions.

---

## 6 — Code Formatting and Linting

C++ sources are formatted with `clang-format` using the repository's `.clang-format` configuration. Most editors can be configured to format on save (see each editor's section below).

Lint with `clang-tidy` against the compilation database in `build/`:

```bash
find core shared language-server debugger -name '*.cpp' | xargs clang-tidy -p build
```

On Windows PowerShell:

```powershell
Get-ChildItem -Recurse -Include *.cpp -Path core,shared,language-server,debugger | ForEach-Object { clang-tidy -p build $_.FullName }
```

The enabled checks are controlled by the repository's `.clang-tidy` configuration file.

---

## 7 — Debugging the C++ Interpreter

To debug the interpreter itself, build a `Debug` configuration and launch it under a native debugger:

```bash
# GDB
gdb --args build/Debug/luma examples/language-features/hello.luma

# LLDB
lldb -- build/Debug/luma examples/language-features/hello.luma
```

Each editor can drive these debuggers through its own interface — see the editor sections below. For debugging **Luma programs** (rather than the interpreter), use the Debug Adapter Protocol integration described per editor, and see [Luma_Debugger.md](Luma_Debugger.md) for the debugger's design.

---

## 8 — Editor Integration

The Luma project ships first-class integration for two editors. Each provides syntax highlighting, Language Server Protocol (LSP) support, and Debug Adapter Protocol (DAP) debugging for `.luma` files. For the design of these tools, see [Luma_Language_Server.md](Luma_Language_Server.md), [Luma_Debugger.md](Luma_Debugger.md), and [Luma_Syntax_Highlighting.md](Luma_Syntax_Highlighting.md).

### Visual Studio Code

#### Recommended Extensions

Install the following extensions for the best experience.

**Core C++ and Luma development:**

- **C/C++ Extension Pack** (`ms-vscode.cpptools-extension-pack`): Rich C++ language support, including IntelliSense, debugging, and code browsing. Bundles **CMake Tools** (`ms-vscode.cmake-tools`) for configuring, building, and testing the interpreter.
- **Luma** (`D3V0N5H1R3.luma-language`): Syntax highlighting, LSP support, and DAP debugging for `.luma` files.

**Language linters and formatters** (matching the project's CI gates):

- **Ruff** (`charliermarsh.ruff`): Python linting and formatting.
- **markdownlint** (`DavidAnson.vscode-markdownlint`): Markdown linting.
- **Stylelint** (`stylelint.vscode-stylelint`): CSS linting.
- **GitHub Actions** (`github.vscode-github-actions`): Workflow authoring and validation.

**AI assistants and pull requests:**

- **GitHub Copilot** (`github.copilot`) and **Copilot Chat** (`github.copilot-chat`): AI code completion and chat.
- **Claude Code** (`anthropic.claude-code`): Anthropic's agentic coding assistant.
- **GitHub Pull Requests** (`github.vscode-pull-request-github`): Review and manage pull requests in the editor.

All of these are listed in `.vscode/extensions.json`, so VS Code offers them together — open the Command Palette (`Ctrl+Shift+P`), run **Extensions: Show Recommended Extensions**, and install the workspace recommendations.

#### Configuration

When you first open the project, CMake Tools prompts you to select a "kit" (the compiler toolchain). Choose a modern C++20-compliant compiler (for example "Visual Studio Build Tools", "GCC", or "Clang"). CMake Tools then configures the project automatically; if it does not, run **CMake: Configure** from the Command Palette.

The Luma extension resolves the `luma_lsp`, `luma`, and `luma_dap` binaries automatically (a bundled download, then `PATH`), so the workspace stays portable and `luma.lsp.path`, `luma.path`, and `luma.dap.path` are intentionally left unset in `.vscode/settings.json`. To attach the editor to a binary you built from source, set those keys in your **user** settings to the layout for your platform: multi-config (Visual Studio) builds land in `build/Release/luma*.exe`, while single-config (Ninja, Makefiles) builds land in `build/luma*`.

#### Building, Running, and Testing

- **Build:** Press `F7` or run **CMake: Build**. To build a specific target, click the **Build** button in the status bar and select it (for example `luma`, `luma_dap`, or a test).
- **Run:** Set the launch target via the play button in the status bar, then press `Ctrl+F5` (run) or `F5` (debug). To run an open `.luma` file, use the **Luma: Run Current File** task.
- **Test:** Use the **CTest: Run All Tests** task or the Test Explorer for C++ tests, and the **Luma: Run All Test Suites** task for Luma feature tests.

#### Debugging the C++ Interpreter

The project ships `.vscode/launch.json` with pre-configured targets, all of which build and launch the **Debug** build (`build-debug/`). Set a breakpoint in a C++ source file (for example `core/runtime/vm/vm.cpp`), open the "Run and Debug" view (`Ctrl+Shift+D`), and select one of:

- **C++: Interpreter (MSVC)** — Windows with Visual Studio (Debug build).
- **C++: Interpreter (GDB)** — Linux with GDB (Debug build).
- **C++: Interpreter (LLDB)** — macOS with LLDB (Debug build).
- **C++: REPL (MSVC)** / **C++: REPL (GDB)** — Launch the REPL under the debugger.

The **C++: Language Server** and **C++: DAP Debugger** targets (with the same MSVC/GDB variants) debug `luma_lsp` and `luma_dap` in the same way.

Press `F5` to start; the selected configuration builds the project automatically via its `preLaunchTask`.

> **Tip:** Edit the `args` array in `.vscode/launch.json` to change which `.luma` file is debugged.

#### Debugging Luma Programs

The Luma extension provides a DAP server for `.luma` files. Set a breakpoint in a `.luma` file, add a launch configuration, and press `F5`:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "type": "luma",
            "request": "launch",
            "name": "Debug Current File",
            "program": "${file}",
            "stopOnEntry": false
        }
    ]
}
```

Set `stopOnEntry` to `true` to pause on the first executable line of `@main`. This is the canonical Luma `launch.json` example; the Debugger and User Manual guides refer back to it.

#### Formatting

The C/C++ extension formats C++ files on save using the repository's `.clang-format`, pre-configured in `.vscode/settings.json`.

### Zed

#### Install the Luma Extension

The Luma extension provides syntax highlighting, LSP integration, DAP debugging, and code snippets for `.luma` files.

From the extension gallery: open the Command Palette (`Cmd+Shift+P` / `Ctrl+Shift+P`), run **zed: extensions**, search for **Luma**, and click **Install**.

For manual installation, copy the extension into your Zed extensions folder and reload Zed:

```bash
cp -r extensions/zed ~/.local/share/zed/extensions/luma
```

The extension automatically downloads the language server (`luma_lsp`) from GitHub Releases on first use. If automatic download is unavailable, build from source and add the output directory to your `PATH`.

#### Building, Running, and Testing

Zed has no built-in CMake integration; use the integrated terminal (`` Ctrl+` ``) and the commands in [Building](#3--building), [Running](#4--running), and [Testing](#5--testing).

#### Debugging Luma Programs

The extension registers a DAP configuration. Open a `.luma` file, set breakpoints in the gutter, then open the Command Palette and run **debugger: start** (or press `F5`) and select the **Debug Current File** template. The extension launches `luma_dap` via stdio transport; ensure `luma_dap` is on your `PATH` or built locally.

#### Formatting

Zed supports `clang-format` as an external formatter. Configure format-on-save in your `settings.json`:

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

#### LSP Features

Once the language server is available, the following LSP features work automatically for `.luma` files: completions, hover documentation, diagnostics, inlay hints (type and parameter hints), and built-in code snippets.

### Keeping Editor Configurations in Sync

The build, run, debug, and test entry points are defined once per editor because each uses a different configuration schema:

| Surface                      | VS Code               | Zed               |
| ---------------------------- | --------------------- | ----------------- |
| Tasks (configure/build/test) | `.vscode/tasks.json`  | `.zed/tasks.json` |
| Run / test current file      | `.vscode/tasks.json`  | `.zed/tasks.json` |
| C++ debug launch             | `.vscode/launch.json` | `.zed/debug.json` |

Both drive the same CMake presets (see [PRESETS.md](../cmake/PRESETS.md)): `default` (Release) and `debug` build the interpreter, while `relwithdebinfo`, `sanitize`, and `coverage` cover profiling, sanitizers, and coverage. Keep these conventions aligned across both editors:

- **Preset names** (`default`, `debug`, `relwithdebinfo`, `sanitize`, `coverage`) and the task **labels** that wrap them.
- **Binary layout** per generator — multi-config (Windows/Visual Studio, macOS/Xcode) under `build/Release/` and `build-debug/Debug/`; single-config (Ninja, Makefiles) under `build/` and `build-debug/`.
- **C++ debug launch configs** target the **Debug** build (`build-debug/`); VS Code exposes only Debug configs, while Zed also offers explicitly-labelled *Release* interpreter configs (`build/Release/`) for the less common case of debugging an optimised binary.

When you add or rename a preset, update both editor configurations and this table together. There is deliberately no generator for these files — the editor schemas differ too much to share one source. If drift becomes recurring, a lightweight CI check that the preset labels match across both editors would catch it.

---

## See Also

- [Tutorial](Luma_Tutorial.md) — write your first Luma programs once the tools are installed
- [REPL Guide](Luma_REPL_Guide.md) — interactive exploration of the language
- [Debugger](Luma_Debugger.md) — Debug Adapter Protocol design and architecture
- [Language Server](Luma_Language_Server.md) — Language Server Protocol design and architecture
- [Syntax Highlighting](Luma_Syntax_Highlighting.md) — editor grammars and extensions
- [Contributing](../CONTRIBUTING.md) — build setup, branch naming, and the pull request workflow
