# Luma — Development Environment

How to set up an editor for working **on** Luma — building, running, testing, and debugging the interpreter, language server, and debug adapter from Visual Studio Code or Zed. For the build itself see [build.instructions.md](../instructions/build.instructions.md); for installing Luma to *write* Luma programs see the [Installation Guide](Luma_Installation_Guide.md).

## Table of Contents

1. [Overview](#1--overview)
2. [Visual Studio Code](#2--visual-studio-code)
3. [Zed](#3--zed)
4. [Debugging the C++ Interpreter from a Terminal](#4--debugging-the-c-interpreter-from-a-terminal)
5. [Keeping Editor Configurations in Sync](#5--keeping-editor-configurations-in-sync)
6. [See Also](#6--see-also)

---

## 1 — Overview

The repository ships workspace configuration for two editors — [`.vscode/`](../.vscode) and [`.zed/`](../.zed) — and a [Dev Container](../.devcontainer/DIRECTORY.md) with the full Linux toolchain preinstalled. Both editors drive the same CMake presets through [`scripts/configure.py`](../scripts/configure.py), so a build started from either editor lands in the same directory as one started from a terminal.

Every preset places its executables at the root of its build directory on every platform: `build/luma`, `build-debug/luma_dap`, and so on, with `.exe` on Windows (see [cmake/PRESETS.md](../cmake/PRESETS.md#build-directories-and-binaries)). The Luma extensions resolve `luma_lsp`, `luma`, and `luma_dap` from a bundled download or `PATH`; to use a local build instead, point the extension at one of these binaries in your **user** settings.

---

## 2 — Visual Studio Code

### Recommended Extensions

All are listed in `.vscode/extensions.json`:

- **C/C++ Extension Pack** (`ms-vscode.cpptools-extension-pack`): IntelliSense, debugging, CMake Tools integration.
- **Luma** (`D3V0N5H1R3.luma-language`): Syntax highlighting, LSP, and DAP for `.luma` files.
- **Ruff** (`charliermarsh.ruff`): Python linting and formatting for `scripts/`.
- **markdownlint** (`DavidAnson.vscode-markdownlint`): Markdown linting against `.markdownlint-cli2.jsonc`.
- **rust-analyzer** (`rust-lang.rust-analyzer`): Rust support and `rustfmt` formatting for the Zed extension crates.
- **PowerShell** (`ms-vscode.powershell`): PowerShell editing, formatting, and PSScriptAnalyzer diagnostics using `PSScriptAnalyzerSettings.psd1`.
- **ESLint** (`dbaeumer.vscode-eslint`) and **Prettier** (`esbenp.prettier-vscode`): Linting and formatting for the VS Code extension in `extensions/vscode/` (run `npm ci` there so both use the pinned versions).
- **GitHub Actions** (`github.vscode-github-actions`): Workflow authoring and validation.
- **GitHub Copilot** (`github.copilot`) and **Copilot Chat** (`github.copilot-chat`): AI code completion and chat.
- **Claude Code** (`anthropic.claude-code`): Anthropic's agentic coding assistant.
- **GitHub Pull Requests** (`github.vscode-pull-request-github`): PR review in the editor.

### Configuration

When you first open the project, CMake Tools prompts you to select a kit. Choose a C++20-compliant compiler. CMake Tools then configures automatically; if not, run **CMake: Configure** from the Command Palette.

The Luma extension resolves `luma_lsp`, `luma`, and `luma_dap` automatically (bundled download, then `PATH`). To use a locally built binary, set `luma.lsp.path`, `luma.path`, and `luma.dap.path` in your **user** settings.

### Building, Running, and Testing

- **Build:** Press `F7` or run **CMake: Build**. The **CMake: Build\*** tasks in `.vscode/tasks.json` call `python scripts/configure.py <preset> --if-needed --build`, which configures the preset only on first use or after `CMakePresets.json` changes, then builds. The **CMake: Configure\*** tasks always re-configure.
- **Run:** **Luma: Run Current File** and **Luma: Test Current File** run the open `.luma` file with the `default` preset's `build/luma`. For the C++ targets, set the launch target via the status bar, then `Ctrl+F5` (run) or `F5` (debug).
- **Test:** Use **CTest: Run All Tests** or the Test Explorer for C++ tests, and **Luma: Run All Test Suites** for the Luma feature tests.
- **Sanitizers and coverage:** The **(Sanitizers)** and **(Coverage)** tasks are Linux/macOS only — their presets are disabled on Windows, where the tasks stop with an explanation. Use WSL or the Dev Container instead.

### Debugging the C++ Interpreter

`.vscode/launch.json` ships pre-configured targets for the `debug` preset (`build-debug/`). Open the **Run and Debug** view (`Ctrl+Shift+D`) and select one of:

- **C++: Interpreter (MSVC / GDB / LLDB)** — Launch the interpreter under the debugger.
- **C++: REPL (MSVC / GDB / LLDB)** — Launch the REPL under the debugger.
- **C++: Language Server** / **C++: DAP Debugger** — Launch `luma_lsp` or `luma_dap` directly (they wait for protocol messages on stdin).
- **C++: Attach to Process (MSVC / GDB / LLDB)** — Attach to a running `luma_lsp`, `luma_dap`, or `luma`. To debug the servers as the editor drives them, set `luma.lsp.path` / `luma.dap.path` in your user settings to the `build-debug/` binary, open a `.luma` file, then attach.
- **Extension: VS Code Luma Extension** — Launch an Extension Development Host for `extensions/vscode` (run `npm ci` there first).

### Debugging Luma Programs

Set a breakpoint in a `.luma` file and use a `"type": "luma"` launch configuration (the workspace ships **Luma: Debug Current File**):

```json
{
    "type": "luma",
    "request": "launch",
    "name": "Debug Current File",
    "program": "${file}",
    "stopOnEntry": false
}
```

See the [Debugger](Luma_Debugger.md) document for the adapter's design.

### Formatting

The C/C++ extension formats C++ on save using the repository's `.clang-format`, pre-configured in `.vscode/settings.json`. It runs `clang-format` from your `PATH`, falling back to the copy bundled with the extension — whose version usually differs from the **clang-format 20** that CI enforces, so it can reformat code you did not touch. Install clang-format 20 on your `PATH`, or point `C_Cpp.clang_format_path` at it in your **user** settings (it is machine-specific, so the workspace leaves it unset). Check the version with `clang-format --version`. Typical clang-format 20 paths:

- **Windows** (LLVM 20 installer): `C:\Program Files\LLVM\bin\clang-format.exe`
- **Windows** (Visual Studio's **C++ Clang tools for Windows**, a newer version that formats this tree identically): `C:\Program Files\Microsoft Visual Studio\<version>\<edition>\VC\Tools\Llvm\x64\bin\clang-format.exe`
- **macOS** (Homebrew `llvm@20`): the output of `echo "$(brew --prefix llvm@20)/bin/clang-format"`
- **Linux** (Debian/Ubuntu `clang-format-20` package): `/usr/bin/clang-format-20`

The Dev Container already sets this. Python, Rust, TypeScript/JavaScript, and PowerShell files use Ruff, rust-analyzer (`rustfmt`), Prettier, and the PowerShell extension as their default formatters; format-on-save is enabled only for C++.

---

## 3 — Zed

### Installing the Luma Extension

Command Palette → **zed: extensions** → search **Luma** → **Install**. The extension downloads `luma_lsp` from GitHub Releases on first use. For a manual installation:

```bash
cp -r extensions/zed ~/.local/share/zed/extensions/luma
```

Once the language server is available, completions, hover, diagnostics, and code snippets work automatically.

### Building, Running, and Testing

Command Palette → **task: spawn** lists the workspace tasks in `.zed/tasks.json`. They mirror the VS Code tasks: **CMake: Configure\*** / **CMake: Build\*** call `python scripts/configure.py <preset>` (the build tasks add `--if-needed --build`), plus **CTest**, **Luma: Run All Test Suites**, **Luma: Run / Test Current File** (using `build/luma`), **Lint: All**, and **Format: All**.

### C++ Code Intelligence (clangd)

clangd finds `build/compile_commands.json` automatically on Linux and macOS. On Windows the `default` preset uses the Visual Studio generator, which writes no compilation database. Configure and build the `ninja` preset from a **Developer PowerShell for VS** (`cmake --preset ninja`, then `cmake --build --preset ninja` once so generated headers exist). Then copy `build-ninja/compile_commands.json` to the repository root, where clangd finds it (the file is git-ignored). Copy it again after adding or removing source files.

### Debugging the C++ Interpreter

`.zed/debug.json` ships CodeLLDB scenarios (Command Palette → **debugger: start**) for the interpreter, REPL, `luma_lsp`, and `luma_dap`, each in a **(Windows)** variant (`.exe`) and a **(Unix)** variant. Each scenario builds its preset first: `debug` (`build-debug/`), or `relwithdebinfo` (`build-relwithdebinfo/`) for the optimised interpreter (the `default` Release build has no debug symbols). To debug the servers as Zed drives them, put `build-debug/` first on your `PATH` (the extension resolves `luma_lsp` and `luma_dap` from `PATH` before downloading), or set `lsp.luma-lsp.binary.path` in your **user** settings. Then use the **Attach** tab of **debugger: start** to pick the running process.

### Debugging Luma Programs

Open a `.luma` file, set breakpoints, then Command Palette → **debugger: start** → select the **Luma** adapter and the **Launch** request. (The extension also registers a locator, so the ▶ run affordance on a `@main` function can start a debug session too.) Ensure `luma_dap` is on your `PATH`, or let the extension download it. See [extensions/zed/DIRECTORY.md](../extensions/zed/DIRECTORY.md#debugger-setup) for a `debug.json` example.

### Formatting

`.zed/settings.json` formats C++ on save with `clang-format` from your `PATH` (as an external formatter), not clangd's built-in formatter. Zed auto-downloads the latest clangd, whose formatter may differ from the **clang-format 20** that CI enforces. Workspace settings take precedence over user settings, so the command must resolve as plain `clang-format` (version 20), or saving a C++ file reports a formatter error. Check with `clang-format --version`:

- **Windows** (LLVM 20 installer): add `C:\Program Files\LLVM\bin` to `PATH`, or Visual Studio's `VC\Tools\Llvm\x64\bin` (its **C++ Clang tools for Windows** component, which formats this tree identically).
- **macOS** (Homebrew `llvm@20`, which is keg-only): add `$(brew --prefix llvm@20)/bin` to `PATH`.
- **Linux** (Debian/Ubuntu `clang-format-20` package, installed as `clang-format-20`): `sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-20 100`. The Dev Container already does this.

---

## 4 — Debugging the C++ Interpreter from a Terminal

Build the `debug` preset and launch the interpreter under a native debugger:

```bash
cmake --preset debug
cmake --build --preset debug

# GDB
gdb --args build-debug/luma examples/language-features/hello.luma

# LLDB
lldb -- build-debug/luma examples/language-features/hello.luma
```

On Windows, open `build-debug\luma.exe` in Visual Studio (`devenv /debugexe build-debug\luma.exe <file>.luma`) or use the editor launch configurations above. For debugging **Luma programs** rather than the interpreter, use the Debug Adapter Protocol integration of either editor.

---

## 5 — Keeping Editor Configurations in Sync

| Surface                      | VS Code               | Zed               |
| ---------------------------- | --------------------- | ----------------- |
| Tasks (configure/build/test) | `.vscode/tasks.json`  | `.zed/tasks.json` |
| Run / test current file      | `.vscode/tasks.json`  | `.zed/tasks.json` |
| C++ debug launch             | `.vscode/launch.json` | `.zed/debug.json` |

Both editors drive the same CMake presets (`default`, `debug`, `relwithdebinfo`, `sanitize`, `coverage`). When you add or rename a preset, update both editor configurations together.

---

## 6 — See Also

- [Contributing](../CONTRIBUTING.md) — the contribution workflow this setup supports.
- [Debugger](Luma_Debugger.md) — design of the debug adapter that both editors launch for Luma programs.
- [Language Server](Luma_Language_Server.md) — design of the language server both editors start.
- [Syntax Highlighting](Luma_Syntax_Highlighting.md) — the editor grammars and extension structure.
- [Installation Guide](Luma_Installation_Guide.md) — installing Luma and its editor extensions as a user rather than a contributor.
