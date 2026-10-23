# Claude Code Configuration

This directory brings **Claude Code** to parity with the project's mature **GitHub Copilot / VS Code** setup. Claude Code reads none of the `.github/` agent, prompt, or hook files — those use Copilot and VS Code formats — so each capability is re-expressed here in the format Claude Code understands, mirroring a `.github/` counterpart from a single source of truth.

The root [`CLAUDE.md`](../CLAUDE.md) bridges the shared instruction set (it imports `.github/copilot-instructions.md`); this directory adds the executable tooling on top of it.

## Layout

| Path                  | Purpose                                           | Mirrors                       |
| --------------------- | ------------------------------------------------- | ----------------------------- |
| `settings.json`       | Project hooks (PreToolUse / PostToolUse)          | `.github/hooks/*.json`        |
| `agents/*.md`         | Subagent definitions (`plan`, `implement`, …)     | `.github/agents/*.agent.md`   |
| `commands/*.md`       | Slash commands (`/bug-fix`, `/build-and-test`, …) | `.github/prompts/*.prompt.md` |
| `settings.local.json` | Personal overrides — git-ignored, never committed | —                             |

## Hooks (`settings.json`)

Two hooks wrap the shared, runtime-agnostic Python scripts in `scripts/agent-hooks/` — the same scripts the Copilot hooks under `.github/hooks/` call:

- **PreToolUse** → `protect_vendored_paths.py` (timeout 10 s). A safety guard that **denies** edits to vendored code under `external/`. A blocked edit is an intentional stop, not a failure. The script signals the decision through its JSON stdout and always exits 0.
- **PostToolUse** → `format_cpp_on_edit.py` (timeout 30 s). Auto-formats `.cpp` and `.hpp` files with `clang-format` after an edit. Fail-open and non-blocking; it never fails a tool call.

Both hooks use the matcher `Write|Edit|MultiEdit|NotebookEdit` — the Claude Code tools that create or modify files, which are the only tools these scripts act on. `MultiEdit` is gone from current Claude Code releases; it stays in the matcher only so older versions remain covered.

### Cross-Platform Command

Claude Code's hook format allows a single `command` string per hook, whereas the Copilot format supplies separate `bash` and `powershell` commands. The same string must also run in two runtimes. Claude Code passes it to bash (Git Bash on Windows). The Copilot harness also reads `.claude/settings.json` and runs the string in PowerShell 7 on Windows. To stay cross-platform from one string, each command is:

```text
python3 "$(git rev-parse --show-toplevel)/scripts/agent-hooks/<script>.py" || python "$(git rev-parse --show-toplevel)/scripts/agent-hooks/<script>.py"
```

The `||` fallback fires whenever the first command exits non-zero. In practice, that happens when `python3` is missing or is the Windows Store alias stub, which prints a "Python was not found" message and exits non-zero without reading stdin. `python` is then tried with the untouched payload. The scripts themselves always exit 0, so a successful first run never triggers the fallback. If a script crashes, the fallback receives an already-consumed stdin and exits as a harmless no-op, which keeps both hooks fail-open.

Script paths are anchored to the repository root because Claude Code runs hooks in the session's *current* directory, which follows any `cd` Claude makes, not in the project root. With a relative path, a hook fired from a subdirectory cannot find its script. Python then exits 2, which Claude Code treats as a blocking `PreToolUse` error, so every edit would be denied.

The root comes from `git rev-parse --show-toplevel` rather than `${CLAUDE_PROJECT_DIR}`. `$(…)` means command substitution in bash and a subexpression inside a double-quoted string in PowerShell, so both shells resolve the same absolute path. `${CLAUDE_PROJECT_DIR}` breaks the Copilot harness. Under PowerShell it names an unset PowerShell variable rather than an environment variable, and `"…"/path` splits into two arguments. Copilot `PreToolUse` command hooks are fail-closed, so either failure denies every edit. The `||` operator needs PowerShell 7 or later; Windows PowerShell 5.1 cannot parse it.

If neither `python3` nor `python` resolves on your machine, override the commands in a git-ignored `settings.local.json` rather than editing this file.

## Subagents (`agents/`)

Ports of the five `.github/agents/*.agent.md` roles — `plan`, `implement`, `review`, `docs`, and `test` — into Claude subagent format. The VS Code tool categories are translated to Claude tool names (for example `search` → `Grep, Glob`, `edit` → `Edit, Write, MultiEdit`, `execute` → `Bash`), preserving each role's boundaries: `review` stays read-only, `docs` cannot run the build, and `plan` cannot edit source.

Claude has no `handoffs` frontmatter, so the handoff intent (plan → implement → test / review, review → implement / test, test → implement / docs) lives in each agent's prose instead. Agents that carry the `Task` tool (`plan`, `implement`, `docs`) can delegate directly; `review` and `test` end with a hand-off recommendation instead.

Claude Code v2.1.63 renamed the `Task` tool to `Agent`, and current releases still accept `Task` as an alias. The frontmatter keeps `Task` so older versions remain covered.

Launch a subagent with the Agent tool, or manage them with `/agents`.

## Slash Commands (`commands/`)

One thin wrapper per `.github/prompts/*.prompt.md` (27 in total). Each wrapper carries the source prompt's `description` (and `argument-hint`, where the prompt has one) verbatim, then instructs the agent to read and follow that prompt file via an `@` import. Wrappers for prompts that take an argument also pass it through `$ARGUMENTS`: an empty optional argument falls back to the prompt's full default scope, and an empty required argument is requested before work starts. The workflow itself is never duplicated — it stays defined once in `.github/prompts/`.

Run one by typing `/` and picking the command by name (for example `/bug-fix`, `/code-review`, `/refactor`), supplying its argument where the prompt takes one.

## Keeping the Layers in Sync

Because each file here mirrors a `.github/` counterpart, changes must be made in both places:

- Add, rename, or remove a prompt → update the matching `commands/*.md` wrapper (regenerate from the prompt frontmatter) and the prompt index in [`.github/prompts/DIRECTORY.md`](../.github/prompts/DIRECTORY.md).
- Change an agent's role, tools, or boundaries → update both the `.github/agents/*.agent.md` source and its `agents/*.md` port.
- Change a hook script's contract or registration → update both `.github/hooks/*.json` and `settings.json`.
