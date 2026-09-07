# Luma — Claude Code guidance for `core/`

<!-- Nested CLAUDE.md: Claude Code auto-loads this when it works on files under
core/. It stands in for the applyTo-scoped guides that VS Code injects but Claude
Code ignores. The guides are named by repo-root-relative path for Claude to read
on demand (robust however Claude Code resolves @import paths). Keep them aligned
with the applyTo globs in instructions/DIRECTORY.md and the mapping in
../CLAUDE.md. -->

`core/` is the C++ interpreter — the analysis front-end (lexer → resolver) and
the runtime back-end (compiler, VM, stdlib, concurrency). Before non-trivial work
here, read the matching guides:

- `instructions/cpp.instructions.md` — C++ naming, const-correctness, RAII, modern idioms.
- `instructions/software-architecture.instructions.md` — simplicity, modularity, separation of concerns, encapsulation.
- `instructions/learnings-compiler.instructions.md` — compiler, optimizer, bytecode, and the scratch-slot invariant (for `core/runtime/compiler/`).
- `instructions/learnings-vm.instructions.md` — VM architecture, dispatch table, and component decomposition (for `core/runtime/vm/`).
- `instructions/learnings-stdlib.instructions.md` — stdlib infrastructure, module registration, and common utilities (for `core/runtime/stdlib/`).

When editing this subtree's `CMakeLists.txt`, also follow
`instructions/cmake.instructions.md` (target-based configuration) and
`instructions/build.instructions.md` (presets, sanitizers, coverage, fuzzing).
