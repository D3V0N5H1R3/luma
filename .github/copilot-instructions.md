# Luma — Copilot Instructions

Luma is an interpreted, statically typed, expression-oriented programming language designed for beginners. The interpreter is a command-line application written in modern C++ (C++20), with a bytecode compiler and stack-based VM execution backend.

## Key Documents

Read these before making significant changes:

- [Documentation Index](../documents/DIRECTORY.md) — Catalogue of every design, reference, and guide document (including the REPL, Solaris, GraphicalUi, and Concurrent Debugging guides not listed below)
- [Luma_Initial_Concept.md](../documents/Luma_Initial_Concept.md) — Language design goals and motivation
- [Luma_Software_Architecture.md](../documents/Luma_Software_Architecture.md) — Interpreter architecture and module design
- [Luma_User_Manual.md](../documents/Luma_User_Manual.md) — Complete language reference
- [Luma_Standard_Library_Reference.md](../documents/Luma_Standard_Library_Reference.md) — Standard library and built-in function reference
- [Luma_Coding_Guidelines.md](../documents/Luma_Coding_Guidelines.md) — Luma coding style and conventions
- [Luma_Error_Handling.md](../documents/Luma_Error_Handling.md) — Error categories, result/optional handling, and stdlib conventions
- [Luma_Performance_Guide.md](../documents/Luma_Performance_Guide.md) — Performance characteristics and optimisation advice
- [Luma_Debugger.md](../documents/Luma_Debugger.md) — DAP debugger design and architecture
- [Luma_Language_Server.md](../documents/Luma_Language_Server.md) — LSP language server design and implementation
- [Luma_Syntax_Highlighting.md](../documents/Luma_Syntax_Highlighting.md) — Syntax highlighting and editor extension design
- [CONTRIBUTING.md](../CONTRIBUTING.md) — Contribution workflow

## Architecture

The interpreter follows a multi-phase pipeline:

```text
Source Code → Lexer → Parser → Include Resolver → Type Checker → Linter → Compiler → VM
```

Each phase transforms a well-defined input into a well-defined output. Information flows in one direction.

### Module Layout

| Directory                    | Responsibility                                            |
| ---------------------------- | --------------------------------------------------------- |
| `core/analysis/ast/`         | AST node type definitions (data only)                     |
| `core/analysis/diagnostics/` | Structured diagnostic messages with source spans          |
| `core/analysis/errors/`      | Error types and diagnostic reporting                      |
| `core/analysis/lexer/`       | Tokenisation of source text                               |
| `core/analysis/linter/`      | Post-type-check code quality warnings                     |
| `core/analysis/parser/`      | AST construction from token stream                        |
| `core/analysis/pipeline/`    | Composable compilation pass pipeline                      |
| `core/analysis/resolver/`    | Name resolution to stack-slot indices for the VM          |
| `core/analysis/source/`      | Source file loading and location tracking                 |
| `core/analysis/types/`       | Static type checking and stdlib type signatures           |
| `core/runtime/cli/`          | Command-line argument parsing and dispatch                |
| `core/runtime/compiler/`     | AST-to-bytecode compiler (105 opcodes)                    |
| `core/runtime/concurrency/`  | Channels, tasks, and thread pool                          |
| `core/runtime/include/`      | File inclusion and deduplication                          |
| `core/runtime/interpreter/`  | Runtime value types, environment, and control flow        |
| `core/runtime/repl/`         | Interactive read-eval-print loop                          |
| `core/runtime/stdlib/`       | Built-in standard library modules                         |
| `core/runtime/vm/`           | Stack-based virtual machine                               |
| `core/common/`               | Shared utilities (resource limits, result types, caches)  |
| `shared/json/`               | JSON utilities shared by language server and debugger     |
| `shared/protocol/`           | Protocol transport shared by language server and debugger |
| `shared/stdlib/`             | Stdlib metadata shared by interpreter and language server |
| `shared/symbols/`            | Shared symbol kinds and qualified-name helpers            |
| `language-server/source/`    | Language Server Protocol (LSP) implementation             |
| `debugger/source/`           | Debug Adapter Protocol (DAP) implementation               |
| `tests/analysis/`            | C++ unit tests for the analysis front-end                 |
| `tests/runtime/`             | C++ unit tests for the runtime back-end                   |
| `tests/integration/`         | Full pipeline integration tests                           |
| `tests/features/language/`   | Core language feature test suites                         |
| `tests/features/stdlib/`     | Standard library module test suites                       |
| `tests/platform/`            | Platform-specific tests (e.g. Win32 UTF-8 handling)       |
| `examples/`                  | Luma example programs and test suites                     |
| `instructions/`              | Detailed coding and tooling instructions                  |

