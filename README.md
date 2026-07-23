# Luma

A statically typed, expression-oriented programming language designed for beginners — as easy as Python, as safe as Rust.

[![CI](https://github.com/d3v0n5h1r3/luma/actions/workflows/ci.yml/badge.svg)](https://github.com/d3v0n5h1r3/luma/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/d3v0n5h1r3/luma/branch/main/graph/badge.svg)](https://codecov.io/gh/d3v0n5h1r3/luma)

## Project Status

Luma is currently in **alpha**. The language, interpreter, standard library, and tooling are all feature-complete, but Luma has not yet reached a stable 1.0 release — interfaces and behaviour may still change, and it has not been battle-tested in production.

| Component         | Status                    | Notes                                                                                                                                                     |
| ----------------- | ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Language & Stdlib | ✅ Feature-Complete | 39 standard library modules including GraphicalUi, plus the Solaris beginner-first GUI surface built on it — with commands, subscriptions, components, routing, and accessibility. See [User Manual][manual] and [Standard Library Reference][stdlib]. |
| Interpreter       | ✅ Feature-Complete | Bytecode compiler and stack-based VM. See [Architecture][arch] for details.                                                                               |
| REPL              | ✅ Feature-Complete | Includes history, multi-line input, and file loading.                                                                                                     |
| Debugger (DAP)    | ✅ Feature-Complete | Supports breakpoints, stepping, and variable inspection.                                                                                                  |
| Language Server   | ✅ Feature-Complete | 27 LSP features. See [Language Server][lsp] for details.                                                                                                  |

[manual]: documents/Luma_User_Manual.md
[stdlib]: documents/Luma_Standard_Library_Reference.md
[arch]: documents/Luma_Software_Architecture.md
[lsp]: language-server/README.md

## Table of Contents

1. [A Taste of Luma](#a-taste-of-luma)
2. [Quick Start](#quick-start)
3. [Prerequisites](#prerequisites)
4. [Installation](#installation)
5. [Usage](#usage)
6. [Documentation](#documentation)
7. [Project Structure](#project-structure)
8. [Debugging](#debugging-luma-programs)
9. [Development](#development)
10. [Troubleshooting](#troubleshooting)
11. [License](#license)

## A Taste of Luma

```luma
record Book {
    string title,
    number price
}

function string describe(Book book) {
    return "${book.title} — ${String.format_number(book.price, 2)}"
}

@main
function void main() {
    array<Book> catalogue = [
        Book { title = "Luma Basics", price = 19.99 },
        Book { title = "Advanced Luma", price = 34.50 }
    ]

    array<string> lines = catalogue
        |> Array.map(describe)
        |> Result.unwrap_or([])

    for line in lines {
        print(line)
    }
}
```

Static types catch mistakes before a program runs, while immutable-by-default
variables, string interpolation, and the `|>` pipe operator keep everyday code
concise. Browse [`examples/`](examples/) for many more runnable programs, from
beginner snippets to complete applications.

## Quick Start

```bash
# Configure and build (Release) using CMake presets
cmake --preset default
cmake --build --preset default

# Run a program
build/luma examples/language-features/hello.luma

# Start the REPL
build/luma
```

> **Note:** On Windows, the binary is at `build\Release\luma.exe`.

## Prerequisites

| Platform | Compiler           | Build System |
| -------- | ------------------ | ------------ |
| Linux    | GCC 13 or later    | CMake 3.21+  |
| macOS    | Clang 15 or later  | CMake 3.21+  |
| Windows  | MSVC 2022 or later | CMake 3.21+  |

All compilers must support C++20. The interpreter, language server, and debugger
build from these tools alone — every third-party library is vendored.

> **Note:** These versions are hard minimums, not recommendations. Luma uses the
> C++20 library features `std::format` and `std::chrono::clock_cast`, which
> libstdc++ ships only from **GCC 13** — so GCC 12 and earlier fail to build. On
> distributions whose default `g++` is still GCC 12, notably Raspberry Pi OS and
> Debian 12 ("bookworm"), install GCC 13 or newer and select it when configuring
> (`-DCMAKE_CXX_COMPILER=g++-13`).

Two optional extras pull in more tooling:

- **`GraphicalUi` on Linux** needs the WebKitGTK development headers and
  `pkg-config` — for example `sudo apt-get install libwebkit2gtk-4.1-dev pkg-config`
  on Debian/Ubuntu. Without them the build still succeeds, but the `GraphicalUi`
  module is disabled (compiled as a stub); pass `-DLUMA_FEATURE_WEBVIEW=OFF` to
  disable it deliberately. Windows (WebView2) and macOS (WebKit) need no extra
  package.
- The Python helper scripts under `scripts/` (test runners, coverage, and the
  Git hook installer) need **Python 3.10 or later**.

## Installation

> **Tip:** To try Luma without installing a local toolchain, open the repository
> in [GitHub Codespaces](.devcontainer/README.md) (**Code → Codespaces → Create
> codespace**). The dev container ships the full C++20 toolchain and builds the
> interpreter automatically.

Luma currently ships as source. Clone the repository and build it with CMake. The
project provides [CMake presets](CMakePresets.json), so the recommended build is:

```bash
cmake --preset default
cmake --build --preset default
```

This is equivalent to running `cmake -B build -DCMAKE_BUILD_TYPE=Release` followed by
`cmake --build build --config Release --parallel`. Additional presets cover debug,
Ninja, sanitizer, coverage, and cross-compilation builds — see
[instructions/build.instructions.md](instructions/build.instructions.md).

Run the test suite to verify the build:

```bash
ctest --preset default
```

Binary locations:

- Linux and macOS: `build/luma`
- Windows: `build\Release\luma.exe`

## Usage

Run a Luma program:

```bash
build/luma examples/language-features/hello.luma
```

Type-check without running:

```bash
build/luma --check examples/language-features/hello.luma
```

Run one feature-test suite:

```bash
build/luma --test tests/features/language/arrays.luma
```

Start the REPL:

```bash
build/luma
```

List every command-line option:

```bash
build/luma --help
```

On Windows, replace `build/luma` with `build\Release\luma.exe` in the commands above.

## Documentation

| Document                             | Contents                                                  |
| ------------------------------------ | --------------------------------------------------------- |
| [User Manual][manual]                | Complete language reference and tutorial.                 |
| [Standard Library Reference][stdlib] | All 39 standard library modules and built-in functions.   |
| [Software Architecture][arch]        | Interpreter pipeline and module design.                   |
| [Performance Guide](documents/Luma_Performance_Guide.md) | Performance characteristics and optimisation advice.  |
| [Error Handling](documents/Luma_Error_Handling.md)       | Error categories and `result` / `optional` conventions. |
| [Debugger](documents/Luma_Debugger.md)                   | Debug Adapter Protocol design and usage.              |
| [Language Server][lsp]               | Language Server Protocol features and setup.              |

The [documents/](documents/README.md) directory indexes every architecture, reference,
and guide document. For contribution and security policies, see
[CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md).

## Project Structure

The root README keeps this overview intentionally high-level. For the detailed module inventory, see [documents/Luma_Software_Architecture.md](documents/Luma_Software_Architecture.md). For LSP-specific internals, see [language-server/README.md](language-server/README.md) and [documents/Luma_Language_Server.md](documents/Luma_Language_Server.md).

```text
luma/
├── benchmarks/        # Performance benchmarks written in Luma
├── core/              # Lexer, parser, type checker, compiler, VM, REPL, stdlib
├── debugger/          # Debug Adapter Protocol server and tests
├── documents/         # Architecture, manual, debugger, and design documents
├── examples/          # Example Luma programs and sample applications
├── extensions/        # Editor integrations and grammar fixtures
├── external/          # Vendored third-party libraries (mbedtls, miniz, webview, etc.)
├── fuzz/              # Optional fuzz targets and corpora
├── instructions/      # Coding and tooling guidelines for contributors
├── language-server/   # Language Server Protocol implementation and tests
├── scripts/           # Build and test helper scripts
├── shared/            # JSON, protocol, and stdlib metadata shared by tools
└── tests/             # C++ unit/integration tests and Luma feature suites
```

Additional indexes:

- [benchmarks/README.md](benchmarks/README.md) describes the performance benchmark suite.
- [documents/README.md](documents/README.md) indexes the architecture, reference, and guide documents.
- [examples/README.md](examples/README.md) explains the example categories.
- [extensions/tests/README.md](extensions/tests/README.md) documents the shared grammar fixture corpus.
- [external/README.md](external/README.md) inventories the vendored third-party libraries and GraphicalUi web assets.
- [fuzz/README.md](fuzz/README.md) covers the LibFuzzer fuzz targets and corpus management.
- [tests/README.md](tests/README.md) explains the C++ and Luma test suite layout.

## Debugging Luma Programs

Luma ships with a [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) (DAP) server (`luma_dap`) that integrates with VS Code and any other DAP-compatible editor.

### VS Code

Install the **Luma** extension from the marketplace. It bundles the language server and debugger. To start debugging:

1. Open a `.luma` file.
2. Set breakpoints by clicking the gutter.
3. Press **F5** (or **Run → Start Debugging**).

The extension provides a default `launch.json` configuration. You can customise it:

```jsonc
{
    "type": "luma",
    "request": "launch",
    "name": "Debug Luma",
    "program": "${file}",
    "stopOnEntry": false,
}
```

### Zed

The [Zed extension](extensions/zed/) provides Tree-sitter highlighting and LSP integration. Install it from the Zed extension gallery or see [extensions/zed/README.md](extensions/zed/README.md).

### Features

- **Breakpoints** — line breakpoints, conditional breakpoints, hit-count breakpoints.
- **Stepping** — step over, step into, step out, continue, pause.
- **Variables** — local, upvalue, and global variable inspection.
- **Watch expressions** — evaluate arbitrary Luma expressions while paused.
- **Call stack** — full stack trace with source locations.
- **Hover evaluation** — hover over variables in the editor to see values.

See [Luma_Debugger.md](documents/Luma_Debugger.md) for architecture details.

## Development

See [CONTRIBUTING.md](CONTRIBUTING.md) for branch naming, commit conventions, and the full development workflow.

## Troubleshooting

| Problem                                | Cause                                                   | Fix                                                |
| -------------------------------------- | ------------------------------------------------------- | -------------------------------------------------- |
| `'break' used outside of a loop`       | `break` or `continue` typed in the REPL or at top level | Use `break` / `continue` only inside a loop        |
| `'X' is not available in sandbox mode` | Function belongs to a module disabled by `--box`        | Remove `--box` or avoid OS-accessing modules       |
| `cannot assign to immutable variable`  | Variable declared without `mutable`                     | Use `mutable` for variables that need reassignment |
| `division by zero`                     | Dividing by zero with `/` or `%`                        | Guard against zero before dividing                 |
| `index N out of bounds`                | Array or string index outside valid range               | Check length before indexing                       |
| `no @main function found`              | Source file has no `@main`-annotated function           | Add `@main` above your entry-point function        |
| `socket limit reached`                 | More than 1,000 open sockets                            | Close unused sockets with `Socket.close`           |
| `task queue is full`                   | Too many pending `spawn` tasks (>100,000)               | `await` existing tasks before spawning more        |
| `unexpected character '&'` or `'\|'`   | Bare `&` or `\|` is not a Luma operator                 | Use `\|>` for pipe; Luma has no `&` or `\|`        |
| `unterminated string`                  | Missing closing `"` or `"""`                            | Close the string literal                           |
| REPL works but file execution fails    | REPL does not require `@main`; file mode does           | Add `@main` to your entry-point function           |

## License

See [LICENSE](LICENSE) for details.
