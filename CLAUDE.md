# Luma — Claude Code Instructions

<!--
Bridge file for Claude Code. Claude Code reads CLAUDE.md, not
.github/copilot-instructions.md, so the import below pulls in the project's
canonical agent guidance — the same file used by VS Code Copilot, the Copilot
coding agent, and Zed's agent.

Keep all shared project guidance in .github/copilot-instructions.md, NOT here,
so every tool stays in sync from a single source. Claude-specific notes go
below the import as visible Markdown; block-level HTML comments like this one
are stripped from Claude's context, so anything Claude must act on cannot live
in here.

The path-specific guides in instructions/*.instructions.md are scoped by
applyTo globs that VS Code honours but Claude Code ignores. To close that gap,
the always-applicable learnings guide (applyTo **/*) is imported directly below,
and each primary source subtree carries a nested CLAUDE.md that names the guides
whose globs match its files, by repo-root-relative path for Claude to read on
demand — Claude Code loads a nested CLAUDE.md automatically when it works on files
in that directory. The section below the import documents the full mapping and
still lists every guide (including the subtrees without a nested file and the
three no-applyTo guides) to read on demand.

Beyond instructions, the .claude/ directory carries Claude-native hooks
(settings.json), subagents (agents/), and slash commands (commands/) that mirror
the .github/ hooks, agents, and prompts. Keep each pair in sync; the map and the
sync rules live in .claude/DIRECTORY.md.
-->

@.github/copilot-instructions.md

<!-- Always-on: the learnings guide's applyTo is **/* (every file), so importing
it here matches how VS Code auto-loads it. Path-scoped guides load via the nested
CLAUDE.md files in the source subtrees; see "Working with this repo" below. -->

@instructions/learnings.instructions.md

## Claude-native tooling in this repo

Claude Code loads none of the `.github/` agent, prompt, or hook files (those are
Copilot/VS Code formats). The `.claude/` directory provides the Claude-native
equivalents, each mirroring a `.github/` counterpart from a single source of
truth. See [`.claude/DIRECTORY.md`](.claude/DIRECTORY.md) for the full map and the
rules for keeping the two layers in sync.

- **Hooks** (`.claude/settings.json`): a **PreToolUse** guard blocks edits to
  `external/` and other vendored paths — treat a denied edit as an intentional
  safety stop, not a glitch — and a **PostToolUse** hook auto-formats C++ files
  after you edit them. Both delegate to the shared, runtime-agnostic scripts in
  `scripts/agent-hooks/`.
- **Subagents** (`.claude/agents/`): `plan`, `implement`, `review`, `docs`, and
  `test` — a port of the `.github/agents/*.agent.md` roles. Launch them with the
  Task tool or manage them with `/agents`.
- **Slash commands** (`.claude/commands/`): 28 workflow commands (`/bug-fix`,
  `/build-and-test`, `/code-review`, …) that each run the matching
  `.github/prompts/*.prompt.md` with your argument, so the workflow is defined
  once.

## Working with this repo in Claude Code

Most of these guides now load automatically. The always-applicable
[`learnings.instructions.md`](instructions/learnings.instructions.md) is imported
at the top of this file, and each primary source subtree — `core/`, `shared/`,
`language-server/`, `debugger/`, `tests/`, `examples/`, `extensions/`, `scripts/`,
and `documents/` — carries a nested `CLAUDE.md` that imports the guides whose
`applyTo` globs match its files, which Claude Code pulls in automatically when you
work in that directory. The list below is the full mapping (the source of truth
for what each nested file imports) and still covers the subtrees without a nested
file plus the three no-`applyTo` guides (`git`, `github-actions-recipes`,
`ux-design`), which you should open on demand. The glob each one targets is shown
for reference:

**Cross-cutting:**

- `instructions/learnings.instructions.md` — accumulated pitfalls; check for any change (`**/*`).
- `instructions/software-architecture.instructions.md` — simplicity, modularity, separation of concerns, encapsulation (`{core,shared,language-server,debugger}/**`).
- `instructions/git.instructions.md` — commit message conventions, branch naming, merge strategy, safe workflows (no `applyTo`; referenced manually).
- `instructions/ux-design.instructions.md` — UX, usability, and graphic design principles: visual hierarchy, Gestalt grouping, colour, typography, spacing, feedback, accessibility (no `applyTo`; referenced manually).

