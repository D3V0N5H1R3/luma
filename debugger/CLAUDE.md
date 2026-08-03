# Luma — Claude Code guidance for `debugger/`

<!-- Nested CLAUDE.md: Claude Code auto-loads this when it works on files under
debugger/. It stands in for the applyTo-scoped guides that VS Code injects but
Claude Code ignores. The guides are named by repo-root-relative path for Claude
to read on demand (robust however Claude Code resolves @import paths). Keep them
aligned with the applyTo globs in instructions/DIRECTORY.md and the mapping in
../CLAUDE.md. -->

`debugger/` is the C++ Debug Adapter Protocol (DAP) implementation (`luma_dap`).
Before non-trivial work here, read the matching guides:

- `instructions/cpp.instructions.md` — C++ naming, const-correctness, RAII, modern idioms.
- `instructions/software-architecture.instructions.md` — simplicity, modularity, separation of concerns, encapsulation.
- `instructions/learnings-dap.instructions.md` — debug adapter architecture, thread safety, expression evaluation, and breakpoint semantics.

When editing this subtree's `CMakeLists.txt`, also follow
`instructions/cmake.instructions.md` (target-based configuration) and
`instructions/build.instructions.md` (presets, sanitizers, coverage, fuzzing).
