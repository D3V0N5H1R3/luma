---
description: "Analyse the project read-only and produce a prioritized, actionable list of refactoring opportunities — without changing any code"
agent: "agent"
tools: ["search", "read"]
argument-hint: "Optional scope, e.g. 'core/runtime/vm/' or 'the whole project'"
---

# Refactor Audit

Survey the codebase — the interpreter core, the standard library (`core/runtime/stdlib/`), the language server (`luma_lsp`), the debugger (`luma_dap`), the shared libraries (`shared/`), or the editor extensions (`extensions/`) — and produce a **prioritized list of refactoring opportunities**. This prompt is the discovery counterpart to [refactor.prompt.md](refactor.prompt.md): this one *finds and ranks* candidate refactorings; that one *executes* a single chosen item end-to-end with the test suite green.

This is a **read-only analysis**. Make no code changes, and no build is required. The deliverable is a report, not a diff.

> **Scope vs sibling prompts:** Stay structural. [code-review.prompt.md](code-review.prompt.md) finds bugs, security, and performance defects; [consistency-check.prompt.md](consistency-check.prompt.md) finds drift between artefacts (code vs CMake, runtime vs type checker, docs vs implementation). This prompt finds **behaviour-preserving structural improvements** — duplication, oversized units, mixed responsibilities, tight coupling, reinvented utilities. When a candidate is really a bug, a security issue, or a consistency gap, note it briefly and cross-reference the right prompt rather than restating it here.

## 1 — Understand the Intended Design

Before judging what *should* be refactored, learn the boundaries the code is meant to respect, so you flag genuine deviations rather than deliberate design:

- [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) — module boundaries, the one-directional pipeline, and the composition-over-monoliths design the codebase already follows.
- [software-architecture.instructions.md](../../instructions/software-architecture.instructions.md) — simplicity, modularity, separation of concerns, encapsulation, and coupling principles. This is the rubric for most findings.
- The per-language style guide for whatever you audit: [cpp.instructions.md](../../instructions/cpp.instructions.md) (first-party C++), [typescript.instructions.md](../../instructions/typescript.instructions.md) (VS Code), [rust.instructions.md](../../instructions/rust.instructions.md) (Zed), [javascript.instructions.md](../../instructions/javascript.instructions.md) and [css.instructions.md](../../instructions/css.instructions.md) (GraphicalUi front-end), [python.instructions.md](../../instructions/python.instructions.md), [shell.instructions.md](../../instructions/shell.instructions.md), and [powershell.instructions.md](../../instructions/powershell.instructions.md) (`scripts/`), and [cmake.instructions.md](../../instructions/cmake.instructions.md) (build files).
- [learnings.instructions.md](../../instructions/learnings.instructions.md) — established patterns and, crucially, the **intentional decisions that look like smells but are deliberate** (see §6).

If the audit targets a specific subsystem, also skim its design doc to learn the behaviour that must be preserved: [Luma_User_Manual.md](../../documents/Luma_User_Manual.md) (lexer→VM semantics), [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md) and [Luma_GraphicalUi_Guide.md](../../documents/Luma_GraphicalUi_Guide.md) (stdlib), [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md) (failure conventions), [Luma_Language_Server.md](../../documents/Luma_Language_Server.md), [Luma_Debugger.md](../../documents/Luma_Debugger.md), [Luma_REPL_Guide.md](../../documents/Luma_REPL_Guide.md), or [Luma_Syntax_Highlighting.md](../../documents/Luma_Syntax_Highlighting.md).

## 2 — Scope and Ground Rules

- **Default scope** is the whole first-party tree. If the invocation names a directory or file, restrict the audit to it (and its immediate collaborators).
- **In scope:** first-party sources under `core/`, `shared/`, `language-server/`, `debugger/`, `tests/`, `fuzz/`, `scripts/`, `extensions/` (excluding generated per-editor copies), the GraphicalUi front-end under `external/gui-framework/`, and the CMake build system (per-directory `CMakeLists.txt`, the modules under `cmake/`, and the root `CMakeLists.txt` / `CMakePresets.json`).
- **Out of scope:** vendored code (`external/` except `external/gui-framework/`), generated code, and build outputs (`build/`, `build-fuzz/`). Do not propose refactorings there.
- **Verify every location.** Read each file you cite — never report a smell or line range you have not confirmed in the source. A hallucinated location wastes the executor's time.
- **Make no changes.** Do not edit, format, build, or run tests. Producing the ranked list is the whole job.

## 3 — What to Look For

Hunt for these structural smells. Each maps to a refactoring the project already has precedent for. The categories are language-agnostic and apply across every in-scope language; the precedents cited are predominantly from the C++ core because it is the bulk of the codebase, so when auditing the TypeScript, Rust, JavaScript/CSS, or Python surfaces, apply the equivalent idiom in that language rather than expecting a C++ helper.

