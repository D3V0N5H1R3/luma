# Maintenance Scripts

Build, test, benchmarking, and developer-tooling helpers that support the Luma project. Each script is a small, focused wrapper invoked either by hand during development or by a CI workflow, keeping the root build, the workflows, and the contributor setup consistent across platforms.

For the build workflow — presets, sanitizers, and coverage — see [build.instructions.md](../instructions/build.instructions.md). For the Python style these scripts follow, see [python.instructions.md](../instructions/python.instructions.md). The directory is also summarised in the source tree in [Luma_Software_Architecture.md](../documents/Luma_Software_Architecture.md).

## Structure

Each script carries a module-level docstring describing its full behaviour and options; the table below is a quick index.

| Script                                                     | Purpose                                                                                              | Runs from                                                  |
| ---------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| [`_common.py`](_common.py)                                 | Python 3.10 version gate, repo-root path constant, and subprocess wrapper imported by all other Python scripts. | Imported by scripts                                        |
| [`_gates.py`](_gates.py)                                   | `Gate` class used by `lint.py` and `format.py` — resolves tool paths, runs each tool, prints pass/skip/fail summary. Not run directly. | Imported by scripts                                        |
| [`agent-hooks/format_cpp_on_edit.py`](agent-hooks/format_cpp_on_edit.py)   | clang-format the `.cpp`/`.hpp` file the AI agent just edited (`PostToolUse`).          | Agent hook ([`.github/hooks/`](../.github/hooks))          |
| [`agent-hooks/protect_vendored_paths.py`](agent-hooks/protect_vendored_paths.py) | Block AI-agent edits to vendored code under `external/` (`PreToolUse`).            | Agent hook ([`.github/hooks/`](../.github/hooks))          |
| [`check_benchmark_suite.py`](check_benchmark_suite.py)     | Verify `benchmarks/suite.luma` includes and runs every `bench_*.luma` module.                        | [Benchmark CI](../.github/workflows/benchmark.yml); manual |
| [`check_warning_sync.py`](check_warning_sync.py)           | Verify that GCC/Clang warning flags in [`LumaCompilerFlags.cmake`](../cmake/LumaCompilerFlags.cmake) and the checks in `.clang-tidy` agree. | [CI](../.github/workflows/ci.yml) (`--strict`); manual     |
| [`compare_benchmarks.py`](compare_benchmarks.py)           | Compare current benchmark results (JSON) against a cached baseline and exit non-zero if any benchmark regressed beyond the allowed threshold. | [Benchmark CI](../.github/workflows/benchmark.yml); manual |
| [`configure.py`](configure.py)                             | Developer setup helper: list available CMake presets, configure a build with one, optionally build, and enable the Git hooks. | Manual                                                     |
| [`container-build.sh`](container-build.sh)                 | Configure, build, and run the full CTest suite using `$CC` / `$CXX`.                                 | CI container matrix                                        |
| [`format.py`](format.py)                                   | Run every auto-formatter (clang-format, ruff, markdownlint, etc.) across the repo. The write counterpart to `lint.py` — fixes what it can, reports what it cannot. | Manual; editor tasks                                       |
| [`generate_coverage.py`](generate_coverage.py)             | Configure a coverage build with GCC or Clang, run the tests, and produce an HTML report via lcov/genhtml (or gcovr). | Manual                                                     |
| [`generate_gui_assets.mjs`](generate_gui_assets.mjs)       | Compress and embed the vendored GUI libraries (lit-html, Pico CSS, uPlot, Lucide icons, renderer JS) into `graphicalui_assets.hpp` as C++ byte arrays. | Manual (Node)                                              |
| [`generate_prelude_asset.mjs`](generate_prelude_asset.mjs) | Embed the Solaris prelude source (`gui_prelude.luma`) into `gui_prelude_generated.hpp` as an uncompressed C++ byte array. | Manual (Node)                                              |
| [`hooks/commit-msg`](hooks/commit-msg)                     | Enforce the project's Conventional Commits message format.                                           | Git (`core.hooksPath`)                                     |
| [`hooks/pre-commit`](hooks/pre-commit)                     | Run clang-format (and clang-tidy when available) over staged C++ files.                              | Git (`core.hooksPath`)                                     |
| [`install_hooks.py`](install_hooks.py)                     | Set `core.hooksPath` to the tracked `hooks/` directory so the pre-commit and commit-msg hooks run on every commit. | Manual; via `configure.py`                                 |
| [`lint.py`](lint.py)                                       | Run every CI lint check locally in one command (clang-tidy, ruff, shellcheck, etc.). Read-only — reports issues but never modifies files. Skips tools that aren't installed. | Manual; editor tasks                                       |
| [`parse_benchmark_results.py`](parse_benchmark_results.py) | Parse the text output of `benchmarks/suite.luma` into a JSON map of benchmark names to per-iteration times in milliseconds. | [Benchmark CI](../.github/workflows/benchmark.yml); manual |
| [`pipeline/Invoke-LumaAll.ps1`](pipeline/Invoke-LumaAll.ps1) | Run the audit then the fix in one command.                                                        | Manual                                                     |
| [`pipeline/Invoke-LumaAudit.ps1`](pipeline/Invoke-LumaAudit.ps1) | Run the read-only improvement-prompt audits via an agent CLI and save ranked reports.          | Manual                                                     |
| [`pipeline/Invoke-LumaFix.ps1`](pipeline/Invoke-LumaFix.ps1) | Apply the fixer prompts via an agent CLI, gated on build + test with git checkpoints.             | Manual                                                     |
| [`pipeline/luma-all.sh`](pipeline/luma-all.sh)             | Shell (macOS/Linux) counterpart of `Invoke-LumaAll.ps1`.                                             | Manual                                                     |
| [`pipeline/luma-audit.sh`](pipeline/luma-audit.sh)         | Shell (macOS/Linux) counterpart of `Invoke-LumaAudit.ps1`.                                           | Manual                                                     |
| [`pipeline/luma-fix.sh`](pipeline/luma-fix.sh)             | Shell (macOS/Linux) counterpart of `Invoke-LumaFix.ps1`.                                             | Manual                                                     |
| [`run_examples.py`](run_examples.py)                       | Run and verify every `examples/` program headlessly, including `@test` blocks.                       | [CI](../.github/workflows/ci.yml); manual                  |
| [`run_luma_tests.py`](run_luma_tests.py)                   | Discover all `.luma` files under a directory and run each with the interpreter in `--strict --test` mode, reporting a pass/fail summary. | Manual; editor tasks                                       |
| [`run_psscriptanalyzer.ps1`](run_psscriptanalyzer.ps1)     | Lint first-party PowerShell scripts with PSScriptAnalyzer using the repo settings.                   | [CI](../.github/workflows/ci-powershell.yml); manual       |
| [`tsan_suppressions.txt`](tsan_suppressions.txt)           | ThreadSanitizer suppression list referenced by the CI sanitizer job. Not a script — plain-text data. | [CI](../.github/workflows/ci.yml) (`TSAN_OPTIONS`)         |

