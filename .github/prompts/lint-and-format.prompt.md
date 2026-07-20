---
description: "Lint and format all first-party source code and documentation across every language in the repository (C++, Luma, Python, TypeScript/JavaScript, Rust, CSS, PowerShell, Shell, CMake, Markdown)."
agent: "agent"
---

# Lint and Format

Lint and format every first-party language in the repository. Run the sections for the file types you changed, or run all of them to sweep the whole tree. Each language's tooling, pinned versions, and configuration mirror the matching CI workflow in `.github/workflows/`, so a clean local pass should reproduce the CI gate. For a broader pass that also reshapes code to the project's style conventions — naming, immutability, single responsibility — beyond what the formatters enforce, use [source-code-cleanup.prompt.md](source-code-cleanup.prompt.md).

> **Order matters — lint before format.** A linter run with `--fix` (clang-tidy,
> Ruff, ESLint, …) rewrites code, and its edits are not guaranteed to match the
> project's formatting style. For example, `clang-tidy --fix` may apply a
> `modernize-*` or `bugprone-*` rewrite that `clang-format` then has to
> normalise — running the formatter first would leave any fixed line
> unformatted. Within each language below, lint first, then format.

## Tooling Overview

| Language              | Linter            | Formatter         | Configuration                   |
| --------------------- | ----------------- | ----------------- | ------------------------------- |
| C++                   | clang-tidy        | clang-format      | `.clang-tidy`, `.clang-format`  |
| Luma                  | luma --check      | —                 | interpreter built-in            |
| Python                | Ruff              | Ruff              | `ruff.toml`                     |
| TypeScript/JavaScript | ESLint            | Prettier          | `extensions/vscode`             |
| Rust                  | Clippy            | rustfmt           | `extensions/zed`                |
| CSS                   | Stylelint         | Stylelint         | `stylelint.config.mjs`          |
| PowerShell            | PSScriptAnalyzer  | PSScriptAnalyzer  | `PSScriptAnalyzerSettings.psd1` |
| Shell                 | ShellCheck        | —                 | —                               |
| CMake                 | cmakelint         | —                 | `.cmakelintrc`                  |
| Markdown              | markdownlint-cli2 | markdownlint-cli2 | `.markdownlint-cli2.jsonc`      |

## C++ — clang-tidy and clang-format

### Static Analysis (clang-tidy)

`clang-tidy` needs the compilation database (`build/compile_commands.json`). Every preset sets `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, but only the Makefiles and Ninja generators actually emit the file — the Visual Studio generator ignores it. Generate it into `build/` (so the `-p build` commands below resolve) if it does not already exist:

- **Linux/macOS:** `cmake --preset default` — the default Makefiles generator writes `build/compile_commands.json`.
- **Windows:** the `default` preset uses the Visual Studio generator, which emits no database, so configure Ninja into the same `build/` directory instead:

    ```powershell
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    ```

1. Run `clang-tidy` on all source files (excluding tests and fuzz targets) using the project's [.clang-tidy](../../.clang-tidy) configuration:

    ```bash
    find core shared language-server/source debugger/source -name '*.cpp' | xargs clang-tidy -p build --fix
    ```

    On Windows, use PowerShell:

    ```powershell
    Get-ChildItem -Recurse -Include *.cpp -Path core,shared,language-server\source,debugger\source | ForEach-Object { clang-tidy -p build --fix $_.FullName }
    ```

2. Fix warnings in priority order. [.clang-tidy](../../.clang-tidy) escalates three categories to errors (`WarningsAsErrors: clang-analyzer-*,bugprone-*,concurrency-*`) — these **fail the build and CI**, so fix them first:
    - **Bugs** — `bugprone-*`, `clang-analyzer-*`
    - **Concurrency** — `concurrency-*` (data races, misused locks and atomics)

    The other enabled groups stay advisory — address them where practical:
    - **Security** — the enabled CERT checks (`cert-env33-c`, `cert-err33-c`, `cert-msc51-cpp`), plus buffer-overflow and uninitialised-variable diagnostics
    - **Performance** — `performance-*`, unnecessary copies
    - **Modernisation** — `modernize-*`, deprecated patterns
3. Do **not** apply fixes that change public API signatures or observable behaviour.
4. If a warning is a false positive, suppress it with a `// NOLINT` comment including the check name:

    ```cpp
    value = reinterpret_cast<T*>(ptr);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    ```

### Formatting (clang-format)

1. Run `clang-format` in-place on all `.cpp` and `.hpp` files under `core/`, `shared/`, `language-server/source/`, `debugger/source/`, `tests/`, and `fuzz/`. This also normalises any code rewritten by clang-tidy above:

    ```bash
    find core shared language-server/source debugger/source tests fuzz -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
    ```

    On Windows, use PowerShell:

    ```powershell
    Get-ChildItem -Recurse -Include *.cpp,*.hpp -Path core,shared,language-server\source,debugger\source,tests,fuzz | ForEach-Object { clang-format -i $_.FullName }
    ```

2. The project configuration is in [.clang-format](../../.clang-format). Do not modify it.

## Luma — Static Checking

Luma source files are checked by the interpreter itself (built by the C++ build above). Unlike the other languages, there is no dedicated `luma --check` lint workflow — but the feature-test suite under `tests/features/` *is* gated in CI via CTest, and it runs `--strict` (see `scripts/run_luma_tests.py`). Luma has no code formatter, so enforce layout and style by hand against the Luma guidelines in [luma.instructions.md](../../instructions/luma.instructions.md).

