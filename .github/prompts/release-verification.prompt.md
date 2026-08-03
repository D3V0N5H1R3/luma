---
description: "Clean the workspace, rebuild every binary and editor extension from scratch, then run the full verification suite — a clean-room release check"
agent: "agent"
version: 1
lastUpdated: "2026-08-01"
---

# Release Verification

Perform a **clean-room** verification of the whole project: wipe all build
artifacts, rebuild every binary **and** editor extension from scratch, then run
every test category. Use this before a release, or whenever you need confidence
that the project builds and passes cleanly from a pristine checkout.

> **Scope:** This is the clean-room release check — it wipes the workspace,
> rebuilds every binary and editor extension, then runs the full test sweep. For
> routine work that does not need a pristine rebuild, use the lighter
> [build-and-test.prompt.md](build-and-test.prompt.md) or
> [full-test-sweep.prompt.md](full-test-sweep.prompt.md) instead.

Every phase must finish with **no errors and no warnings**. Stop and fix the
underlying cause before moving on — do not disable or skip checks.

The test phases (2–6) reuse [full-test-sweep.prompt.md](full-test-sweep.prompt.md)
as the canonical reference; this prompt adds the two areas it does not cover —
**workspace cleanup** (Phase 0) and **editor-extension build, packaging, and
tests** (Phases 1b–1d).

## 0. Clean the Workspace (destructive — opt-in)

> **Warning:** This permanently deletes all generated files (often several GB).
> Skip this phase if you only want an incremental verification.

Remove every gitignored build artifact (`build/`, `build-fuzz/`, `node_modules/`,
Rust `target/`, `*.vsix`, `*.wasm`, compiled `*.lumc`, … — the authoritative set
is whatever [.gitignore](../../.gitignore) declares) while preserving all
tracked sources — including the fuzz corpus seeds under `fuzz/corpus/`.

**Preserve the pipeline run logs.** When this phase runs inside the fix or audit
pipeline (`scripts/pipeline/`), that run's reports and transcripts live under the
gitignored `pipeline-artifacts/` directory. A plain `git clean -X` would delete
them — destroying the very logs you need to judge how the run went — so set that
directory aside across the wipe and restore it afterwards. `git clean` never
scans inside `.git/`, so stash it there; a repo-root copy would itself be caught
by the `*.bak` ignore rule.

```bash
pa="$(git rev-parse --show-toplevel)/pipeline-artifacts"
bak="$(git rev-parse --absolute-git-dir)/_pipeline-artifacts.bak"
rm -rf "$bak"                      # clear any leftover from an interrupted run
[ -d "$pa" ] && mv "$pa" "$bak"    # set the run logs aside (no-op if absent)
git clean -Xdn                     # dry run — preview exactly what will be removed
git clean -Xdf                     # remove all other gitignored artifacts
[ -d "$bak" ] && mv "$bak" "$pa"   # restore the run logs
```

On Windows, perform the same set-aside with PowerShell's `Move-Item` and
`Test-Path` (clearing any stale `$bak` with `Remove-Item` first); the logic is
identical.

`-X` (uppercase) removes only *ignored* files. Use `-x` (lowercase) only if you
also intend to delete untracked, non-ignored files. Afterwards, confirm the tree
is clean with `git status` — apart from `pipeline-artifacts/` (if present), which
is intentionally preserved.

## 1. Build All Binaries and Extensions

Build everything and require a clean, warning-free result for each artifact. See
[build.instructions.md](../../instructions/build.instructions.md) for the full
build reference (presets, sanitizers, coverage, fuzzing).

### 1a. Core binaries — `luma`, `luma_lsp`, `luma_dap`

```bash
cmake --preset default
cmake --build --preset default
```

The default preset builds the interpreter (`luma`), the language server
(`luma_lsp`), and the debug adapter (`luma_dap`). Fix any build error or warning
before proceeding.

### 1b. VS Code extension and `.vsix` package

```bash
cd extensions/vscode
npm ci
npx tsc --noEmit       # type-check
npm run compile        # bundle
npx vsce package --no-dependencies
```

Produces `extensions/vscode/luma-language-<version>.vsix`. `npm ci` may print
advisories for transitive dev dependencies — those are not build warnings and do
not gate this phase; `tsc`, `compile`, and `package` must all complete cleanly.

