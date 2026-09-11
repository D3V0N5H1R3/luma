# Dev Container / GitHub Codespaces

This directory configures a reproducible Linux development environment for Luma,
usable two ways:

- **GitHub Codespaces** — open the repository in a browser or in VS Code with
  no local toolchain.
- **VS Code Dev Containers** — the **Dev Containers: Reopen in Container**
  command builds the same image locally (requires Docker).

## What the container provides

Everything needed to build, test, and package the interpreter, language server,
debugger, and both editor extensions, matching the versions the CI workflows
pin:

- **GCC 14** (`gcc-14` / `g++-14`), **CMake**, **Ninja**, and **Make**.
- **Clang 18** (`clang-18` / `clang++-18`) with the **compiler-rt** runtimes —
  builds the LibFuzzer fuzz targets in [`fuzz/`](../fuzz).
- **clang-format 18** and **clang-tidy 18** — the C++ formatting and
  static-analysis gates.
- **GDB** and **lcov** for native debugging and the coverage preset.
- **Xvfb** — a headless X server for the VS Code extension's Electron
  integration tests (`npm test`).
- **Python 3** with the pinned **Ruff** CLI, plus the **Node** and **Rust**
  (`default` profile, so `rustfmt` and `clippy` are present, with the
  `wasm32-wasip1` target) toolchains for the VS Code and Zed extensions.

Every third-party C/C++ library is vendored in [`external/`](../external), so no
dependencies are fetched at build time.

## First launch

The container builds the interpreter automatically via the `default` CMake
preset (Release), placing the binaries in `build/`, and sets up both editor
extensions (npm dependencies, the tree-sitter parser, and the Zed WASM build):

```bash
build/luma examples/language-features/hello.luma   # run a program
build/luma                                          # start the REPL
ctest --test-dir build --output-on-failure          # run the C++ test suite
python scripts/run_luma_tests.py                     # run the Luma feature tests
```

Rebuild after changes with:

```bash
cmake --build build --parallel "$(nproc)"
```

### Editor extensions

The extension toolchains are all present, so you can build, lint, test, and
package both extensions here (running them in an editor UI is a host activity —
see [Limitations](#limitations)):

```bash
# VS Code extension (extensions/vscode)
npm run compile        # bundle with esbuild
npm run lint           # ESLint + tsc --noEmit
npm run test:unit      # headless unit tests (no Electron)
npm test               # full Electron tests — run under Xvfb: xvfb-run npm test
npm run package        # build the .vsix with vsce

# Zed extension (extensions/zed)
cargo fmt --check
cargo clippy --target wasm32-wasip1 -- -D warnings
cargo test
cargo build --release --target wasm32-wasip1   # produces the extension .wasm

# Tree-sitter grammar (extensions/zed/grammars/tree-sitter-luma)
./node_modules/.bin/tree-sitter generate
```

The network-dependent extension setup in `prepare.sh` is best-effort; if it was
skipped (for example, during an offline prebuild) run the `npm ci` / `cargo`
steps by hand.

To reproduce the CI lint gates locally (C++, Python, Markdown, and more), run:

```bash
python scripts/lint.py     # check every available gate
python scripts/format.py   # apply the auto-fixers
```

## Recommended machine size and prebuilds

A cold build takes roughly 5–8 minutes and the test suite about 3 minutes, so
the configuration requests a **4-core** machine. For the fastest launch, enable
[prebuilds](https://docs.github.com/en/codespaces/prebuilding-your-codespaces)
on the repository: the prebuild bakes a warm `build/` directory into the image,
so opening a Codespace skips the initial compile.

## Limitations

- **Linux only.** Codespaces cannot exercise the MSVC (Windows) or Apple-Clang
  (macOS) code paths, so it complements — but does not replace — building on
  those platforms before a release.
- **Running the editors is a host activity.** The extension *toolchains* are all
  here, but the editors themselves are not: with the VS Code Dev Containers
  extension you can still launch the Extension Development Host (the UI runs on
  your host, the extension host in the container), whereas the Zed extension is
  built here and loaded as a dev extension into Zed on your host.

## Customization

- **Interpreter-only work:** remove the `node` and `rust` entries under
  `features` in [`devcontainer.json`](devcontainer.json) to shorten build time.
- **A different compiler:** change `gcc-14` / `g++-14` in the
  [`Dockerfile`](Dockerfile) and the `CC` / `CXX` values in `devcontainer.json`
  (Clang 15+ is also supported).

## See Also

- [Contributing](../CONTRIBUTING.md)
- [Build presets and options](../instructions/build.instructions.md)
- [Vendored dependencies](../external/DIRECTORY.md)
