# Contributing to Luma

Thank you for your interest in contributing to the Luma programming language.

This project and everyone participating in it is governed by the Luma [Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code.

This page covers the contribution workflow. Each topic it mentions has a detailed guide, listed under [Where to Learn More](#where-to-learn-more).

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Getting Started](#getting-started)
3. [Before You Open a Pull Request](#before-you-open-a-pull-request)
4. [Branch Naming](#branch-naming)
5. [Commit Messages](#commit-messages)
6. [Pull Request Workflow](#pull-request-workflow)
7. [Where to Learn More](#where-to-learn-more)
8. [Reporting Issues](#reporting-issues)
9. [License](#license)

---

## Prerequisites

| Platform | Compiler           | Build System |
| -------- | ------------------ | ------------ |
| Linux    | GCC 13 or later    | CMake 3.21+  |
| macOS    | Clang 15 or later  | CMake 3.21+  |
| Windows  | MSVC 2022 or later | CMake 3.21+  |

- These compiler versions are hard minimums: Luma uses `std::format` and `std::chrono::clock_cast`, which libstdc++ provides only from GCC 13. The default `g++` on Raspberry Pi OS and Debian 12 ("bookworm") is GCC 12 — install GCC 13 or newer and configure with `-DCMAKE_CXX_COMPILER=g++-13`.
- Every third-party library is vendored, so the interpreter, language server, and debugger need nothing beyond the toolchain.
- **Python 3.10 or later** runs the helper scripts under [`scripts/`](scripts/DIRECTORY.md).
- **Node.js 20 or later** (22 to package the extension) is needed for the VS Code extension and the Node-based linters; **Rust** with the `wasm32-wasip1` target is needed for the Zed extension.

The [Dev Container](.devcontainer/DIRECTORY.md) has the full toolchain preinstalled.

---

## Getting Started

```bash
git clone https://github.com/d3v0n5h1r3/luma.git
cd luma
git switch -c feature/short-description

cmake --preset default            # configure (Release, into build/)
cmake --build --preset default    # build luma, luma_lsp, luma_dap, and the tests
ctest --preset default            # C++ unit tests and Luma feature tests

build/luma examples/language-features/hello.luma
```

The executables are written to the root of the build directory on every platform — `build/luma`, or `build\luma.exe` on Windows. Other build configurations (debug, sanitizers, coverage) are presets too; see [cmake/PRESETS.md](cmake/PRESETS.md) and [build.instructions.md](instructions/build.instructions.md).

Two more runners cover the Luma side:

```bash
python scripts/run_luma_tests.py      # every Luma feature test suite
python scripts/run_luma_examples.py   # every example, driven headlessly
```

Run a single feature test the way CI does with `build/luma --strict --test <file>.luma`. [testing.instructions.md](instructions/testing.instructions.md) describes the test framework and how to write tests; [Development Environment](documents/Luma_Development_Environment.md) sets up VS Code or Zed for building and debugging.

---

## Before You Open a Pull Request

### Linters and Formatters

Every language in the repository has a linter or formatter, each enforced by its own CI workflow, with its configuration checked in so local runs match CI. Reproduce the whole CI lint result — or auto-fix most of it — with:

```bash
python scripts/lint.py       # run every check gate (--list to preview, --skip clang-tidy to omit the slow one)
python scripts/format.py     # apply every auto-formatter and safe auto-fix
```

A gate whose tool is not installed is reported as skipped. See [scripts/DIRECTORY.md](scripts/DIRECTORY.md#linting-and-formatting) for the options.

| Scope                     | Tool                 | Configuration              | Workflow            |
| ------------------------- | -------------------- | -------------------------- | ------------------- |
| C++ formatting            | clang-format 20      | `.clang-format`            | `ci.yml`            |
| C++ static analysis       | clang-tidy 18+       | `.clang-tidy`              | `ci.yml`            |
| Compiler-flag / tidy sync | `check_warning_sync` | `scripts/`                 | `ci.yml`            |
| Python                    | Ruff                 | `ruff.toml`                | `ci-python.yml`     |
| Rust (Zed extension)      | rustfmt + Clippy     | `extensions/zed/`          | `ci-zed.yml`        |
| TypeScript / JavaScript   | ESLint + Prettier    | `extensions/vscode/`       | `ci-vscode.yml`     |
| Shell                     | ShellCheck           | auto-discovered            | `ci-shell.yml`      |
| PowerShell                | PSScriptAnalyzer     | `PSScriptAnalyzerSettings` | `ci-powershell.yml` |
| CMake                     | cmakelint            | `.cmakelintrc`             | `ci-cmake.yml`      |
| Markdown                  | markdownlint-cli2    | `.markdownlint-cli2.jsonc` | `ci-markdown.yml`   |

### Git Hooks

Enable the tracked Git hooks once per clone (`python scripts/configure.py <preset>` also does this):

```bash
python scripts/install_git_hooks.py
```

`pre-commit` checks staged C++ files with clang-format (and clang-tidy when a compile database exists); `commit-msg` enforces the [commit message](#commit-messages) format. See [scripts/DIRECTORY.md](scripts/DIRECTORY.md#git-hooks) for details.

### Checklist

- The build is warning-free and `ctest --preset default` passes.
- New behaviour has tests: a C++ unit test, a Luma `@test` feature test, or both.
- User-visible changes are recorded in [CHANGELOG.md](CHANGELOG.md) under **Unreleased**, and affected documentation is updated.
- Standard library changes respect the [sandbox](documents/Luma_Sandbox.md): file, network, and process access is withheld under `--box`, new unbounded resources get a limit in `core/common/resource_limits.hpp`, and inputs are validated at the module boundary, with expected failures returned as `result<T>`.
- Vendored third-party code under `external/` stays byte-for-byte upstream: never edit it in place; update a library as described in [external/DIRECTORY.md](external/DIRECTORY.md). The agent hooks block such edits.

---

## Branch Naming

Use lowercase, hyphen-separated names with a category prefix: `chore/`, `docs/`, `feature/`, `fix/`, `refactor/`, or `test/` — for example `feature/add-match-expression` or `fix/lexer-string-escape`.

---

## Commit Messages

Follow the [Conventional Commits](https://www.conventionalcommits.org/) format, `<type>: <short summary>` (an optional scope and `!` are allowed):

| Type        | Purpose                                 |
| ----------- | --------------------------------------- |
| `chore:`    | Tooling, config, dependencies           |
| `ci:`       | CI workflows and pipeline configuration |
| `docs:`     | Documentation only                      |
| `feat:`     | New feature                             |
| `fix:`      | Bug fix                                 |
| `refactor:` | Code restructuring, no behaviour change |
| `style:`    | Formatting only, no code change         |
| `test:`     | Adding or updating tests                |

Use the imperative mood (`feat: add match expression`), keep the first line under 72 characters, and make one logical change per commit. The `commit-msg` hook enforces the format; [git.instructions.md](instructions/git.instructions.md) covers the wider Git workflow.

---

## Pull Request Workflow

1. Push your branch: `git push --set-upstream origin feature/short-description`.
2. Open a pull request targeting `main` and fill in the template.
3. Make sure CI passes on all platforms (Ubuntu, macOS, Windows), including sanitizers, clang-tidy, and coverage. Running `python scripts/lint.py` and the tests locally first catches most failures.
4. Request a review. Once approved, the pull request is merged with a merge commit (`--no-ff`).

Maintainers cut releases as described in [.github/workflows/DIRECTORY.md](.github/workflows/DIRECTORY.md#6--releases).

---

## Where to Learn More

| Topic                                        | Guide                                                                                                                                                          |
| -------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Every design, reference, and guide document  | [documents/DIRECTORY.md](documents/DIRECTORY.md)                                                                                                               |
| The language                                 | [User Manual](documents/Luma_User_Manual.md), [Standard Library Reference](documents/Luma_Standard_Library_Reference.md)                                       |
| Interpreter architecture and module layout   | [Software Architecture](documents/Luma_Software_Architecture.md)                                                                                               |
| Error-handling policy for each layer         | [Error Handling §9](documents/Luma_Error_Handling.md#9--interpreter-implementation-policy)                                                                      |
| Per-language coding standards                | [`instructions/`](instructions/DIRECTORY.md) (C++, Luma, Python, Rust, TypeScript, CMake, …)                                                                   |
| Building, presets, sanitizers, and coverage  | [build.instructions.md](instructions/build.instructions.md), [cmake/PRESETS.md](cmake/PRESETS.md)                                                              |
| Writing tests                                | [testing.instructions.md](instructions/testing.instructions.md)                                                                                                |
| Editor setup and debugging the interpreter   | [Development Environment](documents/Luma_Development_Environment.md)                                                                                           |
| Adding a standard library module or function | [MODULE_DEVELOPMENT_KIT.md](core/runtime/stdlib/MODULE_DEVELOPMENT_KIT.md)                                                                                     |
| Benchmarks and fuzzing                       | [benchmarks/DIRECTORY.md](benchmarks/DIRECTORY.md), [fuzz/DIRECTORY.md](fuzz/DIRECTORY.md)                                                                     |
| Helper scripts, Git hooks, and lint runners  | [scripts/DIRECTORY.md](scripts/DIRECTORY.md)                                                                                                                   |
| CI workflows and the release process         | [.github/workflows/DIRECTORY.md](.github/workflows/DIRECTORY.md)                                                                                               |

---

## Reporting Issues

> **Security vulnerabilities:** Do **not** open a public issue. Follow the private reporting process in [SECURITY.md](SECURITY.md) instead.

Use the issue templates. A good bug report names the platform (OS, compiler, and version), gives a minimal `.luma` file with the command that reproduces the problem, and describes the expected and the actual behaviour, including the full error message.

---

## License

By contributing, you agree that your contributions will be licensed under the same license as the project.