### 1c. Zed extension (WebAssembly)

```bash
cd extensions/zed
rustup target add wasm32-wasip1   # once, if not already installed
cargo fmt --check
cargo clippy --target wasm32-wasip1 -- -D warnings
cargo build --release --target wasm32-wasip1
```

Produces `extensions/zed/target/wasm32-wasip1/release/luma_zed.wasm`. `clippy`
runs with `-D warnings`, so any lint is a hard failure.

### 1d. Extension test suites

Building an extension does not exercise it — run the extension tests too, since
the `.vsix` and the `.wasm` are shipped release artifacts.
The commands below mirror the editor CI workflows
([ci-vscode.yml](../workflows/ci-vscode.yml),
[ci-zed.yml](../workflows/ci-zed.yml)), which are the authoritative source.

```bash
# VS Code extension unit tests (reuses the install from Phase 1b)
cd extensions/vscode
npm run test:unit
```

```bash
# Cross-editor contract tests — shared defaults, download protocol, resolution
# order (dependency-free Node ESM; run from extensions/)
cd extensions
node tests/validate-defaults.test.mjs
node tests/validate-download.test.mjs
node tests/validate-download-constants.test.mjs
node tests/validate-resolution-order.test.mjs
```

The tree-sitter grammar parse fixtures (`extensions/tests/parse_fixtures.js`,
which needs the grammar generated first) are not reproduced here. Also confirm the generated per-editor files are in sync
(`cd extensions/shared && python ci-check-generated.py`). Every test must pass
before the release is verified.

## 2–6. Run the Full Verification Suite

Run phases 2–6 exactly as described in
[full-test-sweep.prompt.md](full-test-sweep.prompt.md). In short:

- **Phase 2 — C++ unit tests:** `ctest --preset default` (also drives the Luma
  feature tests in strict mode).
- **Phase 3 — Luma feature tests (strict):** `python scripts/run_luma_tests.py`;
  strict mode treats every warning as an error.
- **Phase 4 — Fuzz tests:** build the `fuzz_*` targets and run each briefly to
  confirm it starts and finds no immediate crash. See the platform note below.
- **Phase 5 — Benchmarks:** run `build/Release/luma benchmarks/suite.luma`
  (Windows: `build\Release\luma.exe`), then parse with
  `python scripts/parse_benchmark_results.py`. Verify they run; do not tune
  performance.
- **Phase 6 — Examples (strict):** type-check every `examples/**/*.luma` with
  `luma --check --strict`. The include-only helper `multi_file_utils.luma` has no
  `@main` and is expected to be skipped (see `scripts/run_luma_examples.py`).

> **Note (Windows / clang-cl fuzz):** the bundled LLVM ships only a
> *dynamic* AddressSanitizer (needs `/MD`) but a *static-CRT* libFuzzer runtime
> (`/MT`), which cannot be linked together. Build the fuzzers **fuzzer-only**
> (drop `,address` from the sanitizer flags, add
> `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` and
> `-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`);
> [fuzz/README.md](../../fuzz/README.md) has the full validated `clang-cl` recipe
> (Ninja + `RelWithDebInfo`). Run each target against an isolated, writable corpus
> directory so the tracked seeds in `fuzz/corpus/` are never modified.
> **Windows fuzzing is not authoritative:** use it only to confirm each target
> links and its seeds load (run with `-runs=0`), and reproduce any actual finding
> on Linux before treating it as a release blocker — `fuzz_protocol` in particular
> has a known clang-cl EH-funclet trap that is a codegen artifact, not a transport
> defect. The Linux CI ([fuzz.yml](../workflows/fuzz.yml)) is the reference
> environment (clang + fuzzer + ASan) and the source of truth for crashes.

## 7. Report

Summarise per phase:

- **Phase 0** — whether the workspace was cleaned, and that `git status` is clean.
- **Phase 1** — each binary and package built, its size, and that it is
  warning-free; and that every extension test suite (Phase 1d) passed.
- **Phases 2–6** — totals per category, any failures fixed (what and how), and
  any remaining issues needing manual attention.
- Note any platform-specific caveats (e.g. the Windows fuzz behaviour above).

Conclude with an explicit **overall verdict**: the project is release-ready only
if every phase completed with no errors, no warnings, and no remaining test
failures. Otherwise, state precisely what still blocks the release.
