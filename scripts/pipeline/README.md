# Prompt Pipeline Runners

Tooling that drives the repository's improvement and language-evolution prompts
(`.github/prompts/*.prompt.md`) through an agentic CLI — the
[GitHub Copilot CLI](https://docs.github.com/en/copilot/reference/copilot-cli-reference) (default) or the
[Claude Code CLI](https://code.claude.com/docs/en/cli-reference) — in a
repeatable, safe order. The pipeline is split by risk into two runners, each
provided for both **PowerShell** (Windows) and **bash** (macOS/Linux):

| Role  | PowerShell                                     | Shell (macOS/Linux)              | Agent mode | Risk    |
| ----- | ---------------------------------------------- | -------------------------------- | ---------- | ------- |
| Audit | [`Invoke-LumaAudit.ps1`](Invoke-LumaAudit.ps1) | [`luma-audit.sh`](luma-audit.sh) | **plan**   | None    |
| Fix   | [`Invoke-LumaFix.ps1`](Invoke-LumaFix.ps1)     | [`luma-fix.sh`](luma-fix.sh)     | **agent**  | Mutates |

The **audit** runner performs read-only analysis and saves the ranked report
each prompt produces. The **fix** runner applies the fixer prompts, gating each
phase on build + test with git checkpoints.

A convenience wrapper —
[`Invoke-LumaAll.ps1`](Invoke-LumaAll.ps1) /
[`luma-all.sh`](luma-all.sh) — runs the audit and then the
fix in a single command, defaulting to the **Copilot CLI** driving **Claude Opus
4.8** at **max** reasoning effort, and can optionally chain the conformance pass
as a third stage (`-IncludeConformance` / `--include-conformance`). See
[Both stages in one command](#both-stages-in-one-command).

Each language pair shares one helper module — PowerShell through
[`LumaPipeline.psm1`](LumaPipeline.psm1), bash through
[`luma-pipeline.sh`](luma-pipeline.sh) — so every side effect (invoking the
agent, building, testing, committing) flows through one place and has a uniform
dry-run path (`-DryRun` / `--dry-run`). The two implementations are kept
behaviourally in lock-step.

> **Why two scripts?** Audits are safe to run unattended and produce artifacts a
> human triages. Fixes change source and must be gated, checkpointed, and
> reversible. Keeping them separate keeps a human decision point between
> *finding* problems and *changing* code.

A third runner — [`Invoke-LumaConformance.ps1`](Invoke-LumaConformance.ps1) /
[`luma-conformance.sh`](luma-conformance.sh) — is organised along a different
axis. Instead of driving the improvement prompts, it walks **every file of a
given type** (C++, CSS, CMake, GitHub Actions, JavaScript, Luma, Markdown,
PowerShell, Python, Rust, Shell, TypeScript) and runs **one agent session per
file** to make that file conform to its language's guides under `instructions/`
(and `documents/` for Luma), fixing every issue found. It shares the same helper
module, agent selection, dry-run path, and safety model as the fix runner and
defaults to the **Copilot CLI** driving **Claude Opus 4.8** at **max** effort.
See [Per-file conformance](#per-file-conformance).

A fourth set of runners —
[`Invoke-LumaDiscover.ps1`](Invoke-LumaDiscover.ps1) /
[`luma-discover.sh`](luma-discover.sh) and
[`Invoke-LumaEvolve.ps1`](Invoke-LumaEvolve.ps1) /
[`luma-evolve.sh`](luma-evolve.sh) — drives the **language-evolution** prompts
(`new-requirements` and the four `new-*` implementers) instead of the improvement
prompts. Where audit → fix *improves existing code*, discover → evolve *grows the
language*: **discover** runs read-only and saves a ranked report of candidate
additions — new stdlib functions, types, modules, or language features — that fit
Luma's philosophy, and **evolve** implements the ones you pick, gating and
checkpointing each exactly like the fix runner. Unlike audit → fix, the handoff
is **deliberately manual** — you hand evolve a concrete goal (or a goals file)
rather than a report — so it is never wired into the combined runner. See
[Language evolution](#language-evolution).

## Prerequisites

Common to both variants:

- **An agent CLI** on `PATH` and authenticated — either the **GitHub Copilot
  CLI** (`copilot`; the default) or the **Claude Code CLI** (`claude`). Run your
  chosen tool once interactively to sign in, and select it per run with
  `-Agent` / `--agent` (see [Choosing an agent](#choosing-an-agent)).
- **CMake + CTest** on `PATH` for the fix gate (the `default` preset from [`CMakePresets.json`](../../CMakePresets.json)).
- **Git** on `PATH`.
- **A JSON tool** (`python3`, `python`, or `node`) on `PATH` **when auditing with
  Copilot** — the audit runner reduces Copilot's `--output-format json` event
  stream to each phase's final report with it (`node` ships with a
  Copilot CLI installed via npm, so this is usually already satisfied). Without
  one, the audit still runs but saves the raw JSONL transcript and warns. The
  PowerShell runner uses its built-in `ConvertFrom-Json`, and the Claude backend
  needs no JSON tool.

Then, for your platform:

- **PowerShell 7+** (`pwsh`) for the `*.ps1` runners, or
- **bash 3.2+** for the `*.sh` runners (the stock macOS `/bin/bash` works; no extra packages needed). Invoke them as `bash scripts/pipeline/luma-*.sh …`, or `chmod +x` them once to run directly.

Run everything from the repository root.

## Choosing an agent

Both runners target the **GitHub Copilot CLI** by default and can instead drive
the **Claude Code CLI**. Select the backend per run — `-Agent copilot` (default)
or `-Agent claude` in PowerShell, `--agent copilot` / `--agent claude` in shell:

```powershell
pwsh -File scripts/pipeline/Invoke-LumaAudit.ps1 -Agent claude -DryRun
```

```bash
bash scripts/pipeline/luma-fix.sh --agent claude --dry-run
```

The runner maps each pipeline concept to that CLI's own flags:

| Pipeline concept       | Copilot (`copilot`)             | Claude Code (`claude`)                                          |
| ---------------------- | ------------------------------- | -------------------------------------------------------------- |
| Headless prompt        | `-p "<instruction>"`            | `-p "<instruction>" --output-format text`                      |
| Read-only audit (plan) | `--plan --output-format json`   | `--permission-mode plan`                                       |
| Mutating fix (agent)   | `--allow-all-tools`             | `--permission-mode acceptEdits --allowedTools Bash Edit Write` |
| Deny `git push`        | `--deny-tool="shell(git push)"` | `--disallowedTools "Bash(git push *)"`                         |
| Model override         | `--model=<name>`                | `--model <name>`                                               |
| Reasoning effort       | `--effort=<level>`              | `--effort <level>`                                             |

Both backends are held **read-only** during audits and are **denied `git push`**
during fixes, so the safety model is identical either way. During audits, Claude's
plan mode grants no edit tools, and Copilot additionally denies the file-write
tool (`--deny-tool=write`) on top of `--plan` — a belt-and-suspenders guard, since
`--allow-all-tools` is required for non-interactive runs and deny rules beat it.
Override the
executable name or full path with the `LUMA_COPILOT` / `LUMA_CLAUDE` environment
variables. Model names and `--effort` values differ between the two CLIs — check
each tool's own reference. `--effort` is validated client-side only for copilot
(against `low`…`max`); for claude the value is passed straight through and
validated by the CLI itself. Copilot's `--log-dir` has no Claude equivalent
(Claude keeps its own transcripts), so it is emitted only for Copilot.

Fix-runner checkpoints are attributed to whichever agent did the work: the
commit carries `Co-authored-by: Copilot …` under `copilot` and
`Co-authored-by: Claude …` under `claude`.

## Quick start

**PowerShell (Windows):**

```powershell
# 1. See what each stage will do — no agent, no build, nothing invoked.
pwsh -File scripts/pipeline/Invoke-LumaAudit.ps1 -List
pwsh -File scripts/pipeline/Invoke-LumaAudit.ps1 -DryRun
pwsh -File scripts/pipeline/Invoke-LumaFix.ps1   -List
pwsh -File scripts/pipeline/Invoke-LumaFix.ps1   -DryRun

# 2. Generate the audit reports (read-only).
pwsh -File scripts/pipeline/Invoke-LumaAudit.ps1

# 3. Triage the reports under pipeline-artifacts/audit-<timestamp>/reports/.

# 4. Apply fixes on a dedicated branch, gated on build + test.
pwsh -File scripts/pipeline/Invoke-LumaFix.ps1
```

**Shell (macOS/Linux):**

```bash
# 1. See what each stage will do — no agent, no build, nothing invoked.
bash scripts/pipeline/luma-audit.sh --list
bash scripts/pipeline/luma-audit.sh --dry-run
bash scripts/pipeline/luma-fix.sh   --list
bash scripts/pipeline/luma-fix.sh   --dry-run

# 2. Generate the audit reports (read-only).
bash scripts/pipeline/luma-audit.sh

# 3. Triage the reports under pipeline-artifacts/audit-<timestamp>/reports/.

# 4. Apply fixes on a dedicated branch, gated on build + test.
bash scripts/pipeline/luma-fix.sh
```

Start with `-DryRun` / `--dry-run` every time you change flags: it prints the
exact agent, `cmake`, `ctest`, and `git` commands each phase would run without
touching anything.

### Both stages in one command

To chain the audit and the fix in one invocation — using the **Copilot CLI** with
**Claude Opus 4.8** at **max** reasoning effort (the built-in defaults) — use the
combined runner. It runs the audit first, then the fix, forwarding the agent flags
to both stages and skipping the fix if the audit exits non-zero:

```powershell
# Preview first — nothing invoked, built, or committed.
pwsh -File scripts/pipeline/Invoke-LumaAll.ps1 -DryRun

# Run for real: audit → fix, Copilot CLI + Claude Opus 4.8 + max effort.
pwsh -File scripts/pipeline/Invoke-LumaAll.ps1
```

```bash
# Preview first.
bash scripts/pipeline/luma-all.sh --dry-run

# Run for real.
bash scripts/pipeline/luma-all.sh
```

The three agent knobs default to `-Agent copilot`, `-Model claude-opus-4.8`, and
`-Effort max`; override any of them per run (e.g. `-Model gpt-5.4`, `-Effort high`,
or `-Agent claude`). Run only one stage with `-SkipFix` / `--skip-fix` or
`-SkipAudit` / `--skip-audit`, and forward extra flags to a specific stage with
`-AuditArgs` / `--audit-arg` and `-FixArgs` / `--fix-arg` (for example
`-FixArgs '-RevertOnFailure'`).

Add the [per-file conformance](#per-file-conformance) pass as an optional **third
stage** with `-IncludeConformance` / `--include-conformance`. It runs after the
fix, is skipped if the fix stage fails, and takes its own passthrough via
`-ConformanceArgs` / `--conformance-arg`. Because a full run visits every tracked
file of every target, scope it — for example to one target:

```powershell
pwsh -File scripts/pipeline/Invoke-LumaAll.ps1 -IncludeConformance -ConformanceArgs '-Target', 'cpp'
```

```bash
bash scripts/pipeline/luma-all.sh --include-conformance --conformance-arg --target=cpp
```

Pass `-SkipAudit -SkipFix -IncludeConformance` to drive **only** the conformance
pass through the wrapper (with both audit and fix skipped you must include it, or
there is nothing to run).

> **Heads-up:** running the mutating fixer straight after the audit **skips the
> manual triage step** the two-runner split is designed to preserve. Prefer the
> separate runners when you want to review the reports before any code changes.

## Pipeline order

The stages encode a single governing principle — **audit before you execute**,
progressing correctness → consistency → structure → speed → polish → converge →
gate → capture. The audit runner runs the read-only column; the reports feed the
fix runner, which runs the mutating column.

| # | Audit phase (report)        | → | Fix phase (executor)      | Notes                                                    |
| - | --------------------------- | - | ------------------------- | -------------------------------------------------------- |
| 1 | `01-bug-search-core`        | → | `bug-fix-core`            | Interpreter + stdlib correctness.                        |
| 2 | `02-bug-search-debugger`    | → | `bug-fix-debugger`        | DAP debugger.                                            |
| 3 | `03-bug-search-language-server` | → | `bug-fix-language-server` | LSP language server.                                 |
| 4 | `04-bug-search-editor-extension` | → | `bug-fix-editor-extension` | VS Code / Zed extensions.                          |
| 5 | `05-consistency-check`      |   | *(via executors)*         | Naming/structure drift; fixed as bugs/refactors flow through. |
| 6 | `06-code-review`            |   | *(via executors)*         | Deep net catching what the targeted hunts missed.        |
| 7 | `07-ux-audit`               | → | `ux-improve`              | UX, usability, and visual-design fixes.                  |
| 8 | `08-refactor-audit`         | → | `refactor`                | Structural improvements.                                 |
| 9 | `09-performance-audit`      | → | `optimize`                | Hotspots and algorithmic wins.                           |
|   |                             |   | `source-code-cleanup`     | Dead code, comment/format hygiene.                       |
|   |                             |   | `iterative-improvement`   | Convergence loop (capped); re-runs consistency + review internally. |
|   |                             |   | `release-verification`    | Clean-room build/test/lint gate.                         |
|   |                             |   | `update-learnings`        | Captures new pitfalls into `instructions/learnings.instructions.md`, last. |

**Consistency and code-review findings have no dedicated fixer prompt by
design.** They are audits whose findings are resolved *through* the executor
prompts (`bug-fix`, `refactor`, `optimize`) and the `iterative-improvement`
loop, which itself re-runs consistency and code-review passes. UX is the
exception: its `07-ux-audit` report feeds the dedicated `ux-improve` executor,
which runs in the polish band (after `optimize`, before `source-code-cleanup`)
rather than in audit-number order. This mirrors the prompt pairing in the
project's own prompt set: discovery prompts produce reports, execute prompts
consume them.

## Safety model (fix runner)

The fix runner never touches your current branch or an unverified tree unless
you tell it to (PowerShell flag / shell flag shown together):

1. **Clean tree required** — refuses to start on a dirty working tree (override with `-AllowDirty` / `--allow-dirty`).
2. **Dedicated branch** — creates `pipeline/fix-<timestamp>` and works there (override with `-NoBranch` / `--no-branch`).
3. **Green baseline** — verifies `cmake --build` + `ctest` pass *and* that the tree is already lint-clean *before* any change (override with `-SkipBaseline` / `--skip-baseline`, or skip just the lint check with `-SkipLintFormat` / `--skip-lint-format`).
4. **Per-phase gate** — after each mutating phase re-runs the build + test gate, then applies the formatters and runs the lint checks (`scripts/format.py`, then `scripts/lint.py`, including clang-tidy); the phase must be both green and lint-clean to pass (override the lint/format half with `-SkipLintFormat` / `--skip-lint-format`). Because `lint.py` also runs detect-only tools that no formatter auto-fixes (clang-tidy, shellcheck, cmakelint, `tsc`, clippy), every lint-gated phase's agent instruction now tells the agent to run `format.py` + `lint.py` and **fix every finding** before finishing, so the gate verifies work the agent has already done rather than merely rejecting it.
5. **Checkpoint on green** — commits only when the gate is green, with a `chore(pipeline):` subject and a `Co-authored-by` trailer naming the agent that ran (`Copilot` or `Claude`) (override with `-NoCommit` / `--no-commit`). Any formatting the gate applied is folded into that phase's commit.
6. **Stop on red** — halts at the first failed gate (override with `-ContinueOnFailure` / `--continue-on-failure`; optionally `-RevertOnFailure` / `--revert-on-failure` to hard-reset tracked changes to the last checkpoint — untracked files a failed phase created are left in place, so run `git clean` yourself if you need a full reset; with `-NoCommit` / `--no-commit` there are no checkpoints, so the revert resets to the branch's starting point and discards every phase's work so far).
7. **No remote writes** — `git push` is denied to the agent, so nothing leaves the machine.

Phases whose prompt already builds, tests, and lints exhaustively
(`iterative-improvement`, `release-verification`) are marked *self-verifying*:
the script skips its own redundant gate for them but still checkpoints where
appropriate.

A rejected checkpoint commit — for example when a pre-commit hook blocks it — is
recorded with the `commit-failed` status and handled like any other failed phase
(revert and/or stop), instead of silently leaving the change staged for the next
phase to re-stage over. A `commit-failed` is treated as **systemic**: because a
hook or git keeps rejecting the commit and `git add -A` re-stages the
un-committable change into every later checkpoint, moving on cannot make it
succeed — so it **stops the run even under** `-ContinueOnFailure` /
`--continue-on-failure`, unless `-RevertOnFailure` / `--revert-on-failure` is also
set (which resets the tree clean, making it safe to carry on). Per-file failures
(`agent-failed`, `gate-failed`) still skip forward under `-ContinueOnFailure` as
before. The runner **exits non-zero** whenever it stops early or finishes with any
non-`ok` phase — including `commit-failed` — even under `-ContinueOnFailure` /
`--continue-on-failure`, so `Invoke-LumaAll.ps1` / `luma-all.sh` and CI observe
the failure instead of reading an aborted run as a clean pass.

The lint/format gate runs **`format.py` (apply) then `lint.py` (verify)**:
`format.py` is the mutating step — and it already orders each language's
`lint --fix` before its formatter internally — while `lint.py` is the read-only
verification, so it always runs last and is the authority for whether the gate
passes. clang-tidy is included by default; it needs `build/compile_commands.json`,
which the Ninja and Makefile generators emit but the Visual Studio and Xcode
generators do not. To keep clang-tidy enforced everywhere — Windows included — the
gate **emits the compile database on demand**: when `build/compile_commands.json`
is missing it runs a configure-only pass with a database-capable generator (Ninja,
else NMake / Unix Makefiles) into a dedicated `build-compiledb/` directory and
copies the result into `build/`. On Windows this is wrapped in the MSVC developer
environment (located via `vswhere`) so `cl.exe` and the standard-library headers
resolve for both the configure and the clang-tidy run. Emitting the database is
best-effort: if Ninja, CMake, clang-tidy, or the MSVC environment is unavailable,
`lint.py` simply skips clang-tidy as before rather than failing the gate. A gate
whose tool is not installed is likewise skipped, so the gate enforces exactly the
toolchain you have locally, the same way CI does. Pass `-SkipLintFormat` /
`--skip-lint-format` to opt out of the gate entirely.

`format.py` mechanically fixes the auto-fixable linters (ruff, ESLint, Stylelint,
markdownlint) alongside the formatters, so those findings never reach `lint.py`.
The detect-only tools it cannot auto-fix — clang-tidy, shellcheck, cmakelint,
`tsc`, clippy, and the warning-sync consistency check — are what the agent is
instructed to fix during the phase: the runner appends a directive telling it to
run both scripts and correct the underlying code (not suppress the warnings) until
`lint.py` is clean. The gate is then the deterministic backstop that catches
anything the agent missed. Self-verifying phases already lint exhaustively, so the
directive is omitted for them.

## After the run

The fixer produces **exactly one branch** — `pipeline/fix-<timestamp>`, created
once before the first phase (unless you pass `-NoBranch` / `--no-branch`) — with
**one commit per green phase** (phases marked no-commit, such as
`release-verification`, add none). It performs **no merge and no push**:
`git push` is denied to the agent, and the runner never merges the branch or
touches your base branch. Reviewing and merging `pipeline/fix-<timestamp>` back
into `main` — with `git merge` or by opening a pull request from it — is left to
you. The run ends with a summary printing the branch name and a per-phase
status/commit table to pick up from.

## Per-file conformance

The conformance runner —
[`Invoke-LumaConformance.ps1`](Invoke-LumaConformance.ps1) /
[`luma-conformance.sh`](luma-conformance.sh) — reviews and fixes source files for
conformance to the project's coding-standard guides **one file at a time**. For
each selected file-type target it enumerates every matching tracked file (via
`git ls-files`) and runs one agent session per file, telling the agent to make
that single file conform to the target's guides and to fix every issue it finds.

```powershell
# See the 12 targets and how many files each matches — nothing invoked.
pwsh -File scripts/pipeline/Invoke-LumaConformance.ps1 -List

# See the exact files one target would process.
pwsh -File scripts/pipeline/Invoke-LumaConformance.ps1 -Target cpp -ListFiles

# Preview the agent / cmake / ctest / git commands for one file.
pwsh -File scripts/pipeline/Invoke-LumaConformance.ps1 -Target markdown -MaxFiles 1 -DryRun

# Run for real over one target (Copilot CLI + Claude Opus 4.8 + max effort).
pwsh -File scripts/pipeline/Invoke-LumaConformance.ps1 -Target shell
```

```bash
bash scripts/pipeline/luma-conformance.sh --list
bash scripts/pipeline/luma-conformance.sh --target cpp --list-files
bash scripts/pipeline/luma-conformance.sh --target markdown --max-files 1 --dry-run
bash scripts/pipeline/luma-conformance.sh --target shell
```

### Targets

Each target maps a file type to the authoritative guides its files must satisfy.
Every target also references [`instructions/learnings.instructions.md`](../../instructions/learnings.instructions.md).

| Id               | Files                             | Conforms to                                                                 | Gated |
| ---------------- | --------------------------------- | --------------------------------------------------------------------------- | ----- |
| `cpp`            | `*.cpp` `*.hpp` `*.h`             | `cpp` + `testing`                                                           | yes   |
| `css`            | `*.css`                           | `css`                                                                        | no    |
| `cmake`          | `CMakeLists.txt` `*.cmake`        | `cmake`                                                                      | yes   |
| `github-actions` | `.github/workflows/*.y{a,}ml`     | `github-actions` + `github-actions-recipes`                                 | no    |
| `javascript`     | `*.js` `*.mjs` `*.cjs`            | `javascript` + `testing`                                                    | no    |
| `luma`           | `*.luma`                          | `luma` + `testing` + the seven `documents/Luma_*` guides                    | no    |
| `markdown`       | `*.md`                            | `markdown` + `readme`                                                        | no    |
| `powershell`     | `*.ps1` `*.psm1` `*.psd1`         | `powershell`                                                                 | no    |
| `python`         | `*.py`                            | `python` + `testing`                                                        | no    |
| `rust`           | `*.rs`                            | `rust` + `testing`                                                          | no    |
| `shell`          | `*.sh` `*.bash`                   | `shell`                                                                      | no    |
| `typescript`     | `*.ts` `*.tsx`                    | `typescript` + `testing`                                                    | no    |

Vendored code under `external/` is skipped (the first-party
`external/gui-framework/` is kept), and each runner excludes its own executing
script and shared library from enumeration so a session never rewrites a script
mid-run. Because each runner only self-excludes its **own** pair, the two are
complementary: run the PowerShell runner's `shell` target to conform
`luma-pipeline.sh`, and the shell runner's `powershell` target to conform
`LumaPipeline.psm1`.

### Gating

The runner shares the fix runner's [safety model](#safety-model-fix-runner):
clean tree required, a dedicated `pipeline/conformance-<timestamp>` branch, one
commit per file (`chore(conformance): <target> <path>`), stop-on-failure, and no
push. Only the `cpp` and `cmake` targets change what `cmake --build` + `ctest`
covers, so only those are **gated by the script** — and the green baseline runs
only when a gated target is selected. For every other target the per-file
instruction tells the agent to run the verification appropriate to that file
type (cargo, npm/tsc, shellcheck, PSScriptAnalyzer, markdownlint, actionlint, the
Luma test runner, …) and keep the project green. `-GateMode` / `--gate-mode`
chooses when the script gate runs for the gated targets: `per-target` (default,
once after all of a target's files), `per-file`, or `off`.

As in the fix runner, a rejected checkpoint commit is recorded as `commit-failed`
and treated as a failed file (revert and/or stop). A `commit-failed` is systemic —
`git add -A` re-stages the un-committable change into every later checkpoint — so
it **stops the run even under** `-ContinueOnFailure` / `--continue-on-failure`
unless `-RevertOnFailure` / `--revert-on-failure` is also set; per-file failures
(`agent-failed`, `gate-failed`) still skip forward. The runner **exits non-zero**
when it stops early or finishes with any non-`ok` file, so the failure is visible
to `luma-all.sh` / `Invoke-LumaAll.ps1` and CI.

> **Scale.** The `cpp` (900+) and `luma` (300+) targets match a lot of files, and
> one agent session per file is slow and consuming. Preview with `-List` /
> `-ListFiles`, scope with `-Path` / `--path` (a path substring) and `-MaxFiles`
> / `--max-files`, and run one `-Target` at a time rather than all twelve at once.

## Language evolution

Where audit → fix *improves existing code*, the **discover → evolve** pair
*grows the language*. It drives the `new-*` prompt family: a read-only
**discover** stage that surveys the language and standard library for worthwhile
additions, and a mutating **evolve** stage that implements the ones you pick.

[`Invoke-LumaDiscover.ps1`](Invoke-LumaDiscover.ps1) /
[`luma-discover.sh`](luma-discover.sh) runs `new-requirements.prompt.md` in
**plan** mode and saves the ranked candidate report — each entry tagged with its
*kind* (function, type, module, or feature) — under
`pipeline-artifacts/discover-<timestamp>/reports/new-requirements.md`. Narrow the
survey to one area with `-Focus` / `--focus` (e.g. `-Focus 'String module'`); omit
it for a broad sweep. It is read-only and shares the audit runner's safety model.

[`Invoke-LumaEvolve.ps1`](Invoke-LumaEvolve.ps1) /
[`luma-evolve.sh`](luma-evolve.sh) implements one or more candidates in **agent**
mode. Each candidate is a `(kind, goal)` pair: the *kind* deterministically
selects the implementer prompt, and the *goal* is the concrete thing to build.
Every candidate is gated on build + test + lint and checkpointed on a dedicated
`pipeline/evolve-<timestamp>` branch, reusing the fix runner's
[safety model](#safety-model-fix-runner) verbatim.

```powershell
# Preview without invoking anything: discover's plan, then evolve's kind → prompt map.
pwsh -File scripts/pipeline/Invoke-LumaDiscover.ps1 -DryRun
pwsh -File scripts/pipeline/Invoke-LumaEvolve.ps1   -ListKinds

# 1. Survey for candidate additions (read-only).
pwsh -File scripts/pipeline/Invoke-LumaDiscover.ps1 -Focus 'String module'

# 2. Read pipeline-artifacts/discover-<timestamp>/reports/new-requirements.md
#    and pick what is worth building.

# 3. Implement one candidate, gated on build + test + lint.
pwsh -File scripts/pipeline/Invoke-LumaEvolve.ps1 -Goal 'Add String.center to pad a string to a width' -Kind function
```

```bash
bash scripts/pipeline/luma-discover.sh --dry-run
bash scripts/pipeline/luma-evolve.sh   --list-kinds
bash scripts/pipeline/luma-discover.sh --focus 'String module'
bash scripts/pipeline/luma-evolve.sh --goal 'Add String.center to pad a string to a width' --kind function
```

### Kinds

Each candidate's **kind** routes it to exactly one implementer prompt.
`-ListKinds` / `--list-kinds` prints this mapping.

| Kind       | Implementer prompt               | Adds                                               |
| ---------- | -------------------------------- | -------------------------------------------------- |
| `function` | `new-stdlib-function.prompt.md`  | A built-in function in an existing stdlib module.  |
| `type`     | `new-stdlib-type.prompt.md`      | A record or choice type in an existing module.     |
| `module`   | `new-stdlib-module.prompt.md`    | An entirely new standard-library module.           |
| `feature`  | `new-language-feature.prompt.md` | A language feature across every interpreter phase. |

### Seeding evolve

Evolve does **not** parse the discover report — you seed it with an explicit
goal, so what gets built is always a deliberate choice. Two input modes:

- **One candidate** — `-Goal '<text>' -Kind <kind>` /
  `--goal '<text>' --kind <kind>`.
- **A batch** — `-GoalsFile <path>` / `--goals-file <path>`, one `kind|goal` per
  line. Blank lines and `#` comments are skipped, and only the **first** `|`
  splits the line (so a goal may itself contain `|`):

  ```text
  # kind | goal
  function | Add String.center to pad a string to a width
  module   | Add a Statistics module with mean, median, and mode
  feature  | Add a `where` guard clause to for-loops
  ```

Candidates run in order, each gated and checkpointed independently, so a later
candidate's failure never discards an earlier green one. `-Goal`/`-Kind` and
`-GoalsFile` are mutually exclusive.

### Gating

Evolve shares the fix runner's [safety model](#safety-model-fix-runner): a clean
tree is required, work happens on a dedicated `pipeline/evolve-<timestamp>`
branch, each candidate is gated on build + test then `format.py` + `lint.py`, and
a green candidate is checkpointed as `chore(evolve): <kind> - <goal>` with a
`Co-authored-by` trailer naming the agent. Stop-on-failure, `-ContinueOnFailure`,
`-RevertOnFailure`, and the systemic-`commit-failed` handling all behave exactly
as in the fix runner, and the runner exits non-zero on any non-`ok` candidate.

Because evolve implements whole features and modules — the pipeline's heaviest
work — it defaults, like the conformance runner, to the **Copilot CLI** driving
**Claude Opus 4.8** at **max** effort; override with `-Agent` / `-Model` /
`-Effort` (pass `-Model ''` / `-Effort ''` to defer to the agent's own default).
Because evolve needs a human-seeded goal, it is **not** part of
`Invoke-LumaAll.ps1` / `luma-all.sh`.

## Options

The tables below use the PowerShell flag names. The shell runners take the same
options in `--kebab-case` form (`-Phase` → `--phase`, `-DryRun` → `--dry-run`,
`-ConvergenceMaxPasses` → `--convergence-max-passes`, and so on) and add
`-h` / `--help`. Run `bash scripts/pipeline/luma-audit.sh --help` or
`bash scripts/pipeline/luma-fix.sh --help` for the authoritative shell list.

### `Invoke-LumaAudit.ps1`

| Flag             | Purpose                                                                 |
| ---------------- | ----------------------------------------------------------------------- |
| `-List`          | Print the audit phases in order and exit.                               |
| `-DryRun`        | Print the agent command each phase would run; invoke nothing.           |
| `-Phase <a,b>`   | Run only phases whose Id or Name contains a filter (e.g. `bug-search`). |
| `-Scope <path>`  | Restrict every phase to a scope (e.g. `core/runtime/vm/`).              |
| `-ArtifactRoot <dir>` | Where run artifacts go (default `pipeline-artifacts/`).             |
| `-Agent <name>`  | Agent CLI backend: `copilot` (default) or `claude`.                     |
| `-Model <name>`  | Model override for the chosen agent.                                    |
| `-Effort <lvl>`  | Reasoning effort (`low`…`max`).                                         |

### `Invoke-LumaFix.ps1`

| Flag                        | Purpose                                                           |
| --------------------------- | ---------------------------------------------------------------- |
| `-List` / `-DryRun`         | Same as above (prints agent / `cmake` / `ctest` / `git`).        |
| `-Phase <a,b>`              | Run only matching fix phases (e.g. `bug-fix`, `refactor`).       |
| `-ReportDir <dir>`          | Audit `reports/` folder to consume (default: newest `audit-*` run). |
| `-ArtifactRoot <dir>`       | Where run artifacts go (default `pipeline-artifacts/`).          |
| `-Preset <name>`            | CMake preset for the gate (default `default`).                   |
| `-ConvergenceMaxPasses <n>` | Cap for `iterative-improvement` (default 3).                     |
| `-AllowDirty`               | Do not require a clean working tree.                             |
| `-SkipBaseline`             | Skip the initial green baseline check.                           |
| `-SkipBuild` / `-SkipTest`  | Drop the build or test half of every gate.                       |
| `-SkipLintFormat`           | Skip the lint/format gate (`format.py` + `lint.py`, incl. clang-tidy). |
| `-NoBranch`                 | Work on the current branch instead of a new one.                |
| `-NoCommit`                 | Do not checkpoint after green phases.                            |
| `-ContinueOnFailure`        | Keep going past a red gate instead of stopping.                  |
| `-RevertOnFailure`          | Hard-reset tracked changes to the last checkpoint on a red gate (leaves untracked files; with `-NoCommit` resets to the branch start). |
| `-Agent <name>`             | Agent CLI backend: `copilot` (default) or `claude`.             |
| `-Model` / `-Effort`        | Agent model/effort overrides, as above.                         |

### `Invoke-LumaAll.ps1`

Runs `Invoke-LumaAudit.ps1` and then `Invoke-LumaFix.ps1` in sequence, forwarding
the agent flags to both, and optionally `Invoke-LumaConformance.ps1` as a third
stage. Defaults to the Copilot CLI driving **Claude Opus 4.8** (`claude-opus-4.8`)
at **max** effort. Skips the remaining stages if the audit exits non-zero, skips
the conformance stage if the fix stage fails, and exits with the last stage's exit
code.

| Flag                      | Purpose                                                                          |
| ------------------------- | ------------------------------------------------------------------------------- |
| `-Agent <name>`           | Backend forwarded to every stage: `copilot` (default) or `claude`.              |
| `-Model <name>`           | Model for every stage (default `claude-opus-4.8`; pass `''` to let the agent choose). |
| `-Effort <lvl>`           | Reasoning effort for every stage (default `max`; pass `''` to omit).            |
| `-DryRun`                 | Preview every stage; invoke nothing.                                            |
| `-SkipAudit`              | Skip the audit stage.                                                           |
| `-SkipFix`                | Skip the fix stage.                                                             |
| `-IncludeConformance`     | Also run `Invoke-LumaConformance.ps1` as a third stage (off by default).        |
| `-AuditArgs <args>`       | Extra arguments forwarded verbatim to `Invoke-LumaAudit.ps1`.                   |
| `-FixArgs <args>`         | Extra arguments forwarded verbatim to `Invoke-LumaFix.ps1`.                     |
| `-ConformanceArgs <args>` | Extra arguments forwarded verbatim to `Invoke-LumaConformance.ps1` (scope it, e.g. `-Target`, `cpp`). |

The shell counterpart `luma-all.sh` takes the same options in
`--kebab-case` (`--agent`, `--model`, `--effort`, `--dry-run`, `--skip-audit`,
`--skip-fix`, `--include-conformance`), forwards per-stage extras with the
repeatable `--audit-arg` / `--fix-arg` / `--conformance-arg`, and sends anything
after a literal `--` to every stage.

### `Invoke-LumaConformance.ps1`

Defaults to the Copilot CLI driving **Claude Opus 4.8** (`claude-opus-4.8`) at
**max** effort. Omit `-Target` to run every target in order.

| Flag                       | Purpose                                                                     |
| -------------------------- | --------------------------------------------------------------------------- |
| `-List`                    | List the targets in order with file counts and exit.                        |
| `-ListFiles`               | List the files each selected target would process and exit.                 |
| `-DryRun`                  | Print the agent / `cmake` / `ctest` / `git` commands; invoke nothing.       |
| `-Target <a,b>`            | Run only matching targets — exact id, else id/name substring (e.g. `cpp`).  |
| `-Path <substring>`        | Only process files whose path contains this substring (e.g. `core/runtime/vm/`). |
| `-MaxFiles <n>`            | Cap the number of files processed per target.                               |
| `-ArtifactRoot <dir>`      | Where run artifacts go (default `pipeline-artifacts/`).                      |
| `-Preset <name>`           | CMake preset for the gate (default `default`).                              |
| `-GateMode <mode>`         | When the script gate runs for gated targets: `per-target` (default), `per-file`, `off`. |
| `-AllowDirty`              | Do not require a clean working tree.                                        |
| `-SkipBaseline`            | Skip the initial green baseline (only runs when a gated target is selected). |
| `-SkipBuild` / `-SkipTest` | Drop the build or test half of every gate.                                 |
| `-NoBranch`                | Work on the current branch instead of a new one.                           |
| `-NoCommit`                | Do not checkpoint after each file.                                         |
| `-ContinueOnFailure`       | Keep going past a failed file or gate instead of stopping.                 |
| `-RevertOnFailure`         | Hard-reset tracked changes on failure (to HEAD per file; to the target's start commit on a failed per-target gate). |
| `-Agent <name>`            | Agent CLI backend: `copilot` (default) or `claude`.                        |
| `-Model` / `-Effort`       | Agent model/effort overrides (defaults `claude-opus-4.8` / `max`).         |

The shell counterpart `luma-conformance.sh` takes the same options in
`--kebab-case` (`--target`, `--path`, `--max-files`, `--gate-mode`,
`--list-files`, and so on) plus `-h` / `--help`. Run
`bash scripts/pipeline/luma-conformance.sh --help` for the authoritative list.

### `Invoke-LumaDiscover.ps1`

Read-only; shares the audit runner's model/effort convention (no default — the
agent chooses unless you pass `-Model` / `-Effort`).

| Flag                  | Purpose                                                       |
| --------------------- | ------------------------------------------------------------ |
| `-DryRun`             | Print the agent command the run would issue; invoke nothing. |
| `-Focus <text>`       | Narrow the survey to one area (e.g. `'String module'`); omit for a broad sweep. |
| `-ArtifactRoot <dir>` | Where run artifacts go (default `pipeline-artifacts/`).       |
| `-Agent <name>`       | Agent CLI backend: `copilot` (default) or `claude`.          |
| `-Model <name>`       | Model override for the chosen agent.                         |
| `-Effort <lvl>`       | Reasoning effort (`low`…`max`).                              |

### `Invoke-LumaEvolve.ps1`

Mutating and gated; defaults to the Copilot CLI driving **Claude Opus 4.8**
(`claude-opus-4.8`) at **max** effort. Requires either `-Goal` + `-Kind` or
`-GoalsFile` (mutually exclusive).

| Flag                       | Purpose                                                                     |
| -------------------------- | --------------------------------------------------------------------------- |
| `-ListKinds`               | Print the four kinds and the prompt each routes to, then exit.              |
| `-DryRun`                  | Print the agent / `cmake` / `ctest` / `git` commands; invoke nothing.       |
| `-Goal <text>`             | The single candidate to build (pair with `-Kind`).                          |
| `-Kind <kind>`             | Implementer kind for `-Goal`: `function`, `type`, `module`, or `feature`.   |
| `-GoalsFile <path>`        | Batch input: one `kind\|goal` per line (`#` comments and blanks skipped).   |
| `-ArtifactRoot <dir>`      | Where run artifacts go (default `pipeline-artifacts/`).                      |
| `-Preset <name>`           | CMake preset for the gate (default `default`).                              |
| `-AllowDirty`              | Do not require a clean working tree.                                        |
| `-SkipBaseline`            | Skip the initial green baseline check.                                      |
| `-SkipBuild` / `-SkipTest` | Drop the build or test half of every gate.                                 |
| `-SkipLintFormat`          | Skip the lint/format gate (`format.py` + `lint.py`, incl. clang-tidy).      |
| `-NoBranch`                | Work on the current branch instead of a new one.                           |
| `-NoCommit`                | Do not checkpoint after green candidates.                                  |
| `-ContinueOnFailure`       | Keep going past a red gate instead of stopping.                            |
| `-RevertOnFailure`         | Hard-reset tracked changes to the last checkpoint on a red gate.           |
| `-Agent <name>`            | Agent CLI backend: `copilot` (default) or `claude`.                        |
| `-Model` / `-Effort`       | Agent model/effort overrides (defaults `claude-opus-4.8` / `max`; pass `''` to defer to the agent). |

The shell counterparts `luma-discover.sh` and `luma-evolve.sh` take the same
options in `--kebab-case` (`--focus`, `--goal`, `--kind`, `--goals-file`,
`--list-kinds`, and so on) plus `-h` / `--help`. Run
`bash scripts/pipeline/luma-evolve.sh --help` for the authoritative list.

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
    SUMMARY.md            # table of each phase's status, commit, and log link
    logs/<phase>.log      # per-phase agent transcript
  conformance-<timestamp>/
    SUMMARY.md            # table of each file's target, status, and commit
    logs/<target>/<path>.log  # per-file transcript, nested by source path
  discover-<timestamp>/
    INDEX.md              # table linking the discovery report
    reports/new-requirements.md  # ranked candidate additions, tagged by kind
    logs/                 # agent logs (Copilot --log-dir)
  evolve-<timestamp>/
    SUMMARY.md            # table of each candidate's kind, status, and commit
    logs/<NN-kind>.log    # per-candidate agent transcript
```

The fix runner auto-detects the newest `audit-*` run and feeds each report to
the matching fixer; pass `-ReportDir` / `--report-dir` to pin a specific run.

The default `pipeline-artifacts/` root is git-ignored, so checkpoints never pick
up run artifacts. If you point `-ArtifactRoot` / `--artifact-root` at a custom
location **inside** the working tree that is not git-ignored, the fix runner
unstages that directory before each checkpoint so artifacts are still kept out of
the commit; an artifact root outside the repository needs no special handling.

The `release-verification` phase performs a destructive clean-room wipe
(`git clean -Xdf`) that removes git-ignored files. Because the default
`pipeline-artifacts/` root is git-ignored, the
[release-verification prompt](../../.github/prompts/release-verification.prompt.md)
sets that directory aside inside `.git/` across the wipe and restores it
afterwards, so every prior phase's logs and reports survive the rebuild.
Artifact roots outside the repository — and custom in-tree roots that are not
git-ignored — are left untouched by the clean and need no special handling.

The fix runners (`luma-fix.sh` / `Invoke-LumaFix.ps1`) do not rely on the agent
honouring that prompt step. For the `release-verification` phase they apply the
same protection deterministically: before launching the agent they move the
artifact root into `.git/_pipeline-artifacts.pipeline.bak` (a name distinct from
the prompt's `_pipeline-artifacts.bak`, so the two never collide — during a
pipeline run the runner's set-aside wins and the agent's move-aside simply
no-ops), and they restore it in a `finally`/post-invoke block that runs even if
the phase errors. Crucially, they also stream that one phase's transcript to a
temporary directory **outside** the artifact tree and fold it back into
`logs/` afterwards: on Windows a directory cannot be moved while a file inside
it is held open, and each runner keeps the live phase log open under
`pipeline-artifacts/`, so without this redirection the set-aside would fail and
the wipe would still delete the run's artifacts. This safeguard is skipped for
`--dry-run` / `-DryRun` and for artifact roots outside the working tree.

## Handoff: audit → fix

1. The audit runner writes `reports/01-bug-search-core.md`, `…/08-refactor-audit.md`, and so on.
2. You read them and decide what is worth fixing.
3. The fix runner reads each report as the fixer's input; if a report is absent, that phase self-discovers findings from the prompt instead.

Keeping the reports on disk between the two steps is the human triage gate — the
fixer is never launched automatically from the audit.

Neither runner is wired into CI, a git hook, or any scheduler — every runner in
this directory is developer-invoked from the repository root, and nothing
triggers them automatically. Once you launch a runner it works through its
selected phases on its own, but starting each run is always a deliberate, manual
step.

## Related documentation

- [`scripts/README.md`](../README.md) — the wider maintenance-script catalogue.
- [`instructions/powershell.instructions.md`](../../instructions/powershell.instructions.md) — the PowerShell style the `*.ps1` runners follow.
- [`instructions/shell.instructions.md`](../../instructions/shell.instructions.md) — the shell style the `*.sh` runners follow.
- [`.github/prompts/`](../../.github/prompts) — the prompt definitions the pipeline invokes.
- [`CONTRIBUTING.md`](../../CONTRIBUTING.md) — contributor workflow and commit conventions.
