## Explore Editing Features

Once the language server is running, every `.luma` file gains rich, IDE-quality editing support powered by `luma_lsp`.

### As you type

- **Completions** — suggestions for standard library modules (`Array`, `String`, `Math`, …), your own functions and variables, and keywords. Press `Ctrl+Space` (`Cmd+I` on macOS) to trigger them manually.
- **Signature help** — parameter hints appear automatically inside a function call's parentheses.
- **Hover** — rest the pointer over any symbol to see its type, signature, and documentation.
- **Inlay hints** — inferred types are shown inline beside variables. Toggle them with the `luma.inlayHints.enabled` setting.

### Navigating code

- **Go to Definition** (`F12`) — jump to where a symbol is declared. **Go to Type Definition** and **Go to Implementation** are available from the right-click menu too.
- **Find All References** (`Shift+F12`) — list every usage of a symbol.
- **Go to Symbol in Editor** (`Ctrl+Shift+O` / `Cmd+Shift+O`) — jump to any function, record, or choice type in the file.
- **Code Lens** — reference counts appear above functions and types; click one to see its references. Toggle with `luma.codeLens.enabled`.

### Refactoring and fixes

- **Rename Symbol** (`F2`) — rename a symbol and every reference to it at once.
- **Quick Fix** (`Ctrl+.` / `Cmd+.`) — code actions such as adding a missing `mutable` modifier, inserting a missing `include`, or removing an unused variable.
- **Format Document** (`Shift+Alt+F` / `Shift+Option+F`) — reformat the whole file, or select a range and format just that.

Syntax and type errors are reported as you type; linter warnings appear too (or only on save, if you enable `luma.diagnostics.onSave`). Everything also shows in the **Problems** panel.
