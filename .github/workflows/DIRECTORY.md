# Luma — GitHub Actions Workflows

Continuous integration, release, and maintenance workflows for the Luma project. Every workflow file in this directory opens with a header comment block describing what it does; this index lists them so you can find the right one at a glance.

A few conventions apply throughout:

- **Path-filtered CI.** Most push- and pull-request-triggered workflows are scoped with `paths:` filters, so each runs only when files it cares about change — a Markdown-only edit triggers [`ci-markdown.yml`](ci-markdown.yml) and [`docs.yml`](docs.yml), not the C++ build matrix in [`ci.yml`](ci.yml). The exception is [`codeql.yml`](codeql.yml), which deliberately has no path filter and analyses the C++, Rust, Python, JavaScript/TypeScript, and GitHub Actions sources on every push and pull request to `main`.
- **Pinned actions.** Third-party actions are pinned to a commit SHA (with the version as a trailing comment); [`dependabot.yml`](../dependabot.yml) refreshes those pins weekly.
- **Least privilege.** Each workflow sets its own `permissions:`, kept to the minimum it needs — read-only wherever possible, with write scope only where a job must publish results (the release workflows elevate to `contents: write`; [`codeql.yml`](codeql.yml) requests `security-events: write`). Except for the reusable building blocks, each also guards against overlapping runs with a `concurrency:` group.

---

## 1 — Build and Test

Compile and exercise the interpreter itself.

| Workflow                   | Trigger                     | Purpose                                                     |
| -------------------------- | --------------------------- | ----------------------------------------------------------- |
| [`ci.yml`](ci.yml)         | Push / PR to `main`         | Build and test the interpreter on every supported platform. |
| [`codeql.yml`](codeql.yml) | Push / PR to `main`; weekly | CodeQL security scans of C++, Rust, Python, JS/TS, Actions. |

---

## 2 — Linters and Formatters

Static checks for every non-C++ language in the repository; each runs only when files of its kind change.

| Workflow                                 | Trigger             | Purpose                                                                                                   |
| ---------------------------------------- | ------------------- | --------------------------------------------------------------------------------------------------------- |
| [`ci-cmake.yml`](ci-cmake.yml)           | Push / PR to `main` | cmakelint over the first-party CMake files.                                                               |
| [`ci-markdown.yml`](ci-markdown.yml)     | Push / PR to `main` | markdownlint-cli2 over every Markdown file.                                                               |
| [`ci-powershell.yml`](ci-powershell.yml) | Push / PR to `main` | PSScriptAnalyzer over the PowerShell scripts.                                                             |
| [`ci-python.yml`](ci-python.yml)         | Push / PR to `main` | Ruff lint/format over the Python tooling, plus the extension tooling, agent hook, Git hook, and prompt pipeline tests. |
| [`ci-shell.yml`](ci-shell.yml)           | Push / PR to `main` | ShellCheck over the shell scripts and the Git hooks.                                                      |

---

## 3 — Editor Extensions

Build and validate the two editor integrations and their shared grammar.

| Workflow                         | Trigger             | Purpose                                            |
| -------------------------------- | ------------------- | -------------------------------------------------- |
| [`ci-vscode.yml`](ci-vscode.yml) | Push / PR to `main` | Build and validate the VS Code extension.          |
| [`ci-zed.yml`](ci-zed.yml)       | Push / PR to `main` | Build the Zed extension and run its grammar tests. |

---

## 4 — Documentation

Keeps the prose and prompt files honest.

| Workflow               | Trigger             | Purpose                                                 |
| ---------------------- | ------------------- | ------------------------------------------------------- |
| [`docs.yml`](docs.yml) | Push / PR to `main` | Fail on stale path references in docs and prompt files. |

---

## 5 — Scheduled and On-Demand

Heavier quality gates that run on a timer or on request rather than on every change.