## Conventions

- **Python 3.10+.** The Python scripts share a single version gate in [`_common.py`](_common.py); importing it early (which the other scripts do) aborts with a clear message on older interpreters. `_common.py` also exposes `REPO_ROOT` and a `run()` wrapper, so the scripts resolve paths and invoke subprocesses the same way. The agent hooks in [`agent-hooks/`](agent-hooks) are the deliberate exception: they import only the standard library so they stay dependency-free and fail open (see [Agent Hooks](#agent-hooks)).
- **Mixed toolchain.** The directory is predominantly Python; the non-Python helpers are [`generate_gui_assets.mjs`](generate_gui_assets.mjs) and [`generate_prelude_asset.mjs`](generate_prelude_asset.mjs) (Node), [`container-build.sh`](container-build.sh) (POSIX shell, kept free of bashisms for CI containers), [`run_psscriptanalyzer.ps1`](run_psscriptanalyzer.ps1) and the [`pipeline/`](pipeline) prompt runners (PowerShell `*.ps1` plus their bash `*.sh` counterparts), and the [`hooks/`](hooks) Git hooks.
- **Run from the repository root.** Paths are resolved relative to the repository root (via `_common.py`), so invoke the scripts as `python scripts/<name>.py` rather than from inside this directory.

## Linting and Formatting

Every language in the repository has a dedicated linter or formatter, each enforced by its own CI workflow (see [CONTRIBUTING.md](../CONTRIBUTING.md#linters-and-formatters)). [`lint.py`](lint.py) and [`format.py`](format.py) run that whole surface locally from a single command, so a contributor can reproduce the CI lint result — or auto-fix most of it — before pushing.

```bash
python scripts/lint.py            # run every check gate, mirroring CI
python scripts/format.py          # apply every auto-formatter and safe auto-fix
python scripts/lint.py --list     # show the gates and which are available
python scripts/lint.py --skip clang-tidy    # skip the slowest gate
python scripts/lint.py --only ruff,shellcheck
```

Both scripts run each tool the way its workflow does and print a single pass/fail summary. A gate whose tool (or prerequisite — a configured `build/` for clang-tidy, `extensions/vscode/node_modules` for the extension gates) is missing is reported as *skipped* rather than failing, so the runners are useful with a full toolchain or only part of one. `lint.py` only checks; `format.py` writes. Both share the gate infrastructure in [`_gates.py`](_gates.py).

## Git Hooks

The hooks in [`hooks/`](hooks) are version-controlled rather than copied into `.git/hooks`, so they update on `git pull` and a repository-local `core.hooksPath` reliably overrides any global hooks directory. Two hooks are tracked: [`pre-commit`](hooks/pre-commit) checks staged C++ files with clang-format (and clang-tidy when a compile database exists), and [`commit-msg`](hooks/commit-msg) enforces the [Conventional Commits](https://www.conventionalcommits.org/) format documented in [CONTRIBUTING.md](../CONTRIBUTING.md#commit-messages). Enable them once per clone:

```bash
python scripts/install_hooks.py
```

`python scripts/configure.py <preset>` also enables the hooks as part of configuring a build, so a contributor who configures through that helper gets them automatically. See [CONTRIBUTING.md](../CONTRIBUTING.md) for the full contributor setup.

## Agent Hooks

The scripts in [`agent-hooks/`](agent-hooks) are AI-agent lifecycle hooks rather than build tooling: the agent runtime runs them around its tool calls to clang-format C++ the moment it is edited and to block edits to vendored `external/` code. They are registered by the JSON files in [`.github/hooks/`](../.github/hooks) and documented in full — rationale, scope, and how to test them — in [.github/hooks/README.md](../.github/hooks/README.md). Unlike the other scripts here they are standalone and dependency-free (no `_common.py`) and always fail open, so a missing interpreter or unexpected input is a clean no-op rather than a blocked agent. They complement the Git hooks above: the agent hooks act as the agent edits, the Git hooks act at commit time.

## Prompt Pipeline

The [`pipeline/`](pipeline) directory drives the repository's improvement prompts ([`.github/prompts/`](../.github/prompts)) through an agentic CLI — the [GitHub Copilot CLI](https://docs.github.com/en/copilot/reference/copilot-cli-reference) (default) or the [Claude Code CLI](https://code.claude.com/docs/en/cli-reference), selected per run with `-Agent` / `--agent` — in a repeatable order via two runners — an **audit** runner that runs the read-only audits and saves ranked reports, and a **fix** runner that applies the fixer prompts on a dedicated branch, gating each phase on `cmake --build` + `ctest` and checkpointing only a green tree. Each is provided for both PowerShell ([`Invoke-LumaAudit.ps1`](pipeline/Invoke-LumaAudit.ps1), [`Invoke-LumaFix.ps1`](pipeline/Invoke-LumaFix.ps1)) and bash ([`luma-audit.sh`](pipeline/luma-audit.sh), [`luma-fix.sh`](pipeline/luma-fix.sh)), sharing [`LumaPipeline.psm1`](pipeline/LumaPipeline.psm1) and [`luma-pipeline.sh`](pipeline/luma-pipeline.sh) respectively; both support `-List` / `--list` and `-DryRun` / `--dry-run` so the planned agent / `cmake` / `git` commands can be inspected without invoking anything. A convenience wrapper — [`Invoke-LumaAll.ps1`](pipeline/Invoke-LumaAll.ps1) / [`luma-all.sh`](pipeline/luma-all.sh) — runs the audit and then the fix in a single command. Run artifacts land in the git-ignored `pipeline-artifacts/` directory. See [pipeline/README.md](pipeline/README.md) for the full ordering rationale, safety model, and options.

## Related Documentation

- [build.instructions.md](../instructions/build.instructions.md) — Build workflow, presets, sanitizers, and coverage.
- [python.instructions.md](../instructions/python.instructions.md) — Python style and conventions these scripts follow.
- [benchmarks/README.md](../benchmarks/README.md) — How the benchmark suite and its parse/compare scripts fit together.
- [CONTRIBUTING.md](../CONTRIBUTING.md) — Contributor setup, including the Git hooks and feature-test runner.
- [Luma_Software_Architecture.md](../documents/Luma_Software_Architecture.md) — Project source tree, including this directory.
