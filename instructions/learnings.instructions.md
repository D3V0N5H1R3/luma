---
description: "Core project knowledge — language design, architecture overview, and cross-cutting patterns. Domain-specific learnings are in sibling files."
applyTo: "**/*"
priority: essential
---


# Learnings

Patterns, pitfalls, and non-obvious knowledge discovered during development sessions.

## Project Identity

- Luma is a statically typed, expression-oriented, interpreted programming language for beginners — "as easy as Python, as safe as Rust." Implemented in C++20 with a bytecode compiler and stack-based VM. Status: Alpha (0.5) — language, stdlib, and interpreter feature-complete; tooling under active development.
- The project spans ~150K+ lines across interpreter core, 38 stdlib module namespaces (27 always-available + 3 sandbox-aware + 8 OS-only), an LSP language server (`luma_lsp`), a DAP debugger (`luma_dap`), and editor extensions for VS Code and Zed.

## Architecture & Pipeline

- The interpreter follows a strict 8-phase pipeline: `Source → Lexer → Parser → Include Resolver → Type Checker → Linter → Compiler → VM`. Information flows in one direction only — phases are independent, composable, and testable.
- The pipeline is implemented via a composable pass framework in `core/analysis/pipeline/`. Builder pattern: `Pipeline::builder().add<Pass1>().add<Pass2>().build()`. Each pass declares `required_passes()` dependencies using `pass_name::` constants (compile-time `string_view` values). Advisory passes (linter) can `run_after_failure()`. The generic framework (`Pass`/`Pipeline`/`PipelineResult`) and the front-end pass wrappers (`TypeCheckerPass`/`LintPass`) depend only on the AST and diagnostics, so they compile into `luma_analysis`; `PipelineResult` derives success from `has_errors()` and stores diagnostics plus per-pass timing via `PassTiming` — it is deliberately artifact-agnostic. The back-end pass wrappers (`CompilerPass`/`OptimizerPass`/`VerifierPass`) live next to the code they wrap under `core/runtime/compiler/` and exchange the clean `CompileArtifact` (bytecode only, no diagnostics) through a caller-owned `std::optional<CompileArtifact>` slot, which `compile_program()` surfaces via `CompilationOutcome::artifact`. The `merge_diagnostics()` helper standardises diagnostic transfer across all passes.
- The lexer emits structured `Diagnostic` objects via a `DiagnosticCollector` reference instead of throwing `SyntaxError` exceptions. This enables graceful degradation and better error recovery during analysis.
- The VM is stack-based: value stack (max 65,536 entries), call frame stack (max 256 frames), plus a separate exception handler stack for try/catch/finally.
- Memory management uses C++ smart pointers (`shared_ptr` for heap values, `unique_ptr` for ownership). No garbage collector — reference counting via `shared_ptr`.
- Closures use CLox-style capture chains for upvalues with value semantics (copy at closure-creation time, not open upvalues).
- Red-green trees (immutable green + ephemeral red wrappers) are used for incremental updates, primarily by the LSP. Green nodes store widths (not offsets) enabling reuse even when preceding text changes. Inspired by Roslyn (C# compiler) — unchanged subtrees shared between versions, O(log n) per-change re-parsing.
- `ScopeStack<T>` in `core/common/scope_stack.hpp` is a generic template for nested scope management with RAII Guard objects (push on construction, pop on destruction; constrained on `std::movable<T>`). Its sole user is `LinterTracker` (`core/analysis/linter/linter_tracker.hpp`), which stores `ScopeStack<ScopeData>` for flat variable-usage tracking. `NameResolver` (`ResolveScope`) and the type checker (`TypeScope`) deliberately do NOT use it — they need a `shared_ptr`-linked parent chain for cross-scope lookup and `frame_depth` accounting. See the §Component Usage rationale in `scope_stack.hpp` for why these scopes are not unified.

## Module Layout & Key Files

- `core/analysis/` = front-end (lexer, parser, types, linter, resolver, diagnostics, errors, source, ast, pipeline, prelude).
- `core/runtime/` = back-end (compiler, vm, stdlib, repl, concurrency, cli, include, interpreter).
- `core/common/` = shared utilities (overflow, resource_limits, utf8, scope_guard, scope_stack, export, result, path_utils, platform_utils, string_hash, format_number, escape, lru_cache, overloaded, narrow_int).
- `shared/` = cross-tool resources (json, protocol transport, stdlib catalog/symbols/return types).

## Type System

*Canonical guide: [luma.instructions.md](luma.instructions.md). The notes below are a quick-reference summary.*

- Primitives: `boolean`, `integer` (64-bit signed), `number` (IEEE-754 64-bit float), `string` (UTF-8), `none`.
- **Critical distinction**: `integer` is for indices, array subscripts, loop counters, range bounds only. `number` is for counts, measurements, scores, IDs, and all other numeric values. Getting this wrong causes type errors.
- Type promotion: `integer` + `number` → `number`; `integer` widens to `number` implicitly.
- Collections: `array<T>`, `dictionary<V>` (string keys), `set<T>`, `queue<T>`, `stack<T>`, `binarytree<T>`.
- Wrappers: `optional<T>`, `result<T>` (with optional second type param for error type), `reference<T>`, `channel<T>`, `task<T>`.
- Advanced: tuples (2-4 elements), choice types (ADTs with generics), records (with optional `private` fields), interfaces (structural satisfaction), type aliases (fully transparent).
- Match exhaustiveness is enforced: booleans need both cases, choice types need all variants, results need success/failure. Others require `else`.

## Error Handling

- **Domain failures** → `result<T>` with `match`, `??`, `?` (propagation), `!>` (error pipe). This is the primary mechanism.
- **Programmer errors** → runtime errors + `try`/`catch`/`finally`. Division by zero, out-of-bounds, `assert` failure, `Result.unwrap` on failure, `trusted_downcast` mismatch, max recursion.
- **Compile-time errors** → `SyntaxError`, `TypeError` — cannot be caught at runtime.
- Anti-patterns: using `try`/`catch` for expected failures (use `result<T>`), naked `Result.unwrap`, returning sentinel values, silently discarding results, wrapping infallible functions in `result<T>`.
- `downcast<T>(expr)` returns `result<T>` (safe). `trusted_downcast<T>(expr)` throws runtime error on mismatch (unsafe).
- `catch(err)` binding is an immutable `string` containing the error message.

## Luma Language Conventions

*Canonical guide: [luma.instructions.md](luma.instructions.md). The notes below are a quick-reference summary.*

- Every program requires exactly one `@main`-annotated, parameter-less function. REPL and test mode don't require it.
- Variables are immutable by default; use `mutable` keyword for mutability.
- No semicolons required (treated as whitespace). Comments start with `#`.
- Braces always required, even for single-statement bodies.
- Function declarations put the **return type before the name** (always required): `function <return-type> <name>(<params>) { … }` — e.g. `function void main()`, `function boolean is_ready()`. Writing the return type after the parameters as `function f() -> T` is a parse error (`missing return type for function`). The `->` arrow *is* valid Luma, but only in **lambdas** (`(T x) -> expr`) and **function-type annotations** (`function(T) -> R`) — never a declaration's return type. Variables are declared **type-first**: `<type> name = value` or `mutable <type> name = value` (e.g. `number x = 5`); `let`/`var` are *not* keywords (the lexer warns `'let' is not a Luma keyword`). These bite whenever a tool *synthesizes* Luma source (debugger / LSP / REPL / codegen).
- String interpolation: `"value is ${expr}"`. Triple-quoted strings auto-dedent (common leading whitespace stripped).
- Pipe operator: `value |> Module.function()` — left becomes first argument.
- All collection operations return new collections (immutable by default). Build in single pass with `Array.map`/`Array.filter`/`Array.reduce` instead of incremental mutation.
- Naming: `snake_case` for variables/functions/files, `PascalCase` for records/choices/interfaces/namespaces, question form for booleans (`is_valid`, `has_children`).

## Resource Limits

- Max call depth: 256 frames (configurable via `LUMA_LIMIT_MAX_CALL_DEPTH`).
- Max parser expression depth: 128.
- Max collection size: 10,000,000 (arrays, dictionaries, queues, stacks, sets).
- Max string size: 256 MB.
- Max string repeat count: 10,000,000.
- Max regex pattern size: 10,000.
- Max process output size: 64 MB.
- Max task queue: 100,000.
- Max open sockets: 1,000.
- All limits are centralised in `core/common/resource_limits.hpp` and configurable via `LUMA_LIMIT_*` environment variables.

## Concurrency

- `spawn` + `await` for independent tasks. `task_scope` for structured lifetime management (all children complete before parent exits — no orphaned tasks).
- Channels for inter-task communication with deep-copy semantics.
- Cooperative cancellation: `Task.cancel(t)`, `Task.is_cancelled(t)`.
- Each task gets a lightweight per-thread VM instance.
- Type checker warns on bare `spawn` outside `task_scope`.

## Exit Codes

- 0: Success
- 1: RuntimeError
- 2: TypeError
- 3: SyntaxError
- 4: CompileError
- 5: UsageError (CLI argument issues)

## Testing

*Canonical guide: [testing.instructions.md](testing.instructions.md). The notes below are a quick-reference summary.*

- Two layers: C++ unit tests (custom framework in `tests/test_framework.hpp`, namespace `luma::test`) and Luma feature tests (`@test` annotations + `assert()`).
- C++ tests use `RUN(test_name)` to register/run tests and `return SUMMARY()` from `main()`.
- Assertion macros: `ASSERT_EQ`, `ASSERT_NE`, `ASSERT_TRUE`, `ASSERT_FALSE`, `ASSERT_THROWS`, `ASSERT_LT`, `ASSERT_LE`, `ASSERT_GT`, `ASSERT_GE`, `ASSERT_NEAR(a, b, epsilon)`.
- Snapshot testing: `ASSERT_SNAPSHOT(name, actual, __FILE__)` compares output against `.expected` files in `snapshots/` directory. Set `UPDATE_SNAPSHOTS=1` to create/update baselines.
- Test fixtures: derive from `TestFixture`, override `set_up()`/`tear_down()`, use `TEST_F(FixtureClass, test_name)` macro.
- Benchmarks: `BENCHMARK(name, iterations) { ... }` macro with warmup phase and ns/iter reporting.
- RAII helpers: `StdinRedirect` for redirecting `std::cin` in tests.
- Enum values are auto-formatted via `to_printable()` (casts to underlying integer type for assertion messages).
- Luma feature tests live in `tests/features/`, organised into `language/` and `stdlib/` subdirectories. Run via `luma --test file.luma`. **The ctest wrapper adds `--strict`, which promotes lint warnings to errors, but a bare `luma --test file.luma` does not** — so a feature test can pass locally yet fail its `luma_<stem>` ctest. Always validate with `luma --strict --test file.luma` to match CI. Two warnings bite typed-stdlib feature tests specifically: **W0009** (floating-point `==`/`!=`) fires on any `number == <float-literal>` — compare with `Math.approximately_equal(a, b, 0.001)` instead; **W0001** (unused variable) fires on unused pattern/match-arm bindings — prefix them with `_` (e.g. `failure(_e)`, `case Ns.T.Element(_tag, _attrs, kids)`).
- **Headless interactive-input test APIs** make display/TTY-dependent code testable in CI. `Terminal.test_*` (`terminal_testing.cpp`): `test_start(keys)`, `test_feed(keys)`, `test_output()→string`, `test_remaining()→integer`, `test_stop()→string` — keys are names (`"a"`, `"enter"`, `"ctrl+c"`, `"shift+tab"`) or mouse strings (`"mouse:left_press:5:10"`). `GraphicalUi.test_*` mirrors this for GUIs (see the GraphicalUi section). Prefer driving these from `@test` blocks so an example proves behaviour, not just "doesn't crash".
- **Example runner**: `python scripts/run_luma_examples.py` executes every program in `examples/` headlessly — GUI via `LUMA_GUI_HEADLESS=1` (asserts an "initial render OK" marker), Terminal via scripted keys, Console via scripted stdin — and additionally runs any `@test` blocks (without `--strict`, so lenient on W0010 but still clean). An example with no `@test` block only proves it doesn't crash. `run_luma_examples.py` discovers every `.luma` under `examples/` via `rglob`; current suite ≈ 117 examples (≈ 116 pass + 1 skip — `multi_file_utils.luma`, an include-only helper with no `@main`).
- 30 fuzz targets in `fuzz/` (authoritative list in `fuzz/CMakeLists.txt`) spanning the analysis/runtime pipeline (lexer → VM), stdlib parsers (JSON, CSV, XML, regex, compression, …), include resolver, protocol transport, and the bytecode deserializer (with round-trip oracles), using LibFuzzer with a shared harness and seed corpus.
- **Fuzzing is authoritative on Linux/clang only.** On Windows a clang-cl fuzz exe that exits `0xC0000409` (STATUS_STACK_BUFFER_OVERRUN — a `/GS` `__fastfail`, distinct from a *real* stack overflow's `0xC00000FD`) with **no crash artifact written** is a clang-cl SanitizerCoverage + MSVC hardening (`/GS /sdl /guard:cf`) codegen artifact, **not** a Luma bug — libFuzzer cannot intercept `__fastfail`. On Windows, validate that seeds load with `-runs=0`; run real campaigns on Linux/clang (or a Linux container) as CI does. The `fuzz_protocol` target has a separate, documented trap caveat (`fuzz/DIRECTORY.md`).
- All tests must pass before committing.

## REPL

- `luma` (no args) enters REPL. Also `--repl`/`-r`.
- Type checking is skipped in REPL (errors caught at runtime).
- Persistent environment across lines. Variables and functions can be redefined.
- Commands: `:quit`/`:q`, `:help`/`:h`, `:clear`/`:c`, `:file <path>`/`:f`.
- Multi-line input triggered by unmatched `{`. Tab completion for keywords, stdlib modules, locals, REPL commands.
- History up to 1,000 lines (deduped). Line editing with readline-style shortcuts.

## Non-Obvious Patterns

- Semicolons are tokenized but treated as whitespace — they're neither required nor forbidden.
- **The interpolation nesting-depth limit must be enforced *before* emitting `InterpolationStart`**: `Lexer::begin_interpolation` (`lexer_string.cpp`) checks `check_interpolation_depth()` before consuming `${` and pushing onto `interpolation_state_`. If the token were emitted first, an over-limit level would leave a start token with no matching state entry, and a later `}` would then close an *outer* level prematurely, desynchronising the token stream from the state stack. On over-limit it returns with the cursor still on `$`, so the main loop re-scans `${` as ordinary operators — the `{` still increments the enclosing level's `brace_depth`, which its `}` decrements, keeping outer levels balanced during error recovery. Pinned by `tests/analysis/lexer_test.cpp`.
- Annotations (`@main`, `@test`) must be on the line immediately before the function declaration.
- Private record fields require the `trusted` keyword on function parameters for access. Access is gated by checking if the base object's variable name is in `current_trusted_params_`.
- Include paths resolve relative to the including file's directory (not the working directory, except in REPL). `LUMA_PATH` provides additional search directories. Circular includes, path traversal (`..`), and symlinks in include paths are all detected and rejected (security).
- The type checker collects errors as `vector<Diagnostic>` data — it doesn't throw exceptions. The caller decides whether to halt.
- **A pipe shifts explicit user-function arguments by one during type and ownership checking**: in `a |> f(b, c)` the piped `a` fills parameter 0, so `ExpressionTypeChecker::check_user_function_args` and `check_call_ownership` (`expression_type_checker_calls.cpp`) match explicit arguments starting at parameter index 1 via `param_offset = tc_.context().is_in_pipe ? 1 : 0` — mirroring the stdlib and generic-function checks (the piped value's own ownership is checked separately in `check_pipe_first_parameter`). Omitting the offset mis-reported argument types and ownership for every piped call into a user-defined function; Solaris's heavy fluent-modifier pipe chains surfaced it.
- **TokenType enum**: 140+ token types with type keywords as distinct tokens (not string keywords). `token_type_to_string()` uses constexpr array lookup (replaces switch). Token literals are pre-computed (`TokenLiteral = std::variant<monostate, int64_t, double, bool, string>`) to avoid re-parsing.
- **RecursionGuard** (`core/runtime/interpreter/value.cpp`): RAII class with thread-local depth counters (`to_string_depth`, `equals_depth`, `deep_copy_depth`) to prevent stack overflow on deeply nested recursive ADTs. Max depth is 64. Each operation maintains an independent counter to catch mutual recursion.
- **Value equality uses value semantics**: `Value::equals()` compares by value, not reference. Deduplication in `Array.unique()` uses `Value::equals()`. Closures compare by reference; records compare by type name + all fields; choice types compare by variant + payload. Equality logic is decomposed into named helpers: `equals_numeric()` (handles cross-numeric promotion), `equals_structural()` (choice, record, result, range, reference), and `elements_equal()` (template for container element comparison using ranges).
- **Clamping vs throwing**: Prefer clamping negative values to 0 over throwing for computed-length parameters (e.g., `String.truncate` clamps negative `max_length` to 0). Similar to Rust's `saturating_*` operations — more forgiving for beginners.

## Cross-Cutting Design Patterns

- **Composition over monoliths**: Large classes (VM, Compiler, TypeChecker, LspServer, DebugSession) decomposed into focused components that hold non-owning references to parents. Friendship access maintains encapsulation at module boundaries.
- **Builder/Fluent API**: Used in JsonBuilder (`set → set_if → build`), ArrayBuilder (`add → add_if → build`), ModuleBuilder (`func → constant → native`), Pipeline (`add → add → build`). Methods return `*this` or parent reference for chaining. `build()` is ref-qualified: lvalue copies (builder reusable), rvalue moves (efficient for temporaries).
- **Callback decoupling**: ExpressionEvaluator uses `RefAllocator` callback to decouple from VariableInspector. BreakpointManager uses `ConditionEvaluator` callback. AnalysisService uses callbacks for logging and notifications. Enables modular testing and dependency injection.
- **Lock ordering documentation**: DebugSession documents explicit mutex acquisition order to prevent deadlocks. All code paths follow the documented order. RAII helpers enforce ordering.
- **Safe narrowing**: `narrow_int(int64_t)` throws on overflow (ES.46 compliant), defined in `core/common/narrow_int.hpp` and re-exported by `json_helpers.hpp`. `clamp_to_int(int64_t)` clamps to [0, INT_MAX] for non-negative protocol fields. Null-safe field extraction with defaults throughout DAP helpers.
- **Naming conventions for lookups**: `find_*` methods return `std::optional` or `const` pointer (nullable). `is_*`/`has_*` methods return bool. `*_range`/`*_bounds` return Range or `pair<int, int>`.
- **RAII Guard pattern**: `ScopeStack<T>::Guard` auto-pushes on construction and pops on destruction. Standardized `ScopeGuard<Func>` in `core/common/scope_guard.hpp` for generic cleanup. Used wherever scope depth must track control flow exactly.
- **Query object pattern**: AnalysisQuery provides read-only accessors over analysis results, decoupling consumers from internal data structures.
- **Platform abstraction pattern**: Platform-specific code uses conditional declarations in headers (`#ifdef _WIN32` / `#else`) with separate `*_posix.cpp` / `*_win32.cpp` implementation files. Shared inline utilities wrap OS-specific APIs (e.g., `platform_socket.hpp` provides unified `close()`, `last_error()`, `set_non_blocking()`, `set_timeout()`; `platform_utils.hpp` provides `wstring_to_utf8()` and `safe_getenv()`). Use for sockets, terminal I/O, and environment access where implementations differ significantly.
- **Thread-safe caching**: Mutex-guarded caches with size caps (e.g., ReDoS validation cache: 1024 patterns; compiled-regex cache: 256 entries; compilation cache: 128 entries; semantic token cache). Check-then-validate-then-insert pattern with minimal lock scope for hot paths. Atomic globals (`std::atomic<T>`) for lightweight runtime-configurable values (e.g., `escape_timeout_ms`).
- **Centralised dispatch**: AST dispatch (`ast_dispatch.hpp`) and opcode dispatch (`vm_dispatch_table.cpp`) each map kind/opcode to handler in a single location, enforced by concepts or static_assert.
- **Role interfaces (Interface Segregation)**: Fat back-reference interfaces are decomposed into focused role interfaces so a collaborator depends only on the slice it uses and can be mocked in isolation. The compiler's `ICompilationBackend` is composed from `i_bytecode_emitter.hpp`, `i_scope_lifecycle.hpp`, `i_variable_manager.hpp`, `i_sub_compiler.hpp`, `i_diagnostic_sink.hpp`, and `i_context_access.hpp`; the type checker's `TypeCheckingServices` inherits a set of roles defined in `type_checking_services_roles.hpp`. The aggregate interface still inherits all roles (and the production class remains the sole implementor), so existing code that depends on the whole surface is unaffected.

## C++ Coding Style

*Canonical guide: [cpp.instructions.md](cpp.instructions.md). The notes below are a quick-reference summary.*

- `snake_case` for variables, functions, namespaces, files. `PascalCase` for types/classes/structs. 4-space indentation. Always use braces.
- West-const (`const int`, not `int const`). `const` by default, `constexpr` where possible.
- RAII for all resource management. `std::unique_ptr` for ownership, `std::shared_ptr` for shared ownership.
- Exceptions for errors, `std::optional` for expected absence.
- `[[nodiscard]]` on functions whose return values must not be ignored; to intentionally discard such a result, prefix the call with `(void)` (silences MSVC C4834 — follow the `(void)eval(...)` convention in `tests/shared_eval.hpp` / `tests/runtime/stdlib_test_helpers.hpp`). `explicit` on single-argument constructors.
- Prefer algorithms and range-based for loops over index-based iteration.
- No third-party runtime dependencies except when functionality truly cannot be achieved with the standard library and OS APIs.
- Comments explain **why**, not **what**. Code should be self-documenting.

## Performance

- Lexer: single-pass, O(n) in source length.
- Parser: recursive descent, O(n) tokens.
- Type checker: single AST walk, O(n) nodes.
- Compiler: single pass, O(n) AST nodes.
- VM: O(1) per opcode dispatch via function pointer table (replaces switch). Variable lookup: O(1) locals (stack slot), O(1) globals (inline cache via VMGlobalCache, exploiting pointer stability).
- All collection operations return new copies (O(n)). Incremental string concatenation is O(n²) — use `Array.map` + `String.join` instead.
- The `LinkedList`, `HashSet`, and `Graph` stdlib modules (and their `linked_list`/`hash_set`/`graph` value kinds and type-identifiers) were **removed** to streamline the language: `LinkedList` offered no advantage over `Array` under value/deep-copy semantics; `HashSet` duplicated `Set` (use `Set` — its members compare by structural equality and it needs no hashable-primitive restriction); `Graph` was CS-course surface beyond the beginner audience. Removal spanned the value system (`CollectionKind`/`ValueType`), lexer type-identifiers, stdlib registration, catalog, type checker, LSP hover/keyword catalogs, and the DAP variable inspector/expander.

## Hash & Equality Invariants

- **ValueHash must respect ValueEqual**: Two arrays with equal content are `ValueEqual`, so deep hashing must be content-based, NOT pointer-based. Pointer-based hashing breaks the hash contract. Initial `ValueHash` only hashed the type tag → all arrays/dicts of the same type collided → O(n) lookups. Fix: structural deep-hashing with a depth limit (≤8 levels via `k_max_hash_depth`).
- **Integer/number cross-type hash**: Integers and numbers that compare equal (e.g., `3` and `3.0`) must produce the same hash. Fixed by hashing integers as doubles when they fit without precision loss.
- **Dictionary hash must be order-independent**: Uses XOR combination of key-value pair hashes.

## Refactoring Process Learnings

- **Sub-agent reports can be over-optimistic**: "DONE" reports from sub-agents should be verified. Some items were only superficially touched (comments added, but actual logic not changed). Sub-agents also sometimes apply fixes to wrong files. Always audit sub-agent work.
- **Todo lists may be incomplete**: When decomposing a large refactoring report into SQL todos, ensure all findings are captured. In one session, 20 SQL todos only covered ~1/3 of 60+ report findings.
- **Pre-existing test baselines**: Before refactoring, capture a fresh `ctest` baseline (exact test count + any known failures) so regressions stay distinguishable — don't trust a hardcoded number, the suite count drifts as tests are added. The graphicalui suites (`stdlib_test_graphicalui`, `luma_graphicalui_functions`) need a display and `dap_integration_test` needs loopback networking, so those may be skipped or fail under headless/offline CI.
- **File move strategy**: Create new file at destination, convert old file to thin deprecated redirect (`#include` to new location) — ensures backward compatibility.