1. **Oversized units / monoliths.** Files or functions that have grown too large or mix several concerns. The codebase deliberately decomposes large classes (VM, Compiler, TypeChecker, LspServer, DebugSession) into focused components holding non-owning references to their parent — flag any remaining God class, overlong function, or file that bundles unrelated responsibilities, and propose the same composition pattern.
2. **Duplication.** Copy-pasted or near-identical logic that should become one shared helper, template, or policy. The project already folds such cases into `ContainerOps<Container>`, the escape policies (`escape_string_impl<Policy>`), `ErrorMessages`, and the `ModuleBuilder` DSL — point new duplication at the existing mechanism instead of a new bespoke one.
3. **Mixed responsibilities / weak cohesion.** A unit that parses *and* does I/O *and* formats, or a header that owns several unrelated concepts. Propose extracting a focused type with a single, well-defined responsibility (e.g. the way `SandboxPolicy`, `LazyLoader`, `LexerCursor`, `ConstantPool`, and `SourceMap`/`NameTable` were split out behind facades).
4. **Tight coupling / fat interfaces.** A collaborator that depends on a wide back-reference when it uses only a slice of it. The precedent is Interface Segregation into role interfaces — `ICompilationBackend` composed from `i_bytecode_emitter` / `i_scope_lifecycle` / `i_variable_manager` / …, and `TypeCheckingServices` from its role set. Flag hidden dependencies that block unit testing and propose a narrow seam (callback or role interface) for injection.
5. **Reinvented utilities.** Code that re-implements something already in `core/common/` or `shared/`. Common cases: ad-hoc string-keyed maps instead of `StringMap`/`StringHash` (heterogeneous lookup), a bespoke LRU instead of `LruCache`, manual escaping instead of an escape policy, hand-rolled narrowing instead of `narrow_int`/`clamp_to_int`, manual scope-depth tracking instead of `ScopeStack<T>`, ad-hoc cleanup instead of `ScopeGuard`, hand-written error strings instead of `ErrorMessages` (+ `error_codes`), a one-off result type instead of `Result<T, E>`, or a local method→handler switch instead of `HandlerRegistry`.
6. **Primitive obsession / boolean traps.** Bare `bool`/`int` parameters that invite misuse, or adjacent same-type arguments that are easy to swap. Propose a small enum or struct that encodes intent and makes the call site self-documenting.
7. **Excess complexity.** Deep nesting, long conditional chains, or repeated branching that should flatten into early returns, guard clauses, or a dispatch table (the project drives opcode and AST dispatch through centralized tables in `vm_dispatch_table.cpp` and `ast_dispatch.hpp`).
8. **Scattered magic numbers.** Literals that should be named and centralized — runtime limits belong in `resource_limits.hpp`, compiler/optimizer limits in `compiler_limits.hpp`. Flag duplicated or unexplained constants.
9. **Dead, stale, or drifted code.** Unused functions, unreachable branches, redundant or misleading comments, deprecated thin-redirect headers that no longer have callers, and parallel copies of a concept that have diverged.
10. **Vocabulary drift.** The same concept named differently across files. Propose aligning on one term so new code follows established naming.
11. **Platform `#ifdef` sprawl.** Conditional-compilation tangles that should follow the platform-abstraction pattern (`*_posix.cpp` / `*_win32.cpp` implementations behind a shared inline wrapper, as `platform_socket.hpp` and `platform_utils.hpp` do).

## 4 — How to Gather Evidence

Use the workspace search and file-reading tools first; the snippets below are a fallback for quick metrics. Parallelize independent read-only exploration where it helps. Do **not** build.

- **Largest source files** (decomposition candidates) — the snippets below scan the C++ surface; swap the directories and globs (`*.ts`, `*.rs`, `*.py`, `*.js`, `*.css`) to size up the other in-scope languages:

    ```bash
    # Linux/macOS
    find core shared language-server debugger tests fuzz \
      \( -name '*.cpp' -o -name '*.hpp' \) \
      | xargs wc -l | sort -rn | head -40
    ```

    ```powershell
    # Windows
    Get-ChildItem -Recurse core, shared, language-server, debugger, tests, fuzz -Include *.cpp, *.hpp |
      ForEach-Object { [PSCustomObject]@{ Lines = (Get-Content $_.FullName).Count; Path = $_.FullName } } |
      Sort-Object Lines -Descending | Select-Object -First 40
    ```

