# Luma — GitHub Actions Workflows

Continuous integration, release, and maintenance workflows for the Luma project. Every workflow file in this directory opens with a header comment block describing what it does; this README indexes them so you can find the right one at a glance.

A few conventions apply throughout:

- **Path-filtered CI.** Most push- and pull-request-triggered workflows are scoped with `paths:` filters, so each runs only when files it cares about change — a Markdown-only edit triggers [`ci-markdown.yml`](ci-markdown.yml) and [`docs.yml`](docs.yml), not the C++ build matrix in [`ci.yml`](ci.yml). The exception is [`codeql.yml`](codeql.yml), which deliberately has no path filter and analyses the C++ sources on every push and pull request to `main`.
- **Pinned actions.** Third-party actions are pinned to a commit SHA (with the version as a trailing comment); [`dependabot.yml`](../dependabot.yml) refreshes those pins weekly.
- **Least privilege.** Each workflow sets its own `permissions:`, kept to the minimum it needs — read-only wherever possible, with write scope only where a job must publish results (the release workflows elevate to `contents: write`; [`codeql.yml`](codeql.yml) requests `security-events: write`). Except for the reusable building blocks, each also guards against overlapping runs with a `concurrency:` group.

---

## 1 — Build and Test

Compile and exercise the interpreter itself.

| Workflow                   | Trigger                     | Purpose                                                     |
| -------------------------- | --------------------------- | ----------------------------------------------------------- |
| [`ci.yml`](ci.yml)         | Push / PR to `main`         | Build and test the interpreter on every supported platform. |
| [`codeql.yml`](codeql.yml) | Push / PR to `main`; weekly | CodeQL static security analysis of the C++ sources.         |

---

## 2 — Linters and Formatters

Static checks for every non-C++ language in the repository; each runs only when files of its kind change.

| Workflow                                 | Trigger             | Purpose                                                      |
| ---------------------------------------- | ------------------- | ------------------------------------------------------------ |
| [`ci-cmake.yml`](ci-cmake.yml)           | Push / PR to `main` | cmakelint over the first-party CMake files.                  |
| [`ci-css.yml`](ci-css.yml)               | Push / PR to `main` | Stylelint over the first-party CSS.                          |
| [`ci-javascript.yml`](ci-javascript.yml) | Push / PR to `main` | ESLint over the GraphicalUi renderer JS.                     |
| [`ci-markdown.yml`](ci-markdown.yml)     | Push / PR to `main` | markdownlint-cli2 over every Markdown file.                  |
| [`ci-powershell.yml`](ci-powershell.yml) | Push / PR to `main` | PSScriptAnalyzer over the PowerShell scripts.                |
| [`ci-python.yml`](ci-python.yml)         | Push / PR to `main` | Ruff lint and format check over the Python tooling.          |
| [`ci-shell.yml`](ci-shell.yml)           | Push / PR to `main` | ShellCheck over the shell scripts.                           |

---

## 3 — Editor Extensions and GUI Framework

Build and validate the two editor integrations and their shared grammar, plus the first-party GraphicalUi browser front-end.

| Workflow                                       | Trigger             | Purpose                                            |
| ---------------------------------------------- | ------------------- | -------------------------------------------------- |
| [`ci-gui-framework.yml`](ci-gui-framework.yml) | Push / PR to `main` | Node.js unit tests for the GraphicalUi front-end.  |
| [`ci-vscode.yml`](ci-vscode.yml)               | Push / PR to `main` | Build and validate the VS Code extension.          |
| [`ci-zed.yml`](ci-zed.yml)                     | Push / PR to `main` | Build the Zed extension and run its grammar tests. |

---

## 4 — Documentation

Keeps the prose and prompt files honest.

| Workflow               | Trigger             | Purpose                                                 |
| ---------------------- | ------------------- | ------------------------------------------------------- |
| [`docs.yml`](docs.yml) | Push / PR to `main` | Fail on stale path references in docs and prompt files. |

---

## 5 — Scheduled and On-Demand

Heavier quality gates that run on a timer or on request rather than on every change.

| Workflow                         | Trigger                | Purpose                                                        |
| -------------------------------- | ---------------------- | -------------------------------------------------------------- |
| [`benchmark.yml`](benchmark.yml) | Push to `main`; manual | Run the benchmark suite and compare against a cached baseline.  |
| [`ci-tsan.yml`](ci-tsan.yml)     | Weekly; manual         | Best-effort ThreadSanitizer race detection over the test suite. |
| [`fuzz.yml`](fuzz.yml)           | Weekly; manual         | Build the libFuzzer targets and fuzz each one in parallel.      |

---

## 6 — Releases

