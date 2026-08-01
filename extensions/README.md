# Extensions

Editor extensions for the Luma programming language — providing syntax highlighting, Language Server Protocol (LSP) support, Debug Adapter Protocol (DAP) debugging, and developer tooling for `.luma` files.

## Directory Structure

| Directory / File                               | Purpose                                                                                     |
| ---------------------------------------------- | ------------------------------------------------------------------------------------------- |
| [`vscode/`](vscode/)                           | VS Code extension (TypeScript) — TextMate grammar, LSP client, DAP client, playground.      |
| [`zed/`](zed/)                                 | Zed extension (Rust) — Tree-sitter grammar, LSP integration, DAP debugging, snippets.       |
| [`shared/`](shared/)                           | Cross-editor resources: code generators, platform map, snippets, queries, download logic.    |
| [`tests/`](tests/)                             | Cross-editor integration tests (fixture parsing, snippet validation, download verification). |

## Root-Level Files

### [`BINARY_ASSETS.md`](BINARY_ASSETS.md)

Documents the naming convention for GitHub release assets (`{binary}-{os}-{arch}.{ext}`). The machine-readable source of truth is [`shared/platform-map.json`](shared/platform-map.json); this file is the human-readable reference for how the six platform suffixes map to OS and architecture combinations.

### [`FEATURE_PARITY.md`](FEATURE_PARITY.md)

Tracks feature availability across editor extensions in a comparison matrix. Shows which capabilities each extension supports (syntax highlighting, LSP, DAP, code lens, playground, debug visualiser, etc.) and notes where one extension lags behind the other — useful for planning work to close gaps.

### [`CLAUDE.md`](CLAUDE.md)

Claude Code context file (auto-loaded when working on files under `extensions/`). Points to the TypeScript and Rust coding instruction files so the AI assistant applies the correct conventions for each extension's language.

## Design Principles

- **Shared source of truth.** Platform mappings, snippets, keybindings, built-in type lists, configuration schemas, and download constants are defined once in `shared/` as JSON data files. Python generators in `shared/` produce the editor-specific code (TypeScript for VS Code, Rust for Zed) so the two extensions never drift.
- **Independent release cycles.** Each extension is versioned and released independently of the interpreter (tag prefixes `vscode-v*.*.*` and `zed-v*.*.*`), so a language-server improvement doesn't force an extension update.
- **Feature parity as a goal.** Both extensions target the same baseline feature set. `FEATURE_PARITY.md` documents the current state and any VS Code-only features that Zed does not yet support (code lens, playground, debug visualiser).

## Related Documentation

- [Luma_Syntax_Highlighting.md](../documents/Luma_Syntax_Highlighting.md) — Grammar design and editor extension architecture.
- [Luma_Language_Server.md](../documents/Luma_Language_Server.md) — LSP server that both extensions connect to.
- [Luma_Debugger.md](../documents/Luma_Debugger.md) — DAP adapter that both extensions launch.
- [CONTRIBUTING.md](../CONTRIBUTING.md) — Editor integration setup for contributors.
