# Prompt Pipeline Runner

Tooling that drives the repository's improvement prompts
(`.github/prompts/*.prompt.md`) through an agentic CLI — the
[GitHub Copilot CLI](https://docs.github.com/en/copilot/reference/copilot-cli-reference) (default) or the
[Claude Code CLI](https://code.claude.com/docs/en/cli-reference) — in a
repeatable, safe order. One cross-platform Python CLI,
[`luma_pipeline.py`](luma_pipeline.py), provides three subcommands split by risk:

| Subcommand | Agent mode | Risk    | What it does                                                                 |
| ---------- | ---------- | ------- | ---------------------------------------------------------------------------- |
| `audit`    | **plan**   | None    | Runs the read-only audits and saves the ranked report each prompt produces. |
| `fix`      | **agent**  | Mutates | Applies the fixer prompts, gating each phase on build + test + lint with git checkpoints. |
| `all`      | both       | Mutates | Runs `audit`, then `fix`, with shared agent settings.                        |

| File                                               | Purpose                                                                                                    |
| -------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| [`luma_pipeline.py`](luma_pipeline.py)             | The CLI: phase tables, agent instructions, and the `audit` / `fix` / `all` orchestration.                  |
| [`pipeline_core.py`](pipeline_core.py)             | Every side effect — agent, `cmake`, `ctest`, lint, `git`, artifact moves — behind one uniform dry-run path. |
| [`test_luma_pipeline.py`](test_luma_pipeline.py)   | `unittest` suite pinning the gate and agent contracts with fake `cmake` / `ctest` / `python` / agent tools. |

Every side effect flows through `pipeline_core.Pipeline`, so `--dry-run` prints
the exact agent, `cmake`, `ctest`, and `git` commands each phase would run
without spawning anything. Run the tests with
`python -m unittest discover -s scripts/pipeline -p "test_*.py"`; CI runs them in
[`ci-python.yml`](../../.github/workflows/ci-python.yml).

> **Why separate audit and fix?** Audits are safe to run unattended and produce
> artifacts a human triages. Fixes change source and must be gated,
> checkpointed, and reversible. Keeping them separate keeps a human decision
> point between *finding* problems and *changing* code.

## Prerequisites

- **Python 3.10+** (standard library only; Windows, macOS, and Linux).
- **An agent CLI** on `PATH` and authenticated — the **GitHub Copilot CLI**
  (`copilot`, the default) or the **Claude Code CLI** (`claude`). Run it once
  interactively to sign in. Override the executable name or path with the
  `LUMA_COPILOT` / `LUMA_CLAUDE` environment variables. An npm-installed
  Copilot shim (`copilot.cmd` on Windows) also needs `node` on `PATH`.
- **CMake + CTest** on `PATH` for the fix gate (the `default` preset from
  [`CMakePresets.json`](../../CMakePresets.json)). On Windows, a Visual
  Studio-bundled or standalone CMake is found automatically when it is not on
  `PATH`.
- **Git** on `PATH`.

Run everything from the repository root. A missing agent CLI is reported once,
up front, with exit code `127` (skipped for `--dry-run` and `--list`).

## Choosing an agent

Select the backend per run with `--agent copilot` (default) or `--agent claude`:

```bash
python scripts/pipeline/luma_pipeline.py audit --agent claude --dry-run
```

The runner maps each pipeline concept to that CLI's own flags:

| Pipeline concept       | Copilot (`copilot`)                                      | Claude Code (`claude`)                                          |
| ---------------------- | -------------------------------------------------------- | -------------------------------------------------------------- |
| Headless prompt        | piped on **stdin** (no `-p`)                             | `-p "<instruction>" --output-format text` (prompt on stdin through a `.cmd` shim) |
| Read-only audit (plan) | `--output-format=json --available-tools=view,grep,glob` | `--permission-mode plan`                                       |
| Mutating fix (agent)   | `--allow-all-tools`                                      | `--permission-mode acceptEdits --allowedTools Bash Edit Write` |
| Deny `git push`        | `--deny-tool=shell(git push)`                            | `--disallowedTools "Bash(git push *)"`                         |
| Model override         | `--model=<name>`                                         | `--model <name>`                                               |
| Reasoning effort       | `--effort=<level>` (omitted for `auto` / no model)       | `--effort <level>`                                             |
| Agent logs             | `--log-dir=<run>/logs`                                   | *(none — Claude keeps its own transcripts)*                    |

The Copilot prompt is delivered on **stdin**, not as a `-p` argument: a
multi-line argument is mangled whenever the CLI is launched across a boundary
that cannot carry newlines (an npm `copilot.cmd` shim, the WSL↔Windows interop
layer), which makes Copilot abort with *"Invalid command format"*. For the same
reason, when `claude` resolves to a Windows `.cmd` / `.bat` shim its prompt is
piped to `claude -p` on stdin instead of passed as an argument. `--effort` is
omitted whenever the effective Copilot model is `auto` (including after the
fallback below), because Copilot rejects reasoning effort for `auto`.

Both backends are held **read-only** during audits and are **denied `git push`**
during fixes. Copilot's interactive `--plan` mode is deliberately *not* used: it
delivers its result through `exit_plan_mode` / `plan.md` rather than stdout, so it
would produce an empty report. Instead Copilot is restricted to the
read/search tools, and its JSONL event stream is reduced to the last assistant
message — the final report — since text mode interleaves narration and parallel
sub-agent transcripts. A phase that produces no extractable report keeps the raw
transcript (or, with no output at all, a diagnostic with the captured stderr)
and is marked failed, so an audit never records a hollow success.

`--effort` is validated client-side only for Copilot (`low`, `medium`, `high`,
`xhigh`, `max`); for Claude it is passed through to the CLI. Fix checkpoints are
attributed to the agent that did the work: `Co-authored-by: Copilot …` or
`Co-authored-by: Claude …`.

Copilot exits `0` even after printing *"You have exceeded your monthly quota"*
without doing any work; the fix runner detects that in the transcript and fails
the phase.

### Copilot CLI model availability

The Copilot **CLI** validates `--model` against the catalogue *its own client* is
entitled to, which can differ from what VS Code Copilot Chat sees for the same
account. On some org- or enterprise-managed accounts the CLI logs
`Successfully listed 0 models` and rejects **every** explicit `--model`, so only
`--model auto` runs. To keep that from failing every phase, the runner probes
the requested model once per run (bounded to 90 seconds, killing the whole
process tree on timeout) and **falls back to `auto` with a warning** when the CLI
reports it is not available. A timed-out probe keeps the requested model. To use
the model in the CLI, an org/enterprise Copilot admin must enable it; verify with:

```bash
copilot -p "reply ok" --model claude-opus-4.6 --allow-all-tools --no-ask-user
```

## Quick start

```bash
# 1. See what each stage will do — no agent, no build, nothing invoked.
python scripts/pipeline/luma_pipeline.py audit --list
python scripts/pipeline/luma_pipeline.py audit --dry-run
python scripts/pipeline/luma_pipeline.py fix --list
python scripts/pipeline/luma_pipeline.py fix --dry-run

# 2. Generate the audit reports (read-only).
python scripts/pipeline/luma_pipeline.py audit

# 3. Triage the reports under pipeline-artifacts/audit-<timestamp>/reports/.

# 4. Apply fixes on a dedicated branch, gated on build + test + lint.
python scripts/pipeline/luma_pipeline.py fix
```

Start with `--dry-run` every time you change flags.

### Both stages in one command

`all` runs the audit and then the fix, forwarding the agent flags to both and
skipping the fix when the audit exits non-zero. It defaults to the **Copilot
CLI** driving **Claude Opus 4.6** (`claude-opus-4.6`) at **medium** effort; with
`--agent claude` the model defaults to the Claude CLI's own choice.

```bash
python scripts/pipeline/luma_pipeline.py all --dry-run    # preview
python scripts/pipeline/luma_pipeline.py all              # audit → fix
python scripts/pipeline/luma_pipeline.py all --fix-arg --revert-on-failure
python scripts/pipeline/luma_pipeline.py all -- --phase refactor   # to every stage
```

Every stage's arguments are parsed before anything runs, so a typo in
`--fix-arg` fails immediately rather than after a long audit.

> **Heads-up:** `all` **skips the manual triage step** the audit/fix split is
> designed to preserve. Prefer separate `audit` and `fix` runs when you want to
> review the reports before any code changes.

## Pipeline order

The stages encode one principle — **audit before you execute** — progressing
correctness → consistency → structure → speed → polish → converge → gate →
capture. `audit` runs the left column; its reports feed `fix`, which runs the
right column.

| # | Audit phase (report)             | → | Fix phase (executor)       | Notes                                                    |
| - | -------------------------------- | - | -------------------------- | -------------------------------------------------------- |
| 1 | `01-bug-search-core`             | → | `bug-fix-core`             | Interpreter + stdlib correctness.                        |
| 2 | `02-bug-search-debugger`         | → | `bug-fix-debugger`         | DAP debugger.                                            |
| 3 | `03-bug-search-language-server`  | → | `bug-fix-language-server`  | LSP language server.                                     |
| 4 | `04-bug-search-editor-extension` | → | `bug-fix-editor-extension` | VS Code / Zed extensions.                                |
| 5 | `05-consistency-check`           |   | *(via executors)*          | Naming/structure drift; fixed as bugs/refactors flow through. |
| 6 | `06-code-review`                 |   | *(via executors)*          | Deep net catching what the targeted hunts missed.        |
| 7 | `07-ux-audit`                    | → | `ux-improve`               | UX, usability, and visual-design fixes.                  |
| 8 | `08-refactor-audit`              | → | `refactor`                 | Structural improvements.                                 |
| 9 | `09-performance-audit`           | → | `optimize`                 | Hotspots and algorithmic wins.                           |
|   |                                  |   | `iterative-improvement`    | Convergence loop (capped); re-runs consistency + review internally. |
|   |                                  |   | `release-verification`     | Clean-room build/test/lint gate.                         |
|   |                                  |   | `update-learnings`         | Captures new pitfalls into `instructions/learnings.instructions.md`, last. |

**Consistency and code-review findings have no dedicated fixer prompt by
design.** They are resolved *through* the executor prompts and the
`iterative-improvement` loop, which re-runs consistency and code-review passes
itself. UX is the exception: `07-ux-audit` feeds the dedicated `ux-improve`
executor, which runs in the polish band (after `optimize`).

## Safety model (`fix`)

The fix runner never touches your current branch or an unverified tree unless
you tell it to:

1. **Clean tree required** — refuses to start on a dirty working tree (`--allow-dirty` overrides).
2. **Dedicated branch** — creates `pipeline/fix-<timestamp>` and works there (`--no-branch` overrides).
3. **Green baseline** — verifies `cmake --build` + `ctest` pass *and* that `scripts/lint.py` is already clean before any change (`--skip-baseline`; `--skip-lint-format` skips just the lint check). If it fails after the run branch was created, the error says so and how to switch back (`git switch -`).
4. **Per-phase gate** — after each mutating phase re-runs configure + build + test, then `scripts/format.py` (apply) and `scripts/lint.py` (verify, including clang-tidy); the phase must be green and lint-clean to pass (`--skip-build`, `--skip-test`, `--skip-lint-format` drop parts of it). Configure (`cmake --preset <preset>`) runs before **every** build: the Visual Studio generator never re-runs CMake by itself, so a phase that adds a file to a `CMakeLists.txt` would otherwise be gated on stale project files. Because `lint.py` also runs detect-only tools no formatter fixes (clang-tidy, shellcheck, cmakelint, `tsc`, clippy), every lint-gated phase's instruction tells the agent to run both scripts and **fix every finding** before finishing; the gate is the deterministic backstop.
5. **Checkpoint on green** — commits only a green tree, as `chore(pipeline): <phase>` with a `Co-authored-by` trailer for the agent (`--no-commit` overrides). Formatting the gate applied is folded into that commit.
6. **Stop on red** — halts at the first failed phase (`--continue-on-failure` keeps going). `--revert-on-failure` resets tracked files to the last checkpoint and, when the run started from a clean tree, deletes the untracked files the failed phase created (`git clean -fd`; ignored files and the artifact root survive), so they cannot slip into a later checkpoint. A run started with `--allow-dirty` on a dirty tree keeps untracked files, because they may be yours. With `--no-commit` the revert discards every phase's work so far.
7. **No remote writes** — `git push` is denied to the agent; nothing leaves the machine.

Phases whose prompt already builds, tests, and lints exhaustively
(`iterative-improvement`, `release-verification`) are *self-verifying*: the
runner skips its own redundant gate for them but still checkpoints where
appropriate.

Phase statuses are `ok`, `agent-failed`, `gate-failed`, `commit-failed`, and
`error` (an infrastructure failure such as a missing tool). A **`commit-failed`**
— e.g. a pre-commit hook rejecting the checkpoint — is systemic: `git add -A`
would re-stage the un-committable change into every later checkpoint, so it
**stops the run even under `--continue-on-failure`**, unless
`--revert-on-failure` resets a run that started clean back to a clean tree. The
runner exits non-zero whenever
it stops early or any phase is not `ok`, so `all` and CI never read a failed run
as a pass.

The runner works on its own copy of the environment: the CMake it discovers on
Windows (via `vswhere` or a standalone install) and the MSVC developer
environment are added to that copy, which every child process receives, and
never to the caller's process environment. When a phase is stopped — by an error
or Ctrl+C — the agent's whole process tree is killed (it runs in its own process
group on Linux and macOS).

The lint gate needs `build/compile_commands.json` for clang-tidy, which the
Visual Studio and Xcode generators do not emit. When it is missing and
clang-tidy is installed, the gate runs a configure-only pass with a
database-capable generator (Ninja, else NMake / Unix Makefiles) into
`build-compiledb/` and copies the database into `build/`; on Windows it first
imports the MSVC developer environment (located via `vswhere`) so headers
resolve. All of this is best-effort: a missing tool only leaves clang-tidy
skipped by `lint.py`, which also skips any gate whose tool is not installed.

## After the run

`fix` produces **exactly one branch** — `pipeline/fix-<timestamp>` (unless
`--no-branch`) — with **one commit per green phase** (no-commit phases such as
`release-verification` add none). It performs **no merge and no push**; review
and merge the branch yourself, by `git merge` or a pull request. The run ends
with the branch name and a per-phase status/commit table.

## Exit codes

| Code  | Meaning                                                                                 |
| ----- | --------------------------------------------------------------------------------------- |
| `0`   | Every selected phase is `ok` (or `--list` / `--help` / a successful `--dry-run`).        |
| `1`   | A phase failed, the fix run stopped early, or a safety check (clean tree, branch, baseline) failed. |
| `2`   | Usage error: bad option or value, no phase matched `--phase`, `all` with both stages skipped. |
| `127` | The agent CLI (or the `node` its npm shim needs) was not found.                         |
| `130` | Interrupted with Ctrl+C.                                                                 |

`all` exits with the audit's code when the audit fails (the fix is skipped),
otherwise with the fix's code.

## Options

Run `python scripts/pipeline/luma_pipeline.py <subcommand> --help` for the
authoritative list. Long options also accept the `--option=value` form.

### `audit` and `fix`

| Option                  | Purpose                                                                       |
| ----------------------- | ----------------------------------------------------------------------------- |
| `--list`                | Print the phases in order and exit.                                           |
| `--dry-run`             | Print every command each phase would run; invoke, build, and commit nothing. |
| `--phase <filter>`      | Run only phases whose id or name contains the filter (case-insensitive; repeatable; comma-separated values accepted). |
| `--artifact-root <dir>` | Where run artifacts go (default `pipeline-artifacts/`).                       |
| `--agent <name>`        | Agent CLI backend: `copilot` (default) or `claude`.                           |
| `--model <name>`        | Model override (omit to let the agent choose).                                |
| `--effort <level>`      | Reasoning effort (`low`…`max` for Copilot; passed through for Claude).        |

### `audit` only

| Option           | Purpose                                                       |
| ---------------- | ------------------------------------------------------------- |
| `--scope <path>` | Restrict every phase to a scope (e.g. `core/runtime/vm/`), overriding each phase's default. |

### `fix` only

| Option                         | Purpose                                                              |
| ------------------------------ | -------------------------------------------------------------------- |
| `--report-dir <dir>`           | Audit `reports/` folder to consume (default: newest `audit-*` run that has one). |
| `--preset <name>`              | CMake preset for the gate (default `default`).                       |
| `--convergence-max-passes <n>` | Cap for `iterative-improvement` (1–10, default 3).                   |
| `--allow-dirty`                | Do not require a clean working tree.                                 |
| `--skip-baseline`              | Skip the initial green baseline check.                               |
| `--skip-build` / `--skip-test` | Drop the configure + build, or the test, step of every gate.         |
| `--skip-lint-format`           | Skip the `format.py` + `lint.py` gate (including clang-tidy).         |
| `--no-branch`                  | Work on the current branch instead of a new one.                     |
| `--no-commit`                  | Do not checkpoint after green phases.                                |
| `--continue-on-failure`        | Keep going past a failed phase (a `commit-failed` still stops unless reverted from a clean start). |
| `--revert-on-failure`          | After a failed phase, reset to the last checkpoint and delete its new untracked files (kept when the run started dirty). |

### `all`

| Option              | Purpose                                                                          |
| ------------------- | -------------------------------------------------------------------------------- |
| `--agent <name>`    | Backend for every stage: `copilot` (default) or `claude`.                        |
| `--model <name>`    | Model for every stage (default `claude-opus-4.6` for Copilot, the CLI default for Claude; pass `''` to let the agent choose). |
| `--effort <level>`  | Reasoning effort for every stage (default `medium`; pass `''` to omit).          |
| `--dry-run`         | Preview every stage; invoke nothing.                                             |
| `--skip-audit`      | Skip the audit stage.                                                            |
| `--skip-fix`        | Skip the fix stage.                                                              |
| `--audit-arg <arg>` | Extra argument for the audit stage (repeatable; option-like values are fine).    |
| `--fix-arg <arg>`   | Extra argument for the fix stage (repeatable), e.g. `--fix-arg --no-commit`.     |
| `-- <args…>`        | Everything after a literal `--` is forwarded to every stage.                     |

## Artifacts

Everything a run produces lives under `pipeline-artifacts/` at the repository
root, which is **git-ignored**:

```text
pipeline-artifacts/
  audit-<timestamp>/
    INDEX.md              # table linking every report
    reports/<phase>.md    # one ranked report per audit phase
    logs/                 # agent logs (Copilot --log-dir)
  fix-<timestamp>/
    SUMMARY.md            # each phase's status, commit, and log link
    logs/<phase>.log      # per-phase agent transcript
```

`fix` auto-detects the newest `audit-*` run with a `reports/` folder and feeds
each report to the matching fixer; pass `--report-dir` to pin a specific run. If
a report is absent, that phase self-discovers findings from its prompt.

If you point `--artifact-root` at a location **inside** the working tree that is
not git-ignored, the fix runner unstages it before each checkpoint, so artifacts
never land in a commit.

The `release-verification` phase wipes git-ignored files (`git clean -Xdf`). The
runner protects the artifact tree deterministically rather than relying on the
agent: before that phase it moves an in-tree artifact root into
`.git/_pipeline-artifacts.pipeline.bak` (a name distinct from the
[prompt's own](../../.github/prompts/release-verification.prompt.md)
`_pipeline-artifacts.bak`), streams the phase's transcript to a temporary
directory outside the tree (on Windows a directory cannot be moved while a file
in it is open), and restores both afterwards — even if the phase errors or is
interrupted. Roots outside the working tree need no protection.

## Handoff: audit → fix

1. `audit` writes `reports/01-bug-search-core.md`, `…/08-refactor-audit.md`, and so on.
2. You read them and decide what is worth fixing.
3. `fix` reads each report as the fixer's input.

Nothing in this directory is wired into a git hook or scheduler; every run is a
deliberate, manual step from the repository root. (CI runs only the unit tests.)

## Related documentation

- [`scripts/DIRECTORY.md`](../DIRECTORY.md) — the wider maintenance-script catalogue.
- [`instructions/python.instructions.md`](../../instructions/python.instructions.md) — the Python style this code follows.
- [`.github/prompts/`](../../.github/prompts) — the prompt definitions the pipeline invokes.
- [`CONTRIBUTING.md`](../../CONTRIBUTING.md) — contributor workflow and commit conventions.
