# Luma — Claude Code guidance for `tests/`

<!-- Nested CLAUDE.md: Claude Code auto-loads this when it works on files under
tests/. It stands in for the applyTo-scoped guides that VS Code injects but Claude
Code ignores. The guides are named by repo-root-relative path for Claude to read
on demand (robust however Claude Code resolves @import paths). Keep them aligned
with the applyTo globs in instructions/DIRECTORY.md and the mapping in
../CLAUDE.md. -->

`tests/` holds the C++ unit and integration tests and the Luma `@test` feature
tests. Before non-trivial work here, read the matching guides:

- `instructions/testing.instructions.md` — custom test framework, assertion macros, Luma feature tests.
- `instructions/cpp.instructions.md` — C++ naming, const-correctness, RAII, modern idioms.
- `instructions/luma.instructions.md` — Luma syntax, types, stdlib usage, testing patterns.

When editing this subtree's `CMakeLists.txt`, also follow
`instructions/cmake.instructions.md` (target-based configuration) and
`instructions/build.instructions.md` (presets, sanitizers, coverage, fuzzing).
