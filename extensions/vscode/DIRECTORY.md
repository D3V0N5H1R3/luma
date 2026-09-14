# Luma Language Support for Visual Studio Code

Full-featured editor support for the [Luma](https://github.com/d3v0n5h1r3/luma) programming language — syntax highlighting, language server integration, testing, and more.

## Features

### Language Server (LSP)

The extension automatically downloads and manages the Luma language server (`luma_lsp`), providing:

| Capability            | Description                                                 |
| --------------------- | ----------------------------------------------------------- |
| Diagnostics           | Real-time syntax and type errors as you type                |
| Hover                 | Type information, function signatures, and module docs      |
| Completions           | Standard library modules, user symbols, and keywords        |
| Signature help        | Parameter hints when typing function arguments              |
| Go to definition      | Jump to variable, function, record, and choice declarations |
| Go to type definition | Jump to the type of a variable or expression                |
| Go to implementation  | Jump to interface implementations                           |
| Find references       | Locate all usages of a symbol                               |
| Rename                | Rename a symbol and all its references                      |
| Document symbols      | Outline view of functions, records, and choice types        |
| Semantic highlighting | Rich, context-aware syntax colouring                        |
| Code actions          | Quick fixes (add `mutable`, missing includes, unused vars)  |
| Folding               | Code folding for blocks, declarations, and comments         |
| Document formatting   | Format an entire document                                   |
| Document links        | Clickable `include` paths                                   |

### Syntax Highlighting

TextMate grammar with semantic token support for all Luma constructs:

| Element              | Examples                                                                                             |
| -------------------- | ---------------------------------------------------------------------------------------------------- |
| Annotations          | `@main` `@test`                                                                                      |
| Builtins             | `assert()` `print()` `success()` `type_of()`                                                         |
| Comments             | `# line comment`                                                                                     |
| Constants            | `false` `none` `true`                                                                                |
| Function names       | `function void main()`, `my_func()`                                                                  |
| Keywords             | `await` `choice` `else` `for` `function` `if` `match` `record` `return` `spawn` `task_scope` `while` |
| Modifiers            | `borrow` `internal` `mutable` `unique`                                                               |
| Numbers              | `42` `3.14` `0xFF` `0b1010`                                                                          |
| Operators            | `+` `-` `*` `/` `==` `!=` `<` `>` `&&` `\|\|` `\|>` `!>` `..`                                        |
| Properties           | `point.x`, `Person { name = "Alice" }`                                                               |
| Standard library     | `Array.map` `Math.pi` `String.length`                                                                |
| String interpolation | `"value is ${x + 1}"`                                                                                |
| Strings              | `"hello"` `"""multi-line"""`                                                                         |
| Type names           | `record Point`, `choice Shape`                                                                       |
| Types                | `array` `boolean` `integer` `number` `optional` `result` `string`                                    |
| Variables            | `integer count`, `for x in items`                                                                    |

### Testing

- **Run tests from the editor** — the `Luma: Run Tests in Current File` command (and the editor title-bar Test button) run `@test` functions via `luma --test`, with results routed through the `$luma-test` problem matcher.
- **Task integration** — the predefined `luma` test task discovers `@main` / `@test` files across the workspace.

### Debugging

Powered by the Luma debug adapter (`luma_dap`), which the extension downloads and manages automatically:

- **Breakpoints, stepping, and inspection** — set breakpoints and step over / into / out, with call stack, variables, and watch expressions.
- **Launch configuration** — press `F5` with a `.luma` file open, or add a `luma` configuration with these attributes:

| Attribute     | Default              | Description                                                      |
| ------------- | -------------------- | ---------------------------------------------------------------- |
| `program`     | `${file}`            | Path to the Luma program to debug.                               |
| `stopOnEntry` | `false`              | Pause on the first executable line.                              |
| `args`        | `[]`                 | Arguments passed to the program (via `Process.get_arguments()`). |
| `cwd`         | `${workspaceFolder}` | Working directory for the debugged program.                      |

### Additional Features

- **Run / Test commands** — play button in the editor title bar, or via the Command Palette (`Luma: Run Current File`, `Luma: Run Tests in Current File`).
- **Task provider** — predefined `luma` tasks (`run`, `test`, `check`) with problem matchers for error navigation.
- **Multi-root workspace support** — one LSP client per workspace folder.
- **Snippets** — 64 snippets for common patterns (`@main`, `@test`, `function`, `record`, `match`, `for`, `try`, `pipe`, etc.).
- **Themes** — two bundled colour themes, **Luma Dark** and **Luma Light**, tuned to the grammar's token scopes so Luma constructs are clearly distinguished. Select one from **Preferences: Color Theme** in the Command Palette.
- **File icons** — custom `.luma` file icons for light and dark themes.
- **Markdown code blocks** — syntax highlighting in ` ```luma ` fenced blocks.
- **Workspace trust** — LSP disabled in restricted mode; syntax highlighting and snippets still work.
- **Auto-update** — checks for new `luma_lsp` and `luma_dap` releases on activation (configurable via `luma.lsp.autoUpdate`).

## Commands

All commands are available from the Command Palette under the **Luma** category.

| Command                                 | Keybinding                 | Description                                           |
| --------------------------------------- | -------------------------- | ----------------------------------------------------- |
| Luma: Run Current File                  | `Ctrl+Alt+R` / `Cmd+Alt+R` | Run the active `.luma` file.                          |
| Luma: Run Tests in Current File         | `Ctrl+Alt+T` / `Cmd+Alt+T` | Discover and run `@test` functions in the file.       |
| Luma: Restart Language Server           | —                          | Restart the `luma_lsp` language server.               |
| Luma: Show Language Server Output       | —                          | Reveal the language server output channel.            |
| Luma: Check for Language Server Updates | —                          | Manually check for `luma_lsp` and `luma_dap` updates. |

## Configuration

| Setting                         | Default   | Description                                                               |
| ------------------------------- | --------- | ------------------------------------------------------------------------- |
| `luma.lsp.path`                 | `""`      | Path to the `luma_lsp` binary; auto-downloaded or found on `PATH`.        |
| `luma.lsp.autoUpdate`           | `true`    | Automatically check for `luma_lsp` and `luma_dap` updates on activation.  |
| `luma.path`                     | `""`      | Path to the `luma` interpreter binary; found on `PATH` if empty.          |
| `luma.dap.path`                 | `""`      | Path to the `luma_dap` debug adapter; auto-downloaded or found on `PATH`. |
| `luma.diagnostics.onSave`       | `false`   | Only report linter warnings on save (errors always show immediately).     |

> **Note:** All path settings also accept the `${workspaceFolder}` and `${workspaceFolder:name}` variables.

## Requirements

- Visual Studio Code 1.120 or later.
- The `luma` interpreter, for running and testing programs. The `luma_lsp` language server and `luma_dap` debug adapter download automatically on first use; alternatively, [build them from source](https://github.com/d3v0n5h1r3/luma#build-and-test) and set their paths in the settings above.

## Installation

Install from the Visual Studio Marketplace, or install the `.vsix` file directly:

```bash
code --install-extension luma-language-0.13.0.vsix
```

## Building from Source

Requires **Node.js 20 or later** (CI builds with Node 22).

```bash
cd extensions/vscode
npm install
npm run package
```

This produces `luma-language-0.13.0.vsix` in the current directory.
