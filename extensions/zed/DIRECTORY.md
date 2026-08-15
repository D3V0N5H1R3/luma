# Luma Language Support for Zed

Syntax highlighting, language configuration, and language server integration for the [Luma](https://github.com/d3v0n5h1r3/luma) programming language.

## Features

- Tree-sitter based syntax highlighting for all Luma language constructs.
- Language configuration: comment toggling (`#`), bracket matching, indentation rules.
- Language server integration via `luma_lsp` (completions, hover, diagnostics).
- Inlay hints (type hints, parameter hints) via LSP.
- 64 code snippets for common Luma patterns.
- File association for `.luma` files.
- Debug Adapter Protocol (DAP) support via `luma_dap`.
- Automatic LSP binary download from GitHub Releases.

## What Gets Highlighted

| Element              | Examples                                                                                                                      |
| -------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| Annotations          | `@main` `@test`                                                                                                               |
| Builtins             | `assert()` `failure()` `print()` `some()` `success()` `type_of()`                                                             |
| Comments             | `# line comment`                                                                                                              |
| Constants            | `false` `none` `true`                                                                                                         |
| Function names       | `function void main()`, `my_func()`                                                                                           |
| Keywords             | `await` `choice` `else` `for` `function` `if` `match` `record` `return` `spawn` `task_scope` `while`                          |
| Modifiers            | `borrow` `internal` `mutable` `unique`                                                                                        |
| Numbers              | `42` `3.14` `0xFF` `0b1010`                                                                                                   |
| Operators            | `+` `-` `*` `/` `==` `&&` `\|\|` `\|>` `??` `->`                                                                              |
| Properties           | `point.x`, `Person { name = "Alice" }`                                                                                        |
| Standard library     | `Array.map` `Math.pi` `String.length`                                                                                         |
| String interpolation | `"value is ${x + 1}"`                                                                                                         |
| Strings              | `"hello"` `"""multi-line"""`                                                                                                  |
| Type names           | `record Point`, `choice Shape`                                                                                                |
| Types                | `array` `boolean` `channel` `dictionary` `integer` `number` `optional` `result` `string` `task` `void` and more |
| Variables            | `integer count`, `for x in items`                                                                                             |

## Installation

### From the Zed Extension Gallery

Search for **Luma** in Zed's extension panel (`zed: extensions` command).

### As a Dev Extension

To run the extension from a local checkout, install it as a Zed _dev extension_:

1. Open the extensions page (`zed: extensions` command).
2. Click **Install Dev Extension** and select the `extensions/zed` directory.

Zed compiles and loads the extension immediately, and keeps it in sync as you edit the source. Dev extensions require [Rust installed via rustup](https://www.rust-lang.org/tools/install).

## Language Server Setup

The extension automatically downloads the `luma_lsp` binary from GitHub Releases on first use. If automatic download is unavailable, you can set it up manually.

### Configuration

The extension provides default settings for the language server. You can override them in your Zed `settings.json`:

```jsonc
{
  "lsp": {
    "luma-lsp": {
      "settings": {
        "luma": {
          "inlayHints": {
            "enabled": false  // disable inlay hints (default: true)
          }
        }
      }
    }
  }
}
```

| Setting                     | Type    | Default | Description                          |
| --------------------------- | ------- | ------- | ------------------------------------ |
| `luma.inlayHints.enabled`   | boolean | `true`  | Show inlay hints (inferred types).   |
| `luma.codeLens.enabled`     | boolean | `true`  | Show reference-count code lenses.    |
| `luma.diagnostics.onSave`   | boolean | `false` | Only report linter warnings on save. |

All three nest under `lsp.luma-lsp.settings.luma` in `settings.json`, as shown above.

### Automatic Download

No action required — the extension resolves the correct platform binary (macOS arm64/x64, Linux arm64/x64, Windows arm64/x64) and downloads it into the extension directory.

### Manual Binary Setup

#### Option 1: Download Script

Use the provided download script to fetch the binary:

```bash
cd extensions/zed
LUMA_VERSION=v0.7.0 ./scripts/download_binaries.sh
```

On Windows (PowerShell):

```powershell
cd extensions\zed
$env:LUMA_VERSION = "v0.7.0"
pwsh scripts\Download-Binaries.ps1
```

Set `LUMA_VERSION` to the desired release tag, or omit it to download the latest release. The binary is placed at `bin/luma_lsp`.

#### Option 2: Build from Source

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target luma_lsp --parallel
```

Then add the build output directory to your `PATH`, or copy the `luma_lsp` binary into the extension's `bin/` directory.

#### Option 3: Add to PATH

If `luma_lsp` is already on your `PATH`, the extension will find and use it automatically — no additional configuration needed.

## Keybindings

Zed extensions cannot register keybindings automatically. See [KEYBINDINGS.md](KEYBINDINGS.md) for suggested key bindings you can add to your `keymap.json`.

## Project Structure

```text
extensions/zed/
├── Cargo.toml              # Rust crate manifest (WASM extension)
├── extension.toml          # Extension metadata, LSP, snippet, and debugger config
├── scripts/
│   ├── download_binaries.sh   # Fetch pre-built luma_lsp binary (Unix)
│   └── Download-Binaries.ps1  # Fetch pre-built luma_lsp binary (Windows)
├── grammars/
│   └── tree-sitter-luma/   # Tree-sitter grammar source
├── languages/
│   └── luma/
│       ├── brackets.scm    # Bracket pair queries
│       ├── config.toml     # Language configuration
│       ├── folds.scm       # Code folding queries
│       ├── highlights.scm  # Syntax highlighting queries
│       ├── indents.scm     # Auto-indentation queries
│       ├── injections.scm  # Language injection queries
│       ├── outline.scm     # Symbol outline queries
│       ├── overrides.scm   # Scope override queries
│       ├── redactions.scm  # Sensitive data redaction queries
│       ├── runnables.scm   # Runnable detection queries
│       └── textobjects.scm # Vim text-object queries
└── src/
    ├── download.rs         # Binary download and checksum verification
    ├── generated/          # Code generated from extensions/shared (do not edit)
    ├── labels.rs           # LSP completion/symbol → CodeLabel rendering
    ├── lib.rs              # Extension entry point (LSP/DAP launcher)
    ├── tests.rs            # Unit tests
    └── util.rs             # Shared utilities (JSON merge helper)
```

Snippets are sourced from the shared `extensions/shared/snippets/luma.json` (64 snippets) via the `snippets` field in `extension.toml`.

## Building from Source

Installing as a [dev extension](#as-a-dev-extension) compiles the extension for you. To build the WebAssembly module manually, install [Rust via rustup](https://www.rust-lang.org/tools/install) and add the WebAssembly target once:

```bash
cd extensions/zed

# One-time: install the WebAssembly target (and the components CI lints with)
rustup target add wasm32-wasip1
rustup component add clippy rustfmt

cargo build --release --target wasm32-wasip1
```

## Tree-sitter Grammar

Syntax highlighting is powered by the vendored `tree-sitter-luma` grammar in `grammars/tree-sitter-luma`. The extension manifest points Zed at that local grammar source instead of relying on an external repository.
