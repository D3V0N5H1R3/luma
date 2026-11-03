# Luma — Claude Code Guidance for `extensions/`

<!-- Nested CLAUDE.md: Claude Code auto-loads this when it works on files under
extensions/. It stands in for the applyTo-scoped guides that VS Code injects but
Claude Code ignores. The guides are named by repo-root-relative path for Claude to
read on demand (robust however Claude Code resolves @import paths). Keep them
aligned with the applyTo globs in instructions/DIRECTORY.md and the mapping in
../CLAUDE.md. -->

`extensions/` holds the editor extensions — the VS Code extension (TypeScript) and the Zed extension (Rust) — plus the cross-editor tooling around them: the Python code generators in `shared/`, the Tree-sitter grammar and JavaScript contract tests, the `.luma` test fixtures, and the Zed binary-download scripts. Before non-trivial work here, read the matching guides:

- `instructions/typescript.instructions.md` — TypeScript type safety, async patterns, idioms.
- `instructions/rust.instructions.md` — Rust ownership, error handling, unsafe code, idioms.
- `instructions/learnings-extensions.instructions.md` — editor extensions, Tree-sitter queries, and highlight pitfalls.
- `instructions/python.instructions.md` — Python type hints, error handling, idioms (for the `shared/` generators and checks).
- `instructions/javascript.instructions.md` — JavaScript modules, async patterns, idioms (for `grammar.js` and the `tests/` contract tests).
- `instructions/luma.instructions.md` — Luma syntax, types, stdlib usage (for the `tests/fixtures/` programs).
- `instructions/powershell.instructions.md` — PowerShell naming, pipeline patterns, modules (for `zed/scripts/Download-Binaries.ps1`).
- `instructions/shell.instructions.md` — shell portability, quoting, safe scripting (for `zed/scripts/download_binaries.sh`).
