# Luma — Software Architecture

> Software architecture for the Luma programming language interpreter — a command-line application written in modern C++.

---

## Table of Contents

1. [Introduction](#1--introduction)
2. [Design Goals and Constraints](#2--design-goals-and-constraints)
3. [High-Level Architecture](#3--high-level-architecture)
4. [Module Decomposition](#4--module-decomposition)
5. [Data Structures](#5--data-structures)
6. [Processing Pipeline](#6--processing-pipeline)
7. [Bytecode Compiler and Virtual Machine Internals](#7--bytecode-compiler-and-virtual-machine-internals)
8. [Type System Design](#8--type-system-design)
9. [Memory Management Strategy](#9--memory-management-strategy)
10. [Error Handling Strategy](#10--error-handling-strategy)
11. [Standard Library Architecture](#11--standard-library-architecture)
12. [Concurrency Architecture](#12--concurrency-architecture)
13. [REPL Architecture](#13--repl-architecture)
14. [Testing Architecture](#14--testing-architecture)
15. [File Inclusion and Source Management](#15--file-inclusion-and-source-management)
16. [Project File Structure](#16--project-file-structure)
17. [Cross-Platform Considerations](#17--cross-platform-considerations)
18. [Debugger Architecture](#18--debugger-architecture)
19. [Design Decisions and Rationale](#19--design-decisions-and-rationale)

- [See Also](#see-also)

---

## 1 — Introduction

Luma is an interpreted, statically typed, expression-oriented programming language designed for beginners. Its interpreter is a command-line application written in modern C++ that reads Luma source files, analyses them, and executes them directly.

This document describes the internal architecture of the Luma 1.0 interpreter — how it is structured, how its modules interact, and why specific design decisions were made.

### Scope

This architecture covers the complete interpreter: lexical analysis, parsing, static type checking, runtime execution, the standard library, the REPL, the test runner, concurrency support, and the DAP debugger.

### Guiding Principles

Every architectural decision follows the principles defined in the Software Architecture Instructions:

- **KISS** — Choose the simplest design that solves the problem correctly.
- **Single Responsibility** — Each module owns exactly one concern.
- **Separation of Concerns** — Analysis, type checking, and execution are distinct phases.
- **Fail Fast** — Detect and report errors as early as possible in the pipeline.
- **Encapsulation** — Modules expose narrow interfaces; internals remain private.
- **High Cohesion, Low Coupling** — Group related logic together; minimise cross-module dependencies.

---

## 2 — Design Goals and Constraints

### Goals

| Goal                 | Description                                                                              |
| -------------------- | ---------------------------------------------------------------------------------------- |
| Beginner-friendly    | The language and its tooling should feel approachable — no complex setup, no surprises.  |
| Clear error messages | Every error includes the source location, a description, and enough context to act on.   |
| Cross-platform       | The interpreter builds and runs on Windows, Linux (Ubuntu), and macOS.                   |
| Safety               | Static type checking catches errors before execution. No undefined behaviour at runtime. |
| Simplicity           | The codebase should be understandable by a developer who has not seen it before.         |

### Constraints

| Constraint          | Description                                                                                                                                  |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| Language            | The interpreter is written in modern C++ (C++20 or later).                                                                                   |
| Minimal third-party | Third-party libraries only as exceptions for functionality beyond the C++ standard library and OS APIs (e.g., compression, TLS, native GUI). |
| Single entry point  | Every Luma program has exactly one `@main`-annotated function.                                                                               |
| Bytecode execution  | The interpreter uses a bytecode compiler and stack-based VM execution backend.                                                               |

---

## 3 — High-Level Architecture

The interpreter follows a classic multi-phase pipeline. Each phase transforms a well-defined input into a well-defined output and passes it to the next phase.

```text
┌─────────────┐    ┌─────────┐    ┌──────────┐    ┌──────────────────┐    ┌──────────────┐    ┌────────┐    ┌──────────┐    ┌────┐
│ Source Code │───▸│  Lexer  │───▸│  Parser  │───▸│ Include Resolver │───▸│ Type Checker │───▸│ Linter │───▸│ Compiler │───▸│ VM │
│   (string)  │    │         │    │          │    │                  │    │              │    │        │    │          │    │    │
└─────────────┘    └─────────┘    └──────────┘    └──────────────────┘    └──────────────┘    └────────┘    └──────────┘    └────┘
                        │              │                  │                     │              │              │
                   Token Stream       AST           Merged AST            Typed AST       Warnings      Bytecode    Execution
```

Each phase transforms a well-defined input into a well-defined output. Information flows in one direction, which keeps coupling low and makes each phase independently testable.

### Execution Modes

The CLI module selects one of several modes based on command-line arguments. Each mode drives the same pipeline but stops at a different point or adds behaviour on top.

| Mode            | Command                         | Pipeline Phases Used                                                                            |
| --------------- | ------------------------------- | ----------------------------------------------------------------------------------------------- |
| Run             | `luma <file.luma>`              | Lexer → Parser → Include Resolver → Type Checker → Linter → Compiler → VM                       |
| Type-check only | `luma --check, -c <file.luma>`  | Lexer → Parser → Include Resolver → Type Checker → Linter                                       |
| Strict run      | `luma --strict, -s <file.luma>` | Lexer → Parser → Include Resolver → Type Checker → Linter (warnings are errors) → Compiler → VM |
| Strict check    | `luma --check --strict <file>`  | Lexer → Parser → Include Resolver → Type Checker → Linter (warnings are errors)                 |
| Test            | `luma --test, -t <file.luma>`   | Lexer → Parser → Include Resolver → Type Checker → Linter → Compiler → VM Test Runner           |
| Sandbox         | `luma --box, -b <file.luma>`    | Lexer → Parser → Include Resolver → Type Checker → Linter → Compiler → VM (OS modules disabled) |
| Package init    | `luma pkg init`                 | Creates a `luma.json` manifest in the current directory                                         |
| REPL            | `luma`                          | Loop: Lexer → Parser → Compiler → VM (`:file` adds Include Resolver)                            |
| REPL (explicit) | `luma --repl, -r`               | Loop: Lexer → Parser → Compiler → VM (`:file` adds Include Resolver)                            |
| Eval (stdin)    | `luma --eval, -e`               | Stdin → Lexer → Parser → Type Checker → Compiler → VM (no `@main` required)                     |

---

## 4 — Module Decomposition

Each module has a single responsibility, a well-defined interface, and minimal knowledge of other modules.

### 4.1 CLI

**Responsibility:** Parse command-line arguments and dispatch to the appropriate execution mode.

**Interface:**

- Input: `argc`, `argv` from `main()`.
- Output: A configuration value describing the selected mode and file path.

**Behaviour:**

- Parses flags: `--test` / `-t`, `--check` / `-c`, `--strict` / `-s`, `--box` / `-b`, `--version` / `-v`, `--help` / `-h`, `--repl` / `-r`, `--eval` / `-e`, `--optimize` / `-O`, `--verify`, and the optional source file path. The `pkg` subcommand delegates to a package management handler.
- Flags may appear in any order — the parser determines the requested command (run, test, check, etc.) and identifies the file path as the first non-flag argument.
- Validates that the provided file path has the `.luma` extension.
- On an unknown flag, computes the Levenshtein edit distance against all known flags and suggests the closest match (threshold ≤ 2): `error: unknown option '--hepl'; did you mean '--help'?`. Returns exit code 5 (`usage_error`).
- Delegates to the REPL module (`--repl` / `-r` or no file argument), the standard-input evaluator (`--eval` / `-e`), the pipeline runner (file argument), the test runner (`--test` / `-t`), the type-checker (`--check` / `-c`), the package manager (`pkg`), or information output (`--version` / `-v`, `--help` / `-h`).
- Uses standardised exit codes: 0 (success), 1 (runtime error), 2 (type error), 3 (syntax error), 4 (compile error), 5 (usage error). The `exit_code::from_diagnostic_category()` function maps `DiagnosticCategory` values to their corresponding codes.

**Dependencies:** Source Manager, Lexer, Parser, Include Resolver, Type Checker, Linter, Compiler, VM, Environment, Standard Library, Test Runner, Common (Terminal).

_Note:_ The CLI module has two roles: parsing command-line arguments (pure logic, no dependencies) and orchestrating the pipeline (depends on all pipeline phases). These are kept in a single module because separating them would add indirection without meaningful benefit — the orchestration logic is a straightforward sequential dispatch.

### 4.2 Source Manager

**Responsibility:** Load source files from disk and track source locations for error reporting.

**Interface:**

- `load(path) → const SourceFile&` — Read a file and return a reference to its stored metadata. Throws `std::runtime_error` if the file cannot be opened. Subsequent calls with the same path return the previously loaded entry without re-reading.
- `is_loaded(path) → bool` — Return `true` if the path has already been loaded.
- `get_line(file_id, line) → string_view` — Retrieve a specific line (1-based) for error context display. Returns an empty view if the file ID or line number is out of range.
- `get_file(file_id) → const SourceFile*` — Return a pointer to the `SourceFile` for a given ID, or `nullptr` if not found.

**Behaviour:**

- Reads source files as raw bytes (binary mode) and treats them as UTF-8 text, so byte offsets into the stored text match the on-disk file identically on every platform.
- Assigns each loaded file a unique `FileId` identifier (starting at 1; 0 is reserved for REPL input with no backing file).
- Maintains a path-to-ID registry so that `load()` is idempotent and `is_loaded()` is O(1). The Include Resolver uses `is_loaded()` to skip already-included files.
- Stores the original source text alongside a compact per-line byte-offset index (rather than a second copy of each line), so that error messages can display the offending line with a caret pointing to the exact column.

**Dependencies:** None.

### 4.3 Lexer

**Responsibility:** Convert a source text string into a flat sequence of tokens.

**Interface:**

- Constructor: `Lexer(source, file_id)` — takes the source text and an optional file identifier (default 0 for REPL/inline use).
- `tokenize() → vector<Token>` — Scan the source text and return all tokens. Throws `SyntaxError` on the first unrecognised character.

**Behaviour:**

- Single-pass, character-by-character scanning from left to right.
- Produces one `Token` per lexical unit: keyword, identifier, literal, operator, punctuation, annotation, or end-of-file.
- Classifies numeric literals as `IntegerLiteral` (no decimal point or exponent) or `NumberLiteral` (contains `.` or `e`/`E`).
- Handles string interpolation by producing a sequence of tokens: `StringStart`, `StringMiddle` (between interpolation slots), and `StringEnd`, with normal expression tokens in between. Plain strings without interpolation produce a single `StringLiteral` token.
- Handles triple-quoted multi-line strings (`"""..."""`) by stripping common leading whitespace (dedenting) and producing the same token types as single-line strings.
- Recognises annotations (`@main`, `@test`) as `Annotation` tokens.
- Skips whitespace (spaces, tabs, carriage returns, newlines, semicolons) and comments (`#` to end of line). Semicolons are treated as whitespace because they are optional in Luma.
- Attaches a `SourceLocation` (file identifier, line, column) to every token.
- On encountering an unrecognised character, throws a `SyntaxError` with its location and aborts scanning.

**Dependencies:** Source Manager (for file identifiers), Errors (for error types), Common (for resource limits).

### 4.4 Parser

**Responsibility:** Transform a flat sequence of tokens into an Abstract Syntax Tree.

**Interface:**

- Constructor: `Parser(tokens)` — takes the token stream produced by the Lexer.
- `parse() → Program` — Consume all tokens and return the root `Program` node. Throws `SyntaxError` on parse failure.

**Behaviour:**

- Recursive-descent parser consuming tokens from left to right.
- Parses top-level declarations (`function`, `record`, `choice`, `interface`, `namespace`, `type`, `include`, `use`) and top-level statements.
- Expression parsing uses precedence climbing (see Section 6.3 for the full precedence table).
- Named arguments (`name: expr`) are parsed as part of call expressions and stored alongside positional arguments.
- Attaches the `SourceLocation` of the primary token to each AST node.
- On a parse error, throws a `SyntaxError` with the location and aborts; no partial AST is returned.

**Dependencies:** Lexer (token stream), AST, Errors, Common (for resource limits).

### 4.5 AST (Abstract Syntax Tree)

**Responsibility:** Define the data structures that represent the syntactic structure of a Luma program.

**Interface:**

- A set of node types (C++ structs/classes) organised into three categories: expressions, statements, and declarations.
- Every node stores its `SourceLocation` for error reporting in later phases.

This module contains only data definitions — no logic.

**Node Categories:**

_Expressions_ (produce a value):

- `ArrayLiteralExpression` — array literal with element expressions.
- `AwaitExpression` — `await task` producing `T`.
- `BinaryExpression` — two operands and an operator (`+`, `-`, `*`, `/`, `//`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `??`, `&`, `|`, `^`, `<<`, `>>`, `in`). The `in` operator checks membership: element in array, key in dictionary, or substring in string. The result type is `boolean`.
- `CallExpression` — function or lambda invocation with positional and named arguments.
- `DictionaryLiteralExpression` — dictionary literal with key-value pairs.
- `DowncastExpression` — `downcast<T>(expr)` keyword expression producing `result<T>`. Parsed as a dedicated AST node, not as a function call.
- `ErrorPipeExpression` — `left !> right` — unwrap `success(v)` from left and pipe `v` into right; short-circuit to `failure(msg)` if left is a failure result.
- `FailureExpression` — `failure(msg)` keyword expression producing `result<T>`. Parsed as a dedicated AST node, not as a function call.
- `FieldAccessExpression` — record field or tuple element access (`expr.field`, `expr.0`).
- `IfExpression` — conditional expression producing a value from both branches.
- `IndexAccessExpression` — array element access (`expr[index]`).
- `IsExpression` — `is<T>(expr)` keyword expression producing `boolean`. Tests whether a value matches a type without binding it. Parsed as a dedicated AST node, not as a function call.
- `LambdaExpression` — anonymous function with parameters, optional return type, and body.
- `LiteralExpression` — integer, number, boolean, string, none.
- `MatchExpression` — pattern-match expression producing a value. Contains a subject expression and a list of `MatchArm` nodes. Each arm has one of the following kinds:
    - `BooleanCase` — `case true` or `case false`.
    - `ChoiceCase` — choice variant pattern with optional destructured field bindings (e.g., `case Shape.Circle(r)`).
    - `Comparison` — operator and value (e.g., `case >= 90`, `case == "quit"`).
    - `Else` — the fallback `else` arm.
    - `IntegerCase` — bare integer literal (e.g., `case 1`, `case 42`).
    - `NoneCase` — `case none`.
    - `SomeCase` — `some(binding)` with a bound variable name.
    - `SuccessResult` / `FailureResult` — `success(binding)` or `failure(binding)` with a bound variable name.
    - `VariantCase` — choice variant without destructuring (e.g., `case Direction.North`).

    Each arm may also carry a list of `AlternativePattern` entries for multi-pattern syntax (`case A | B`). Alternatives are limited to simple pattern kinds (boolean, integer, choice variant, string, comparison, none).

- `PipeExpression` — left-hand expression piped into right-hand function call.
- `RangeExpression` — `a..b` (exclusive end) or `a..=b` (inclusive end) producing a range value.
- `RecordCreationExpression` — record instantiation with field values.
- `RecordWithExpression` — `record with { field = value, ... }` producing a copy of a record with the given fields overridden.
- `SomeExpression` — `some(expr)` keyword expression producing `optional<T>`. Parsed as a dedicated AST node, not as a function call.
- `SpawnExpression` — `spawn fn(args)` producing `task<T>`.
- `TaskScopeExpression` — `task_scope { ... }` structured concurrency block producing `array<T>` of child task results.
- `StringInterpolationExpression` — string template composed of literal parts and embedded expressions.
- `SuccessExpression` — `success(expr)` keyword expression producing `result<T>`. Parsed as a dedicated AST node, not as a function call.
- `TupleLiteralExpression` — tuple literal with 2–4 element expressions.
- `UnaryExpression` — prefix operator and operand (`!`, `-`, `~`); or postfix propagation operator `?` applied to a `result<T>` or `optional<T>` value.
- `VariableExpression` — variable reference by name.

_Statements_ (perform an action, produce no value):

- `AssignmentStatement` — target (variable, field, index) and value expression.
- `BlockStatement` — a scoped block of statements delimited by `{` and `}`.
- `BreakStatement` / `ContinueStatement` — loop control.
- `CompoundAssignmentStatement` — target, operator (`+=`, `-=`, `*=`, `/=`, `//=`, `%=`, `|=`, `&=`, `^=`, `<<=`, `>>=`), and value expression.
- `ExpressionStatement` — expression evaluated for its side effects (e.g., function call).
- `ForStatement` — loop variable(s), iterable expression (range, array, string, or dictionary), and body block.
- `IfStatement` — condition, then-block, optional else-if chain, optional else-block.
- `IncrementStatement` / `DecrementStatement` — target variable and `++` or `--`.
- `MatchStatement` — subject expression and match arms (for side effects, not value).
- `ReturnStatement` — optional value expression.
- `TryStatement` — try body, optional catch clause (with bound error variable), and optional finally block.
- `TupleDestructuringStatement` — binds tuple elements to named variables.
- `VariableDeclaration` — type, name, mutability flag, and initialiser expression. Listed here because variable declarations appear as statements inside function bodies; at the top level, they are treated as declarations.
- `WhileStatement` — condition expression and body block.

_Declarations_ (define names at the top level or within a namespace):

- `ChoiceDeclaration` — name, optional type parameters, and variant definitions. Each variant has a name and optional typed fields. Variants may reference the enclosing choice type (recursive ADTs), including generic self-references (e.g., `List<T>` containing `List<T>` fields). Choice declarations with all unit variants (no data fields) serve the same role as enumerations.
- `FunctionDeclaration` — return type (before the name), name, parameters (with optional `trusted` and `mutable` modifiers, optional defaults), body, annotations.
- `IncludeDeclaration` — file path string.
- `InterfaceDeclaration` — name and required field definitions (name + type).
- `NamespaceDeclaration` — name and contained declarations.
- `RecordDeclaration` — name and field definitions (name + type + optional `private` flag).
- `TypeAliasDeclaration` — alias name and target type.
- `UseDeclaration` — namespace name or namespace-qualified name.

**Dependencies:** None.

### 4.6 Type System

**Responsibility:** Define the representation of all Luma types and provide operations for type comparison, compatibility checking, and interface satisfaction.

**Interface:**

- `TypeInfo` — A struct with a `Kind` discriminator, an optional name (for choices, records, interfaces), inner types (for generics), and a return type (for function types). Equality is tested via `TypeInfo::operator==`.
- `TypeInfo::to_string() → string` — Human-readable type name for error messages. A cached variant `TypeInfo::to_string_cached()` memoises the result to avoid repeated string allocations during type checking.
- `TypeChecker::is_assignable(target, source) → bool` — Can a value of `source` type be used where `target` is expected? (Method on `TypeChecker`.)
- `TypeChecker::satisfies_interface(record_name, interface_name) → bool` — Does the named record satisfy the named interface? (Method on `TypeChecker`.)
- Common-type promotion (e.g., `integer` + `number` → `number`) is computed inline during type inference in the `TypeChecker`.

**Type Representation:**

| Type Kind             | `TypeInfo::Kind` | Notes                                                                                                                |
| --------------------- | ---------------- | -------------------------------------------------------------------------------------------------------------------- |
| `(T1, T2, ...)`       | `Tuple`          | Inner types hold element types.                                                                                      |
| `array<T>`            | `Array`          | One inner type (element type).                                                                                       |
| `boolean`             | `Boolean`        | Primitive.                                                                                                           |
| `channel<T>`          | `Channel`        | One inner type.                                                                                                      |
| `dictionary<T>`       | `Dictionary`     | One inner type (value type; key is always `string`).                                                                 |
| `function(P...) -> R` | `Func`           | Inner types are parameter types; `return_type` holds `R`.                                                            |
| `integer`             | `Integer`        | Primitive.                                                                                                           |
| `none`                | `None`           | Primitive. The empty case of `optional<T>`.                                                                          |
| `number`              | `Number`         | Primitive.                                                                                                           |
| `optional<T>`         | `Optional`       | One inner type (the wrapped value type).                                                                             |
| `range`               | `Range`          | Produced by range expressions (`a..b`, `a..=b`).                                                                     |
| `reference<T>`        | `Reference`      | One inner type (the wrapped value type). Shared identity.                                                            |
| `result<T>`           | `Result`         | One inner type (success value type).                                                                                 |
| `socket`              | `Socket`         | Opaque handle for network connections.                                                                               |
| `string`              | `String`         | Primitive.                                                                                                           |
| `task<T>`             | `Task`           | One inner type (result type).                                                                                        |
| `void`                | `Void`           | Return type of functions that produce no value.                                                                      |
| Choice                | `Choice`         | Named type; variant names and optional fields stored in the type checker tables. Declared with the `choice` keyword. |
| Interface             | `Interface`      | Named structural type; field types stored in tables.                                                                 |
| Namespace             | `Namespace`      | Internal tag used when a namespace name appears as a symbol.                                                         |
| Record                | `Record`         | Named type; field types stored in the type checker tables.                                                           |
| _(internal)_          | `StdlibAny`      | Internal marker for stdlib returns whose concrete type depends on arguments. Not user-accessible.                    |
| _(internal)_          | `Unknown`        | Error-recovery placeholder during type inference.                                                                    |

**Key Rules:**

- `integer` and `number` are distinct types. Mixed arithmetic (`integer op number`) implicitly promotes the `integer` operand to `number` and produces a `number` result. `integer` also widens to `number` in assignments.
- `none` is the empty case of `optional<T>`. It is assignable to `optional<T>` variables. Use `some(value)` to wrap a value. Assigning `none` to a concrete type such as `integer` is a compile-time error.
- Interface satisfaction is structural — a record satisfies an interface if it has all required fields with assignable types.
- Type aliases are fully transparent — the alias and its underlying type are interchangeable.

**Dependencies:** None.

### 4.7 Include Resolver

**Responsibility:** Process all `include` declarations after parsing, loading and merging included files into a single combined AST.

**Interface:**

- Constructor takes a `SourceManager&` reference (uses the default Lexer/Parser pipeline) or a `SourceManager&` plus a `ParseFileCallback` (enables custom parse implementations or test mocks).
- `resolve(program)` — Walk the AST, load all included files (parsing each via the configured callback), and merge their declarations and top-level statements into `program` in place. Returns `false` if any error diagnostics were emitted.
- `get_diagnostics()` — Returns all errors and warnings from the last `resolve()` call, including diagnostics propagated from included files.
- `k_max_include_depth` — Maximum nesting depth for includes (64).

**Behaviour:**

- Collects all `IncludeDeclaration` nodes from the parsed AST.
- For each include, resolves the path relative to the including file's directory (falls back to the working directory when no file path is available, e.g. from the REPL).
- Rejects symbolic links — emits a compile error with the message `include rejected: '<path>' contains a symbolic link in its path chain`.
- Checks the Source Manager's registry to skip already-loaded files (each file is included at most once, deduplicated by canonical path).
- Maintains an `InclusionStack` depth counter to enforce `k_max_include_depth` and guard against runaway nesting. Circular and self includes need no separate cycle check — the include-once registry above already prevents any file from being entered twice.
- Recursively resolves includes within included files. Uses an RAII `InclusionGuard` to keep the depth counter balanced on early returns.
- Mutates `program` in place: `IncludeDeclaration` nodes are replaced by the declarations from the included files; top-level statements from included files are appended to the program's statement list.

**Dependencies:** Source Manager. Lexer and Parser are injected via `ParseFileCallback` (defaulting to the standard implementation).

### 4.8 Type Checker

**Responsibility:** Walk the merged AST after include resolution and verify that every expression, statement, and declaration is type-correct.

**Implementation files:** The type checker is organised across several focused files: `type_checker.cpp` (core logic, scope management, entry point), `type_info.cpp` (TypeInfo methods — equality, formatting, factory functions), `type_scope.cpp` (TypeScope methods — scope lookup and management), `type_checker_decl.cpp` (declaration registration and checking), `type_checker_resolve.cpp` (type resolution and lookup), `expression_type_checker.cpp` and its sub-files `_access`, `_binary`, `_calls`, `_literals`, `_misc` (expression type inference), `statement_type_checker.cpp` and its sub-files `_control`, `_declarations`, `_misc` (statement checking and match exhaustiveness), `stdlib_type_handler.cpp` (stdlib return-type signatures, refinement, and synthetic type definitions), `generic_resolver.cpp` (generic type parameter binding and inference), and `symbol_exporter.cpp` (symbol table export from stdlib signatures and user definitions).

**Interface:**

- `check(program, require_main) → vector<Diagnostic>` — Analyse the full program and return a list of type errors as `Diagnostic` objects. An empty vector indicates the program is type-correct. The `require_main` flag (default `true`) controls whether the absence of `@main` is an error; set to `false` for test-mode files.

**Behaviour:**

_Name resolution:_

- Resolve every variable reference to its declaration.
- Resolve every function call to its function declaration or lambda.
- Resolve namespace-qualified names (`Geometry.distance`, `Geometry.Point`, `Geometry.Color.Red`).
- Apply `use` declarations to bring names into the current scope.
- Report `undefined variable` or `undefined function` if a name cannot be resolved.

_Type validation:_

- Verify that the right-hand side of every variable declaration matches the declared type.
- Verify that function arguments match parameter types (positional and named).
- Verify that return expressions match the declared return type.
- Verify that if-expressions produce the same type in both branches.
- Verify that match-expression arms all produce the same type.
- Verify that only mutable variables are reassigned, incremented, or decremented.
- Verify that array element types are homogeneous.
- Verify that dictionary keys are strings.
- Verify that tuple literals have 2–4 elements.
- Verify that arithmetic operators are applied only to numeric types.
- Verify that the `?` operator (postfix propagation) is applied only to `result<T>` or `optional<T>` operands; the result type is the inner type `T`.
- Verify that logical operators are applied only to booleans.
- Verify that try-statements are well-typed: the try body is checked; the catch binding (if present) is registered as an immutable `string` in a new scope (it holds the error message string) and the catch body is checked; the optional `finally` body is checked.

_Exhaustiveness checking:_

- Match on `boolean` must cover `true` and `false`.
- Match on a choice type must cover all variants. Data-variant arms bind field variables. For generic recursive choices, the type checker pushes the subject's concrete type parameters before resolving variant field types (e.g., matching on `List<integer>` binds fields as `integer` and `List<integer>`).
- Match on `result<T>` must cover `success` and `failure`.
- Match on other types (e.g., `integer`, `string`) is not required to be exhaustive, but must include an `else` arm to be valid. If comparison arms are used without an `else` fallback, the type checker reports an error.
- Missing arms produce a compile-time error.

_Linter warnings:_

- Always-false condition — `while false { ... }` loop body will never execute.
- Discarded result — a function returning `result<T>` is called and the return value is not used.
- Discarded value — a non-void function call expression whose return value is not assigned, piped, or otherwise consumed. Suppress with `_ = expr`.
- Downcast always fails — `downcast<T>` on a value whose type is incompatible with `T`.
- Floating-point equality — `==` or `!=` on `number` values may give unexpected results due to rounding.
- Function / namespace interpolation — interpolating a function or namespace in a string produces unhelpful output.
- Incompatible comparison — `==` or `!=` between unrelated types (e.g. `integer` and `string`); result is always `false` / `true`.
- Mutable but never mutated — a variable or parameter is declared `mutable` but is never reassigned.
- Not a Luma keyword — using `var` or `let` instead of `define`.
- Optional chain not unwrapped — `?.` or `?[` result assigned to a non-`optional<T>` variable without a `??` fallback.
- Redundant boolean — comparing a boolean with `true` or `false` (e.g. `x == true`, `x != false`) is redundant; use the boolean directly.
- Redundant downcast — `downcast<T>` on a value already known to be `T`.
- Self-assignment — assigning a variable to itself has no effect.
- Shadow variable — a local variable shadows a variable from an outer scope.
- Unconsumed unique — a `unique` variable leaves scope without being consumed.
- Unnecessary semicolon — Luma does not use semicolons.
- Unreachable code — code appears after a `return`, `break`, or `continue`.
- Unsafe trusted_downcast — `trusted_downcast` on an unrefined stdlib value has no compile-time safety guarantee.
- Unused function — a function is declared but never called.
- Unused parameter — a function parameter is never used.
- Unused variable — a variable is declared but never read.
- Void assignment — a variable is assigned the return value of a function that returns nothing.
- Type mismatch hints — when a type error occurs, suggest a conversion function or corrective action.

By default, linter warnings are informational. With `--strict` (or `-s`), all linter warnings become errors.

_Ownership tracking:_

- Variables declared with `unique` must be consumed exactly once (passed to a function or assigned away). Using a consumed unique variable is a type error.
- Variables declared with `borrow` are read-only references and cannot be consumed or moved.

_Annotation validation:_

- Exactly one `@main` function must exist across all included files when running in normal mode. In test mode (`luma --test`) the `@main` requirement is disabled — test files need not declare `@main`.
- `@main` and `@test` annotations must precede a function declaration.
- `@main` and `@test` functions must take no parameters.

_Interface satisfaction:_

- When a record value is passed where an interface type is expected, verify structural compatibility.

_Information hiding:_

- Track the set of `trusted` parameter names for the current function or lambda in `current_trusted_params_`.
- When a `private` record field is accessed, require that the base object is a variable whose name is in `current_trusted_params_`. Otherwise, report a type error.
- Construction of records always permits `private` fields to be supplied in the literal.

**Dependencies:** AST, Type System.

_Note:_ The Type Checker operates on the merged AST produced by the Include Resolver (Section 4.7). It does not load or parse files itself.

### 4.9 Runtime Value Types and Control Flow

**Responsibility:** Define the runtime value representation, control-flow signals, and arithmetic overflow utilities shared by the VM and standard library.

**Contents:**

- `value.hpp` / `value.cpp` — The `Value` variant type representing all Luma runtime values (integers, numbers, strings, arrays, dictionaries, records, choices, functions, tasks, channels, etc.). Used by the VM stack, the global environment, and native functions.
- `environment.hpp` — The `Environment` class that manages the global scope for standard library bindings and global variables (see §4.10).
- `control_flow.hpp` — The `ExitSignal` struct thrown by `Process.exit()` to unwind the VM and terminate with an exit code. Caught at the top level in `main()`.
- `overflow.hpp` — Arithmetic overflow detection utilities for safe integer operations.
- `lazy_hash_index.hpp` — A lazily-built hash index template (`LazyHashIndex<Key, Hash, Equal>`) used by `DictionaryValue` and `RecordValue` to provide O(1) key lookups while keeping entries in insertion order. Supports heterogeneous lookup (e.g. `std::string_view` keys without allocating `std::string`).
- `recursion_guard.hpp` — RAII guard (`RecursionGuard`) that tracks thread-local recursion depth for value operations (`to_string`, `equals`, `deep_copy`) to detect and prevent stack overflow from cyclic data structures.
- `value_hash.cpp` — Structural deep-hashing implementation (`ValueHash`) for all `Value` types. Uses cross-type int/double normalisation so that `int(42)` and `number(42.0)` hash identically when they compare equal. Dictionaries use order-independent hashing (XOR of entry hashes). Recursion is depth-limited (max depth 8) to prevent pathological cost on deeply nested structures. Constants are from Boost.Hash's `hash_combine` algorithm.
- `runtime_exceptions.hpp` — Typed exception hierarchy for VM runtime errors. Provides `VMError` (with subclasses `StackError` and `BytecodeError`), `ChannelClosedError`, `ChannelFullError`, `ChannelEmptyError`, and `TypeMismatchError`, all inheriting from `RuntimeError`.

**Dependencies:** Source Location (for error reporting on `Value` operations).

### 4.10 Environment

**Responsibility:** Manage the runtime scope chain — variable and function bindings organised in a hierarchy of scopes.

**Interface:**

- `create(parent) → EnvPtr` — Static factory. Create a new scope nested inside `parent` (or a root scope if `parent` is null).
- `define(name, value, is_mutable)` — Bind a name to a value in the current scope. Throws `RuntimeError` if the name is already defined in this scope.
- `get(name, loc) → Value` — Look up a name, walking from the current scope outward. Throws `RuntimeError` if the name is not found.
- `set(name, value, loc)` — Update an existing mutable binding, walking the scope chain. Throws `RuntimeError` if the binding is immutable or does not exist.

**Behaviour:**

- Each scope is a hash map (`string → Binding`) where `Binding` holds the value, its type, and a mutability flag.
- Lookup walks the scope chain from innermost to outermost.
- The global scope is pre-populated by the Standard Library module with built-in function bindings.

**Dependencies:** Errors (for error types), Interpreter/Value (for the `Value` variant).

### 4.11 Standard Library

**Responsibility:** Provide all built-in functions and constants organised into 39 namespaces defined by the language (String, Array, Dictionary, Math, Result, Converter, DateTime, Decimal, Console, FileSystem, RegularExpression, Process, Random, Encoder, Resource, Set, Channel, Task, Terminal, GraphicalUi, Socket, Optional, Reference, Queue, Stack, Log, Json, Csv, Xml, LinearAlgebra, Calculus, Hash, Compression, Http, KeyValueStore, HashSet, LinkedList, BinaryTree, Graph) plus the core built-ins (`print`, `assert`, `type_of`) — 40 registration units in total. Note: `success` and `failure` are language keywords parsed into dedicated AST nodes (`SuccessExpression`, `FailureExpression`), not runtime functions.

**Interface:**

- `register_all(environment)` — Populate the global environment with all built-in function bindings.
- Each namespace module exposes a function that registers its functions into the environment under the qualified namespace prefix.

**Behaviour:**

- Each built-in function is implemented as a `NativeFunction` — a callable value that receives a vector of runtime `Value` arguments and returns a `Value`.
- Native functions validate their arguments at runtime (argument count, argument types). On invalid input, they either return a `result<T>` containing `failure(...)` (for recoverable errors) or throw a C++ exception that the VM catches and converts into a `RuntimeError` with full source location (for unrecoverable errors such as wrong argument count or type).
- Larger modules are split into multiple files for maintainability. For example, the `GraphicalUi` module is conditionally compiled behind `LUMA_HAS_WEBVIEW` and renders via an embedded webview using HTML/CSS/JS. It is the most architecturally involved module because it spans a **C++/JavaScript boundary**: the C++ side builds widgets and drives the application loop, while a bundled JavaScript renderer turns them into DOM. Its source is decomposed by responsibility — widget builders (`graphicalui_widgets_{basic,layout,advanced,charts,interaction}.cpp`), command execution (`graphicalui_commands.cpp`), the event bridge (`graphicalui_events.cpp`), JSON serialisation (`graphicalui_serialization.cpp`), the headless test API (`graphicalui_testing.cpp`), and CSS validation (`graphicalui_css_properties.cpp` for the property catalog and typo suggestions, `graphicalui_css_sanitiser.cpp` for security sanitisation) — behind the umbrella `graphicalui_internal.hpp`. The module implements the Elm Architecture (Model–Update–View) loop with several subsystems:
    - **Rendering pipeline** — a `view(model)` function returns a widget, which is just a nested dictionary tree (`type` plus properties). The runtime serialises it to JSON, hands it to the embedded [lit-html](https://lit.dev) based renderer, which builds a template and **diffs it against the previous tree to patch only the DOM nodes that changed**. `view` is re-invoked after every `update`, so the UI is a pure function of the model.
    - **Event/callback bridge** — interactive widgets carry Luma callbacks. During serialisation each callback is registered on the `AppState` under an allocated numeric id (e.g. `_callback_id`), and only the id crosses into JSON. When the user interacts, the JavaScript renderer calls back through the `__gui_event` webview binding with `{type, id, value}`; the host looks up the callback by id, invokes it, and routes the result through `update` (a string return is an Elm message; any other value is the new model directly).
    - **Commands** — side effects (HTTP requests, clipboard writes, delays, random numbers, focus management, screen reader announcements) are represented as data. The `update` function returns a `(model, command)` pair via `with_command`; the runtime executes the command and delivers the result as a message.
    - **Subscriptions** — timer ticks, keyboard input, window resize, focus changes, and mouse events are managed declaratively. A `subscribe` function returns the active subscription array; the runtime diffs it against the previous array and sets up or tears down JavaScript listeners accordingly. Subscription callbacks are refreshed on every render cycle to capture the latest model state.
    - **Components** — `component(id, model_slice, render_fn)` provides identity-based caching for reusable widget subtrees. The runtime memoizes by `id` and only re-invokes `render_fn` when `model_slice` changes (compared by JSON serialisation).
    - **Routing** — `router` selects a child widget by route key; supports both callable and pre-built widget values, and parameterised routes with `{name}` placeholders. `navigate` and `navigate_back` commands update the current route using immutable model copies.
    - **Accessibility** — `accessible` wraps widgets with ARIA attributes; `focus` and `announce` commands manage keyboard focus and screen reader live regions.
    - **Keyed lists** — `keyed(key, child)` assigns stable DOM identities for efficient list diffing.
    - **Error boundaries** — `error_boundary(fallback_fn, view_fn)` catches rendering exceptions and renders a fallback widget instead of crashing the entire view.
    - **Embedded front-end assets** — the renderer, chart bridge, subscription manager, Pico CSS, lit-html, uPlot, and the Lucide icon set are compressed and baked into `graphicalui_assets.hpp` at build time by `scripts/generate_gui_assets.mjs` (a manual dev step), then decompressed via miniz at runtime — so a single binary ships the whole web front-end with no external files. `LUMA_GUI_DEV_ASSETS` reads the raw files from disk instead, allowing front-end iteration without a C++ rebuild.
    - **Theming** — a bundled Pico CSS base is bridged to semantic `--gui-*` design tokens (colour, spacing, type scale) in `gui-overrides.css`; dark mode follows `prefers-color-scheme` automatically, and developer-supplied CSS is validated and sanitised by the shared helpers in `graphicalui_css_sanitiser.cpp`.
    - **Headless execution & conditional compilation** — `LUMA_GUI_HEADLESS=1` runs the full lifecycle (init → view → subscribe, plus scripted `update` messages) with no window, and the `GraphicalUi.test_*` API drives interactions against a throwaway app for automated tests. When the platform has no webview support (`LUMA_HAS_WEBVIEW` undefined), a stub path registers every function to throw a descriptive "not available" error, while the pure constants and CSS-validation helpers still work.
- Namespace functions are registered with their qualified name (e.g., `String.length`, `Array.map`) so that they are accessible via qualified calls and via `use` imports.
- Pure namespace constants (e.g., `Math.pi`, `Math.e`, `Math.tau`, `Math.infinity`) are registered as immutable bindings in the global scope.

See Section 11 for detailed organisation.

**Dependencies:** Environment, Type System (for argument validation).

### 4.12 Error Reporter

**Responsibility:** Format and display all errors (lexer errors, parse errors, type errors, runtime errors) in a consistent, human-readable format.

**Interface:**

- `format(error) → string` — Format a single error into a human-readable string.
- `report(error)` — Format and print a single error to `stderr`.
- `report_all(errors)` — Format and print a list of errors to `stderr`.

**Behaviour:**

- Every error includes:
    - A category label: `CompileError`, `SyntaxError`, `TypeError`, `RuntimeError`.
    - The source location: file name, line number, column number.
    - A clear, specific message describing what went wrong.
    - The offending source line with a caret (`^`) pointing to the exact position.
- Diagnostic output is colourised when `stderr` is connected to an interactive terminal (ANSI escape codes). Category labels are bold red (errors) or bold yellow (warnings), source line gutters are cyan, carets are bold red, and hints are bold cyan. Colours are automatically suppressed when output is piped or redirected.
- Example output:

    ```text
    TypeError in main.luma:12:5
       |
    12 |     number x = "hello"
       |                ^
    type mismatch: expected 'number', got 'string'
    ```

- Errors are written to `stderr`. Normal output goes to `stdout`.

**Dependencies:** Source Manager (to retrieve source lines), Common (Terminal for colourised output).

### 4.13 REPL

**Responsibility:** Provide an interactive read-eval-print loop for exploratory programming.

**Interface:**

- `run()` — Start the REPL loop. Returns when the user exits.

**Behaviour:**

- Displays a prompt (`luma>`) and reads a line of input.
- The prompt and continuation prompt (`...`) are colourised when `stdout` is connected to an interactive terminal — the main prompt is bold, the continuation prompt is cyan. Expression results are prefixed with a cyan `=>`.
- Runs Lexer → Parser → Type Checker → Compiler → VM on each input.
- Maintains a persistent environment across lines — variables and functions defined in one line are available in subsequent lines.
- Prints the result of expressions automatically (prefixed with `=>`).
- Handles REPL commands: `:quit` / `:q`, `:help` / `:h`, `:clear` / `:c`, `:file <path>` / `:f <path>`.
- The `:file` command loads a `.luma` file through the full pipeline (Lexer → Parser → Include Resolver → Type Checker → Linter → Compiler → VM), registers its declarations in the REPL environment, and executes its top-level statements. All definitions become available for subsequent interactive use.
- On error, displays the error message and returns to the prompt without crashing.
- Does not require a `@main` function — each line is evaluated directly.

**Dependencies:** Source Manager, Lexer, Parser, Include Resolver, Type Checker, Compiler, VM, Environment, Error Reporter, Common (Terminal).

### 4.14 Test Runner

**Responsibility:** Discover and execute all `@test`-annotated functions, report results, and set the process exit code.

**Interface:**

- `run_tests(program, interpreter, global_env) → vector<TestResult>` — Execute all test functions and return a list of individual results.
- `print_test_results(results) → TestCounts` — Print one line per test result and a summary line. Returns pass and fail counts.

**Behaviour:**

- Scans the AST for all functions annotated with `@test`.
- Executes each test function in a child environment of the global environment.
- Catches assertion failures and runtime errors within each test.
- Records each test as `PASS` (completed without error) or `failure` (assertion failed or runtime error occurred).
- `print_test_results` prints all results after execution completes:

    ```text
    [PASS] test_addition
    [FAIL] test_division — assertion failed: expected 4 but got 5
    ```

    `[PASS]` is coloured green and `[FAIL]` is coloured red when `stdout` is connected to an interactive terminal. The summary line is also colourised.
- Prints a summary line: `N passed, M failed.`
- The caller uses the returned `TestCounts` to set a non-zero process exit code if any test failed (exit code 1 — `runtime_error`, suitable for CI integration).

**Dependencies:** Interpreter, Environment, Error Reporter, Common (Terminal).

### 4.15 Concurrency Module

**Responsibility:** Provide the runtime infrastructure for tasks (`spawn`/`await`), structured concurrency (`task_scope`), cancellation tokens, and channels.

See Section 12 for detailed design.

**Dependencies:** Environment, Type System.

### 4.16 Common

**Responsibility:** Provide shared, cross-cutting utilities used by multiple modules.

**Interface:**

- `ResourceLimits` — A struct of mutable `static inline` values defining upper bounds for resource consumption (call depth, parser expression depth, string interpolation nesting depth, collection sizes, string size, string repeat count and pad width, regex pattern size, process output size, environment variable name and value size, task queue size, open socket count). Defaults can be overridden at startup from `LUMA_LIMIT_*` environment variables via `ResourceLimits::init_from_env()`. Fixed bounds that are baked into data-structure sizing or compiled bytecode (e.g. VM value-stack depth, display nesting depth) live in the companion `CompileTimeLimits` namespace as `constexpr` constants.
- TTY detection (`stderr_is_tty()`, `stdout_is_tty()`), ANSI colour helpers, and `enable_ansi_escapes()` for Windows virtual terminal processing live in `core/runtime/cli/terminal.hpp`.
- Escape utilities (`escape.hpp`) — Policy-based string escape/unescape functions for JSON, JavaScript, HTML, and XML contexts, used by the JSON, XML, and HTTP stdlib modules to prevent injection attacks during serialization.

**Behaviour:**

- Centralises all resource-limit constants in one location so that modules do not scatter magic numbers.
- Limits are intentionally generous for normal programs but prevent pathological inputs from exhausting system resources (denial-of-service prevention).
- Terminal colour functions cache the TTY check result on first call. When `stderr`/`stdout` is not a terminal, colour functions return empty strings so output remains clean in pipes and redirects.

**Dependencies:** None (header-only).

### 4.17 Compiler

**Responsibility:** Compile a type-checked AST into a bytecode stream (`Chunk`) that the VM executes.

**Interface:**

- `Compiler::compile(program) → CompileResult` — Produces a list of `CompiledFunction` values, a top-level `Chunk`, and any diagnostics. On success, consumers extract a `CompileArtifact` (bytecode only, no diagnostics) for the pipeline artifact and compilation cache.
- `CompiledFunction` — A named function with its `Chunk`, arity, upvalue descriptors, and `is_main`/`is_test` flags.
- `Chunk` — A bytecode container holding the instruction stream (`vector<uint8_t>`), a constant pool (`vector<Value>`), a source-location table (one entry per instruction), and an interned name table for globals and fields.

**Behaviour:**

- Walks every AST node type and emits one or more opcodes per node.
- Binary operator compilation uses a constexpr sparse array (`k_binary_op_lookup`) indexed by `TokenType` for O(1) opcode lookup, replacing a switch-case dispatch. The table is built at compile time via an immediately-invoked lambda.
- Uses precedence-aware expression compilation with stack-effect tracking.
- Emits forward jumps for control flow (`if`, `match`, `for`, `while`) and patches them after the branch body is compiled.
- Resolves upvalues using a CLox-style capture chain: each `CompilerScope` (one per function or lambda) tracks which enclosing locals are captured.
- Supports 105 opcodes across 25 categories (see Section 5.5).
- Operands use big-endian `u16` encoding for jump offsets and constant-pool indices.

**Dependencies:** AST, Source Location, Interpreter Value, Diagnostics.

The `compilation_pipeline.hpp` header (with definitions in `compilation_pipeline.cpp`) provides `compile_program()`, a convenience function that orchestrates the full analysis-to-bytecode pipeline (parse → type-check → lint → compile → optional optimize/verify). Its `CompilerProfile` struct configures which passes run and how they behave (including a `compile` flag that stops after type-check/lint for the check-only `luma check` path). The pipeline returns a `CompilationOutcome` containing the `PipelineResult`, the compiled `CompileArtifact` (present on success unless compilation is disabled), an exit code, and a success flag.

### 4.18 Virtual Machine (VM)

**Responsibility:** Execute compiled bytecode using a stack-based dispatch loop, with support for closures, exception handling, and structured concurrency.

**Interface:**

- `VM(env, source_manager)` — Constructs a VM with a global environment and source manager.
- `execute(functions, top_level)` — Run a compiled program (top-level chunk + functions).
- `execute_function(func)` — Execute a single function (used by the REPL).
- `execute_tests(functions)` — Discover and run `@test`-annotated functions.

**Behaviour:**

- Maintains a value stack (max 65,536 entries) and a call-frame stack (max 256 frames).
- The dispatch loop (`run_dispatch`) fetches, decodes, and executes one opcode per iteration.
- Function calls push a new `CallFrame` with an instruction pointer (`ip`) into the callee's `Chunk` and a `slots` pointer into the value stack.
- Closures capture upvalues as `shared_ptr<Value>` references, enabling upvalue sharing between closures.
- Exception handling uses a separate handler stack managed by `VMExceptionManager`; each handler records the catch IP, frame index, and stack depth for unwinding.
- Structured concurrency: `Spawn` creates a lightweight per-thread VM that shares the parent's thread pool. `TaskScopeBegin`/`TaskScopeEnd` manage structured task scopes. `Await` blocks until a task completes and pushes its result.
- Native stdlib functions call back into user-defined compiled functions via a `vm_call_fn_` callback, enabling higher-order functions like `Array.map`.

**Dependencies:** Compiler (Chunk, Opcode), Concurrency (Thread Pool, Task Scope, Cancellation Token), Diagnostics, Interpreter Value, Standard Library (Native Function).

### 4.19 Diagnostics

**Responsibility:** Provide structured diagnostic messages (errors, warnings, hints) with multi-span source locations, suggested fixes, and terminal rendering.

**Interface:**

- `Diagnostic` — A complete diagnostic: severity, category, message, source spans, optional hint, and suggested fixes.
- `DiagnosticBuilder` — Fluent builder: `diag::error("msg").primary(loc).hint("try X").build()`.
- `DiagnosticRenderer` — Formats diagnostics to the terminal with source context and carets.
- `DiagnosticEmitter` — Base class for diagnostic-emitting compiler passes. Each phase (Lexer, Parser, TypeChecker, Linter, NameResolver) inherits from `DiagnosticEmitter` and receives a default category and source tag, providing uniform `emit_error()` and `emit_warning()` methods.

**Behaviour:**

- Supports four severity levels: `Error`, `Warning`, `Hint`, `Info`.
- Primary spans display carets (`^`), secondary spans display dashes, following the Rust `rustc` diagnostic style.
- `Fix` values carry suggested text replacements, enabling future auto-fix tooling.

**Dependencies:** Source Manager, Source Location.

### 4.20 Linter

**Responsibility:** Post-type-check code quality analysis that produces warnings (never errors) for common issues.

**Interface:**

- `Linter::lint(program) → vector<Diagnostic>` — Analyse the AST and return warnings.

**Checks:**

1. Unused variables, parameters, and functions
2. Mutable variables never mutated
3. Self-assignment (`x = x`)
4. Unreachable code after return/break/continue
5. Discarded result values
6. Always-false/always-true conditions
7. Incompatible comparisons
8. Floating-point equality
9. Empty blocks
10. Redundant else after return

**Dependencies:** AST, Diagnostics.

### 4.21 Pipeline

**Responsibility:** Composable, sequential pass pipeline that orchestrates compilation phases and collects diagnostics.

**Interface:**

- `Pass` (abstract) — Virtual interface with `name()`, `run(Program&, PipelineResult&)`, and `required_passes()` (returns dependency names using `pass_name::` constants).
- `Pipeline` — Owns a sequence of passes. `.add(pass)` chains them; `.run(program)` executes them sequentially, stopping at the first error-producing pass. Records per-pass timing in `PipelineResult::timings`.

**Behaviour:**

- Each pass can mutate the `Program` AST and append diagnostics via `merge_diagnostics()`.
- `PipelineResult` derives success from `has_errors()` (no separate success flag), and tracks error count, warning count, and per-pass timing.

**Dependencies:** AST (forward-declared), Diagnostics.

### 4.22 Name Resolver

**Responsibility:** Resolve all variable references to stack-slot indices for O(1) access in the bytecode compiler.

**Interface:**

- `NameResolver::resolve(program) → vector<Diagnostic>` — Walk the AST, resolve names, and annotate variables with slot information.

**Behaviour:**

- Produces `ResolvedVar` values with `frame_depth` (0 = local, 1+ = enclosing), `slot_index`, and `is_mutable`.
- Pushes and pops `ResolveScope` objects that track local definitions.
- Detects undeclared variables and emits diagnostics.

**Dependencies:** AST (Expression, Statement), Diagnostics, Source Location.

### 4.23 Optimizer

**Responsibility:** Apply peephole optimizations and constant folding to compiled bytecode, reducing instruction count without changing semantics.

**Interface:**

- `Optimizer(level)` — Construct with optimization level: 0 (pass-through), 1 (peephole only), 2 (peephole + constant folding + dead code elimination).
- `optimize(chunk) → size_t` — Optimize a `Chunk` in-place. Returns the number of bytes eliminated.

**Behaviour:**

- Peephole pass: replaces known sequences (e.g., `Push(0) + Add` → no-op, `Push(1) + Add` → `Increment`).
- Constant folding pass: folds `Constant(a) + Constant(b) + ArithOp` into a single `Constant(result)`.
- Unary folding pass: folds `Constant(a) + UnaryOp` into `Constant(result)`.
- Integrated into the pipeline via `OptimizerPass`.

**Dependencies:** Chunk, Opcode.

### 4.24 Bytecode Verifier

**Responsibility:** Validate structural integrity of compiled bytecode before execution, catching compiler bugs and corrupt bytecode early.

**Interface:**

- `BytecodeVerifier::verify(func) → vector<VerifyError>` — Verify a compiled function's chunk. Returns an empty vector on success.

**Behaviour:**

- Checks that all opcodes are valid and operands are in-bounds.
- Validates that jump targets land on valid instruction boundaries.
- Checks constant-pool and name-table indices.
- Simulates stack depth to detect underflows and overflows.
- Integrated into the pipeline via `VerifierPass`.

**Dependencies:** Chunk, CompiledFunction.

### 4.25 Compilation Cache

**Responsibility:** Avoid re-compiling unchanged source files by caching compilation results in memory.

**Interface:**

- `CompilationCache(max_entries)` — Construct with an LRU capacity (default 128).
- `get(path, content, options) → optional<CompileArtifact>` — Look up a cached artifact keyed by path, content hash, and options.
- `put(path, content, options, artifact)` — Store a compiled artifact.

**Behaviour:**

- Keyed by (absolute path, compilation options, content hash). When content or options change, the cached entry is invalidated.
- Uses LRU eviction when the cache exceeds `max_entries`.
- Thread-safe (guarded by mutex) for use by the language server's analysis thread.
- Complements the on-disk `.lumc` cache (§4.26), which persists compiled bytecode across process restarts.

**Dependencies:** Compiler (CompileArtifact).

### 4.26 Bytecode Serializer (On-Disk `.lumc` Cache)

**Responsibility:** Persist compiled bytecode to disk so that unchanged source files skip the lex → parse → type-check → compile pipeline on subsequent runs, giving near-instant startup.

**Interface:** (`BytecodeSerializer`, static methods)

- `serialize(top_level, functions, source_hash, timestamp) → vector<uint8_t>` — Encode compiled functions to a byte buffer.
- `deserialize(bytes) → DeserializeResult` — Decode a buffer, returning a typed error code on failure.
- `write_file(path, top_level, functions, source_hash, timestamp) → bool` — Serialize and write a `.lumc` file.
- `read_file(path, expected_source_hash) → DeserializeResult` — Read, validate, and decode a `.lumc` file.
- `hash_source(source) → uint64` — FNV-1a 64-bit content hash used for invalidation.
- `cache_path_for(source) → path` — The cache path for a source file; replaces the source extension with `.lumc` (e.g. `app.luma` → `app.lumc`).

**Behaviour:**

- After a successful compile, `run_file` (in `cli_runner.cpp`) writes the bytecode to a `<source>.lumc` file **next to the source**. On the next run it first checks the in-memory `CompilationCache` (§4.25), then attempts `read_file`; a valid hit executes the deserialized bytecode without recompiling.
- **Invalidation:** the disk cache key mixes the FNV-1a source hash with the optimization level, so editing the source or changing `-O` produces a miss. `read_file` rejects a stale or damaged file — recompiling from source — on bad magic, format-version mismatch, source-hash mismatch, or corruption (see `DeserializeError`).
- `.lumc` files are a **transparent cache, not a distribution format**: bumping `k_bytecode_format_version` simply rejects old files (`DeserializeError::VersionMismatch`) rather than migrating them. They are safe to delete and are git-ignored.
- Only normal program runs produce `.lumc` files; `--check` and `--test` compile in memory and write nothing. A failed write (e.g. a read-only directory) emits a warning to stderr and execution continues.

**File format:** A 24-byte header (`"LUMC"` magic, u32 format version, u32 flags, u64 FNV-1a source hash, u64 timestamp) followed by a function table (count + per-function metadata, bytecode chunk, constant pool, source map, and name table).

**Dependencies:** Chunk, CompiledFunction, `common/hash.hpp` (FNV-1a).

### 4.27 Debugger (DAP)

**Responsibility:** Provide interactive debugging of Luma programs via the Debug Adapter Protocol (DAP).

**Interface:**

- The `luma_dap` binary communicates over standard input/output using Content-Length framed JSON messages.
- Supports `initialize`, `launch`, `setBreakpoints`, `configurationDone`, `threads`, `stackTrace`, `scopes`, `variables`, `continue`, `next`, `stepIn`, `stepOut`, `pause`, `evaluate`, `disconnect`, and `terminate` requests.

**Behaviour:**

- Embeds the full Luma compilation pipeline (Lexer → Parser → Include Resolver → Type Checker → Compiler) and VM. (The debugger does not run the Linter pass.)
- Runs two threads: a protocol I/O thread for reading/writing DAP messages, and a VM execution thread that runs the target program with debug hooks.
- The VM pauses at breakpoints and step targets by calling a pause callback that blocks the execution thread until the DAP client sends a resume command.
- Breakpoints are resolved by matching source file paths to compiler file IDs. Pending breakpoints set before launch are applied once the program is compiled.
- Stack traces, scopes, and variables are read directly from the VM's call frame stack.
- Program output (`print`) is redirected to DAP `output` events via a `streambuf` redirect.

**Dependencies:** Compiler, VM, Source Manager, Lexer, Parser, Include Resolver, Type Checker, JSON (reuses `shared/json/json.hpp`).

---

## 5 — Data Structures

This section defines the core data structures that flow between modules.

### 5.1 SourceLocation

Attached to every token and every AST node. Carries the information needed to produce accurate error messages.

```text
SourceLocation {
    file_id : integer # identifies the source file in the Source Manager
    line    : integer # 1-based line number
    column  : integer # 1-based column number
}
```

### 5.2 Token

Produced by the Lexer, consumed by the Parser.

```text
Token {
    type     : TokenType       # keyword, operator, literal, identifier, etc.
    lexeme   : string          # the raw text of the token
    literal  : optional<Value> # pre-computed value for literals (integer, number, string, boolean)
    location : SourceLocation
}
```

**Token types** (grouped by category):

| Category      | Token Types                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Annotations   | `Annotation` (carries the annotation name, e.g., `main`, `test`)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| Identifiers   | `Identifier`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| Interpolation | `InterpolationStart` (`${`), `InterpolationEnd` (`}` closing an interpolation)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| Keywords      | `Await`, `Borrow`, `Break`, `Case`, `Catch`, `Choice`, `Continue`, `Downcast`, `Else`, `Failure`, `Finally`, `For`, `Function`, `If`, `In`, `Include`, `Interface`, `Internal`, `Is`, `Match`, `Mutable`, `Namespace`, `Record`, `Return`, `Some`, `Spawn`, `Success`, `TaskScope`, `TrustedDowncast`, `Try`, `Type`, `Unique`, `Use`, `While`, `With`                                                                                                                                                                                                                                                                       |
| Literals      | `BooleanLiteral`, `IntegerLiteral`, `NoneLiteral`, `NumberLiteral`, `StringEnd`, `StringLiteral`, `StringMiddle`, `StringStart`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| Operators     | `Ampersand`, `AmpersandAmpersand`, `AmpersandEquals`, `Arrow`, `Bang`, `BangEquals`, `BangGreater`, `Caret`, `CaretEquals`, `DotDot`, `DotDotEquals`, `Equals`, `EqualsEquals`, `Greater`, `GreaterEquals`, `GreaterGreater`, `GreaterGreaterEquals`, `Less`, `LessEquals`, `LessLess`, `LessLessEquals`, `Minus`, `MinusEquals`, `MinusMinus`, `Percent`, `PercentEquals`, `Pipe`, `PipeEquals`, `PipeGreater`, `PipePipe`, `Plus`, `PlusEquals`, `PlusPlus`, `QuestionBracket`, `QuestionDot`, `QuestionMark`, `QuestionQuestion`, `Slash`, `SlashEquals`, `SlashSlash`, `SlashSlashEquals`, `Star`, `StarEquals`, `Tilde` |
| Punctuation   | `Colon`, `ColonColon`, `Comma`, `Dot`, `LeftBrace`, `LeftBracket`, `LeftParen`, `RightBrace`, `RightBracket`, `RightParen`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| Special       | `EndOfFile`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| Type keywords | `ArrayType`, `BinaryTreeType`, `BooleanType`, `ChannelType`, `DictionaryType`, `HashSetType`, `IntegerType`, `KeyValueStoreType`, `LinkedListType`, `NumberType`, `OptionalType`, `QueueType`, `ReferenceType`, `ResultType`, `SetType`, `SocketType`, `StackType`, `StringType`, `TaskType`, `WidgetType`, `XmlType`                                                                                                                                                                                                                                                                                                        |

### 5.3 AST Nodes

Defined in Section 4.5. All nodes are allocated using `std::unique_ptr` to express single ownership. Child nodes are owned by their parent.

### 5.4 Runtime Value

The central runtime data type. Implemented as a `std::variant` in C++.

```text
Value = variant {
    NoneValue           : (no data)
    bool                : boolean primitive
    double              : number primitive
    int64_t             : integer primitive
    string              : string (stored inline, not heap-allocated)
    ArrayValue          : shared_ptr<Array>          # Array = { vector<Value> }
    BinaryTreeValue     : shared_ptr<BinaryTree>     # BinaryTree = { BST nodes }
    ChannelValue        : shared_ptr<Channel>        # Channel = { thread-safe queue }
    ChoiceValue         : shared_ptr<Choice>         # Choice = { type_name, variant_name, fields }
    DictionaryValue     : shared_ptr<Dictionary>     # Dictionary = { vector<pair<string, Value>>, hash index }
    FunctionValue       : shared_ptr<Function>       # Function = { name, parameters, body, closure }
    GraphValue          : shared_ptr<Graph>          # Graph = { adjacency list }
    HashSetValue        : shared_ptr<HashSet>        # HashSet = { hash-based set }
    KeyValueStoreValue  : shared_ptr<KeyValueStore>  # KeyValueStore = { path, entries }
    LinkedListValue     : shared_ptr<LinkedList>     # LinkedList = { doubly-linked nodes }
    NativeFunctionValue : shared_ptr<NativeFunction> # NativeFunction = { name, callable }
    QueueValue          : shared_ptr<Queue>          # Queue = { deque<Value> }
    RangeValue          : shared_ptr<Range>          # Range = { start, end, inclusive }
    RecordValue         : shared_ptr<Record>         # Record = { type_name, vector<pair<string, Value>> }
    ReferenceValue      : shared_ptr<Reference>      # Reference = { shared mutable cell }
    ResultValue         : shared_ptr<Result>         # Result = { is_ok, shared_ptr<Value> inner }
    SetValue            : shared_ptr<Set>            # Set = { vector<Value> }
    SocketValue         : shared_ptr<Socket>         # Socket = { OS socket handle }
    StackValue          : shared_ptr<Stack>          # Stack = { vector<Value> }
    TaskValue           : shared_ptr<Task>           # Task = { future<Value> }
    TupleValue          : shared_ptr<Tuple>          # Tuple = { vector<Value> }
    XmlValue            : shared_ptr<Xml>            # Xml = { DOM tree }
}
```

**Ownership rules:**

- Primitive values (`none`, `boolean`, `integer`, `number`) are stored inline — no heap allocation.
- Strings are stored inline as `std::string` — no heap indirection beyond the string's own internal buffer.
- Compound values (`array`, `binary_tree`, `channel`, `choice`, `dictionary`, `function`, `graph`, `hash_set`, `key_value_store`, `linked_list`, `queue`, `range`, `record`, `reference`, `result`, `set`, `socket`, `stack`, `task`, `tuple`, `xml`) use `std::shared_ptr` for shared ownership within the interpreter (e.g., closures capturing outer variables, channels shared between tasks).

### 5.5 Bytecode Data Structures

Used by the bytecode compiler and VM.

```text
Chunk {
    code      : vector<uint8_t>       # bytecode instruction stream
    constants : vector<Value>         # constant pool (literals, function refs)
    locations : vector<SourceLocation> # one per instruction for error reporting
    names     : vector<string>        # interned name table (globals, fields)
}

CompiledFunction {
    name      : string
    chunk     : Chunk                 # the function's bytecode
    arity     : integer               # number of parameters
    upvalues  : vector<Upvalue>       # captured variables from enclosing scopes
    is_main   : bool
    is_test   : bool
}

Upvalue {
    index    : uint8_t                # slot index in the enclosing scope
    is_local : bool                   # true if captured from the immediately enclosing scope
}

CallFrame {
    function    : CompiledFunction*   # the function being executed
    ip          : const uint8_t*      # instruction pointer into the chunk's code
    slots       : Value*              # pointer into the VM's value stack
    slot_offset : size_t              # absolute position in the value stack
    closure     : shared_ptr<FunctionValue> # the FunctionValue with captured upvalues
}
```

**Opcode categories (105 opcodes):**

| Category        | Count | Opcodes                                                                                                   |
| --------------- | ----- | --------------------------------------------------------------------------------------------------------- |
| Stack           | 6     | `Constant`, `ConstantLong`, `Pop`, `Dup`, `Dup2`, `Swap`                                                  |
| Variables       | 6     | `GetLocal`, `SetLocal`, `GetUpvalue`, `SetUpvalue`, `GetGlobal`, `SetGlobal`                              |
| Literals        | 5     | `None`, `True`, `False`, `Zero`, `One`                                                                    |
| Arithmetic      | 9     | `Add`, `Subtract`, `Multiply`, `Divide`, `IntDivide`, `Modulo`, `Negate`, `Increment`, `Decrement`        |
| Comparison      | 6     | `Equal`, `NotEqual`, `Less`, `LessEqual`, `Greater`, `GreaterEqual`                                       |
| Logical         | 3     | `Not`, `And`, `Or`                                                                                        |
| Bitwise         | 6     | `BitwiseAnd`, `BitwiseOr`, `BitwiseXor`, `BitwiseNot`, `ShiftLeft`, `ShiftRight`                          |
| Strings         | 2     | `Concatenate`, `Interpolate`                                                                              |
| Collections     | 8     | `MakeArray`, `MakeDict`, `MakeTuple`, `MakeRange`, `MakeRangeInc`, `IndexGet`, `IndexSet`, `IndexGetOpt`  |
| Records         | 5     | `MakeRecord`, `GetField`, `SetField`, `GetFieldOpt`, `RecordWith`                                         |
| Choice          | 2     | `MakeChoice`, `MakeChoiceConstructor`                                                                     |
| Result/Optional | 8     | `MakeSuccess`, `MakeFailure`, `MakeSome`, `Unwrap`, `ResultInner`, `IsSuccess`, `IsSome`, `EnsureSuccess` |
| Downcast        | 3     | `Downcast`, `TrustedDowncast`, `IsType`                                                                   |
| Control flow    | 5     | `Jump`, `JumpIfFalse`, `JumpIfTrue`, `Loop`, `NullCoalesce`                                               |
| Functions       | 5     | `Call`, `CallNamed`, `TailCall`, `Return`, `MakeClosure`                                                  |
| Pipe            | 2     | `Pipe`, `ErrorPipe`                                                                                       |
| Exception       | 3     | `TryCatch`, `TryEnd`, `Rethrow`                                                                           |
| Match           | 3     | `MatchStart`, `MatchArm`, `MatchEnd` (defined but unused — match compiles to jumps)                       |
| Containment     | 1     | `Contains`                                                                                                |
| Concurrency     | 4     | `Spawn`, `Await`, `TaskScopeBegin`, `TaskScopeEnd`                                                        |
| Iteration       | 3     | `ForIterInit`, `ForIterStep`, `ForIterStepKV`                                                             |
| Misc            | 3     | `Print`, `Assert`, `TypeOf`                                                                               |
| Fused           | 4     | `IncrementLocal`, `DecrementLocal`, `SetLocalPop`, `GetLocalReturn`                                       |
| Conversions     | 2     | `IntToNumber`, `Clone`                                                                                    |
| Module          | 1     | `EndModule`                                                                                               |

> **Note:** `Break` and `Continue` are not opcodes. Break compiles to scope-cleanup `Pop`s followed by a forward `Jump` (patched at loop exit). Continue compiles to scope-cleanup `Pop`s followed by a backward `Loop` instruction.

### 5.6 Binding

Stored in each Environment scope, wrapping a value with additional metadata.

```text
Binding {
    value          : Value
    type           : string # type name, derived from the value at definition time
    is_mutable     : bool
    from_namespace : bool   # true when registered as a bare namespace alias via 'use'
}
```

---

## 6 — Processing Pipeline

This section traces the complete path of a Luma program from source text to execution, illustrating how each module contributes.

### 6.1 Phase 1 — Source Loading

The CLI validates the path and then the Source Manager reads the file from disk:

1. Validate that the file path has the `.luma` extension (performed by the CLI before invoking the Source Manager; throws `SyntaxError` on mismatch).
2. Read the file contents as a byte stream into a string. Strip the UTF-8 BOM if present. No further UTF-8 validation is performed.
3. Assign a unique `FileId` identifier (starting at 1; 0 is reserved for REPL input).
4. Split the source text into lines for error context display.
5. Return a `SourceFile` reference containing the path, text, lines, and file identifier.

### 6.2 Phase 2 — Lexical Analysis

The Lexer scans the source text and produces a token stream:

1. Initialise the cursor at position (line 1, column 1).
2. Skip whitespace and comments.
3. Match the longest valid token at the current position.
4. For string literals:
    - If the string contains `${`, produce `StringStart`, then `InterpolationStart`, tokenize the embedded expression normally, produce `InterpolationEnd`, continue with `StringMiddle` or `StringEnd`.
    - If no interpolation, produce a single `StringLiteral`.
5. For triple-quoted strings (`"""`), consume until the closing `"""`, compute the common leading whitespace, strip it, and produce the dedented content using the same interpolation token sequence.
6. Attach a `SourceLocation` to each token.
7. Append an `EndOfFile` token.
8. Return the completed token stream.

**Error recovery:** On an unrecognised character, throw a `SyntaxError` with the character and its location. Scanning stops at the first unrecognised character.

### 6.3 Phase 3 — Parsing

The Parser transforms the token stream into an AST:

1. Consume tokens from left to right using a recursive descent parser.
2. Top level: parse declarations (`function`, `record`, `choice`, `interface`, `namespace`, `type`, `include`, `use`) and statements.
3. Within function bodies: parse statements and expressions.
4. Expression parsing uses precedence climbing to correctly handle operator precedence and associativity.

**Operator precedence** (lowest to highest):

| Level        | Operators                                      | Associativity  |
| ------------ | ---------------------------------------------- | -------------- |
| 1 (lowest)   | `\                                             | >` `!>`        |
| 2            | `??`                                           | Left           |
| 3            | `\                                             | \              |
| 4            | `&&`                                           | Left           |
| 5            | `==` `!=`                                      | Left           |
| 6            | `<` `>` `<=` `>=` `in`                         | Left           |
| 7            | `\                                             | `              |
| 8            | `^`                                            | Left           |
| 9            | `&`                                            | Left           |
| 10           | `<<` `>>`                                      | Left           |
| 11           | `+` `-`                                        | Left           |
| 12           | `*` `/` `//` `%`                               | Left           |
| 13           | `-` `!` `~` (unary prefix)                     | Right (prefix) |
| 14 (highest) | `()` `[]` `.` `?.` `?[` `..` `..=` postfix `?` | Left           |

1. The pipe operator (`|>`) is parsed as a left-associative binary operator that rewrites into a function call (the left-hand value becomes the first argument).
2. Named arguments (`name: expr`) are parsed as part of call expressions and stored alongside positional arguments.
3. Each AST node receives the `SourceLocation` of its primary token.

**Error recovery:** On a parse error, throw a `SyntaxError` with the location and abort parsing. The error surfaces to the caller immediately.

### 6.4 Phase 4 — Include Resolution

After parsing the main file, includes are resolved before type checking. This keeps include handling separate from type analysis (Separation of Concerns):

1. Walk the AST and collect all `IncludeDeclaration` nodes.
2. For each included path:
   a. Resolve the path relative to the directory of the including file.
   b. Check the Source Manager's registry — if already loaded, skip (each file is included at most once).
   c. Check the inclusion stack for cycles. If the file is already on the stack, report a circular inclusion error and stop.
   d. Load, lex, and parse the included file (phases 1–3).
   e. Recursively resolve any includes within the included file.
   f. Merge the included file's declarations into the main program's declaration list.
3. The result is a single merged AST containing declarations from all files.

### 6.5 Phase 5 — Type Checking

The Type Checker walks the merged AST and validates all type constraints:

1. Build a scope stack mirroring the lexical structure.
2. Register all top-level declarations (functions, records, choices, interfaces, type aliases, namespaces) so that forward references are valid. During namespace registration, any member declared with the `internal` keyword is recorded in the `internal_members_` set using its fully qualified name (e.g., `"Geometry.helper"`).
3. Resolve `use` declarations by importing names into the current scope, skipping any names in `internal_members_`.
4. Walk each function body, validating every expression and statement:
    - Infer the type of each expression bottom-up.
    - Compare inferred types against declared types.
    - When resolving a namespace-qualified access (`Namespace.member`), check whether the qualified name is in `internal_members_`. If so, and if the current namespace context (`current_namespace_`) does not match the target namespace, emit a type error.
    - Record the resolved type on each AST node (producing the Typed AST).
5. Check exhaustiveness of `match` expressions on booleans, choice types, and results.
6. Verify `@main` annotation uniqueness and that annotated functions have no parameters.
7. Collect all type errors and return them together — do not stop at the first error.

### 6.6 Phase 6 — Compilation and Execution

The Compiler translates the type-checked AST into bytecode and the VM executes it:

1. The Compiler walks the AST and emits bytecode into `Chunk` containers — one per function, lambda, and the top-level script.
2. Upvalue resolution links closures to captured variables using a CLox-style capture chain.
3. Forward jumps for control flow (`if`, `match`, `for`, `while`) are emitted and patched after the branch body is compiled.
4. The VM creates the global environment and registers the standard library.
5. The VM loads the compiled functions and begins executing the top-level chunk.
6. After the top-level chunk finishes, the VM locates the function marked `@main` (its `is_main` flag) and invokes it directly through its internal call machinery — the same path used by the `Call` opcode, not a dedicated opcode. The VM dispatch loop fetches, decodes, and executes one opcode per iteration.
7. On runtime error, the VM formats the error with source location (from the chunk's source map) and terminates with the category-specific exit code (1 for runtime, 2 for type, 3 for syntax, 4 for compile).
8. On `Process.exit(code)`, an `ExitSignal` propagates up to `main()`, terminating with the specified exit code.
9. On successful completion, exit with code 0.

---

## 7 — Bytecode Compiler and Virtual Machine Internals

This section provides a detailed technical reference for the bytecode compiler and virtual machine — the two components that transform a type-checked AST into running code.

### 7.1 Compilation Model

The compiler translates a type-checked AST into a flat bytecode stream that a stack-based VM can execute. The design follows the CLox tradition (Crafting Interpreters, Robert Nystrom): one-pass AST walk, scope-local variable resolution by stack slot, forward-jump patching, and upvalue capture chains for closures.

**Key design choices:**

- **One compilation unit per function.** Each function, lambda, and the top-level script produces its own `CompiledFunction` containing a `Chunk` (bytecode + constant pool + source map + name table).
- **Two-pass top-level compilation.** Pass 1 compiles all declarations (functions, records, choices, interfaces, namespaces) to register them as globals. Pass 2 compiles top-level statements. This allows forward references between top-level functions.
- **No intermediate representation.** The compiler emits bytecode directly from the AST — there is no SSA, control-flow graph, or optimisation pass.

### 7.2 Compiler Architecture

```text
CompilerScope                          CompileResult
┌───────────────────────────┐          ┌──────────────────────────┐
│ function : CompiledFunction│         │ top_level : CompiledFunction│
│ locals   : vector<Local>  │         │ functions : vector<CompiledFunction>│
│ scope_depth : int         │         │ diagnostics : vector<Diagnostic>│
│ loops    : vector<LoopInfo>│        │ success : bool            │
│ enclosing : CompilerScope*│         └──────────────────────────┘
└───────────────────────────┘
```

The compiler maintains a linked list of `CompilerScope` values — one per function or lambda being compiled. The innermost scope is `current_`. When a nested function or lambda is entered, a new scope is pushed; when it finishes, `end_function()` pops it and appends the completed `CompiledFunction` to `compiled_functions_`.

**Scope management:**

- `begin_scope()` / `end_scope()` — Increment/decrement `scope_depth`. On `end_scope()`, any locals at the closing depth are popped from the stack via `Pop` instructions.
- `begin_function(name, arity)` / `end_function()` — Push/pop a `CompilerScope`. Slot 0 is always reserved for the callee reference ("self").
- `declare_local(name, is_mutable)` — Adds a `Local` to the current scope's locals vector. The local's slot index is its position in the vector.

### 7.3 Variable Resolution

Variables are resolved at compile time using a three-tier lookup:

1. **Local** — `resolve_local()` searches the current scope's `locals` vector from top to bottom. If found, emits `GetLocal`/`SetLocal` with the slot index.
2. **Upvalue** — `resolve_upvalue()` walks the `CompilerScope` chain toward the enclosing scope. If the variable is found in an enclosing scope's locals, it marks that local as `is_captured = true` and records an `Upvalue` descriptor with `is_local = true`. If it was already captured as an upvalue in an intermediate scope, a forwarding descriptor is recorded with `is_local = false`.
3. **Global** — Falls back to `GetGlobal`/`SetGlobal` with a name-table index.

### 7.4 Operand Encoding

All multi-byte operands are encoded in **big-endian** byte order.

| Format            | Size     | Description                          | Used by                                                                                          |
| ----------------- | -------- | ------------------------------------ | ------------------------------------------------------------------------------------------------ |
| (none)            | 1 byte   | Opcode only, no operands             | `Pop`, `Dup`, `Add`, `Return`, etc.                                                              |
| u8                | 2 bytes  | Opcode + 1-byte operand              | `Interpolate`, `MakeTuple`, `Call`, `Spawn`, `Print`, `Assert`, `RecordWith`                     |
| u16               | 3 bytes  | Opcode + 2-byte operand (big-endian) | `Constant`, `GetLocal`, `SetLocal`, `Jump`, `JumpIfFalse`, `Loop`, `GetField`, `MakeArray`, etc. |
| u32               | 5 bytes  | Opcode + 4-byte operand (big-endian) | `ConstantLong`                                                                                   |
| u8 + u8           | 3 bytes  | Opcode + two 1-byte operands         | `CallNamed` (pos_count, named_count)                                                             |
| u16 + u8          | 4 bytes  | Opcode + u16 + u8                    | `MakeRecord` (type_name, field_count)                                                            |
| u16 + u8 + inline | variable | Opcode + u16 + u8 + inline u16s      | `MakeRecord` (followed by field name indices)                                                    |

**Jump encoding:**

- Forward jumps (`Jump`, `JumpIfFalse`, `JumpIfTrue`, `NullCoalesce`): The u16 operand is a **forward offset** added to the instruction pointer after reading the operand bytes.
- Backward jumps (`Loop`): The u16 operand is a **backward offset** subtracted from the instruction pointer. The offset includes the 2 operand bytes themselves.
- `TryCatch`: The u16 operand is the offset from the current IP to the catch block entry point.

### 7.5 Jump Patching

Forward jumps require backpatching because the target offset is not known when the jump instruction is emitted.

```text
1. emit_jump(Op::JumpIfFalse)
   → emits: JumpIfFalse 0xFF 0xFF
   → returns: offset of the first placeholder byte

2. [compile the body that the jump skips over]

3. patch_jump(offset)
   → calculates: distance = current_code_size - offset - 2
   → writes distance as big-endian u16 at the placeholder offset
```

Break statements inside loops use the same mechanism: each `break` emits `Jump 0xFF 0xFF` and stores the placeholder offset in the enclosing `LoopInfo::breaks` vector. When the loop body compilation finishes, all break offsets are patched to jump past the loop exit.

### 7.6 Control Flow Compilation

#### If/Else

```text
[condition]
JumpIfFalse → else_label      # skip then-body if false
Pop                            # discard condition (truthy path)
[then_body]
Jump → end_label               # skip else-body
else_label:
Pop                            # discard condition (falsy path)
[else_body]
end_label:
```

#### While Loop

```text
loop_start:
[condition]
JumpIfFalse → exit_label       # exit if false
Pop                            # discard condition
[body]
Loop → loop_start              # backward jump to re-evaluate condition
exit_label:
Pop                            # discard condition (false)
```

The compiler pushes a `LoopInfo{start: loop_start, scope_depth}` before the body and pops it after, patching all break jumps.

#### For Loop (Iterator-Based)

```text
[iterable expression]
ForIterInit                    # convert iterable → (iterable, index=0) state tuple
; declare hidden __iter__ local (slot N)
None                           # placeholder for loop variable
; declare loop_var local (slot N+1)
Zero                           # placeholder for index variable (if present)
; declare index_var local (slot N+2, optional)
loop_start:
GetLocal __iter__              # load iterator state
ForIterStep                    # → (element, true) or (false)
JumpIfFalse → exit_label       # exhausted?
Pop                            # discard the true flag
SetLocal loop_var              # store element
Pop                            # discard the copy left by SetLocal
[body]
; if index_var: GetLocal index_var → Increment → SetLocal index_var → Pop
Loop → loop_start
exit_label:
Pop                            # discard the false flag
```

`ForIterInit` creates an internal tuple `(iterable, 0)` as iterator state. `ForIterStep` advances the index and pushes either `(element, true)` or `(false)`. `ForIterStepKV` is used for key-value iteration (e.g., `for key, value in dict`) and pushes `(value, key, true)` or `(false)`.

#### Break and Continue

These are not opcodes. They compile to stack cleanup followed by jump instructions:

- **Break:** Emit `Pop` for each local deeper than the loop's `scope_depth`, then emit a forward `Jump` (placeholder). The offset is stored in `LoopInfo::breaks` and patched when the loop finishes.
- **Continue:** Emit `Pop` for each local deeper than the loop's `scope_depth`, then emit `Loop` with a backward offset to `loop_start`.

#### Try/Catch/Finally

```text
TryCatch → catch_label         # push exception handler
[try_body]
TryEnd                         # pop handler (normal exit)
Jump → finally_label
catch_label:                   # VM pushes error message string onto stack
; declare catch_var local
[catch_body]
finally_label:
[finally_body]                 # (if present)
```

The VM's `ExceptionHandler` records the catch IP, the frame index, and the stack depth. On error, the VM unwinds frames and the stack back to the handler state, pushes the error message as a string, and jumps to the catch IP.

#### Match Statement

Match compiles to a chain of test-and-jump sequences — not to the `MatchStart`/`MatchArm`/`MatchEnd` opcodes (which are defined but unused).

```text
; subject stored as hidden local __match_stmt_subject__
; for each arm:
GetLocal __match_stmt_subject__    # duplicate subject for test
[pattern test]                     # e.g., Constant + Equal, IsSuccess, IsType
JumpIfFalse → next_arm
Pop                                # discard test result
[optional bindings via ResultInner, IndexGet]
[arm body]
Jump → end_label
next_arm:
Pop                                # discard test result
; ... next arm ...
end_label:
```

**Pattern test opcodes by arm kind:**

| Arm Kind       | Test Opcodes                    |
| -------------- | ------------------------------- |
| Integer/String | `Constant(val)` + `Equal`       |
| Boolean        | `True`/`False` + `Equal`        |
| None           | `None` + `Equal`                |
| Comparison     | `[expr]` + comparison op        |
| success(v)     | `IsSuccess`                     |
| failure(m)     | `IsSuccess` + `Not`             |
| some(v)        | `IsSome`                        |
| Choice variant | `IsType(name_idx)`              |
| else           | No test; falls through directly |

Bindings are extracted using `ResultInner` (for success/failure/some) or `IndexGet` with positional indices (for choice variant fields).

### 7.7 Closure Compilation

When a function or lambda is compiled, the compiler:

1. Calls `begin_function(name, arity)` to create a new `CompilerScope`.
2. Declares each parameter as a local in the new scope.
3. Compiles the function body into the scope's `Chunk`.
4. Emits `Op::None` + `Op::Return` as the implicit return.
5. Calls `end_function()` which pops the scope and produces a `CompiledFunction`.
6. Appends the `CompiledFunction` to the global `compiled_functions_` vector.
7. In the **enclosing** scope, emits:

    ```text
    MakeClosure <u16 func_index> <u8 upvalue_count>
    ```

At runtime, `MakeClosure` creates a `FunctionValue` referencing the compiled function and captures upvalues:

- `is_local = true`: Copy the value directly from the enclosing frame's stack slot.
- `is_local = false`: Forward from the enclosing frame's own upvalue array (for variables captured from grandparent scopes).

**Capture semantics:** Luma uses **value capture**. The captured value is copied at closure-creation time. The original local can be popped normally when its scope ends. This differs from CLox's heap-allocated open-upvalue model but simplifies the implementation and avoids garbage collection.

### 7.8 Pipe Operator Compilation

#### Standard Pipe (`|>`)

The pipe `left |> right(args...)` rewrites the call so `left` becomes the first argument:

```text
[callee]                       # e.g., GetGlobal "Array.filter"
[left expression]              # piped value becomes first argument
[additional args]
Call(1 + N)                    # total argument count
```

#### Error Pipe (`!>`)

The error pipe short-circuits on failure and unwraps success:

```text
[left]
Dup                            # [val, val]
IsSuccess                      # [val, bool]
JumpIfFalse → fail_label       # [val, bool]
Pop                            # [val]       (discard bool)
Unwrap                         # [inner]     (extract success value)
[callee]                       # [inner, func]
Swap                           # [func, inner]
[additional args]
Call(1 + N)
Jump → end_label
fail_label:
Pop                            # [val]       (discard bool; failure stays on stack)
end_label:
```

### 7.9 Virtual Machine Architecture

The VM is a stack-based bytecode interpreter with a single-pass dispatch loop.

```text
VM
┌─────────────────────────────────────────────────┐
│ stack_  : vector<Value>        (max 65,536)     │
│ frames_ : vector<CallFrame>    (max 256)        │
│ exception_handlers_ : vector<ExceptionHandler>  │
│ global_env_ : shared_ptr<Environment>           │
│ compiled_functions_ : const vector<CompiledFunction>*│
│ thread_pool_ : unique_ptr<ThreadPool>           │
│ task_scopes_ : vector<unique_ptr<TaskScope>>    │
│ base_depth_ : size_t                            │
│ vm_call_fn_ : CallFn                            │
└─────────────────────────────────────────────────┘
```

**Stack:** A flat `vector<Value>` shared by all call frames. Each frame owns a contiguous region of stack slots starting at `slot_offset`. Slot 0 holds the callee; subsequent slots hold parameters and locals.

**Call frames:** A `vector<CallFrame>`, one per active function invocation. Each frame contains:

- A pointer to its `CompiledFunction` (for bytecode and metadata access).
- An instruction pointer (`ip`) into the function's `Chunk::code`.
- A `slots` pointer into the value stack (for fast local-variable access).
- A `slot_offset` for stack restoration on return.
- A `closure` (`shared_ptr<FunctionValue>`) for upvalue access.

**Exception handlers:** A stack of `ExceptionHandler` structs, each recording:

- `catch_ip` — The instruction to jump to on error.
- `frame_index` — Which call frame owns this handler.
- `stack_depth` — Stack depth to restore on error.

### 7.10 Execution Lifecycle

```text
execute(functions, top_level)
├── run(top_level)                  # register declarations as globals
│   ├── push CallFrame for top_level
│   ├── run_dispatch()              # execute bytecode until Return/EndModule
│   └── pop CallFrame
├── find @main in functions
└── run(@main)                      # execute @main function
    ├── push CallFrame for @main
    ├── run_dispatch()
    └── pop CallFrame
```

**`run(func)`** sets up a `CallFrame`, pushes placeholder slots for locals, sets `base_depth_` to the current frame depth, and calls `run_dispatch()`. When `Return` pops the frame back to `base_depth_`, the dispatch loop exits and returns the result value.

**`run_dispatch()`** is the core infinite loop. Each iteration:

1. Reads the current call frame (`frames_.back()`).
2. Fetches and decodes the next opcode (`*cf.ip++`).
3. Executes the opcode via a `switch` statement.
4. On `RuntimeError`, checks the exception handler stack. If a handler exists at or above `base_depth_`, unwinds to it. Otherwise, re-throws.

**`run_to_return()`** is used when native stdlib functions need to call back into compiled user functions (e.g., `Array.map` calling a user lambda). It saves the current `base_depth_`, sets a new one, calls `run_dispatch()`, and restores the previous base depth. This allows nested dispatch loops.

### 7.11 Function Calling Convention

```text
Before Call:
    stack: [...] [callee] [arg0] [arg1] ... [argN]

Call(N+1):
    1. Create new CallFrame
    2. frame.function = callee.compiled
    3. frame.ip = function.chunk.code.data()
    4. frame.slot_offset = stack_size - arg_count - 1
    5. frame.slots = stack.data() + slot_offset
    6. frame.closure = callee (for upvalue access)
    7. Push frame onto frames_

During execution:
    slot[0] = callee (self reference)
    slot[1] = arg0
    slot[2] = arg1
    ...
    slot[N+1] = first local variable

Return:
    1. Pop return value from stack
    2. Pop CallFrame
    3. Resize stack to callee's slot_offset (discard all slots)
    4. Push return value
```

**Native function calls** pop the arguments into a `vector<Value>`, pop the callee, invoke the native C++ function directly, and push the result. No `CallFrame` is created.

### 7.12 Native Callback Mechanism

Standard library higher-order functions (e.g., `Array.map`, `Array.filter`, `Array.reduce`) accept user-defined compiled functions as arguments. When a native function needs to call a compiled user function, it uses the `vm_call_fn_` callback:

1. The callback pushes the callee and arguments onto the VM stack.
2. It creates a new `CallFrame` pointing to the compiled function.
3. It calls `run_to_return()`, which enters a nested dispatch loop.
4. When the user function returns, `run_to_return()` captures the result and returns it to the native function.

The `CallFnScope` RAII guard registers this callback with the standard library's thread-local `CallFn` slot during execution.

### 7.13 Exception Handling at Runtime

```text
TryCatch <catch_offset>         # Push ExceptionHandler {catch_ip, frame_index, stack_depth}
[try body]
TryEnd                          # Pop ExceptionHandler (normal exit)
Jump → finally

catch_label:                    # (entered via exception)
[catch body]

finally:
[finally body]
```

When a `RuntimeError` is thrown (or a `std::runtime_error` from a native function):

1. Check if the handler stack is non-empty and the top handler's `frame_index >= base_depth_`.
2. Pop the handler.
3. Unwind: pop frames until `frames_.size() == handler.frame_index + 1`.
4. Restore the stack: `stack_.resize(handler.stack_depth)`.
5. Push the error message as a `Value{string}`.
6. Set `frames_.back().ip = handler.catch_ip`.
7. Continue the dispatch loop (now executing catch-block bytecode).

If no handler is found (or the handler belongs to an outer execution context below `base_depth_`), the exception propagates through the C++ call stack.

### 7.14 Concurrency Execution Model

**Spawn:** The `Spawn` opcode pops the callee and arguments (deep-copied for thread safety), creates a `std::promise<Value>`, and enqueues a task on the thread pool. The task creates a lightweight child VM (`VM(env, shared_pool, compiled_fns)`) that shares the parent's thread pool and global environment. The child VM executes the callee and sets the promise. A `TaskValue` wrapping the `future<Value>` is pushed onto the parent's stack.

**Await:** Pops the `TaskValue` and calls `future.get()`, blocking until the spawned task completes. Pushes the result. Checks for cooperative cancellation before blocking.

**Task scopes:** `TaskScopeBegin` creates a `TaskScope` (linked to the parent scope, if any) and sets it as `current_scope_`. All tasks spawned within this scope are tracked by the scope. `TaskScopeEnd` calls `scope.join_all()` to wait for all child tasks and pushes their results as an array. On error, the scope cancels all children before re-throwing.

### 7.15 REPL Execution Model

The REPL creates a single `VM` instance that persists across input lines. Each line is independently lexed, parsed, and compiled into a `CompiledFunction`. The VM calls `execute_function(func, functions)`, which:

1. Saves the current stack size, frame count, handler count, and base depth.
2. Calls `run(func)`.
3. On error, restores all saved state so the VM remains usable for the next input.

The top-level scope's last expression is compiled with `Return` instead of `Pop` (REPL mode), allowing the REPL to display the result.

### 7.16 Bytecode Disassembly

The `Chunk::disassemble(name)` method produces a human-readable listing of the bytecode stream:

```text
=== @main ===
0000 Constant             0 (42)
0003 SetGlobal            0 (x)
0006 GetGlobal            0 (x)
0009 Print                1
0011 None
0012 Return
```

Each line shows the byte offset, the opcode name, operand values, and (where applicable) the resolved constant value or name in parentheses.

### 7.17 Opcode Reference

The complete opcode set (105 opcodes) with stack effects. Stack notation: `(before → after)`, where the top of stack is rightmost.

#### Stack Manipulation

| Opcode         | Operands  | Stack Effect      | Description                                    |
| -------------- | --------- | ----------------- | ---------------------------------------------- |
| `Constant`     | u16 index | `(→ value)`       | Push `constants[index]` onto the stack.        |
| `ConstantLong` | u32 index | `(→ value)`       | Push `constants[index]` (large constant pool). |
| `Pop`          | —         | `(value →)`       | Discard top of stack.                          |
| `Dup`          | —         | `(a → a a)`       | Duplicate top of stack.                        |
| `Dup2`         | —         | `(a b → a b a b)` | Duplicate top two stack values.                |
| `Swap`         | —         | `(a b → b a)`     | Swap top two values.                           |

#### Variables

| Opcode       | Operands     | Stack Effect      | Description                                                |
| ------------ | ------------ | ----------------- | ---------------------------------------------------------- |
| `GetLocal`   | u16 slot     | `(→ value)`       | Push the value at stack slot `slot_offset + slot`.         |
| `SetLocal`   | u16 slot     | `(value → value)` | Store top-of-stack into the slot (value remains).          |
| `GetUpvalue` | u16 index    | `(→ value)`       | Push captured upvalue at `index`.                          |
| `SetUpvalue` | u16 index    | `(value → value)` | Store top-of-stack into upvalue at `index`.                |
| `GetGlobal`  | u16 name_idx | `(→ value)`       | Look up `names[name_idx]` in the global environment.       |
| `SetGlobal`  | u16 name_idx | `(value → value)` | Define/update `names[name_idx]` in the global environment. |

#### Literals

| Opcode  | Operands | Stack Effect | Description          |
| ------- | -------- | ------------ | -------------------- |
| `None`  | —        | `(→ none)`   | Push the none value. |
| `True`  | —        | `(→ true)`   | Push boolean true.   |
| `False` | —        | `(→ false)`  | Push boolean false.  |
| `Zero`  | —        | `(→ 0)`      | Push integer 0.      |
| `One`   | —        | `(→ 1)`      | Push integer 1.      |

#### Arithmetic

| Opcode      | Operands | Stack Effect     | Description                                                                          |
| ----------- | -------- | ---------------- | ------------------------------------------------------------------------------------ |
| `Add`       | —        | `(a b → result)` | Add. Integer + integer → integer; any number mix → number; string + string → string. |
| `Subtract`  | —        | `(a b → result)` | Subtract. Integer or number operands.                                                |
| `Multiply`  | —        | `(a b → result)` | Multiply. Integer or number operands.                                                |
| `Divide`    | —        | `(a b → result)` | Divide. Checks for division by zero. Integer / integer → integer.                    |
| `IntDivide` | —        | `(a b → result)` | Integer division. Both operands must be integers.                                    |
| `Modulo`    | —        | `(a b → result)` | Modulo. Both operands must be integers.                                              |
| `Negate`    | —        | `(a → -a)`       | Negate. Integer or number.                                                           |
| `Increment` | —        | `(a → a+1)`      | Increment integer by 1.                                                              |
| `Decrement` | —        | `(a → a-1)`      | Decrement integer by 1.                                                              |

#### Comparison

| Opcode         | Operands | Stack Effect   | Description                                                 |
| -------------- | -------- | -------------- | ----------------------------------------------------------- |
| `Equal`        | —        | `(a b → bool)` | Structural equality via `Value::equals()`.                  |
| `NotEqual`     | —        | `(a b → bool)` | Negation of `Equal`.                                        |
| `Less`         | —        | `(a b → bool)` | Integer fast path; string lexicographic; otherwise numeric. |
| `LessEqual`    | —        | `(a b → bool)` | Same type dispatch as `Less`.                               |
| `Greater`      | —        | `(a b → bool)` | Same type dispatch as `Less`.                               |
| `GreaterEqual` | —        | `(a b → bool)` | Same type dispatch as `Less`.                               |

#### Logical

| Opcode | Operands | Stack Effect   | Description                                                                     |
| ------ | -------- | -------------- | ------------------------------------------------------------------------------- |
| `Not`  | —        | `(a → bool)`   | Logical negation via `is_truthy()`.                                             |
| `And`  | —        | `(a b → bool)` | Logical AND. The compiler also uses `JumpIfFalse` for short-circuit evaluation. |
| `Or`   | —        | `(a b → bool)` | Logical OR. The compiler also uses `JumpIfTrue` for short-circuit evaluation.   |

#### Bitwise

| Opcode       | Operands | Stack Effect  | Description              |
| ------------ | -------- | ------------- | ------------------------ |
| `BitwiseAnd` | —        | `(a b → int)` | Bitwise AND on integers. |
| `BitwiseOr`  | —        | `(a b → int)` | Bitwise OR on integers.  |
| `BitwiseXor` | —        | `(a b → int)` | Bitwise XOR on integers. |
| `BitwiseNot` | —        | `(a → int)`   | Bitwise NOT on integer.  |
| `ShiftLeft`  | —        | `(a b → int)` | Left shift on integers.  |
| `ShiftRight` | —        | `(a b → int)` | Right shift on integers. |

#### Strings

| Opcode        | Operands | Stack Effect                 | Description                                          |
| ------------- | -------- | ---------------------------- | ---------------------------------------------------- |
| `Concatenate` | —        | `(a b → string)`             | Concatenate two values as strings via `to_string()`. |
| `Interpolate` | u8 count | `(part₁ ... partₙ → string)` | Pop `count` values, concatenate as strings.          |

#### Collections

| Opcode         | Operands  | Stack Effect                          | Description                                                               |
| -------------- | --------- | ------------------------------------- | ------------------------------------------------------------------------- |
| `MakeArray`    | u16 count | `(elem₁ ... elemₙ → array)`           | Create array from `count` stack values.                                   |
| `MakeDict`     | u16 count | `(key₁ val₁ ... keyₙ valₙ → dict)`    | Create dictionary from `count` key-value pairs.                           |
| `MakeTuple`    | u8 count  | `(elem₁ ... elemₙ → tuple)`           | Create tuple from `count` stack values.                                   |
| `MakeRange`    | —         | `(start end → range)`                 | Create exclusive range `[start, end)`.                                    |
| `MakeRangeInc` | —         | `(start end → range)`                 | Create inclusive range `[start, end]`.                                    |
| `IndexGet`     | —         | `(container index → value)`           | Index into array, dict, string, tuple, or choice. Supports range slicing. |
| `IndexSet`     | —         | `(container index value → container)` | Set element in array or dictionary. Returns the modified container.       |
| `IndexGetOpt`  | —         | `(container index → value\            | none)`                                                                    |

#### Records

| Opcode        | Operands                                                | Stack Effect                    | Description                                                        |
| ------------- | ------------------------------------------------------- | ------------------------------- | ------------------------------------------------------------------ |
| `MakeRecord`  | u16 type_name, u8 field_count, inline u16×N field_names | `(val₁ ... valₙ → record)`      | Create record with named fields. Field name indices follow inline. |
| `GetField`    | u16 name_idx                                            | `(obj → value)`                 | Access field on record, tuple (numeric index), or choice.          |
| `SetField`    | u16 name_idx                                            | `(obj value → obj)`             | Set field on record. Returns the modified record.                  |
| `GetFieldOpt` | u16 name_idx                                            | `(obj → value\                  | none)`                                                             |
| `RecordWith`  | u8 count, inline u16×N field_names                      | `(base val₁ ... valₙ → record)` | Clone record with field overrides (`record with { ... }`).         |

#### Choice Types

| Opcode                  | Operands       | Stack Effect                                | Description                                                                                                                                                                                                                                                           |
| ----------------------- | -------------- | ------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `MakeChoice`            | —              | `(type_name variant_name → choice)`         | Create a unit choice variant (no fields).                                                                                                                                                                                                                             |
| `MakeChoiceConstructor` | u8 field_count | `(type_name variant_name → constructor_fn)` | Create a constructor function for a data choice variant. The function captures type/variant names and accepts `field_count` arguments, returning a choice value. Emitting strings avoids storing non-serialisable `NativeFunctionValue` objects in the constant pool. |

#### Result / Optional

| Opcode          | Operands | Stack Effect       | Description                                                             |
| --------------- | -------- | ------------------ | ----------------------------------------------------------------------- |
| `MakeSuccess`   | —        | `(value → result)` | Wrap value in `success(...)`.                                           |
| `MakeFailure`   | —        | `(value → result)` | Wrap value in `failure(...)` with source location.                      |
| `MakeSome`      | —        | `(value → value)`  | No-op — non-null values are already "some".                             |
| `Unwrap`        | —        | `(value → inner)`  | Extract success value; runtime error on failure or none.                |
| `ResultInner`   | —        | `(value → inner)`  | Extract inner value regardless of success/failure (for match bindings). |
| `IsSuccess`     | —        | `(value → bool)`   | True if value is a successful result (or non-null non-result).          |
| `IsSome`        | —        | `(value → bool)`   | True if value is not null.                                              |
| `EnsureSuccess` | —        | `(value → result)` | Wrap TOS in `success()` if it is not already a result.                  |

#### Downcast / Is

| Opcode            | Operands      | Stack Effect       | Description                                                             |
| ----------------- | ------------- | ------------------ | ----------------------------------------------------------------------- |
| `Downcast`        | u16 type_name | `(value → result)` | Returns `success(value)` if type matches, `failure(message)` otherwise. |
| `TrustedDowncast` | u16 type_name | `(value → value)`  | No-op pass-through (trusted by the type checker).                       |
| `IsType`          | u16 type_name | `(value → bool)`   | True if value's type or variant name matches.                           |

#### Control Flow

| Opcode         | Operands   | Stack Effect      | Description                                 |
| -------------- | ---------- | ----------------- | ------------------------------------------- |
| `Jump`         | u16 offset | `(→)`             | Unconditional forward jump: `ip += offset`. |
| `JumpIfFalse`  | u16 offset | `(value → value)` | Jump if top is falsy. Does **not** pop.     |
| `JumpIfTrue`   | u16 offset | `(value → value)` | Jump if top is truthy. Does **not** pop.    |
| `Loop`         | u16 offset | `(→)`             | Backward jump: `ip -= offset`.              |
| `NullCoalesce` | u16 offset | `(value → value\  | inner)`                                     |

#### Functions

| Opcode        | Operands                  | Stack Effect                       | Description                                                      |
| ------------- | ------------------------- | ---------------------------------- | ---------------------------------------------------------------- |
| `Call`        | u8 arg_count              | `(callee arg₁ ... argₙ → [frame])` | Call a function. Pushes a new `CallFrame`.                       |
| `CallNamed`   | u8 pos, u8 named          | `(callee args... → [frame])`       | Call with named arguments (simplified dispatch).                 |
| `TailCall`    | u8 arg_count              | `(callee arg₁ ... argₙ → [frame])` | Tail call. Reuses the current `CallFrame`.                       |
| `Return`      | —                         | `([frame] → result)`               | Return from function. Pops frame, restores stack, pushes result. |
| `MakeClosure` | u16 func_idx, u8 uv_count | `(→ closure)`                      | Create a `FunctionValue` with captured upvalues.                 |

#### Pipe Operators

| Opcode      | Operands | Stack Effect              | Description                                     |
| ----------- | -------- | ------------------------- | ----------------------------------------------- |
| `Pipe`      | —        | `(value callee → [call])` | Call `callee(value)`. Reorders stack and calls. |
| `ErrorPipe` | —        | `(value callee → [call]\  | failure)`                                       |

#### Exception Handling

| Opcode     | Operands         | Stack Effect | Description                                           |
| ---------- | ---------------- | ------------ | ----------------------------------------------------- |
| `TryCatch` | u16 catch_offset | `(→)`        | Push an `ExceptionHandler`.                           |
| `TryEnd`   | —                | `(→)`        | Pop the `ExceptionHandler` (normal completion).       |
| `Rethrow`  | —                | `(value →)`  | Re-throw TOS as `RuntimeError` (used by try/finally). |

#### Match (Unused)

| Opcode       | Operands | Stack Effect | Description                             |
| ------------ | -------- | ------------ | --------------------------------------- |
| `MatchStart` | —        | `(→)`        | No-op. Match compiles to jumps instead. |
| `MatchArm`   | —        | `(→)`        | No-op.                                  |
| `MatchEnd`   | —        | `(→)`        | No-op.                                  |

#### Containment

| Opcode     | Operands | Stack Effect                 | Description                                            |
| ---------- | -------- | ---------------------------- | ------------------------------------------------------ |
| `Contains` | —        | `(element container → bool)` | Element-in-array, key-in-dict, or substring-in-string. |

#### Concurrency

| Opcode           | Operands     | Stack Effect                    | Description                                                 |
| ---------------- | ------------ | ------------------------------- | ----------------------------------------------------------- |
| `Spawn`          | u8 arg_count | `(callee arg₁ ... argₙ → task)` | Spawn a task on the thread pool. Deep-copies all arguments. |
| `Await`          | —            | `(task → value)`                | Block until task completes; push result.                    |
| `TaskScopeBegin` | —            | `(→)`                           | Create a `TaskScope` for structured concurrency.            |
| `TaskScopeEnd`   | —            | `(→ array)`                     | Join all tasks in scope; push results as array.             |

#### Iteration

| Opcode          | Operands | Stack Effect                                    | Description                                                                            |
| --------------- | -------- | ----------------------------------------------- | -------------------------------------------------------------------------------------- |
| `ForIterInit`   | —        | `(iterable → state)`                            | Convert iterable to `(iterable, 0)` iterator state.                                    |
| `ForIterStep`   | —        | `(state → element true)` or `(state → false)`   | Advance iterator. Two values if not exhausted, one if exhausted.                       |
| `ForIterStepKV` | —        | `(state → value key true)` or `(state → false)` | Advance iterator for key-value pairs. Three values if not exhausted, one if exhausted. |

#### Miscellaneous

| Opcode      | Operands     | Stack Effect                   | Description                                                |
| ----------- | ------------ | ------------------------------ | ---------------------------------------------------------- |
| `Print`     | u8 arg_count | `(arg₁ ... argₙ → none)`       | Print arguments separated by spaces, followed by newline.  |
| `Assert`    | u8 arg_count | `(condition [message] → none)` | Runtime error if condition is falsy.                       |
| `TypeOf`    | —            | `(value → string)`             | Push the type name of the value.                           |
| `EndModule` | —            | `(→)`                          | End of bytecode stream. Returns `none` from dispatch loop. |

**Fused opcodes (peephole optimisations):**

| Opcode           | Operands | Stack Effect | Description                                                    |
| ---------------- | -------- | ------------ | -------------------------------------------------------------- |
| `IncrementLocal` | u16 slot | `(→)`        | Equivalent to `GetLocal slot; Increment; SetLocal slot; Pop`.  |
| `DecrementLocal` | u16 slot | `(→)`        | Equivalent to `GetLocal slot; Decrement; SetLocal slot; Pop`.  |
| `SetLocalPop`    | u16 slot | `(value →)`  | Equivalent to `SetLocal slot; Pop`.                            |
| `GetLocalReturn` | u16 slot | `(→ result)` | Equivalent to `GetLocal slot; Return` — returns `local[slot]`. |

**Conversion opcodes:**

| Opcode        | Operands | Stack Effect     | Description                                                                  |
| ------------- | -------- | ---------------- | ---------------------------------------------------------------------------- |
| `IntToNumber` | —        | `(int → number)` | Convert integer on top of stack to number (double). No-op if already number. |
| `Clone`       | —        | `(a → a')`       | Deep-copy top of stack (value semantics for mutable bindings).               |

---

## 8 — Type System Design

### 8.1 Static Typing Model

Luma uses a **static, strong, manifest** type system:

- **Static:** All types are checked before execution. No type-related surprises at runtime (except for `downcast`).
- **Strong:** No implicit type coercions between unrelated types. `integer` and `number` are distinct. Mixing them in arithmetic implicitly promotes the `integer` operand to `number` — this is the only automatic numeric promotion in the language and is not a general coercion.
- **Manifest:** Variable types are explicitly declared by the programmer (e.g., `integer x = 10`). There is no type inference for variable declarations.

### 8.2 Type Compatibility Rules

| Source Type | Target Type     | Allowed?                                                           |
| ----------- | --------------- | ------------------------------------------------------------------ |
| `integer`   | `number`        | Implicit in assignments and arithmetic (integer widens to number). |
| `none`      | `none`          | Always (identity, or as `optional<T>`).                            |
| `Record`    | `Interface`     | If the record has all required fields with assignable types.       |
| `T`         | `T`             | Always (identity).                                                 |
| Type alias  | Underlying type | Always (transparent).                                              |

### 8.3 Generic Types

Luma supports user-defined generic type parameters on functions, records, interfaces, and type aliases. Declare with `<T>` after the name; the type parameter is resolved at each call or construction site. Built-in generic types (`array<T>`, `dictionary<T>`, `result<T>`, `task<T>`, `channel<T>`, `function(...) -> T`) use the same syntax. Dictionary keys are always `string` — `T` is the value type.

The type parameter `T` is checked at each usage site:

- `Array.push(arr, v)` — `v` must match the array's element type.
- `Result.unwrap(r)` — the return type matches the result's value type.
- `Channel.send(ch, v)` — `v` must match the channel's element type.

#### Bounded Generics

A type parameter may carry interface bounds (`<T: Comparable>` or `<T: A + B>`). The Type Checker verifies that the concrete type passed at each call site structurally satisfies every listed interface before allowing the substitution. Bounds are stored in the `TypeParam` struct alongside the parameter name and checked during `infer_generic_call`.

#### Turbofish — Explicit Type Arguments

When type inference is insufficient (e.g. a function's return type depends on `T` but no argument constrains it), the caller may supply explicit type arguments using the turbofish operator: `func::<integer>(args)`. The Lexer emits a `ColonColon` token, the Parser detects `::` followed by `<` to enter `parse_turbofish_call`, and the Type Checker binds the supplied types directly instead of running inference.

### 8.4 Structural Interface Checking

Interface satisfaction is checked structurally, not nominally. A record `R` satisfies an interface `I` if and only if for every field `(name, type)` in `I`, the record `R` has a field with the same name and an assignable type (`integer` satisfies a `number` field, for example).

No registration, no `implements` keyword, no runtime cost. The check is performed entirely at compile time by the Type Checker when a record value is passed as an interface-typed parameter.

---

## 9 — Memory Management Strategy

The interpreter uses C++ RAII and smart pointers for fully automatic memory management. There is no manual allocation, no garbage collector, and no reference-counting cycle risk at the language level.

### 9.1 Ownership Model

| Value Kind       | C++ Ownership     | Rationale                                                                                                                        |
| ---------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| Primitives       | Stored by value   | Small types (`bool`, `double`, `int64_t`); no heap allocation.                                                                   |
| Strings          | Stored by value   | Stored inline as `std::string`; no shared-pointer indirection beyond the string's own internal buffer.                           |
| Arrays           | `std::shared_ptr` | Arrays can be shared; standard library returns new arrays.                                                                       |
| AST nodes        | `std::unique_ptr` | Single ownership — the parent node owns its children.                                                                            |
| Channels         | `std::shared_ptr` | Channels are shared between producer and consumer tasks.                                                                         |
| Choices          | `std::shared_ptr` | Choice values carry variant name and optional fields.                                                                            |
| Dictionaries     | `std::shared_ptr` | Same as arrays.                                                                                                                  |
| Environments     | `std::shared_ptr` | Scope chains use shared pointers so child scopes can reference their parent. Closures deep-copy the environment at capture time. |
| Lambdas/closures | `std::shared_ptr` | Closures own a deep-copied environment snapshot that must outlive them.                                                          |
| Records          | `std::shared_ptr` | Records can be passed by reference to functions.                                                                                 |
| Results          | `std::shared_ptr` | Results flow through pipelines and match arms.                                                                                   |
| Stdlib values    | `std::shared_ptr` | Queue, Stack, Set, HashSet, LinkedList, BinaryTree, Graph, KeyValueStore, Xml, Reference — all use shared pointers.              |
| Tasks            | `std::shared_ptr` | Tasks are shared between the spawning scope and the thread pool.                                                                 |
| Tuples           | `std::shared_ptr` | Same as arrays.                                                                                                                  |

### 9.2 Immutability and Copy Semantics

- **Immutable variables** (the default) can never be reassigned. The binding is effectively `const`.
- **Mutable variables** allow reassignment and compound assignment, but the value itself may still be a shared pointer. Mutations to array elements or record fields operate on the shared data in place.
- **Standard library functions** that "modify" collections (e.g., `Array.push`, `Dictionary.set`) return new values. The original remains unchanged. This is consistent with Luma's default immutability and avoids side effects.
- **Channel send** deep-copies the value before enqueuing (copy-on-send semantics) so that sender and receiver have independent copies.

---

## 10 — Error Handling Strategy

### 10.1 Error Categories

| Category       | When               | Examples                                                                  |
| -------------- | ------------------ | ------------------------------------------------------------------------- |
| `CompileError` | Include resolution | Circular include, missing include file.                                   |
| `RuntimeError` | Execution          | Division by zero, index out of bounds, unwrap on failure, channel errors. |
| `SyntaxError`  | Lexing or parsing  | Unexpected token, unterminated string, invalid character.                 |
| `TypeError`    | Type checking      | Type mismatch, undefined variable, non-exhaustive match.                  |

### 10.2 Error Representation

Runtime errors are represented by `RuntimeError`, which inherits from `std::runtime_error`:

```text
RuntimeError {
    message  : string           # human-readable description
    location : SourceLocation   # file, line, column
    hint     : optional<string> # suggestion for how to fix the issue
    payload  : optional<any>    # typed error value (for result<T,E> propagation)
}
```

Analysis-phase errors (syntax, type, compile, lint) are represented as `Diagnostic` objects collected by the `DiagnosticEmitter` base class and aggregated in a `DiagnosticCollector`.

### 10.3 Error Reporting Principles

1. **Report as many type errors as possible per phase.** The type checker collects all type errors before stopping. The lexer and parser abort on the first error. This minimises the number of compile-fix-compile cycles for type issues.
2. **Never proceed to the next phase if the current phase produced errors.** If the lexer finds errors, the parser is not invoked. If the parser finds errors, the type checker is not invoked. This prevents confusing cascading errors.
3. **Every error includes a source location.** The Error Reporter uses the Source Manager to display the offending line and mark the precise column.
4. **Messages are specific and actionable.** "Type mismatch: expected `number`, got `string`" is better than "type error." Include the names of the expected and actual types, the variable name, or the function name wherever relevant.

### 10.4 Runtime Error Propagation

Runtime errors in Luma are surfaced through two mechanisms:

- **`result<T>` values** — Functions that can fail return `result<T>`. The caller handles the failure explicitly via `match`, `??`, `Result.unwrap_or`, or `Result.is_success`. This is the primary and preferred mechanism.
- **Hard runtime errors** — Conditions that cannot be expressed as `result<T>` (division by zero on raw arithmetic, index out of bounds with `[]`, `Result.unwrap` on a `failure` value) terminate execution immediately with a `RuntimeError` including the full source location. Channel operations use typed exception subclasses (`ChannelClosedError`, `ChannelFullError`, `ChannelEmptyError`) that inherit from `RuntimeError`.

The interpreter does not expose exceptions to user code. C++ exceptions are used only internally for control flow (break, continue, return) and are caught at the appropriate scope boundary.

---

## 11 — Standard Library Architecture

### 11.1 Organisation

The standard library is organised as a flat collection of namespace modules. Each module is a separate source file in the interpreter codebase that registers its functions during startup.

```text
Standard Library
├── Array module             — Array.map, Array.filter, Array.push, ...
├── BinaryTree module        — BinaryTree.new, BinaryTree.insert, BinaryTree.remove, BinaryTree.inorder, ...
├── Calculus module          — Calculus.derivative, Calculus.integrate, Calculus.root, ...
├── Channel module           — Channel.new, Channel.send, Channel.receive, ...
├── Compression module       — Compression.deflate, Compression.inflate, Compression.gzip, Compression.encode_rle, ...
├── Converter module         — Converter.to_string, Converter.to_integer, ...
├── Core built-ins           — print, assert, type_of
├── Csv module               — Csv.deserialize, Csv.serialize, Csv.deserialize_records, Csv.header, ...
├── DateTime module          — DateTime.milliseconds_since_start, DateTime.now_iso_string, DateTime.to_iso_string, DateTime.add_months, DateTime.difference_days, DateTime.to_offset, DateTime.from_offset, DateTime.to_iso_string_offset, DateTime.from_parts_offset, DateTime.offset_hours, ...
├── Dictionary module        — Dictionary.get, Dictionary.set, Dictionary.keys, ...
├── Encoder module           — Encoder.encode_base64, Encoder.decode_base64, Encoder.encode_base64url, Encoder.encode_url, ...
├── FileSystem module        — FileSystem.list_directories, FileSystem.create_directory, FileSystem.delete_directory, FileSystem.rename_directory, ...
├── Graph module             — Graph.directed, Graph.undirected, Graph.add_vertex, Graph.add_edge, Graph.breadth_first_search, Graph.shortest_path, ...
├── Hash module              — Hash.md5, Hash.sha256, Hash.sha512, Hash.hmac_sha256, Hash.verify, ...
├── HashSet module           — HashSet.new, HashSet.from_array, HashSet.contains, HashSet.union, ...
├── Http module              — Http.get, Http.post, Http.parse_url, Http.download, ...
├── Console module          — Console.prompt, Console.read_from_stdin, Console.write_to_stdout, ...
├── Json module              — Json.serialize, Json.deserialize, Json.is_valid, ...
├── KeyValueStore module     — KeyValueStore.open, KeyValueStore.get, KeyValueStore.set, KeyValueStore.save, ...
├── LinearAlgebra module     — LinearAlgebra.add, LinearAlgebra.dot, LinearAlgebra.solve, ...
├── LinkedList module        — LinkedList.new, LinkedList.prepend, LinkedList.append, LinkedList.first, ...
├── Log module               — Log.info, Log.warn, Log.error, Log.set_level, ...
├── Math module              — Math.floor, Math.square_root, Math.pi, ...
├── Optional module          — Optional.is_some, Optional.is_none, Optional.unwrap, Optional.unwrap_or, ...
├── Process module           — Process.run, Process.get_environment_variable, Process.exit, ...
├── Queue module             — Queue.new, Queue.enqueue, Queue.dequeue, Queue.peek, ...
├── Random module            — Random.generate_number, Random.generate_integer, Random.choice, ...
├── Reference module         — Reference.new, Reference.get, Reference.set, Reference.update, Reference.swap, ...
├── RegularExpression module — RegularExpression.matches, RegularExpression.find, RegularExpression.replace, ...
├── Resource module          — Resource.with
├── Result module            — Result.is_success, Result.unwrap, Result.map_number, ...
├── Set module               — Set.from_array, Set.length, Set.contains, Set.union, Set.intersection, Set.add, Set.remove, ...
├── Socket module            — Socket.connect, Socket.listen, Socket.accept, Socket.send, Socket.receive, Socket.close, ...
├── Stack module             — Stack.new, Stack.push, Stack.pop, Stack.peek, ...
├── String module            — String.length, String.uppercase, String.trim, String.byte_length, String.is_whitespace, ...
├── Task module              — Task.all, Task.race, Task.timeout, Task.delay, Task.retry, ...
├── Terminal module           — Terminal.move_to, Terminal.move_to_row, Terminal.color, Terminal.read_key, Terminal.enable_mouse, ...
└── Xml module               — Xml.deserialize, Xml.element, Xml.serialize, Xml.find, ...
```

Two in-tree developer guides document the registration machinery for contributors: `core/runtime/stdlib/MODULE_DEVELOPMENT_KIT.md` (step-by-step module creation) and `core/runtime/stdlib/STDLIB_MODULE_REGISTRATION_GUIDE.md` (when to use each module builder).

### 11.2 Native Function Interface

Every built-in function is implemented as a `NativeFunction`:

```text
NativeFunction {
    name            : string                          # qualified name, e.g. "String.length"
    parameter_types : vector<Type>                    # expected argument types
    return_type     : Type                            # return type
    body            : function(vector<Value>) → Value # the C++ implementation
}
```

At startup, the Standard Library module iterates over all namespace modules and calls their registration function, which adds `NativeFunction` bindings to the global environment.

### 11.3 Registration Flow

```text
main()
  → create global Environment
  → StandardLibrary::register_all(environment)
      → CoreBuiltins::register(environment) # print, assert, type_of
      → StringModule::register(environment) # String.length, String.uppercase, ...
      → ArrayModule::register(environment)  # Array.map, Array.filter, ...
      → ... (one call per namespace module)
  → load and execute user program
```

### 11.4 Security Boundaries

Several standard library modules interact with the operating system. These modules validate all inputs at their boundary:

| Module              | Security Measures                                                                                                                                                                                                                                                     |
| ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Csv`               | Validate file paths for `read_file` / `write_file`. Return `result<T>` on failure.                                                                                                                                                                                    |
| `FileSystem`        | Validate file paths. Reject path traversal sequences. Reject absolute paths outside the working directory where appropriate. Return `result<T>` on failure.                                                                                                           |
| `Http`              | Validate header names and values — reject CRLF characters to prevent header injection. Return `result<T>` on failure.                                                                                                                                                 |
| `Console`           | Return `result<T>` on failure.                                                                                                                                                                                                                                        |
| `Log`               | Validate file paths for `set_output`. Return `result<T>` on failure.                                                                                                                                                                                                  |
| `Process`           | Validate command strings. Do not pass raw user input to the shell without sanitisation.                                                                                                                                                                               |
| `RegularExpression` | Limit pattern size (`max_regex_pattern_size`). Uses C++ `std::regex` (ECMAScript dialect). No backtracking protection — nested quantifiers can cause exponential time on pathological input. `find_all` and `split` iteration bounded by `max_array_size`. |
| `Socket`            | Validate host and port inputs. Return `result<T>` on connection failure. Enforce timeouts. Limit total open sockets (`max_open_sockets`).                                                                                                                              |
| `Xml`               | Validate file paths for `parse_file` / `write_file`. Return `result<T>` on failure.                                                                                                                                                                                   |

The numeric values behind these limits (`max_regex_pattern_size`, `max_array_size`, `max_open_sockets`, and the rest) and their `LUMA_LIMIT_*` overrides are listed in the canonical [resource-limit table](Luma_Performance_Guide.md#6--resource-limits) in the Performance Guide.

### 11.5 Sandbox Mode

When the interpreter is started with `--box` (or `-b`), the `register_all` function skips registration of all modules that interact with the operating system. The following modules are disabled in sandbox mode:

- `Console` — console I/O (stdin, stdout, stderr)
- `Csv` — CSV file I/O
- `FileSystem` — file reading, writing, directory listing, file deletion, copying
- `Http` — HTTP requests and downloads
- `KeyValueStore` — persistent file-backed storage
- `Process` — command execution, environment variables, exit
- `Socket` — TCP and UDP networking
- `Xml` — XML file I/O

All other modules (`Array`, `BinaryTree`, `Calculus`, `Channel`, `Compression`, `Converter`, `DateTime`, `Decimal`, `Dictionary`, `Encoder`, `Graph`, `Hash`, `HashSet`, `Json`, `LinearAlgebra`, `LinkedList`, `Log`, `Math`, `Optional`, `Queue`, `Random`, `Reference`, `RegularExpression`, `Resource`, `Result`, `Set`, `Stack`, `String`, `Task`, `Terminal`, etc.) remain available. Within these safe modules, individual functions that perform file I/O are also disabled: `Log.set_output`, `Compression.gzip_file`, `Compression.gunzip_file`, `Hash.sha256_file`, and `Hash.sha512_file`. Programs running in sandbox mode can perform pure computation and produce output via `print`, but cannot access the file system, network, or spawn processes.

Attempting to call a function from a sandbox-blocked module produces a clear error message (`'Module.function' is not available in sandbox mode (--box)`) instead of the generic "undefined variable" error. The `Environment` class maintains a set of blocked module prefixes that is checked during variable lookup.

Sandbox mode is combinable with `--test` (`luma --box --test <file>`) to run test suites in a restricted environment.

---

## 12 — Concurrency Architecture

### 12.1 Overview

Luma provides structured concurrency through three primitives:

- **Tasks** — background computations launched with `spawn` and joined with `await`.
- **Task scopes** — structured lifetime blocks (`task_scope { ... }`) that guarantee all child tasks complete before the scope exits.
- **Channels** — thread-safe message queues for communication between tasks.

### 12.2 Thread Pool

The interpreter maintains a thread pool that executes spawned tasks. The pool is created once at startup and shut down when the program exits.

```text
ThreadPool {
    workers      : vector<thread>    # worker threads
    task_queue   : thread-safe queue # pending work items
    worker_count : integer           # defaults to hardware concurrency
}
```

- `spawn fn(args)` enqueues a work item (the function call) onto the task queue and returns a `TaskValue` handle immediately.
- Worker threads dequeue work items and execute them.
- The task handle wraps a `std::future<Value>` that the caller can `await`.

### 12.3 Task Lifecycle

```text
  spawn fn(args)                    await task
       │                                │
       ▼                                ▼
  ┌──────────┐    ┌───────────┐    ┌──────────┐
  │ Created  │───▸│ Running   │───▸│ Completed│
  └──────────┘    └───────────┘    └──────────┘
```

- **Created:** The task is enqueued in the thread pool but has not started yet.
- **Running:** A worker thread is executing the function.
- **Completed:** The function has returned. The result (a `Value`) is stored in the future.

Un-awaited tasks are not tracked per-scope. When the `Interpreter` is destroyed at program exit, the `ThreadPool` destructor sets `should_stop_` and joins all worker threads. The worker loop drains the pending task queue before exiting, so all enqueued tasks that have not yet started are still executed to completion. Tasks that are already running are allowed to finish before the thread is joined.

### 12.4 Structured Concurrency — `task_scope`

A `task_scope { ... }` block provides structured task lifetimes:

```text
task_scope {
    spawn A()
    spawn B()
    spawn C()
}
# All three tasks have completed (or been cancelled) here.
```

**Implementation:**

- `TaskScope` holds a shared `CancellationToken` and a list of child `TaskValue` handles.
- Each `spawn` inside a scope registers its task as a child and shares the scope's cancellation token.
- At scope exit, `join_all()` awaits every child. Results are collected in spawn order and returned as an `array<T>`.
- If any child throws, `cancel_all()` sets the cancellation token, causing cooperative cancellation of remaining siblings. The first error is rethrown.

**Cancellation model — cooperative:**

- `CancellationToken` wraps an `atomic<bool>`. Tasks check the token at yield points (function calls, loop iterations, await).
- Cancellation does not forcefully terminate threads — tasks must check `is_cancelled()` and throw `CancelledException`.
- Scopes may be nested. Each child scope has its own token and a pointer to its parent scope.

**Thread-local tracking:**

- `Interpreter::current_scope_` (thread-local) points to the active `TaskScope` on the current thread.
- When a task is spawned, it captures the current scope's token and registers itself. Outside a scope, `current_scope_` is `nullptr` and the task runs unstructured (fire-and-forget).
- The type checker emits a warning for `spawn` outside a `task_scope`.

### 12.5 Task Environment Isolation

Each spawned task receives a **deep copy** of the environment at the point of spawning. Tasks do not share mutable state with the spawning scope or with each other. This eliminates data races by design.

Communication between tasks happens exclusively through channels (copy-on-send semantics).

### 12.6 Channel Implementation

```text
Channel<T> {
    buffer    : thread-safe deque<Value>
    capacity  : integer (0 = unlimited / unbuffered)
    is_closed : atomic<bool>
    mutex     : mutex
    not_empty : condition_variable
    not_full  : condition_variable
}
```

- **Unbuffered channel** (`Channel.new()`): `capacity` is `0`, meaning the queue is unlimited. `send` never blocks (it always enqueues immediately); `receive` blocks only until a value is available or the channel is closed.
- **Buffered channel** (`Channel.new_buffered(n)`): `send` blocks only when the buffer is full; `receive` blocks only when the buffer is empty.
- **Closing** (`Channel.close(ch)`): After closing, `send` returns `false` and `receive` returns `failure(...)` once the buffer is drained.
- **Copy-on-send:** Values are deep-copied before being placed into the channel's buffer. Sender and receiver operate on independent copies.

---

## 13 — REPL Architecture

### 13.1 REPL Loop

```text
┌──────────────────────────────────────────────────┐
│                                                  │
│  ┌──────────┐                                    │
│  │  Prompt  │◂──────────────────────────┐        │
│  └────┬─────┘                           │        │
│       ▼                                 │        │
│  ┌──────────┐    ┌──────────┐    ┌──────┴─────┐  │
│  │ Read     │───▸│ Evaluate │───▸│ Print      │  │
│  │ input    │    │ pipeline │    │ result     │  │
│  └──────────┘    └──────────┘    └────────────┘  │
│                       │                          │
│                  On error:                       │
│                  print error,                    │
│                  continue loop                   │
│                                                  │
└──────────────────────────────────────────────────┘
```

### 13.2 REPL-Specific Behaviour

- **Persistent state:** The environment persists across inputs. Variables, functions, records, and choice types defined on one line remain available.
- **Expression auto-print:** If the input is a standalone expression (not an assignment or declaration), the result is printed automatically with `=>` prefix.
- **No `@main` requirement:** The REPL evaluates input directly without requiring a main function.
- **Error isolation:** Errors in one input do not corrupt state. The REPL catches errors and returns to the prompt.
- **Commands:** Lines starting with `:` are REPL commands, not Luma code:
    - `:quit` / `:q` — Exit the REPL.
    - `:help` / `:h` — Print available commands.
    - `:clear` / `:c` — Reset the environment.
    - `:file <path>` / `:f <path>` — Load and execute a `.luma` file in the current session.

---

## 14 — Testing Architecture

### 14.1 Test Discovery

The `--test` mode drives the full pipeline (Lexer → Parser → Include Resolver → Type Checker → Linter) and then scans the typed AST for all functions carrying the `@test` annotation.

### 14.2 Test Execution

Each test function is executed independently:

1. Create a child environment (child of the global environment containing standard library bindings and user-defined declarations).
2. Call the test function with no arguments.
3. If the function completes without error → `PASS`.
4. If `assert` fails or a runtime error occurs → `failure`, with the error message recorded.

### 14.3 Test Isolation

Each test runs in a child environment of the global environment. Variables declared inside a test function are isolated — they exist only for the duration of that test and are discarded when it ends. However, mutations to top-level mutable variables (in the global environment) are visible to subsequent tests. Execution order is deterministic (declaration order in the source file), but tests must not depend on order.

### 14.4 Assert Mechanism

`assert(condition)` and `assert(condition, message)` are core built-in functions:

- If `condition` is `true`, they do nothing.
- If `condition` is `false`, they raise a `RuntimeError` with the message `"assertion failed"` (or the custom message). The Test Runner catches this error and records the test as failed.

---

## 15 — File Inclusion and Source Management

### 15.1 Include Resolution

```luma
include "utils.luma"
```

The `include` declaration instructs the interpreter to load and compile another source file. Include resolution is performed as a dedicated phase between parsing and type checking (see Phase 4 in Section 6.4):

1. Resolve the path relative to the directory of the _including_ file.
2. Check the Source Manager's registry to see if this file has already been loaded.
    - If already loaded, skip it (each file is included at most once).
    - If not loaded, load, lex, and parse it.
3. Recursively resolve any includes within the included file.
4. Merge the included file's declarations into the main program's declaration list.
5. The type checker processes all declarations (from all files) together so that cross-file references resolve correctly.

### 15.2 Circular Inclusion Prevention

Circular includes are handled gracefully by the include-once registry rather than reported as errors. Before entering a file, the resolver asks the Source Manager whether that file's canonical path has already been loaded; if so, the repeated include is skipped. Because every file is entered at most once, a cycle such as `main.luma → utils.luma → main.luma` resolves the second reference to `main.luma` as a no-op — each file's declarations are merged exactly once and no error is raised.

As a backstop against pathological nesting (for example a very deep chain of distinct files), the resolver tracks the current nesting depth with an `InclusionStack` counter and rejects includes beyond `k_max_include_depth` (64):

```text
CompileError in file_64.luma:1:1
  include depth limit exceeded (64) while including 'file_65.luma'
```

---

## 16 — Project File Structure

The source tree mirrors the module decomposition: each module is a self-contained directory of header/source pairs. The map below is **directory-level** — for the responsibilities of the modules within each directory, see [§4 — Module Decomposition](#4--module-decomposition). The per-file inventories for the debugger and language server are maintained in their own documents (the single source of truth) and cross-linked below rather than mirrored here.

```text
luma/
├── cmake/               # CMake helper modules (compiler flags, packaging, code generation)
├── core/                # the luma interpreter
│   ├── main.cpp         # entry point — parses CLI args and dispatches
│   ├── analysis/        # front-end: source text → typed AST
│   │   ├── ast/         # AST node type definitions (§4.5)
│   │   ├── diagnostics/ # structured diagnostics and terminal rendering (§4.19)
│   │   ├── errors/      # RuntimeError — VM/stdlib runtime exception type
│   │   ├── lexer/       # tokenisation (§4.3)
│   │   ├── linter/      # post-type-check code-quality warnings (§4.20)
│   │   ├── parser/      # recursive-descent AST construction (§4.4)
│   │   ├── pipeline/    # composable compilation-pass pipeline (§4.21)
│   │   ├── resolver/    # name resolution to stack-slot indices (§4.22)
│   │   ├── source/      # source loading and location tracking (§4.2)
│   │   └── types/       # static type checker and stdlib signatures (§4.6, §4.8)
│   ├── common/          # shared utilities — caches, resource limits, codecs, UTF-8 (§4.16)
│   └── runtime/         # back-end: bytecode compilation and execution
│       ├── cli/         # argument parsing, pipeline orchestration, test runner (§4.1, §4.14)
│       ├── compiler/    # AST-to-bytecode compiler, optimizer, verifier, caches (§4.17, §4.23–§4.26)
│       ├── concurrency/ # channels, task_scope, thread pool (§4.15)
│       ├── include/     # file inclusion and deduplication (§4.7)
│       ├── interpreter/ # runtime Value variant, environment, control flow (§4.9, §4.10)
│       ├── repl/        # interactive read-eval-print loop (§4.13)
│       ├── stdlib/      # built-in standard library modules (§4.11)
│       └── vm/          # stack-based virtual machine (§4.18)
├── shared/              # libraries shared by the interpreter, language server, and debugger
│   ├── json/            # JSON value type, parser, and serialiser
│   ├── protocol/        # Content-Length framed transport (LSP and DAP)
│   ├── stdlib/          # stdlib metadata catalog shared with the language server
│   └── symbols/         # shared symbol kinds and qualified-name helpers
├── debugger/            # Debug Adapter Protocol (DAP) adapter — luma_dap
│   ├── source/          # implementation — see Luma_Debugger.md (File Layout)
│   └── tests/           # DAP unit and integration tests
├── language-server/     # Language Server Protocol (LSP) server — luma_lsp
│   ├── source/          # implementation — see Luma_Language_Server.md (File Layout)
│   └── tests/           # LSP unit and protocol tests
├── tests/               # C++ and Luma test suites (§14 — Testing Architecture)
│   ├── analysis/        # analysis front-end unit tests (with golden snapshots)
│   ├── runtime/         # runtime back-end unit tests
│   ├── integration/     # full-pipeline integration tests
│   ├── features/        # Luma @test suites — language/ and stdlib/
│   └── platform/        # platform-specific tests (e.g. Win32 UTF-8)
├── examples/            # example Luma programs (applications, debug, design-patterns, language-features)
├── benchmarks/          # Luma benchmark programs and shared harness
├── fuzz/                # LibFuzzer targets (Clang only)
├── extensions/          # editor extensions — vscode and zed — and shared assets
├── external/            # vendored third-party libraries (webview + GUI assets, mbedTLS, miniz)
├── scripts/             # build, test, and code-generation helper scripts
├── instructions/        # coding and tooling guidelines
└── documents/           # project design and reference documents (this set)
```

The three executables enter at `core/main.cpp` (`luma`), `language-server/source/main.cpp` (`luma_lsp`), and `debugger/source/main.cpp` (`luma_dap`). Root-level configuration and metadata files — `CMakeLists.txt`, `CMakePresets.json`, `.clang-format`, `.clang-tidy`, `README.md`, `LICENSE`, and similar — sit alongside these directories.

### Naming Conventions

| Element             | Convention    | Example                     |
| ------------------- | ------------- | --------------------------- |
| Classes / structs   | `PascalCase`  | `TypeChecker`, `Token`      |
| Constants           | `snake_case`  | `max_call_frames`           |
| Directories         | `snake_case`  | `interpreter`               |
| Enum variants       | `PascalCase`  | `TokenType::IntegerLiteral` |
| Functions / methods | `snake_case`  | `tokenize`, `check_type`    |
| Macros              | `UPPER_SNAKE` | `LUMA_UNREACHABLE`          |
| Namespaces (C++)    | `snake_case`  | `luma::lexer`               |
| Source files        | `snake_case`  | `type_checker.cpp`          |

---

## 17 — Cross-Platform Considerations

### 17.1 Build System

CMake is the single build system. A single `CMakeLists.txt` at the project root defines the build for all platforms. Platform-specific code is isolated behind preprocessor guards or CMake conditions — never scattered across the codebase.

### 17.2 Platform Abstractions

Most of the interpreter is platform-independent (lexer, parser, type checker, interpreter, standard library logic). The following areas require platform-specific handling:

| Area              | Abstraction Strategy                                                                      |
| ----------------- | ----------------------------------------------------------------------------------------- |
| Console I/O       | Use `std::cin` / `std::cout` / `std::cerr`. Handle UTF-8 console mode on Windows.         |
| File system paths | Use `std::filesystem` (C++17) for all path operations. Normalise separators internally.   |
| Process execution | Use `std::system` or platform APIs behind a wrapper. Sanitise inputs.                     |
| Threading         | Use `std::thread`, `std::mutex`, `std::condition_variable`, `std::future` (C++ standard). |
| Time              | Use `std::chrono` for all timing. Use OS APIs only for wall-clock timestamps.             |

### 17.3 Supported Platforms

| Platform       | Compiler Requirement |
| -------------- | -------------------- |
| Linux (Ubuntu) | GCC 13 or later      |
| macOS          | Clang 15 or later    |
| Windows        | MSVC 2022 or later   |

All three compilers support C++20, which is the minimum language standard for the project.

---

## 18 — Debugger Architecture

The Luma debugger is a standalone executable (`luma_dap`) that implements the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) (DAP). It enables interactive debugging of Luma programs in VS Code, Zed, and any other DAP-capable editor. See [Luma_Debugger.md](Luma_Debugger.md) for the full design document.

### 18.1 Overview

The debugger embeds the complete Luma compilation pipeline and VM. It communicates with the editor over standard input/output using Content-Length framed JSON messages — the same base protocol used by the LSP server. The debugger's module and file breakdown — transport, protocol dispatch, breakpoint management, execution engine, and inspection — is documented in [Luma_Debugger.md](Luma_Debugger.md), the single source of truth for the implementation.

### 18.2 Threading Model

The debugger runs two threads:

1. **Protocol thread** — reads DAP requests from stdin, dispatches them, and writes responses/events to stdout.
2. **Execution thread** — runs the Luma VM with a debug hook that pauses at breakpoints and step targets.

The threads synchronise via `std::mutex` and `std::condition_variable`. The VM's pause callback blocks the execution thread until the protocol thread signals a resume, step, or terminate command.

### 18.3 Supported Capabilities

The debugger implements a comprehensive DAP capability set — breakpoints (line, function, conditional, hit-count, log, and exception), full stepping including reverse debugging, variable inspection and modification, expression evaluation, and advanced features such as hot code reload and time-travel debugging. See [Luma_Debugger.md §5 — Supported DAP Requests](Luma_Debugger.md#5--supported-dap-requests) for the authoritative, complete list.

---

## 19 — Design Decisions and Rationale

This section records the key design decisions and the reasoning behind each one, following the architectural principles.

### 19.1 Bytecode VM Execution Backend

**Decision:** The interpreter uses a bytecode compiler and stack-based VM as its sole execution backend.

**Rationale (KISS, Pragmatism):**

- The bytecode backend compiles the AST into a compact instruction stream and executes it via a stack-based dispatch loop, offering good performance for all programs.
- The VM reuses the existing `Value` type, keeping it compatible with the standard library implementation.
- Spawned tasks in the VM create lightweight per-thread VM instances that share the parent's thread pool, enabling structured concurrency with minimal overhead.
- A single execution path simplifies the codebase and eliminates the maintenance burden of keeping two backends in sync.

### 19.2 Recursive Descent Parser (Not Parser Generator)

**Decision:** The parser is a hand-written recursive descent parser, not generated from a grammar file.

**Rationale (KISS, Explicit Over Implicit):**

- Recursive descent parsers are straightforward to write and produce the clearest error messages.
- A hand-written parser gives full control over error recovery and synchronisation.
- No external tool dependency (no YACC, Bison, ANTLR).
- The Luma grammar is simple enough that a recursive descent parser handles it comfortably.

### 19.3 `std::variant` for Runtime Values (Not Class Hierarchy)

**Decision:** Runtime values are represented as a `std::variant` rather than a polymorphic class hierarchy.

**Rationale (KISS, Explicit Over Implicit):**

- A variant makes all possible value types visible in one place.
- Pattern matching on variants (via `std::visit`) is explicit and exhaustive — the compiler warns if a case is missing.
- No virtual dispatch overhead. No need to manage an inheritance tree.
- Primitives are stored inline without heap allocation.

### 19.4 Shared Pointers for Heap Values (Not Garbage Collection)

**Decision:** Heap-allocated values use `std::shared_ptr`. There is no custom garbage collector.

**Rationale (KISS, Occam's Razor):**

- `std::shared_ptr` provides automatic, deterministic memory management via reference counting.
- It integrates naturally with C++ RAII and requires no runtime infrastructure.
- The risk of reference cycles is minimal because Luma's value model is acyclic — closures deep-copy their environment at capture time, not by mutable reference.
- A garbage collector would add significant complexity without proportional benefit for version 1.0.

### 19.5 Copy-on-Send for Channels (Not Shared Memory)

**Decision:** Values sent through channels are deep-copied before being placed in the channel buffer.

**Rationale (Secure by Default, Fail Fast):**

- Deep copying eliminates data races between sender and receiver — each side owns its data independently.
- This matches Luma's default immutability philosophy.
- The simplicity gain outweighs the performance cost for the targeted use cases.

### 19.6 Deep Copy for Task Environments (Not Shared State)

**Decision:** Each spawned task receives a deep copy of its captured environment.

**Rationale (Secure by Default, Explicit Over Implicit):**

- Shared mutable state between threads is the primary source of concurrency bugs.
- Deep copying makes each task self-contained and race-free by construction.
- Communication between tasks is explicit — via channels — not implicit through shared variables.

### 19.7 Multi-Phase Pipeline (Not Single-Pass)

**Decision:** The interpreter uses a strict multi-phase pipeline (Lex → Parse → Include Resolution → Type Check → Execute) with phase boundaries that prevent proceeding on error.

**Rationale (Separation of Concerns, Fail Fast, Single Responsibility):**

- Each phase has a single, clear responsibility.
- Errors are caught at the earliest possible phase — syntax errors during parsing, type errors during checking — before execution begins.
- Each phase can be tested independently.
- The strict boundary (stop on error) prevents cascading errors that confuse the user.

### 19.8 No Exceptions in User Code (Result-Based Error Handling)

**Decision:** Luma exposes `result<T>` as the primary error-handling mechanism. No exceptions are visible to the user.

**Rationale (Explicit Over Implicit, Fail Fast):**

- `result<T>` makes error handling explicit — the caller must handle the failure case.
- Exceptions create invisible control-flow paths that are hard to reason about, especially for beginners.
- `match` with `success`/`failure` arms is more readable and harder to get wrong than try/catch.
- C++ exceptions are used only _internally_ within the interpreter for control flow (break, continue, return) and are never exposed to the Luma programmer.

---

## See Also

- [User Manual](Luma_User_Manual.md) — the language the interpreter implements
- [Error Handling](Luma_Error_Handling.md) — error categories and the interpreter's implementation policy
- [Performance Guide](Luma_Performance_Guide.md) — runtime performance characteristics and optimisation advice
- [Debugger](Luma_Debugger.md) — Debug Adapter Protocol design and architecture
- [Language Server](Luma_Language_Server.md) — Language Server Protocol design and architecture
- [Coding Guidelines](Luma_Coding_Guidelines.md) — Luma coding style and conventions
- [Initial Concept](Luma_Initial_Concept.md) — original language design goals and motivation
