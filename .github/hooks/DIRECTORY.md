# Agent Hooks

Deterministic guardrails that run at the AI agent's tool-call lifecycle points. Each `*.json` file in this directory registers a hook that the agent runtime discovers automatically and runs around every tool call, turning a project guideline into an enforced, non-negotiable behaviour instead of advice the model might overlook.

These are **agent** hooks — distinct from the **Git** hooks in [`scripts/git-hooks/`](../../scripts/git-hooks) (`pre-commit`, `commit-msg`) that run on `git commit`. The two layers are complementary: the agent hooks catch things as the agent edits, and the Git hooks catch anything that reaches a commit regardless of how it was produced. Where they overlap (C++ formatting), the agent hook simply means an edit is already clean by the time the pre-commit gate sees it.

For the guidelines these hooks enforce, see [cpp.instructions.md](../../instructions/cpp.instructions.md) (formatting) and the vendored-code boundary documented across the agent guides and [CONTRIBUTING.md](../../CONTRIBUTING.md). For the sibling prompt files, see [.github/prompts/DIRECTORY.md](../prompts/DIRECTORY.md).

## Structure

Each hook is a small JSON registration paired with a standalone Python script under [`scripts/agent-hooks/`](../../scripts/agent-hooks); the script carries a module-level docstring with the full behaviour. Payload parsing shared by both scripts lives in [`hook_common.py`](../../scripts/agent-hooks/hook_common.py). The table below is a quick index.

| Hook                                                         | Event         | Script                                                                             | What it does                                                                                              |
| ------------------------------------------------------------ | ------------- | ---------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| [`format-cpp-on-edit.json`](format-cpp-on-edit.json)         | `PostToolUse` | [`format_cpp_on_edit.py`](../../scripts/agent-hooks/format_cpp_on_edit.py)         | Runs `clang-format` (18+) over any `.cpp`/`.hpp` file the agent just edited, so edits land already clean. |
| [`protect-vendored-paths.json`](protect-vendored-paths.json) | `PreToolUse`  | [`protect_vendored_paths.py`](../../scripts/agent-hooks/protect_vendored_paths.py) | Denies file-mutating tool calls that target vendored code under `external/`.                              |

## How It Works

