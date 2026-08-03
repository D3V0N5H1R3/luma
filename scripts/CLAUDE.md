# Luma — Claude Code guidance for `scripts/`

<!-- Nested CLAUDE.md: Claude Code auto-loads this when it works on files under
scripts/. It stands in for the applyTo-scoped guides that VS Code injects but
Claude Code ignores. The guides are named by repo-root-relative path for Claude to
read on demand (robust however Claude Code resolves @import paths). Keep them
aligned with the applyTo globs in instructions/DIRECTORY.md and the mapping in
../CLAUDE.md. -->

`scripts/` holds the developer tooling scripts — predominantly Python, with
PowerShell and shell helpers. Before non-trivial work here, read the matching
guides:

- `instructions/python.instructions.md` — Python type hints, error handling, idioms.
- `instructions/powershell.instructions.md` — PowerShell naming, pipeline patterns, modules.
- `instructions/shell.instructions.md` — shell portability, quoting, safe scripting.
- `instructions/learnings-build.instructions.md` — build system, CI, and static-analysis pitfalls (also scoped to `scripts/`).