Check a file without running it (reports type errors and linter warnings). Prefer `--strict`, which treats warnings as errors — it mirrors the project's own gate (CTest and `scripts/run_luma_tests.py` always run strict), so it catches everything CI would:

```bash
luma --check --strict path/to/file.luma
```

`--strict` is not limited to `--check`; it also applies when running a file's `@test` blocks: `luma --strict --test path/to/file.luma`.

## Python — Ruff

Ruff handles both linting and formatting. Install the CI-pinned version with `pip install ruff==0.15.17`. Rules live in [ruff.toml](../../ruff.toml).

1. Lint and auto-fix:

    ```bash
    ruff check --fix .
    ```

2. Format:

    ```bash
    ruff format .
    ```

## TypeScript and JavaScript — ESLint and Prettier

Run from `extensions/vscode` (install dependencies once with `npm install`):

1. Lint (append `npx eslint . --fix` to auto-fix what ESLint can):

    ```bash
    npm run lint:eslint
    ```

2. Type-check:

    ```bash
    npm run lint:types
    ```

3. Format with Prettier:

    ```bash
    npm run format
    ```

4. Run the unit tests. This mirrors the VS Code extension's CI gate (`ci-vscode.yml`), and is also a safety net if you used the optional `eslint --fix` above — its autofixes are not guaranteed to preserve behaviour (Prettier and the check-only lint cannot break logic):

    ```bash
    npm run test:unit
    ```

## Rust — rustfmt and Clippy

Run from `extensions/zed` (the Zed extension is the only first-party Rust crate):

1. Format:

    ```bash
    cargo fmt
    ```

2. Lint (warnings are errors, matching CI):

    ```bash
    cargo clippy --target wasm32-wasip1 -- -D warnings
    ```

## CSS — Stylelint

Stylelint both lints and fixes. Install the CI-pinned versions with `npm install --no-save stylelint@17.13.0 stylelint-config-standard@40.0.0`. Rules live in [stylelint.config.mjs](../../stylelint.config.mjs); vendored `*.min.css` is excluded by the config.

```bash
npx stylelint --fix $(git ls-files '*.css' ':!:*.min.css')
```

On Windows, use PowerShell:

```powershell
npx stylelint --fix @(git ls-files '*.css' ':!:*.min.css')
```

## PowerShell — PSScriptAnalyzer

PSScriptAnalyzer lints and applies fixable rules in one pass. Settings live in [PSScriptAnalyzerSettings.psd1](../../PSScriptAnalyzerSettings.psd1).

```powershell
Install-Module PSScriptAnalyzer -RequiredVersion 1.25.0 -Scope CurrentUser -Force
git ls-files '*.ps1' '*.psm1' | ForEach-Object {
    Invoke-ScriptAnalyzer -Path $_ -Settings ./PSScriptAnalyzerSettings.psd1 -Fix
}
```

`-Fix` rewrites auto-fixable findings in place; review any remaining `Warning`, `Error`, or `ParseError` diagnostics by hand — they are blocking in CI.

## Shell — ShellCheck

ShellCheck lints only; the project does not auto-format shell scripts. Fix reported issues by hand.

```bash
shellcheck $(git ls-files '*.sh' '*.bash' ':!:external/**') scripts/hooks/pre-commit
```

## CMake — cmakelint

cmakelint lints only; there is no CMake formatter in the project. Install the CI-pinned version with `pip install cmakelint==1.4.3`. Rules live in [.cmakelintrc](../../.cmakelintrc).

```bash
cmakelint --config=.cmakelintrc $(git ls-files 'CMakeLists.txt' '**/CMakeLists.txt' '*.cmake' ':!:external/**')
```

On Windows, use PowerShell:

```powershell
cmakelint --config=.cmakelintrc @(git ls-files 'CMakeLists.txt' '**/CMakeLists.txt' '*.cmake' ':!:external/**')
```

## Markdown — markdownlint-cli2

1. Auto-fix mechanical issues. File globs and rules come from [.markdownlint-cli2.jsonc](../../.markdownlint-cli2.jsonc), so pass no paths:

    ```bash
    npx --yes markdownlint-cli2@0.22.1 --fix
    ```

2. Then review against the canonical rules in [markdown.instructions.md](../../instructions/markdown.instructions.md) — the linter cannot catch every convention:
    - Heading hierarchy (no skipped levels, Title Case).
    - Table column alignment (padded cells, aligned pipes).
    - Code blocks with language identifiers.
    - No multiple consecutive blank lines.
    - Single trailing newline at end of file.
    - Short paragraphs (two to four sentences).

## Verify

After applying fixes, confirm nothing regressed — most important where you used an autofix (`clang-tidy --fix`, `ruff check --fix`, `eslint --fix`, `stylelint --fix`, PSScriptAnalyzer `-Fix`), since those can alter behaviour.

- **C++ and Luma:** one CTest run covers both. It builds and runs the C++ unit tests *and* every `.luma` feature test under `tests/features/` — each is registered as its own CTest case:

    ```bash
    cmake --build --preset default
    ctest --preset default
    ```

- **TypeScript:** the extension's unit tests are not part of CTest, so they are run separately via `npm run test:unit` (already covered in the TypeScript section above).

- **Other languages:** Rust, CSS, PowerShell, Shell, CMake, and Markdown have no test suite in their CI gate — the lint and format commands in their sections are the full check. (The Zed crate does have `cargo test`; CI does not run it, but run it manually for extra confidence after a Rust autofix.)