**Domain learnings (path-scoped pitfalls, split out of `learnings.instructions.md`):**

- `instructions/learnings-build.instructions.md` — build system, CI, static analysis, and C++ portability pitfalls (`{CMakeLists.txt,CMakePresets.json,cmake/**,.github/workflows/**,scripts/**}`).
- `instructions/learnings-compiler.instructions.md` — compiler, optimizer, bytecode serialization, and the scratch-slot invariant (`core/runtime/compiler/**`).
- `instructions/learnings-vm.instructions.md` — VM architecture, dispatch table, and component decomposition (`core/runtime/vm/**`).
- `instructions/learnings-stdlib.instructions.md` — stdlib infrastructure, module registration, common utilities, and sandbox mode (`{core/runtime/stdlib/**,shared/stdlib/**}`).
- `instructions/learnings-lsp.instructions.md` — language server architecture, symbol resolution, and column/range pitfalls (`language-server/**`).
- `instructions/learnings-dap.instructions.md` — debug adapter architecture, thread safety, expression evaluation, and breakpoint semantics (`debugger/**`).
- `instructions/learnings-extensions.instructions.md` — VS Code and Zed editor extensions, Tree-sitter queries, and highlight pitfalls (`extensions/**`).

**Languages:**

- `instructions/cpp.instructions.md` — C++ sources: naming, const-correctness, RAII, modern idioms (`**/*.{cpp,hpp,h}`).
- `instructions/luma.instructions.md` — Luma sources: syntax, types, stdlib usage, testing patterns (`**/*.luma`).
- `instructions/rust.instructions.md` — Rust sources: ownership, error handling, unsafe code, idioms (`**/*.rs`).
- `instructions/typescript.instructions.md` — TypeScript sources: type safety, async patterns, idioms (`**/*.{ts,tsx}`).
- `instructions/javascript.instructions.md` — JavaScript sources: modules, async patterns, idioms (`**/*.{js,mjs,cjs}`).
- `instructions/python.instructions.md` — Python sources: type hints, error handling, idioms (`**/*.py`).
- `instructions/css.instructions.md` — CSS sources: BEM naming, specificity, custom properties, responsive design (`**/*.css`).
- `instructions/shell.instructions.md` — shell scripts: portability, quoting, safe scripting (`**/*.{sh,bash}`).
- `instructions/powershell.instructions.md` — PowerShell scripts: naming, pipeline patterns, modules (`**/*.{ps1,psm1,psd1}`).

**Build & CI:**

- `instructions/cmake.instructions.md` — CMake files: target-based configuration, dependency management (`**/{CMakeLists.txt,*.cmake}`).
- `instructions/build.instructions.md` — build presets, sanitizers, coverage, fuzzing, cross-platform (`**/{CMakeLists.txt,CMakePresets.json}`).
- `instructions/github-actions.instructions.md` — workflow triggers, permissions, caching, CI/CD security (`.github/workflows/**`).
- `instructions/github-actions-recipes.instructions.md` — copy-paste workflow recipes (C++/CMake CI, Docker, release, deployment, CodeQL) and debugging guidance (no `applyTo`; referenced manually).

**Tests & docs:**

- `instructions/testing.instructions.md` — custom test framework, assertion macros, Luma feature tests (`tests/**`).
- `instructions/markdown.instructions.md` — Markdown structure, formatting, linking, content (`**/*.md`).
- `instructions/readme.instructions.md` — README structure, section ordering, update safety (`{**/DIRECTORY.md,README.md}`).

The "Coding Conventions" section of the imported guidance covers most of these
with additional style detail. The canonical inventory — kept in sync with the
directory — is the table in [`instructions/DIRECTORY.md`](instructions/DIRECTORY.md);
treat that as the source of truth and re-list the folder if this summary ever
drifts.
