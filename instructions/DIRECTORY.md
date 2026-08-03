# Coding and Tooling Instructions

This directory contains the authoritative style and convention guides for the Luma project. Every document here is a full reference — read it completely before making significant changes in the relevant area.

Each file has YAML frontmatter with a `description` and optional `applyTo` pattern. VS Code Copilot uses these to automatically apply the right instructions when editing matching files.

For repository-wide context — the architecture overview, module layout, and core build commands — see [`copilot-instructions.md`](../.github/copilot-instructions.md), which references the guides below.

## Structure

| File                                                                             | Purpose                                                                | Applies To                                  |
| -------------------------------------------------------------------------------- | ---------------------------------------------------------------------- | ------------------------------------------- |
| [build.instructions.md](build.instructions.md)                                   | Build commands, presets, sanitizers, coverage, and fuzz testing        | `**/{CMakeLists.txt,CMakePresets.json}`     |
| [cmake.instructions.md](cmake.instructions.md)                                   | Target-based CMake configuration, dependency management                | `**/{CMakeLists.txt,*.cmake}`               |
| [cpp.instructions.md](cpp.instructions.md)                                       | C++ naming, style, const-correctness, RAII, error handling             | `**/*.{cpp,hpp,h}`                          |
| [css.instructions.md](css.instructions.md)                                       | CSS naming, specificity, layout, responsive design, theming            | `**/*.css`                                  |
| [git.instructions.md](git.instructions.md)                                       | Commit messages, branch naming, merge strategy                         | Manually referenced                         |
| [github-actions.instructions.md](github-actions.instructions.md)                 | CI/CD workflow conventions                                             | `.github/workflows/**`                      |
| [github-actions-recipes.instructions.md](github-actions-recipes.instructions.md) | Copy-paste workflow recipes and debugging guidance                     | Manually referenced                         |
| [javascript.instructions.md](javascript.instructions.md)                         | JavaScript naming, style, async patterns, modules, error handling      | `**/*.{js,mjs,cjs}`                         |
| [learnings-build.instructions.md](learnings-build.instructions.md)               | Build system, CI, static analysis, and C++ portability pitfalls        | `{CMakeLists.txt,CMakePresets.json,cmake/**,.github/workflows/**,scripts/**}` |
| [learnings-compiler.instructions.md](learnings-compiler.instructions.md)         | Compiler, optimizer, bytecode serialization, and the scratch-slot invariant | `core/runtime/compiler/**`              |
| [learnings-dap.instructions.md](learnings-dap.instructions.md)                   | Debug adapter architecture, thread safety, expression evaluation, and breakpoint semantics | `debugger/**`                 |
| [learnings-extensions.instructions.md](learnings-extensions.instructions.md)     | VS Code and Zed editor extensions, Tree-sitter queries, and highlight pitfalls | `extensions/**`                     |
| [learnings-gui.instructions.md](learnings-gui.instructions.md)                   | GraphicalUi module, Solaris prelude, headless testing, and renderer pitfalls | `{core/runtime/stdlib/**/graphicalui*,core/analysis/prelude/**,external/gui-framework/**}` |
| [learnings-lsp.instructions.md](learnings-lsp.instructions.md)                   | Language server architecture, symbol resolution, and column/range pitfalls | `language-server/**`                  |
| [learnings-stdlib.instructions.md](learnings-stdlib.instructions.md)             | Standard library infrastructure, module registration, common utilities, and sandbox mode | `{core/runtime/stdlib/**,shared/stdlib/**}` |
| [learnings-vm.instructions.md](learnings-vm.instructions.md)                     | Virtual machine architecture, dispatch table, and component decomposition | `core/runtime/vm/**`                    |
| [learnings.instructions.md](learnings.instructions.md)                           | Core project knowledge — language design, architecture overview, and cross-cutting patterns | `**/*` (all files)            |
| [luma.instructions.md](luma.instructions.md)                                     | Luma language syntax, types, standard library usage                    | `**/*.luma`                                 |
| [markdown.instructions.md](markdown.instructions.md)                             | Documentation structure and formatting                                 | `**/*.md`                                   |
| [powershell.instructions.md](powershell.instructions.md)                         | PowerShell naming, style, pipeline, error handling, Pester             | `**/*.{ps1,psm1,psd1}`                      |
| [python.instructions.md](python.instructions.md)                                 | Python naming, style, type hints, error handling, testing              | `**/*.py`                                   |
| [readme.instructions.md](readme.instructions.md)                                 | README content and update rules                                        | `{**/DIRECTORY.md,README.md}`                  |
| [rust.instructions.md](rust.instructions.md)                                     | Rust naming, style, ownership, error handling, unsafe code             | `**/*.rs`                                   |
| [shell.instructions.md](shell.instructions.md)                                   | Shell portability, quoting, error handling, Linux/macOS support        | `**/*.{sh,bash}`                            |
| [software-architecture.instructions.md](software-architecture.instructions.md)   | Language-agnostic design principles, modularity, safety                | `{core,shared,language-server,debugger}/**` |
| [testing.instructions.md](testing.instructions.md)                               | C++ test framework, Luma `@test` tests, assertion macros, fuzz testing | `tests/**`                                  |
| [typescript.instructions.md](typescript.instructions.md)                         | TypeScript naming, style, type safety, async patterns                  | `**/*.{ts,tsx}`                             |
| [ux-design.instructions.md](ux-design.instructions.md)                           | UX, usability, and graphic design principles                           | Manually referenced                         |

Keep this table in sync with the directory: when you add or remove a guide, update its row and preserve the alphabetical order.

## Design Notes

These conventions explain why the guides are structured the way they are. Keep them in mind
when adding or editing instruction files.

### Self-contained guides (intentional duplication)

VS Code Copilot loads each guide **independently**, based on its `applyTo` glob, and only when
a matching file is in context. There is no include mechanism between guides. Consequently, each
guide is deliberately **self-contained**: shared ideas — the "is there a simpler way?" KISS
check, naming-by-intent, fail-fast error handling — are intentionally repeated rather than
extracted into a common file. Extracting them would break the model, because the shared file
would not load for the matched file type. When a rule changes, update every guide that states
it rather than trying to centralise it.

### Auto-loaded baseline and overlap

- [learnings.instructions.md](learnings.instructions.md) uses `applyTo: **/*`, so it loads as a
  baseline alongside whichever language-specific guide matches the file in context. It is the
  one guide that is always active; the language guides layer on top of it.
- The narrower `learnings-*.instructions.md` sibling files add domain-specific detail on top of
  that baseline when their targeted `applyTo` globs match the files you are editing.
- Some globs overlap by design. A `CMakeLists.txt` matches both
  [build.instructions.md](build.instructions.md) and [cmake.instructions.md](cmake.instructions.md);
  both sets of guidance apply together.
- [git.instructions.md](git.instructions.md) has **no** `applyTo` pattern. Git conventions are
  not tied to a file type, so it is referenced manually rather than auto-applied.

### Heading convention

The topical language and tooling guides number their top-level sections as `## N — Title`
(em dash), with a Table of Contents linking to each. Two files are deliberate exceptions and use
plain, unnumbered headings: this `DIRECTORY.md` (an index, not a guide) and
[learnings.instructions.md](learnings.instructions.md) (an append-only log whose sections are
added over time). Follow the numbered style when authoring a new topical guide.
