---
description: "Add a new language feature to Luma by implementing it across all interpreter phases"
agent: "agent"
argument-hint: "Feature description, e.g. 'while loops' or 'optional chaining operator'"
---

# New Language Feature

Implement a new language feature in Luma. Follow the interpreter pipeline and modify each phase:

1. Read [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) for the full architecture.
2. Read [Luma_User_Manual.md](../../documents/Luma_User_Manual.md) for existing language features.
3. Read [Luma_Coding_Guidelines.md](../../documents/Luma_Coding_Guidelines.md) for Luma coding style conventions.
4. **Design the feature concept before writing any code.** Produce a short design covering:
    - **Syntax:** concrete grammar (EBNF), new keywords/operators, and how it parses alongside existing constructs (precedence, ambiguity).
    - **Semantics & types:** runtime behaviour, type rules (`integer` vs `number`, promotion), match exhaustiveness, and error categories (`SyntaxError`/`TypeError`/runtime).
    - **Interactions:** impact on existing features (pipes, concurrency, immutability) and notable edge cases.
    - **Examples:** a few Luma snippets demonstrating the feature, to anchor the later tests and manual updates.

5. **Review the concept and refine it.** Critique the design for gaps, ambiguities, missed feature interactions, and unhandled edge cases; check the grammar for parsing conflicts and the type rules for soundness. Address any findings, then confirm the design before proceeding to implementation.
6. Follow this implementation order (one phase at a time):
    1. **Lexer** (`core/analysis/lexer/`): Add any new tokens or keywords.
    2. **AST** (`core/analysis/ast/`): Define the new AST node type(s) in the appropriate header (expression.hpp, statement.hpp, or declaration.hpp).
    3. **Parser** (`core/analysis/parser/`): Parse the new syntax into the AST node(s). Add a recursion guard to every new branch that recurses.
    4. **Type Checker** (`core/analysis/types/`): Add type checking rules for the new construct.
    5. **Linter** (`core/analysis/linter/`): Add code-quality warnings for the new construct where applicable (e.g. unused binding, dead code).
    6. **Compiler** (`core/runtime/compiler/`): Emit bytecode for the new AST node(s). Scope and variable→slot resolution happens here via `VariableResolver` (the standalone `core/analysis/resolver/` `NameResolver` is not wired into the run path). Reserve scratch slots around any value-producing block that leaves temporaries on the stack.
    7. **VM** (`core/runtime/vm/`): Implement any new opcodes and wire them into the dispatch table (`vm_dispatch_table.cpp`).
    8. **Optimizer & Verifier** (`core/runtime/compiler/`): Teach the optimizer about any new foldable operators, and register every new opcode with the bytecode verifier so valid programs are not rejected.

    (The **Include Resolver** in `core/runtime/include/` only needs changes if the feature affects file inclusion — usually not.)
7. **Review the implementation and refine it.** Check the code against the confirmed design, the documented project pitfalls in [learnings.instructions.md](../../instructions/learnings.instructions.md) (e.g. scratch-slot reservation, parser recursion guards, MSVC-vs-clang portability), security (OWASP), performance, and the C++ style guide. Address any findings before writing tests.
8. Add tests at each level:
    - `tests/analysis/lexer_test.cpp` for new tokens.
    - `tests/analysis/parser_test.cpp` for AST construction.
    - The appropriate `tests/analysis/type_checker_test_*.cpp` file for type rules, using `type_checker_test_helpers.hpp`.
    - `tests/analysis/linter_test.cpp` for any new lint warnings.
    - `tests/runtime/compiler_test.cpp` for bytecode compilation.
    - `tests/runtime/optimizer_test.cpp` and `tests/runtime/verifier_test.cpp` for new opcodes (optimisation and bytecode validation).
    - `tests/runtime/vm_test.cpp` for runtime behaviour.
    - `tests/integration/integration_test.cpp` for full-pipeline (Lexer → VM) coverage.
    - `tests/features/language/` for a Luma-level test file.
    - `fuzz/corpus/{lexer,parser,type_checker}/` seed inputs and `fuzz/dictionary.txt` entries for the new syntax, so the fuzzers exercise the new parser/type-checker branches (recursion guards in particular).

    (Add a benchmark in `benchmarks/` only if the feature is performance-sensitive — e.g. a new loop, operator, or hot-path construct. Register it in both hand-maintained lists in `benchmarks/suite.luma`.)
9. Update the documentation. The User Manual is always required; update the rest only when the feature actually touches them:
    - [Luma_User_Manual.md](../../documents/Luma_User_Manual.md) — **always**: document the new construct's syntax, types, and semantics.
    - [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) — if you added AST nodes or opcodes, or changed the pipeline.
    - [Luma_Syntax_Highlighting.md](../../documents/Luma_Syntax_Highlighting.md) — if you added keywords or operators. **Also update the editor grammars** (not Markdown, but highlighting breaks without them): `extensions/vscode/syntaxes/luma.tmLanguage.json`, `extensions/zed/grammars/tree-sitter-luma/grammar.js`, the highlight queries under `extensions/zed/languages/luma/`, and snippets in `extensions/shared/snippets/luma.json`.
    - [Luma_Coding_Guidelines.md](../../documents/Luma_Coding_Guidelines.md) — if the feature introduces a new idiom or best practice.
    - [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md) — if it adds an error category or interacts with `result`/`optional`.
    - [Luma_Performance_Guide.md](../../documents/Luma_Performance_Guide.md) — if it has notable performance characteristics.
    - The root [README.md](../../README.md) — if it changes the advertised feature set or a showcased example.

    (The [Standard Library Reference](../../documents/Luma_Standard_Library_Reference.md) covers stdlib surface, not language features. The REPL, Language Server, Debugger, GraphicalUi, and Concurrent Debugging guides — and their subsystem READMEs — only need updates if you changed those subsystems.)
10. Build and run all tests to verify nothing is broken — see [build-and-test.prompt.md](build-and-test.prompt.md) for the canonical workflow. Because a language feature touches the whole pipeline (and you added fuzz seeds in step 8), finish with the deeper [full-test-sweep.prompt.md](full-test-sweep.prompt.md), which also exercises the fuzz smoke tests, benchmarks, and examples that `ctest` does not cover.