Tag-triggered publishing; each responds to its own tag prefix. [`release.yml`](release.yml) builds the interpreter for every platform and cuts the main GitHub Release (bundling the VS Code extension archive alongside the binaries), while the two extension workflows handle their own stores. [`tag-release.yml`](tag-release.yml) is the on-demand counterpart: dispatch it to create the `v*.*.*` tag from the [`VERSION`](../../VERSION) file (validated and guarded against duplicates), which then triggers [`release.yml`](release.yml). It pushes the tag with the `RELEASE_PAT` secret so that trigger fires — see [Releasing](../../CONTRIBUTING.md#releasing) for the one-time secret setup.

For the step-by-step release procedure — how these tag-triggered Releases differ from the temporary CI build artifacts, and how to cut one — see [Releasing](../../CONTRIBUTING.md#releasing) in the contributing guide.

| Workflow                                   | Trigger             | Purpose                                                         |
| ------------------------------------------ | ------------------- | --------------------------------------------------------------- |
| [`release.yml`](release.yml)               | Tag `v*.*.*`        | Build cross-platform binaries and publish a GitHub Release.     |
| [`tag-release.yml`](tag-release.yml)       | Manual              | Validate the `VERSION` file and push the `v*.*.*` release tag.  |
| [`release-vscode.yml`](release-vscode.yml) | Tag `vscode-v*.*.*` | Publish the VS Code extension to the Visual Studio Marketplace. |
| [`release-zed.yml`](release-zed.yml)       | Tag `zed-v*.*.*`    | Build the Zed extension and attach it to a GitHub Release.      |

---

## 7 — Reusable Building Blocks

Internal `workflow_call` components — never triggered directly, only invoked by the workflows above.

| Workflow                                                 | Trigger                   | Purpose                                                            |
| -------------------------------------------------------- | ------------------------- | ------------------------------------------------------------------ |
| [`reusable-linux-build.yml`](reusable-linux-build.yml)   | Called by other workflows | Build and test inside a Linux container image.                     |
| [`build-raspberry-pi.yml`](build-raspberry-pi.yml)       | Called by other workflows | Raspberry Pi OS (ARM64) build on a native ARM64 runner; wraps the container build. |
| [`resolve-distro-matrix.yml`](resolve-distro-matrix.yml) | Called by other workflows | Expose linux-distros.json as a job matrix.                         |

---

## 8 — How the Reusable Workflows Fit Together

[`ci.yml`](ci.yml) and [`release.yml`](release.yml) fan out over the same Linux building blocks. [`resolve-distro-matrix.yml`](resolve-distro-matrix.yml) turns the single distro list into a matrix, [`reusable-linux-build.yml`](reusable-linux-build.yml) runs one containerised build per entry, and [`build-raspberry-pi.yml`](build-raspberry-pi.yml) is a thin wrapper that pins the Raspberry Pi OS (ARM64) toolchain on top of it.

```text
ci.yml
├── resolve-distro-matrix.yml      reads .github/workflows/linux-distros.json → matrix
├── reusable-linux-build.yml       one job per distro in the matrix
└── build-raspberry-pi.yml
    └── reusable-linux-build.yml   Raspberry Pi OS (ARM64), native runner

release.yml
├── resolve-distro-matrix.yml
├── reusable-linux-build.yml       validate the release on each distro
└── build-raspberry-pi.yml
    └── reusable-linux-build.yml   Raspberry Pi OS (ARM64) release binary
```

---

## 9 — Required Status Checks (Branch Protection)

If you gate merges to `main` on status checks (through a branch protection rule or a repository ruleset), the **path filters above decide which checks are safe to require**. A required check only clears a pull request once it reports a result — and a path-filtered workflow that a given PR doesn't trigger never reports one, leaving that PR stuck on _"Expected — waiting for status to be reported."_ Requiring a path-filtered check therefore deadlocks every PR that doesn't touch its paths: a docs-only PR would wait forever on the C++ build matrix.

Only [`codeql.yml`](codeql.yml) runs on every pull request regardless of paths, so its four `Analyze (…)` checks are the only ones safe to require **unconditionally**:

- `Analyze (c-cpp)`
- `Analyze (actions)`
- `Analyze (javascript-typescript)`
- `Analyze (python)`

To hard-require the build/test/lint checks as well without deadlocking unrelated PRs, either leave them **advisory** (they still run and show on every PR — they just don't block the merge), or first make each workflow always report: move its `paths:` filter off the `on:` trigger and onto a `changes` gate job so skipped work reports success instead of never running.

Two caveats when picking checks:

- **`ThreadSanitizer`** now lives in its own scheduled/manual workflow ([`ci-tsan.yml`](ci-tsan.yml)), so it never runs on a pull request and cannot be a required check — leave it out. libtsan cannot initialise on the current hosted `ubuntu-24.04` image, so it ran perpetually red on every push; the `ubuntu-latest — gcc-13-sanitizers` build (AddressSanitizer + UBSan) in [`ci.yml`](ci.yml) is the gating sanitizer check.
- A check only appears in the ruleset picker **after it has run against a PR at least once**, so open one pull request and let CI run before selecting from the list. Reusable-workflow checks (the distro and Raspberry Pi builds) appear under a `Caller / Callee` name.

For reference, the checks each pull-request-triggered workflow reports:

| Workflow                                       | Reported checks (job names)                                                                                                                                                                                                                                                                                                                                                                                              |
| ---------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| [`codeql.yml`](codeql.yml)                     | `Analyze (c-cpp)`, `Analyze (actions)`, `Analyze (javascript-typescript)`, `Analyze (python)`                                                                                                                                                                                                                                                                                                                            |
| [`ci.yml`](ci.yml)                             | `ubuntu-latest — gcc-14`, `macos-latest — clang`, `windows-latest — msvc`, `ubuntu-latest — gcc-13-sanitizers`, `Static Analysis (clang-tidy) (0–3)`, `Formatting (clang-format)`, `Warning Flag Sync`, `Code Coverage`, `Feature Flag Build (no-tls)`, `Feature Flag Build (no-webview)`, `Resolve Linux distro matrix`, the four distro builds (`Debian (trixie)`, `Kali Linux`, `Fedora`, `Arch Linux`), and `Raspberry Pi OS (ARM64)` |
| [`docs.yml`](docs.yml)                         | `Documentation Consistency`                                                                                                                                                                                                                                                                                                                                                                                              |
| [`ci-cmake.yml`](ci-cmake.yml)                 | `cmakelint`                                                                                                                                                                                                                                                                                                                                                                                                              |
| [`ci-css.yml`](ci-css.yml)                     | `stylelint`                                                                                                                                                                                                                                                                                                                                                                                                              |
| [`ci-javascript.yml`](ci-javascript.yml)       | `ESLint`                                                                                                                                                                                                                                                                                                                                                                                                                 |
| [`ci-markdown.yml`](ci-markdown.yml)           | `markdownlint`                                                                                                                                                                                                                                                                                                                                                                                                           |
| [`ci-powershell.yml`](ci-powershell.yml)       | `PSScriptAnalyzer`                                                                                                                                                                                                                                                                                                                                                                                                       |
| [`ci-python.yml`](ci-python.yml)               | `ruff`                                                                                                                                                                                                                                                                                                                                                                                                                   |
| [`ci-shell.yml`](ci-shell.yml)                 | `ShellCheck`                                                                                                                                                                                                                                                                                                                                                                                                             |
| [`ci-gui-framework.yml`](ci-gui-framework.yml) | `node --test`                                                                                                                                                                                                                                                                                                                                                                                                            |
| [`ci-vscode.yml`](ci-vscode.yml)               | `VS Code Extension`                                                                                                                                                                                                                                                                                                                                                                                                      |
| [`ci-zed.yml`](ci-zed.yml)                     | `Zed Extension`, `Tree-sitter Grammar Tests`                                                                                                                                                                                                                                                                                                                                                                             |

[`benchmark.yml`](benchmark.yml) and [`fuzz.yml`](fuzz.yml) don't run on pull requests, so they can't be required.

---

## 10 — Related Documentation

- [github-actions.instructions.md](../../instructions/github-actions.instructions.md) — the conventions these workflows follow (triggers, permissions, caching, security).
- [github-actions-recipes.instructions.md](../../instructions/github-actions-recipes.instructions.md) — copy-paste workflow recipes and debugging guidance.
- [build.instructions.md](../../instructions/build.instructions.md) — the CMake presets, sanitizers, and coverage the build and test jobs drive.
- [../actions](../actions) — the composite actions these workflows call (`apt-install`, `cmake-build`, `package-binaries`, `build-vscode-extension`, `build-zed-extension`).
- [linux-distros.json](linux-distros.json) — the distribution matrix [`resolve-distro-matrix.yml`](resolve-distro-matrix.yml) loads.
- [stale-path-denylist.txt](../stale-path-denylist.txt) — the patterns [`docs.yml`](docs.yml) rejects.
- [prompts/DIRECTORY.md](../prompts/DIRECTORY.md) and [hooks/DIRECTORY.md](../hooks/DIRECTORY.md) — the sibling `.github/` indexes.

> **Note:** When you add, rename, or remove a workflow, update the tables above so this index stays accurate.
