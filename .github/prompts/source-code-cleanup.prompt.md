---
description: "Clean up all first-party source code by applying the project's general and per-language style, linting, and formatting rules"
agent: "agent"
---

# Source Code Cleanup

## Objective

Clean up the project's first-party source code so every file conforms to the
general rules and to its language's documented conventions, formatter, and
linter. All changes must be **behaviour-preserving**: do not alter public APIs
or observable behaviour, and keep each diff minimal and reviewable.

## Scope

- **Include** first-party sources under `core/`, `shared/`, `language-server/`,
  `debugger/`, `tests/`, `fuzz/`, `scripts/`, `examples/`, `benchmarks/`,
  `extensions/`, `cmake/`, `documents/`, `instructions/`, and the repository root.
  One file inside an otherwise-excluded tree is first-party and *is* in scope:
  the GraphicalUi override stylesheet `external/gui-framework/gui-overrides.css`
  (project-authored; linted by `ci-css.yml`).
- **Exclude** vendored, generated, and fixture code: `external/`, `build/`,
  `build-*/` (e.g. `build-fuzz/`), `node_modules/`, `target/` (Rust build output
  under `extensions/zed/`), and `fuzz/corpus/` (intentionally malformed fuzzer
  seeds — never lint or format these), plus any other build, output, or
  vendored-dependency directory.
- **Do not modify** tool configuration files (e.g. `.clang-format`, `.clang-tidy`,
  `.cmakelintrc`, `ruff.toml`, `stylelint.config.mjs`,
  `.markdownlint-cli2.jsonc`, `PSScriptAnalyzerSettings.psd1`, and the
  `extensions/vscode` / `extensions/zed` configs).

## Procedure

For every in-scope file, apply the **general rules** together with the rules for
that file's language (see the table). For each language:

1. Apply the general rules and the language's documented conventions (the linked
   `*.instructions.md`, plus the manual and guidelines for Luma).
2. Run the linter, apply safe auto-fixes, then resolve the remaining findings by
   hand. Prioritise correctness/bug and security findings over style.
3. Run the formatter (where one exists) with the project configuration. Run it
   last, so it has the final say on layout and normalises any code the linter's
   auto-fixes introduced.
4. Re-run the linter to confirm it is clean; if a fix changed layout, re-run the
   formatter too. Repeat until both pass.

Run each tool as documented in [lint-and-format.prompt.md](lint-and-format.prompt.md)
— the canonical command, pinned-version, and configuration reference — plus
CONTRIBUTING.md › "Linters and Formatters" and the per-language instructions. If a
finding is a justified false positive, suppress it narrowly with the check name and
a short reason (e.g. `// NOLINT(check-name)`), not a blanket disable.

## General Rules

**Naming & identifiers:**

- Follow the language's naming conventions.
- Use meaningful, self-documenting identifiers.
- Avoid abbreviations unless they are universally understood in the domain.

**Types & immutability:**

- Use explicit type annotations.
- Keep variables, parameters, attributes, and members immutable whenever possible.
- Prefer strong types over primitive types to encode meaning.

**Functions & structure:**

- Keep functions/methods small and focused on one thing.
- Give each class/struct/record/enumeration/trait/interface/module table/choice
  type a single, well-defined responsibility.
- Avoid deep nesting; prefer early returns and guard clauses.
- Prefer range-based loops over index-based loops.
- Write the simplest code that solves the problem correctly; do not over-abstract.

**Layout:**

- Use braces on all control structures.
- Group related logic with blank lines and use consistent spacing.

**Encapsulation & modularity:**

- Default to private visibility; expose only what consumers need.
- Hide internal state; expose behaviour through a controlled interface.
- Divide the system into distinct sections, each addressing a separate concern.
- Maximise cohesion (everything in a module is closely related) and minimise
  coupling (depend on other modules as little as possible, through the narrowest
  possible interface).

**Comments:**

- Comment *why*, not *what*.
- Remove redundant or stale comments.

**Errors & safety:**

- Detect errors as early as possible and surface them immediately.
- Prevent undefined behaviour and accidental misuse through disciplined
  initialization.

## Language Rules

| Language | Files | Conventions | Formatter (config) | Linter (config) |
| --- | --- | --- | --- | --- |
| C++ | `**/*.{cpp,hpp,h}` | `cpp.instructions.md` + [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) | clang-format (`.clang-format`) | clang-tidy (`.clang-tidy`) |
| CMake | `**/{CMakeLists.txt,*.cmake}` | `cmake.instructions.md` | — | cmakelint (`.cmakelintrc`) |
| CSS | `**/*.css` | `css.instructions.md` | — | Stylelint (`stylelint.config.mjs`) |
| JavaScript | `**/*.{js,mjs,cjs}` | `javascript.instructions.md` | Prettier (`extensions/vscode/.prettierrc.json`) | ESLint (`extensions/vscode/eslint.config.mjs`) |
| Luma | `**/*.luma` | `luma.instructions.md`, `Luma_User_Manual.md`, `Luma_Coding_Guidelines.md` | — | `luma --strict` |
| Markdown | `**/*.md` | `markdown.instructions.md`, `readme.instructions.md` | — | markdownlint-cli2 (`.markdownlint-cli2.jsonc`) |
| PowerShell | `**/*.{ps1,psm1,psd1}` | `powershell.instructions.md` | — | PSScriptAnalyzer (`PSScriptAnalyzerSettings.psd1`) |
| Python | `**/*.py` | `python.instructions.md` | Ruff (`ruff.toml`) | Ruff (`ruff.toml`) |
| Rust | `**/*.rs` | `rust.instructions.md` | rustfmt (`extensions/zed/rustfmt.toml`) | Clippy (no config file) |
| Shell | `**/*.{sh,bash}` | `shell.instructions.md` | — | ShellCheck (auto-discovered; CI `ci-shell.yml`) |
| TypeScript | `**/*.{ts,tsx}` | `typescript.instructions.md` | Prettier (`extensions/vscode/.prettierrc.json`) | ESLint (`extensions/vscode/eslint.config.mjs`) |

## Verify

After cleanup, run each language's linter and formatter (per the Language Rules
table) — where both exist — and confirm all report clean on the files you touched:

- **C++** — clang-tidy and clang-format, then build and test:

    ```bash
    cmake --build --preset default
    ctest --preset default
    ```

- **Python** — `ruff check .` and `ruff format --check --diff .`.
- **Shell** — ShellCheck (as enforced by `ci-shell.yml`).
- **PowerShell** — PSScriptAnalyzer with `PSScriptAnalyzerSettings.psd1`.
- **CSS** — `npx stylelint external/gui-framework/gui-overrides.css` with `stylelint.config.mjs`.
- **CMake** — cmakelint with `.cmakelintrc` (as enforced by `ci-cmake.yml`).
- **Markdown** — `npx markdownlint-cli2` with `.markdownlint-cli2.jsonc`.
- **Luma** — `luma --strict` on the touched `.luma` files.
- **Extensions** — run the VS Code (TS/JS — ESLint + Prettier) and Zed (Rust — Clippy +
  rustfmt) lint/format/test gates as documented
  in CONTRIBUTING.md.
