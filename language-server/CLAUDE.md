# Luma — Claude Code guidance for `language-server/`

<!-- Nested CLAUDE.md: Claude Code auto-loads this when it works on files under
language-server/. It stands in for the applyTo-scoped guides that VS Code injects
but Claude Code ignores. The guides are named by repo-root-relative path for
Claude to read on demand (robust however Claude Code resolves @import paths). Keep
them aligned with the applyTo globs in instructions/README.md and the mapping in
../CLAUDE.md. -->

`language-server/` is the C++ Language Server Protocol (LSP) implementation
(`luma_lsp`). Before non-trivial work here, read the matching guides:

- `instructions/cpp.instructions.md` — C++ naming, const-correctness, RAII, modern idioms.
- `instructions/software-architecture.instructions.md` — simplicity, modularity, separation of concerns, encapsulation.

When editing this subtree's `CMakeLists.txt`, also follow
`instructions/cmake.instructions.md` (target-based configuration) and
`instructions/build.instructions.md` (presets, sanitizers, coverage, fuzzing).