## Build and Test

```bash
# Configure, build, and test (Release) using CMake presets
cmake --preset default
cmake --build --preset default
ctest --preset default

# Built binaries are placed at the build-tree root, so the path depends on the generator:
#   single-config (Ninja, Makefiles):    build/luma, build/luma_lsp, build/luma_dap
#   multi-config  (Visual Studio, Xcode): build/Release/luma(.exe), build/Release/luma_lsp(.exe), ...

# Run a Luma program
build/Release/luma examples/language-features/hello.luma

# Start the REPL
build/Release/luma

# Start the language server and DAP debugger (normally launched by the editor, not manually)
build/Release/luma_lsp
build/Release/luma_dap
```

> For build presets, sanitizers, coverage, and fuzz testing, see [instructions/build.instructions.md](../instructions/build.instructions.md).

## Coding Conventions

Follow the detailed instructions in `instructions/`:

- **C++ style:** `snake_case` for variables, functions, namespaces, files. `PascalCase` for types. 4-space indentation. Always use braces. West-const (`const int`). See [instructions/cpp.instructions.md](../instructions/cpp.instructions.md).
- **Rust style:** `snake_case` for variables, functions, modules. `PascalCase` for types, traits, enum variants. `UPPER_CASE` for constants. See [instructions/rust.instructions.md](../instructions/rust.instructions.md).
- **TypeScript style:** `camelCase` for variables, functions. `PascalCase` for types, classes, interfaces. `UPPER_CASE` for constants. See [instructions/typescript.instructions.md](../instructions/typescript.instructions.md).
- **JavaScript style:** `camelCase` for variables, functions. `PascalCase` for classes. `UPPER_CASE` for constants. `kebab-case` for file names. `const` by default, `let` when needed, never `var`. Always use `===`/`!==`. See [instructions/javascript.instructions.md](../instructions/javascript.instructions.md).
- **CSS style:** BEM naming (`block__element--modifier`). `kebab-case` for custom properties. 4-space indentation. Low specificity — no IDs for styling, no `!important`. Custom properties for all colours and spacing. Mobile-first responsive design. See [instructions/css.instructions.md](../instructions/css.instructions.md).
- **Python style:** `snake_case` for variables, functions, modules. `PascalCase` for classes. `UPPER_CASE` for constants. Type hints on all functions. `pytest` for testing. See [instructions/python.instructions.md](../instructions/python.instructions.md).
- **Shell style:** `snake_case` for variables and functions. `UPPER_CASE` for constants. `set -euo pipefail`. Always quote variables. Portable across Linux and macOS. See [instructions/shell.instructions.md](../instructions/shell.instructions.md).
- **PowerShell style:** `PascalCase` for variables, functions, parameters. Approved Verb-Noun cmdlet names. `[CmdletBinding()]` on functions. Full cmdlet names — no aliases. See [instructions/powershell.instructions.md](../instructions/powershell.instructions.md).
- **Luma language:** Types, naming, mutability, error handling, pipes, and testing. See [instructions/luma.instructions.md](../instructions/luma.instructions.md).
- **CMake style:** Targets over variables. No `file(GLOB)`. Pin dependency versions. See [instructions/cmake.instructions.md](../instructions/cmake.instructions.md).
- **Git conventions:** Conventional commit prefixes (`feat:`, `fix:`, `refactor:`, `test:`, `docs:`, `chore:`). Branch naming: `feature/`, `fix/`, `docs/`. See [instructions/git.instructions.md](../instructions/git.instructions.md).
- **GitHub Actions:** Workflow triggers, permissions, job structure, caching, and CI/CD security. See [instructions/github-actions.instructions.md](../instructions/github-actions.instructions.md).
- **GitHub Actions recipes:** Copy-paste workflow recipes (C++/CMake CI, Docker, release, deployment, CodeQL) and debugging guidance. See [instructions/github-actions-recipes.instructions.md](../instructions/github-actions-recipes.instructions.md).
- **Testing:** Custom C++ test framework, Luma `@test` feature tests, assertion macros, and test structure. See [instructions/testing.instructions.md](../instructions/testing.instructions.md).
- **Markdown:** Structure, formatting, linking, and content guidelines. See [instructions/markdown.instructions.md](../instructions/markdown.instructions.md).
- **README files:** Structure, section ordering, writing style, and completeness. See [instructions/readme.instructions.md](../instructions/readme.instructions.md).
- **Software architecture:** Simplicity, modularity, separation of concerns, naming, and encapsulation. See [instructions/software-architecture.instructions.md](../instructions/software-architecture.instructions.md).
- **UX and visual design:** User-centred design, visual hierarchy, Gestalt grouping, colour and contrast, typography, spacing, feedback, accessibility, and the usability heuristics. See [instructions/ux-design.instructions.md](../instructions/ux-design.instructions.md).

