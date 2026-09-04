# Dev Container / GitHub Codespaces

This directory configures a reproducible Linux development environment for Luma,
usable two ways:

- **GitHub Codespaces** — open the repository in a browser or in VS Code with
  no local toolchain.
- **VS Code Dev Containers** — the **Dev Containers: Reopen in Container**
  command builds the same image locally (requires Docker).

## What the container provides

Everything needed to build and test the interpreter, language server, and
debugger, matching the versions the CI workflows pin:

- **GCC 14** (`gcc-14` / `g++-14`), **CMake**, **Ninja**, and **Make**.
- **clang-format 18** and **clang-tidy 18** — the C++ formatting and
  static-analysis gates.
- **GDB** and **lcov** for native debugging and the coverage preset.
- **Python 3** with the pinned **Ruff** CLI, plus the **Node** and **Rust**
  (with the `wasm32-wasip1` target) toolchains for the VS Code and Zed
  extensions.

Every third-party C/C++ library is vendored in [`external/`](../external), so no
dependencies are fetched at build time.

## First launch

The container builds the interpreter automatically via the `default` CMake
preset (Release), placing the binaries in `build/`:

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
