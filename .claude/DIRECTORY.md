# Claude Code configuration

This directory brings **Claude Code** to parity with the project's mature
**GitHub Copilot / VS Code** setup. Claude Code reads none of the `.github/`
agent, prompt, or hook files — those use Copilot and VS Code formats — so each
capability is re-expressed here in the format Claude Code understands, mirroring
a `.github/` counterpart from a single source of truth.

The root [`CLAUDE.md`](../CLAUDE.md) bridges the shared instruction set (it
imports `.github/copilot-instructions.md`); this directory adds the executable
tooling on top of it.

## Layout

| Path                     | Purpose                                          | Mirrors                     |
| ------------------------ | ------------------------------------------------ | --------------------------- |
| `settings.json`          | Project hooks (PreToolUse / PostToolUse)         | `.github/hooks/*.json`      |
| `agents/*.md`            | Subagent definitions (`plan`, `implement`, …)    | `.github/agents/*.agent.md` |
| `commands/*.md`          | Slash commands (`/bug-fix`, `/build-and-test`, …) | `.github/prompts/*.prompt.md` |
| `settings.local.json`    | Personal overrides — git-ignored, never committed | —                           |

## Hooks (`settings.json`)

Two hooks wrap the shared, runtime-agnostic Python scripts in
`scripts/agent-hooks/` — the same scripts the Copilot hooks under `.github/hooks/`
call:

- **PreToolUse** → `protect_vendored_paths.py` (timeout 10 s). A safety guard
  that **denies** edits to `external/` and other vendored paths. A blocked edit
  is an intentional stop, not a failure. The script signals the decision through
  its JSON stdout and always exits 0.
- **PostToolUse** → `format_cpp_on_edit.py` (timeout 30 s). Auto-formats `.cpp`
  and `.hpp` files with `clang-format` after an edit. Fail-open and
  non-blocking; it never fails a tool call.

Both hooks use the matcher `Write|Edit|MultiEdit|NotebookEdit` — the Claude Code
tools that create or modify files, which are the only tools these scripts act on.

### Cross-platform command

Claude Code's hook format allows a single `command` string per hook, whereas the
Copilot format supplies separate `bash` and `powershell` commands. To stay
cross-platform from one string, each command is:

```text
python3 scripts/agent-hooks/<script>.py || python scripts/agent-hooks/<script>.py
```

The `||` fallback fires only when the first interpreter fails to launch (for
example, when `python3` is not on `PATH`, as is common on Windows), so `python`
is tried next. Because a failed launch never consumes stdin and the scripts
always exit 0, the fallback never causes a double run. Paths are relative;
Claude Code runs hooks from the project root.

If neither `python3` nor `python` resolves on your machine, override the commands
in a git-ignored `settings.local.json` rather than editing this file.

## Subagents (`agents/`)

Ports of the five `.github/agents/*.agent.md` roles — `plan`, `implement`,
`review`, `docs`, and `test` — into Claude subagent format. The VS Code tool
categories are translated to Claude tool names (for example `search` →
`Grep, Glob`, `edit` → `Edit, Write, MultiEdit`, `execute` → `Bash`), preserving
each role's boundaries: `review` stays read-only, `docs` cannot run the build,
and `plan` cannot edit source.

Claude has no `handoffs` frontmatter, so the handoff intent (plan → implement →
test / review) lives in the agent prose instead; agents that carry the `Task`
tool can delegate directly.

Launch a subagent with the Task tool, or manage them with `/agents`.

## Slash commands (`commands/`)

One thin wrapper per `.github/prompts/*.prompt.md` (27 in total). Each wrapper
carries the source prompt's `description` and `argument-hint` verbatim, then
instructs the agent to read and follow that prompt file with `$ARGUMENTS`
substituted in. The workflow itself is never duplicated — it stays defined once
in `.github/prompts/`.

Run one by typing `/` and picking the command by name (for example `/bug-fix`,
`/code-review`, `/refactor`), supplying its argument where the prompt takes one.

## Keeping the layers in sync

Because each file here mirrors a `.github/` counterpart, changes must be made in
both places:

- Add, rename, or remove a prompt → update the matching `commands/*.md` wrapper
  (regenerate from the prompt frontmatter) and the prompt index in
  [`.github/prompts/DIRECTORY.md`](../.github/prompts/DIRECTORY.md).
- Change an agent's role, tools, or boundaries → update both the
  `.github/agents/*.agent.md` source and its `agents/*.md` port.
- Change a hook script's contract or registration → update both
  `.github/hooks/*.json` and `settings.json`.