| Workflow                         | Trigger                | Purpose                                                                                    |
| -------------------------------- | ---------------------- | ------------------------------------------------------------------------------------------ |
| [`benchmark.yml`](benchmark.yml) | Push to `main`; manual | Run the benchmark suite and compare against a cached baseline.                             |
| [`ci-tsan.yml`](ci-tsan.yml)     | Weekly; manual         | Best-effort ThreadSanitizer race detection over the test suite.                            |
| [`fuzz.yml`](fuzz.yml)           | Nightly; manual        | Build the libFuzzer targets and fuzz each one in parallel with a persisted, seeded corpus. |

---

## 6 — Releases

Tag-triggered publishing; each responds to its own tag prefix. The core release filter is digits-only (`v[0-9]+.[0-9]+.[0-9]+`) because a `v*.*.*` glob would also match `vscode-v1.2.3`. [`release.yml`](release.yml) builds the interpreter for every platform from a clean tree, checks the tag matches [`VERSION`](../../VERSION), and cuts the main GitHub Release (bundling the VS Code extension archive alongside the binaries), while the two extension workflows handle their own stores. [`tag-release.yml`](tag-release.yml) is the on-demand counterpart: dispatch it **from `main`** to create the `vMAJOR.MINOR.PATCH` tag from the [`VERSION`](../../VERSION) file (validated and guarded against duplicates and non-`main` dispatches), which then triggers [`release.yml`](release.yml). It pushes the tag with the `RELEASE_PAT` secret so that trigger fires — see [One-Time Setup: The `RELEASE_PAT` Secret](#one-time-setup-the-release_pat-secret).

| Workflow                                   | Trigger                  | Purpose                                                                   |
| ------------------------------------------ | ------------------------ | ------------------------------------------------------------------------- |
| [`release.yml`](release.yml)               | Tag `vMAJOR.MINOR.PATCH` | Build cross-platform binaries and publish a GitHub Release.               |
| [`tag-release.yml`](tag-release.yml)       | Manual (from `main`)     | Validate the `VERSION` file and push the `vMAJOR.MINOR.PATCH` tag.        |
| [`release-vscode.yml`](release-vscode.yml) | Tag `vscode-v*.*.*`      | Publish the VS Code extension to the Visual Studio Marketplace.           |
| [`release-zed.yml`](release-zed.yml)       | Tag `zed-v*.*.*`         | Build the Zed extension and attach it to a GitHub Release as one archive. |

### Build Artifacts Versus Releases

Luma produces builds in two distinct places:

|             | **Actions build artifacts**                                                                                                             | **GitHub Releases**                                                                                                                                                                                     |
| ----------- | --------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Produced by | [`ci.yml`](ci.yml) on every build                                                                                                       | [`release.yml`](release.yml) when a version tag is pushed                                                                                                                                               |
| Lifetime    | **Temporary** — auto-deleted after 7 days (`retention-days: 7`; the coverage report keeps 14)                                           | **Permanent** — kept until you delete the release                                                                                                                                                       |
| Where       | An Actions run's **Artifacts** section on the [Actions page](https://github.com/D3V0N5H1R3/luma/actions), or `gh run download <run-id>` | The repository's [Releases page](https://github.com/D3V0N5H1R3/luma/releases), or `gh release download <tag>`                                                                                           |
| Named       | `luma-<os>-<compiler>` (e.g. `luma-ubuntu-latest-gcc`)                                                                                  | Per platform: the full bundle `luma-<os>-<arch>`, plus standalone `luma_lsp-<os>-<arch>` and `luma_dap-<os>-<arch>` archives (`.tar.gz` / `.zip`); also the VS Code `.vsix` and a `SHA256SUMS` manifest |
| Use it for  | Debugging or smoke-testing the build from one specific commit                                                                           | Distributing a version to users — the canonical download                                                                                                                                                |

Because [`ci.yml`](ci.yml) is path-filtered, a docs- or workflow-only commit does not trigger a build, so the newest artifacts may sit on an earlier commit than `HEAD`. Release assets never expire, so they are what users should download.

### Cutting a Release

Releases are **tag-triggered**: pushing a tag of the form `vMAJOR.MINOR.PATCH` runs [`release.yml`](release.yml), which checks the tag matches [`VERSION`](../../VERSION), builds every platform (Linux x86_64, Linux aarch64 / Raspberry Pi, macOS, Windows), validates on the Linux distros, packages the VS Code `.vsix`, generates a changelog from the commit log since the previous `v` tag, and publishes a GitHub Release with all binaries and a `SHA256SUMS` manifest attached.

> **Release contract — always ship the per-binary archives.** Every release **must** attach the standalone `luma_lsp-<os>-<arch>` and `luma_dap-<os>-<arch>` archives (and the `SHA256SUMS` covering them) for all supported platforms, not just the full `luma-<os>-<arch>` bundle. The VS Code and Zed extensions auto-download these per-binary archives from the Releases page on demand — the language server on activation, the debug adapter on first debug session — and verify them against `SHA256SUMS` before extracting (see [`extensions/shared/binary-download/SPECIFICATION.md`](../../extensions/shared/binary-download/SPECIFICATION.md)). A release missing them breaks language features and debugging for extension users on that version. The [`package-binaries`](../actions/package-binaries/action.yml) action emits both the bundle and the per-binary archives on every run so this holds automatically; keep it that way.

1. Bump the version on `main` with [`scripts/bump_version.py`](../../scripts/bump_version.py). It rewrites [`VERSION`](../../VERSION) — the single source of truth for the project version — together with the extension manifests and the documentation references that embed the version, so they never drift apart. Then commit the result:

    ```bash
    git switch main && git pull
    python scripts/bump_version.py --minor   # or --patch, --major, or an explicit MAJOR.MINOR.PATCH
    git commit -am "chore: bump version to $(python scripts/bump_version.py --current)"
    git push
    ```

2. Create the `v`-prefixed tag that matches `VERSION`. Use **either** option:

    **A. Automated (recommended) — [`tag-release.yml`](tag-release.yml).** From the repository's **Actions → Tag Release** page, run the workflow (`workflow_dispatch`) **on the `main` branch** (it refuses any other ref) and enter the new version in the `confirm_version` input. It reads [`VERSION`](../../VERSION), validates it is a strict `MAJOR.MINOR.PATCH` version, confirms your input matches the file, checks the `v<VERSION>` tag does not already exist, then creates and pushes the annotated tag for you — which triggers [`release.yml`](release.yml). Tick `dry_run` first to validate without pushing. This requires the one-time `RELEASE_PAT` secret setup described below.

    **B. Manual.** Create an **annotated** tag whose version matches `VERSION`, prefixed with `v`, and push it:

    ```bash
    git tag -a v0.15.0 -m "Release version 0.15.0"
    git push origin v0.15.0
    ```

3. Watch the **Release** workflow run under [Actions](https://github.com/D3V0N5H1R3/luma/actions). When it finishes, the new release (with its binaries) appears on the [Releases](https://github.com/D3V0N5H1R3/luma/releases) page. It publishes immediately — there is no manual draft step.

The two editor extensions publish from their own tag prefixes — `vscode-v*.*.*` ([`release-vscode.yml`](release-vscode.yml), to the Visual Studio Marketplace) and `zed-v*.*.*` ([`release-zed.yml`](release-zed.yml)) — so they can be versioned independently of the interpreter. See [Git tag conventions](../../instructions/git.instructions.md) for the tag commands.

### One-Time Setup: The `RELEASE_PAT` Secret

The automated [`tag-release.yml`](tag-release.yml) workflow needs a `RELEASE_PAT` repository secret. This is required because a tag pushed with the default `GITHUB_TOKEN` does **not** trigger another workflow (GitHub deliberately blocks recursive workflow runs), so the tag must be pushed with a separate token for [`release.yml`](release.yml) to fire. The workflow fails fast with an explanatory message if the secret is missing, so it never pushes a tag that would silently never release.

Configure it once (a repository **admin** with a Personal Access Token, or a GitHub App token, is needed):

1. Create a **fine-grained Personal Access Token**: GitHub → your **Settings** → **Developer settings** → **Personal access tokens** → **Fine-grained tokens** → **Generate new token**.
2. Scope it to the `D3V0N5H1R3/luma` repository (**Only select repositories**), set a short expiry, and under **Repository permissions** grant **Contents: Read and write** (this covers pushing tags). Leave everything else at **No access**.
3. **Generate token** and copy the value — it is shown only once.
4. In the `D3V0N5H1R3/luma` repository, go to **Settings → Secrets and variables → Actions → New repository secret**, name it exactly `RELEASE_PAT`, paste the token, and save.
5. Rotate the token before it expires and update the secret; if it lapses, the workflow's first step reports that `RELEASE_PAT` is not set.

> **Tip:** A classic PAT with the `repo` scope works too, but a fine-grained token limited to this repository with only `Contents: write` is the least-privilege choice.

### Optional Setup: Windows Code Signing

Unsigned Windows executables are often quarantined by Microsoft Defender and other antivirus engines as false positives, and trigger the SmartScreen "Windows protected your PC" prompt. [`release.yml`](release.yml) Authenticode-signs and timestamps `luma.exe`, `luma_lsp.exe`, and `luma_dap.exe` before packaging them, but only when both of these repository secrets are set; otherwise it logs a notice and publishes the binaries unsigned:

| Secret                        | Value                                                    |
| ----------------------------- | -------------------------------------------------------- |
| `WINDOWS_SIGNING_CERTIFICATE` | The code-signing certificate as a base64-encoded `.pfx`. |
| `WINDOWS_SIGNING_PASSWORD`    | The `.pfx` password.                                     |

Encode the certificate with `[Convert]::ToBase64String([IO.File]::ReadAllBytes("cert.pfx"))` in PowerShell, then add both secrets under **Settings → Secrets and variables → Actions → New repository secret**. A signing failure fails the Windows build job, so a release is never published with a broken signature. Independently of signing, every Windows executable embeds version information and an application manifest ([`cmake/LumaWindowsResources.cmake`](../../cmake/LumaWindowsResources.cmake)).

---

## 7 — Reusable Building Blocks

Internal `workflow_call` components — never triggered directly, only invoked by the workflows above.

| Workflow                                                 | Trigger                   | Purpose                                                                            |
| -------------------------------------------------------- | ------------------------- | ---------------------------------------------------------------------------------- |
| [`reusable-linux-build.yml`](reusable-linux-build.yml)   | Called by other workflows | Build and test inside a Linux container image.                                     |
| [`build-raspberry-pi.yml`](build-raspberry-pi.yml)       | Called by other workflows | Raspberry Pi OS (ARM64) build on a native ARM64 runner; wraps the container build. |
| [`resolve-distro-matrix.yml`](resolve-distro-matrix.yml) | Called by other workflows | Expose linux-distros.json as a job matrix.                                         |

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

If you gate merges to `main` on status checks (through a branch protection rule or a repository ruleset), the **path filters above decide which checks are safe to require**. A required check only clears a pull request once it reports a result — and a path-filtered workflow that a given PR doesn't trigger never reports one, leaving that PR stuck on *"Expected — waiting for status to be reported."* Requiring a path-filtered check therefore deadlocks every PR that doesn't touch its paths: a docs-only PR would wait forever on the C++ build matrix.

Only [`codeql.yml`](codeql.yml) runs on every pull request regardless of paths, so its five `Analyze (…)` checks are the only ones safe to require **unconditionally**:

- `Analyze (c-cpp)`
- `Analyze (actions)`
- `Analyze (javascript-typescript)`
- `Analyze (python)`
- `Analyze (rust)`

To hard-require the build/test/lint checks as well without deadlocking unrelated PRs, either leave them **advisory** (they still run and show on every PR — they just don't block the merge), or first make each workflow always report: move its `paths:` filter off the `on:` trigger and onto a `changes` gate job so skipped work reports success instead of never running.

Two caveats when picking checks:

- The `ThreadSanitizer` job lives in its own scheduled/manual workflow ([`ci-tsan.yml`](ci-tsan.yml)), so it never runs on a pull request and cannot be a required check — leave it out. libtsan cannot initialise on the current hosted `ubuntu-24.04` image, so it ran perpetually red on every push; the `ubuntu-latest — gcc-13-sanitizers` build (AddressSanitizer + UBSan) in [`ci.yml`](ci.yml) is the gating sanitizer check.
- A check only appears in the ruleset picker **after it has run against a PR at least once**, so open one pull request and let CI run before selecting from the list. Reusable-workflow checks (the distro and Raspberry Pi builds) appear under a `Caller / Callee` name.

For reference, the checks each pull-request-triggered workflow reports:

| Workflow                                 | Reported checks (job names)                                                                                                                                                                                                                                                                                                                                                                            |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| [`codeql.yml`](codeql.yml)               | `Analyze (c-cpp)`, `Analyze (actions)`, `Analyze (javascript-typescript)`, `Analyze (python)`, `Analyze (rust)`                                                                                                                                                                                                                                                                                        |
| [`ci.yml`](ci.yml)                       | `ubuntu-latest — gcc-14`, `macos-latest — clang`, `windows-latest — msvc`, `ubuntu-latest — gcc-13-sanitizers`, `Static Analysis (clang-tidy) (0–3)`, `Formatting (clang-format)`, `Warning Flag Sync`, `Code Coverage`, `Feature Flag Build (no-tls)`, `Resolve Linux distro matrix`, the four distro builds (`Debian (trixie)`, `Kali Linux`, `Fedora`, `Arch Linux`), and `Raspberry Pi OS (ARM64)` |
| [`docs.yml`](docs.yml)                   | `Documentation Consistency`                                                                                                                                                                                                                                                                                                                                                                            |
| [`ci-cmake.yml`](ci-cmake.yml)           | `cmakelint`                                                                                                                                                                                                                                                                                                                                                                                            |
| [`ci-markdown.yml`](ci-markdown.yml)     | `markdownlint`                                                                                                                                                                                                                                                                                                                                                                                         |
| [`ci-powershell.yml`](ci-powershell.yml) | `PSScriptAnalyzer`                                                                                                                                                                                                                                                                                                                                                                                     |
| [`ci-python.yml`](ci-python.yml)         | `ruff`                                                                                                                                                                                                                                                                                                                                                                                                 |
| [`ci-shell.yml`](ci-shell.yml)           | `ShellCheck`                                                                                                                                                                                                                                                                                                                                                                                           |
| [`ci-vscode.yml`](ci-vscode.yml)         | `VS Code Extension`                                                                                                                                                                                                                                                                                                                                                                                    |
| [`ci-zed.yml`](ci-zed.yml)               | `Zed Extension`, `Tree-sitter Grammar Tests`                                                                                                                                                                                                                                                                                                                                                           |

[`benchmark.yml`](benchmark.yml) and [`fuzz.yml`](fuzz.yml) don't run on pull requests, so they can't be required.

---

## 10 — Related Documentation

- [github-actions.instructions.md](../../instructions/github-actions.instructions.md) — the conventions these workflows follow (triggers, permissions, caching, security).
- [github-actions-recipes.instructions.md](../../instructions/github-actions-recipes.instructions.md) — copy-paste workflow recipes and debugging guidance.
- [build.instructions.md](../../instructions/build.instructions.md) — the CMake presets, sanitizers, and coverage the build and test jobs drive.
- [actions/DIRECTORY.md](../actions/DIRECTORY.md) — the composite actions these workflows call (`apt-install`, `cmake-build`, `package-binaries`, `build-vscode-extension`, `build-zed-extension`).
- [linux-distros.json](linux-distros.json) — the distribution matrix [`resolve-distro-matrix.yml`](resolve-distro-matrix.yml) loads. Point-release distros are pinned to a version (`debian:trixie`, `fedora:44`) — bump them deliberately. Arch Linux and Kali are rolling by design: their install commands upgrade to the current repositories, so pinning the image would not freeze the toolchain. They act as an early warning on every CI push, well before a release tag.
- [stale-path-denylist.txt](../stale-path-denylist.txt) — the patterns [`docs.yml`](docs.yml) rejects.
- [prompts/DIRECTORY.md](../prompts/DIRECTORY.md) and [hooks/DIRECTORY.md](../hooks/DIRECTORY.md) — the other sibling `.github/` indexes.

> **Note:** When you add, rename, or remove a workflow, update the tables above so this index stays accurate.
