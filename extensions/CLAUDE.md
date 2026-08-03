# Luma — Claude Code guidance for `extensions/`

<!-- Nested CLAUDE.md: Claude Code auto-loads this when it works on files under
extensions/. It stands in for the applyTo-scoped guides that VS Code injects but
Claude Code ignores. The guides are named by repo-root-relative path for Claude to
read on demand (robust however Claude Code resolves @import paths). Keep them
aligned with the applyTo globs in instructions/DIRECTORY.md and the mapping in
../CLAUDE.md. -->

`extensions/` holds the editor extensions — the VS Code extension (TypeScript)
and the Zed extension (Rust). Before non-trivial work here, read the matching
guides:

- `instructions/typescript.instructions.md` — TypeScript type safety, async patterns, idioms.
- `instructions/rust.instructions.md` — Rust ownership, error handling, unsafe code, idioms.
- `instructions/learnings-extensions.instructions.md` — editor extensions, Tree-sitter queries, and highlight pitfalls.
