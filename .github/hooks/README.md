# Agent Hooks

Deterministic guardrails that run at the AI agent's tool-call lifecycle points. Each `*.json` file in this directory registers a hook that the agent runtime discovers automatically and runs around every tool call, turning a project guideline into an enforced, non-negotiable behaviour instead of advice the model might overlook.

These are **agent** hooks — distinct from the **Git** hooks in [`scripts/hooks/`](../../scripts/hooks) (`pre-commit`, `commit-msg`) that run on `git commit`. The two layers are complementary: the agent hooks catch things as the agent edits, and the Git hooks catch anything that reaches a commit regardless of how it was produced. Where they overlap (C++ formatting), the agent hook simply means an edit is already clean by the time the pre-commit gate sees it.

For the guidelines these hooks enforce, see [cpp.instructions.md](../../instructions/cpp.instructions.md) (formatting) and the vendored-code boundary documented across the agent guides and [CONTRIBUTING.md](../../CONTRIBUTING.md). For the sibling prompt files, see [.github/prompts/README.md](../prompts/README.md).

## Structure

Each hook is a small JSON registration paired with a standalone Python script under [`scripts/agent-hooks/`](../../scripts/agent-hooks); the script carries a module-level docstring with the full behaviour. The table below is a quick index.

| Hook                                                       | Event         | Script                                                                          | What it does                                                                                              |
| ---------------------------------------------------------- | ------------- | ------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| [`format-cpp-on-edit.json`](format-cpp-on-edit.json)       | `PostToolUse` | [`format_cpp_on_edit.py`](../../scripts/agent-hooks/format_cpp_on_edit.py)      | Runs `clang-format` (18+) over any `.cpp`/`.hpp` file the agent just edited, so edits land already clean. |
| [`protect-vendored-paths.json`](protect-vendored-paths.json) | `PreToolUse`  | [`protect_vendored_paths.py`](../../scripts/agent-hooks/protect_vendored_paths.py) | Denies file-mutating tool calls that target vendored code under `external/` (except `external/gui-framework/`). |

## How it works

- **Discovery.** The runtime loads every `*.json` in this directory. Each file is a flat map of `{ "hooks": { "<Event>": [ <command entry> ] } }`. `PreToolUse` entries run *before* a tool call and may deny it; `PostToolUse` entries run *after* and observe the result.
- **Cross-platform invocation.** Each entry provides both a `bash` command (`python3 …`, used on Linux and macOS) and a `powershell` command (`python …`, used on Windows), invoking the same script. The runtime pipes a JSON payload describing the tool call to the script on stdin.
- **Tool gating lives in the scripts, not the config.** The configs register unconditionally; each script inspects `tool_name` itself and no-ops for tools it does not care about. This keeps the gating logic testable and in one place.

## Design principles

- **Fail-open.** Every script treats a missing interpreter, an unrecognised payload, or any internal error as a clean no-op (exit 0, no decision). A hook must never wedge the agent; the worst case is that it silently does nothing, never a wrongful block or a crash.
- **Narrow and confident.** The guard denies *only* when it is certain a call is a file write to a protected path. Reads, searches, shell commands, and anything ambiguous are allowed. The formatter only ever formats; it never blocks.
- **Dependency-free.** The scripts import only the Python standard library and deliberately do **not** import `scripts/_common.py`, so they run identically whether or not the rest of the tooling is set up. They require Python 3 and (for the formatter) `clang-format` 18+.
- **Mirrors existing gates.** The formatter matches [`scripts/hooks/pre-commit`](../../scripts/hooks/pre-commit) and the CI *Formatting* job (same tool, same `.clang-format`, same `.cpp`/`.hpp` scope), so the three layers never disagree.

## The hooks

### `format-cpp-on-edit` (PostToolUse)

After the agent edits a file, this hook reads the edited path from the payload and, if it is a first-party `.cpp`/`.hpp` file that exists on disk, reformats it in place with `clang-format`. `clang-format` discovers the repository's `.clang-format` by walking up from the file, so the result matches the project style exactly. If `clang-format` is missing or older than 18, the hook is a no-op — the pre-commit gate and CI still enforce formatting, so nothing slips through.

### `protect-vendored-paths` (PreToolUse)

The project boundary is: **never modify vendored, third-party code in `external/`**, with the single exception of the first-party GraphicalUi front-end in `external/gui-framework/`. This hook enforces that deterministically — a file-mutating tool (edit / write / create) that targets `external/` outside `gui-framework/` is denied before it runs, with a message explaining the boundary. Shell-based mutation (for example `rm external/…`) is out of scope by design: detecting it reliably would require parsing shell commands and would risk blocking legitimate reads such as `cat`/`grep` over `external/`.

## Testing a hook

The scripts are ordinary stdin-to-stdout filters, so you can exercise them directly by piping a sample payload. The payload is the JSON the runtime sends: `tool_name` plus a `tool_input` object holding the target path (the scripts accept `file_path`, `path`, or the camelCase variants).

PowerShell (Windows):

```powershell
# Guard: a write into external/ is denied (prints a deny decision)…
'{ "tool_name": "Write", "tool_input": { "file_path": "external/zlib/x.c" } }' |
    python scripts/agent-hooks/protect_vendored_paths.py

# …but the gui-framework exception, reads, and edits elsewhere are allowed (no output).
'{ "tool_name": "Write", "tool_input": { "file_path": "external/gui-framework/a.js" } }' |
    python scripts/agent-hooks/protect_vendored_paths.py
```

Bash (Linux/macOS):

```bash
# Formatter: badly formatted C++ is rewritten in place.
printf 'int  main( ){return 0;}' > /tmp/hooktest.cpp
echo '{ "tool_name": "Edit", "tool_input": { "path": "/tmp/hooktest.cpp" } }' |
    python3 scripts/agent-hooks/format_cpp_on_edit.py
cat /tmp/hooktest.cpp   # now formatted
```

A denied call prints a JSON object containing `"permissionDecision": "deny"`; an allowed call prints nothing and exits 0.

## Related documentation

- [scripts/agent-hooks/](../../scripts/agent-hooks) — The hook scripts, each with a full behavioural docstring.
- [scripts/README.md](../../scripts/README.md) — The wider script directory, including the Git hooks in `scripts/hooks/`.
- [cpp.instructions.md](../../instructions/cpp.instructions.md) — The C++ style the formatter enforces.
- [CONTRIBUTING.md](../../CONTRIBUTING.md) — Contributor setup and the vendored-code boundary.
- [.github/prompts/README.md](../prompts/README.md) — The sibling prompt files that the agent runs on demand.