- **Discovery.** The Copilot runtime (CLI, VS Code, and cloud agent) loads every `*.json` in this directory once, when a session starts — edits take effect in the next session. Each file follows the [Copilot hooks schema](https://docs.github.com/en/copilot/reference/hooks-reference): `{ "version": 1, "hooks": { "<Event>": [ <command entry> ] } }`. `PreToolUse` entries run *before* a tool call and may deny it; `PostToolUse` entries run *after* a successful one. The Copilot CLI loads repository hooks only for a trusted folder.
- **PascalCase events.** The events are spelled `PreToolUse`/`PostToolUse` (not the camelCase `preToolUse`), which selects the VS Code-compatible payload: snake_case fields (`tool_name`, `tool_input`) and Claude tool names (`Write` for `create`; `Edit` for `edit`, `apply_patch`, and `str_replace_editor`). The scripts also accept the camelCase payload (`toolName`, `toolArgs`), so renaming the events cannot silently disable them.
- **Matchers.** Each entry carries `"matcher": "Edit|Write|edit|create|apply_patch|str_replace_editor"`, so the scripts start only for file-editing tools — not on every read and shell command. The list names each tool by both its Claude name and its runtime name, so it matches whichever form the runtime compares against. The scripts still check the tool name themselves, for runtimes that ignore matchers.
- **Cross-platform invocation.** Each entry provides both a `bash` command (`python3 …`, used on Linux, macOS, and the cloud agent) and a `powershell` command (`python …`, used on Windows), invoking the same script with `"cwd": "."` (the repository root). The runtime pipes the JSON payload to the script on stdin.
- **Patch-format edits.** An `apply_patch` call carries its target paths inside the patch text rather than a `path` field. The scripts read them from the `*** Add File:`, `*** Update File:`, `*** Move to:`, and `*** Delete File:` headers.

## Design Principles

- **Fail-open.** Every script treats an unrecognised payload or any internal error as a clean no-op (exit 0, no decision). A missing interpreter is handled in the registration instead: the runtime treats a crashing or non-zero `PreToolUse` command as a *deny*, so a machine without `python3`/`python` on `PATH` (or with only the Windows Store alias stub, which exits 9009) would otherwise have every edit refused. Each command therefore ends in `; exit 0`. A hook must never wedge the agent; the worst case is that it silently does nothing, never a wrongful block or a crash. Timeouts already fall through to the normal permission flow.
- **Narrow and confident.** The guard denies *only* when it is certain a call is a file write to a protected path. Reads (including an editor tool's `view` command), searches, shell commands, and anything ambiguous are allowed. The formatter only ever formats; it never blocks.
- **Dependency-free.** The scripts import only the Python standard library and their sibling `hook_common.py`, and deliberately do **not** import `scripts/_common.py`, so they run identically whether or not the rest of the tooling is set up. They require Python 3.10+ and (for the formatter) `clang-format` 18+.
- **Mirrors existing gates.** The formatter matches [`scripts/git-hooks/pre-commit`](../../scripts/git-hooks/pre-commit) and the CI *Formatting* job (same tool, same `.clang-format`, same `.cpp`/`.hpp` scope), so the three layers never disagree.
- **One run per tool call.** The Copilot runtime also reads [`.claude/settings.json`](../../.claude/settings.json), which registers the same scripts for Claude Code. Those registrations pass `--claude-settings`, and a script launched with that flag stands down when it is plainly running under Copilot (`COPILOT_PROJECT_DIR` is set and the payload has no Claude Code `transcript_path`). Copilot therefore runs each hook once, from this directory, while Claude Code — which never reads `.github/hooks/` — still runs its own copy.

## The Hooks

### `format-cpp-on-edit` (PostToolUse)

After the agent edits a file, this hook reads the edited path(s) from the payload and, for each first-party `.cpp`/`.hpp` file that exists on disk — inside the repository and outside `external/` — reformats it in place with `clang-format`. Files elsewhere on disk and an editor tool's read-only `view` command are ignored. `clang-format` discovers the repository's `.clang-format` by walking up from the file, so the result matches the project style exactly. When formatting actually changes a file, the hook returns `additionalContext` (and the Claude Code `hookSpecificOutput` equivalent) telling the model to re-read the file, so its next edit does not target stale text; an already-clean file produces no output. If `clang-format` is missing or older than 18, the hook is a no-op — the pre-commit gate and CI still enforce formatting, so nothing slips through.

### `protect-vendored-paths` (PreToolUse)

The project boundary is: **never modify vendored, third-party code in `external/`**. This hook enforces that deterministically — a file-mutating tool (edit / write / create / patch) that targets `external/` is denied before it runs, with a message explaining the boundary. Paths are resolved first, so `core/../external/…` and case variants such as `External/` are caught; payloads are decoded as UTF-8 regardless of the console code page, so non-ASCII paths survive intact on Windows. Shell-based mutation (for example `rm external/…`) is out of scope by design: detecting it reliably would require parsing shell commands and would risk blocking legitimate reads such as `cat`/`grep` over `external/`.

## Testing a Hook

The unit tests in [`test_agent_hooks.py`](../../scripts/agent-hooks/test_agent_hooks.py) cover payload parsing, the guard's deny contract (run end-to-end as a subprocess), the formatter (with `clang-format` mocked), and the registrations in this directory and `.claude/settings.json`. CI runs them in the *CI — Python* workflow:

```bash
python -m unittest discover -s scripts/agent-hooks -p "test_*.py"
```

The scripts are also ordinary stdin-to-stdout filters, so you can exercise them directly by piping a sample payload. The payload is the JSON the runtime sends: `tool_name` plus a `tool_input` object holding the target path (the scripts accept `file_path`, `path`, or the camelCase variants, plus patch text).

PowerShell (Windows):

```powershell
# Guard: a write into external/ is denied (prints a deny decision)…
'{ "tool_name": "Write", "tool_input": { "file_path": "external/mbedtls/x.c" } }' |
    python scripts/agent-hooks/protect_vendored_paths.py

# …but reads and edits elsewhere are allowed (no output).
'{ "tool_name": "Write", "tool_input": { "file_path": "core/runtime/vm/vm.cpp" } }' |
    python scripts/agent-hooks/protect_vendored_paths.py
```

Bash (Linux/macOS):

```bash
# Formatter: badly formatted C++ is rewritten in place, and the hook prints an
# additionalContext note telling the model to re-read the file.
printf 'int  main( ){return 0;}' > /tmp/hooktest.cpp
echo '{ "tool_name": "Edit", "tool_input": { "path": "/tmp/hooktest.cpp" } }' |
    python3 scripts/agent-hooks/format_cpp_on_edit.py
cat /tmp/hooktest.cpp   # now formatted
```

A denied call prints a JSON object containing `"permissionDecision": "deny"`; an allowed call prints nothing and exits 0.

## Related Documentation

- [scripts/agent-hooks/](../../scripts/agent-hooks) — The hook scripts, each with a full behavioural docstring, and their unit tests.
- [.claude/DIRECTORY.md](../../.claude/DIRECTORY.md) — The Claude Code registrations of the same scripts.
- [scripts/DIRECTORY.md](../../scripts/DIRECTORY.md) — The wider script directory, including the Git hooks in `scripts/git-hooks/`.
- [cpp.instructions.md](../../instructions/cpp.instructions.md) — The C++ style the formatter enforces.
- [CONTRIBUTING.md](../../CONTRIBUTING.md) — Contributor setup and the vendored-code boundary.
- [.github/prompts/DIRECTORY.md](../prompts/DIRECTORY.md) — The sibling prompt files that the agent runs on demand.