### C++ Specifics

- C++20, third-party runtime dependencies only as exceptions when functionality cannot be achieved with the standard library and OS APIs (see `instructions/cpp.instructions.md` §7).
- `const` by default, `constexpr` where possible.
- RAII for all resource management. `std::unique_ptr` for ownership.
- Exceptions for errors, `std::optional` for expected absence.
- Prefer algorithms and range-based for loops over index-based iteration.
- `[[nodiscard]]` on functions whose return values must not be ignored.
- Mark single-argument constructors `explicit`.

### Luma Language

- Entry point: `@main` annotated function.
- Types: `boolean`, `integer`, `number`, `decimal`, `string`, `array<T>`, `dictionary<V>`, `queue<T>`, `stack<T>`, `channel<T>`, `task<T>`, `optional<T>`, `result<T>`, `socket`, tuples, choice types (ADTs), records, interfaces.
- Variables are immutable by default; use `mutable` keyword for mutability.
- No semicolons required. Comments start with `#`.
- String interpolation: `"value is ${expr}"`.
- Pipe operator: `value |> Module.function()`.
- Structured concurrency: `task_scope { }` blocks with cooperative cancellation; `Task.cancel(t)`, `Task.is_cancelled(t)`.
- Standard library modules: `String`, `Array`, `Dictionary`, `Math`, `Bits`, `Result`, `Converter`, `DateTime`, `Decimal`, `Console`, `FileSystem`, `RegularExpression`, `Process`, `Random`, `Encoder`, `Resource`, `Set`, `Channel`, `Task`, `Terminal`, `GraphicalUi`, `Socket`, `Optional`, `Reference`, `Queue`, `Stack`, `Log`, `Json`, `Csv`, `Xml`, `LinearAlgebra`, `Calculus`, `Statistics`, `Hash`, `Compression`, `Http`, `KeyValueStore`.

> **Note for AI assistants:** This file serves as the single source of truth for project context. See [instructions/learnings.instructions.md](../instructions/learnings.instructions.md) for accumulated development learnings and pitfalls (auto-loaded for all files).
