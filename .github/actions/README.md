# Composite Actions

Reusable composite actions shared across the CI and release workflows. Each action encapsulates a single build or packaging concern so the calling workflows stay concise and consistent.

## Actions

| Action | Purpose |
| --- | --- |
| [apt-install](apt-install/action.yml) | Update the APT index and install packages (Linux jobs). |
| [build-vscode-extension](build-vscode-extension/action.yml) | Install, type-check, compile, and package the VS Code extension (.vsix). |
| [build-zed-extension](build-zed-extension/action.yml) | Install the Rust WASM toolchain and check, lint, test, and build the Zed extension. |
| [cmake-build](cmake-build/action.yml) | Configure and build the Luma project with CMake (cross-platform). |
| [package-binaries](package-binaries/action.yml) | Archive the Luma binaries into per-binary and full-bundle archives. |

## apt-install

Centralises the `apt-get update && apt-get install` pattern repeated across Linux CI jobs into a single step.

**Inputs:**

- `packages` (required) — space-separated list of packages to install.

## build-vscode-extension

Sets up Node.js, installs dependencies, type-checks, compiles TypeScript, and packages the extension into a `.vsix` artifact. Used by the VS Code CI, release, and bundled-in-core release workflows.

**Inputs:**

- `working-directory` (default: `extensions/vscode`) — path to the extension directory.

**Outputs:**

- `vsix-file` — filename of the packaged `.vsix` (relative to `working-directory`).

## build-zed-extension

Installs the stable Rust toolchain with the `wasm32-wasip1` target, restores the Cargo cache, then runs format check, compilation check, Clippy lint, tests, and a release WASM build. Used by the Zed CI and release workflows.

**Inputs:**

- `working-directory` (default: `extensions/zed`) — path to the extension directory.

## cmake-build

Hides Unix/Windows configure differences behind a single step. On Unix the compilers are selectable via `cc`/`cxx` inputs; on Windows the default MSVC toolchain is used. Defaults to the detected core count for build parallelism (overridable for memory-heavy builds like sanitizers).

**Inputs:**

- `build-type` (default: `Release`) — CMake build type.
- `build-dir` (default: `build`) — build directory to configure into.
- `cc` (optional) — C compiler (Unix only).
- `cxx` (optional) — C++ compiler (Unix only).
- `extra-args` (optional) — extra arguments appended to the CMake configure command.
- `parallel` (optional) — explicit job count; defaults to detected core count.

## package-binaries

Archives the three Luma binaries (`luma`, `luma_lsp`, `luma_dap`) into a full distribution bundle plus one archive per binary, so each editor extension downloads only what it needs. Produces `.tar.gz` on Unix and `.zip` on Windows. Archive names follow `extensions/BINARY_ASSETS.md`.

**Inputs:**

- `suffix` (required) — the `{os}-{arch}` suffix for archive names (e.g. `linux-x86_64`).
- `build-dir` (default: `build`) — build directory containing the binaries.