- **Markers and intent signals:** search for `TODO`, `FIXME`, `HACK`, `XXX`, `deprecated`, and `NOLINT` — they often flag known structural debt or temporary shims worth retiring.
- **Long functions and deep nesting:** scan the largest files for functions that span many screens or nest beyond three or four levels.
- **Duplication:** search for repeated literal blocks, near-identical helper bodies, and the same constant defined in more than one place. A semantic/code search across the subsystem surfaces near-duplicates that an exact-text search misses.
- **Reinvented utilities:** grep for the smell signatures — e.g. `std::unordered_map<std::string,` (candidate `StringMap`), hand-written `static_cast<int>` after a range check (candidate `narrow_int`), bespoke `try { ... } catch` cleanup (candidate `ScopeGuard`), local lexer/parser of an already-shared format.

## 5 — Prioritize

Rank every candidate so the executor can pick the highest-value item first. Weigh:

- **Benefit** — how much duplication is removed, coupling cut, or responsibility clarified; how many call sites or future changes it simplifies.
- **Risk** — blast radius and how mechanical vs semantic the change is. A wide-reaching change to a hot path (VM dispatch, type checker) is higher risk than a localized extraction.
- **Test safety net** — refactoring is only safe behind green tests. A candidate with strong existing coverage is lower risk; one with little coverage should list "add characterization tests first" as a prerequisite.
- **Effort** — rough size (small / medium / large), independent of benefit.

Synthesize these four into the single **Priority** rating (High / Medium / Low) the report ranks by — Priority rises with benefit and falls with risk and effort. Favour high-benefit, low-risk, well-covered items at the top. Where a high-benefit candidate is under-tested, treat "add characterization tests first" as the prerequisite that buys down its risk rather than a reason to drop it down the list.

## 6 — What to Exclude

- **No behaviour changes.** A refactoring preserves observable behaviour. Anything that fixes a bug, changes an API contract, or adds a feature belongs in [bug-fix.prompt.md](bug-fix.prompt.md) or [new-language-feature.prompt.md](new-language-feature.prompt.md) — note it and move on.
- **No over-engineering.** Per the project's implementation discipline, a refactoring must pay for itself in reduced duplication, clearer responsibility, or removed coupling. Do not propose abstractions, helpers, or generality for one-time operations or speculative future needs.
- **No cosmetic churn.** Formatting and lint-only changes are handled by [source-code-cleanup.prompt.md](source-code-cleanup.prompt.md) and [lint-and-format.prompt.md](lint-and-format.prompt.md); do not list them here.
- **Respect deliberate decisions that look like smells.** Several are documented in [learnings.instructions.md](../../instructions/learnings.instructions.md) and must **not** be proposed as refactorings:
    - The two optimizer passes removed as unsound at the bytecode level (constant propagation and multiply→shift strength reduction) — do **not** suggest re-adding them.
    - Deliberate space/time trade-offs in data-structure design: the LSP function-body ranges held **twice** (a map plus a sorted vector) for O(log n) enclosing-function lookup, and the `Chunk` source map kept **sparse** (one entry per opcode, binary-searched) to save ~60% memory. Do not flag either the duplication or the sparseness as redundant storage or needless complexity to "simplify".
    - The VS Code TextMate grammar being hand-maintained by design rather than unified with the tree-sitter grammar.
    - Deprecated thin-redirect files kept deliberately for backward compatibility after a move — only flag one when it genuinely has no remaining callers.

## 7 — Output Format

Produce the report in two parts.

First, a summary table ordered by priority for quick scanning:

```markdown
| ID  | Refactoring                          | Category        | Priority | Effort | Risk |
| --- | ------------------------------------ | --------------- | -------- | ------ | ---- |
| R01 | Split `foo.cpp` God class            | Oversized unit  | High     | Large  | Med  |
| R02 | Fold duplicate escape loop into …    | Duplication     | High     | Small  | Low  |
| R03 | …                                    | …               | …        | …      | …    |
```

Then, one detailed entry per candidate:

```markdown
### R01 — <Short, action-oriented title>

- **Category:** <one of the §3 categories>
- **Priority:** <High | Medium | Low>
- **Location:** `path/to/file.cpp` (lines A–B), `path/to/other.hpp` (lines C–D)
- **Smell:** <what is wrong and why it costs the project — coupling, duplication, risk, readability>
- **Proposed refactoring:** <high-level approach, naming the established project pattern to apply>
- **Effort / risk:** <Small | Medium | Large> effort, <Low | Medium | High> risk
- **Test safety net:** <which tests cover this today; note "add characterization tests first" if coverage is thin>
- **Handoff goal:** "<one-line goal string ready to paste into refactor.prompt.md>"
```

Close with a short note on what you would tackle first and why (highest benefit-to-risk). Make each **Handoff goal** specific enough that [refactor.prompt.md](refactor.prompt.md) can act on it without re-discovering the problem.
