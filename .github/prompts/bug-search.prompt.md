---
description: "Analyse the Luma interpreter and standard library read-only and produce a prioritized, actionable list of suspected bugs — without changing any code"
agent: "agent"
tools: ["search", "read"]
argument-hint: "Optional scope, e.g. 'core/runtime/vm/' or 'the whole interpreter'"
version: 1
lastUpdated: "2026-08-01"
---

# Bug Search

Survey the Luma interpreter core (lexer → VM) and the standard library (`core/runtime/stdlib/`) and produce a **prioritized list of suspected bugs**. This prompt is the discovery counterpart to [bug-fix.prompt.md](bug-fix.prompt.md): this one *finds and ranks* candidate defects; that one *reproduces, root-causes, and fixes* a single chosen item with a regression test and the suite green.

This is a **read-only hunt**. Make no code changes, and no build is required — confirming each candidate with a live repro is the first step of [bug-fix.prompt.md](bug-fix.prompt.md), which is why every finding carries a **confidence** rating. The deliverable is a ranked report, not a diff.

> **Scope vs sibling prompts:** This hunt covers the interpreter core and the standard library. If the fault is in another subsystem, use the matching hunt instead: [bug-search-language-server.prompt.md](bug-search-language-server.prompt.md) for the language server (`luma_lsp`), [bug-search-debugger.prompt.md](bug-search-debugger.prompt.md) for the debugger (`luma_dap`), or [bug-search-editor-extension.prompt.md](bug-search-editor-extension.prompt.md) for the editor extensions. Stay on defects. [code-review.prompt.md](code-review.prompt.md) is a *deep, file-scoped* review that also weighs style, readability, and maintainability; this prompt is a *wide, subsystem-scoped* hunt tuned to feed [bug-fix.prompt.md](bug-fix.prompt.md). [refactor-audit.prompt.md](refactor-audit.prompt.md) finds behaviour-preserving structural improvements, and [consistency-check.prompt.md](consistency-check.prompt.md) finds drift between artefacts (code vs CMake, runtime vs type checker, docs vs implementation) — when a candidate is really a smell, a style nit, or a consistency gap, note it briefly and cross-reference the right prompt rather than restating it here.

## 1 — Understand the Intended Behaviour

Before judging what looks wrong, learn the behaviour the code is meant to have, so you flag genuine defects rather than deliberate design:

- [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) — especially §6 (Processing Pipeline) and §7 (Bytecode Compiler and Virtual Machine Internals): the one-directional pipeline and the phase contracts a bug must have violated.
- [Luma_User_Manual.md](../../documents/Luma_User_Manual.md) — the language semantics (lexer→VM) that any correct behaviour, and therefore any fix, must preserve.
- [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md) — the stdlib contracts: signatures, return types, and documented error behaviour a built-in may deviate from.
- [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md) — the error categories, exit codes, and `result`/`optional` conventions a bug may break.
- [learnings.instructions.md](../../instructions/learnings.instructions.md) — the known pitfalls (see **C++ Pitfalls Discovered**, **Hash & Equality Invariants**, **Non-Obvious Patterns**) that make excellent bug signatures, and crucially the **deliberate decisions that look like bugs but are not** (see §6).
- [cpp.instructions.md](../../instructions/cpp.instructions.md) — the C++ idioms whose violation is frequently the defect.

## 2 — Scope and Ground Rules

- **Default scope** is the whole interpreter: `core/analysis/` (lexer, parser, resolver, types, linter, diagnostics, source) and `core/runtime/` (include, compiler, optimizer, vm, interpreter, stdlib, concurrency, repl, cli), plus the shared utilities in `core/common/`. If the invocation names a directory or file, restrict the hunt to it and its immediate collaborators.
- **In scope, even when it surfaces elsewhere:** the language server and debugger reuse this front-end, so a defect that first shows up in the LSP or DAP but roots in the lexer, parser, type checker, compiler, VM, or a `Value` *is* this prompt's territory.
- **Out of scope:** the language server's own code (`language-server/`), the debugger's own code (`debugger/`), the editor extensions (`extensions/`), vendored code (`external/`), generated code, and build outputs (`build/`, `build-fuzz/`). Point findings there at the matching hunt.
- **Verify every location.** Read each file you cite — never report a defect or line range you have not confirmed in the source. A hallucinated location wastes the fixer's time.
- **Make no changes.** Do not edit, format, build, or run tests. Producing the ranked list is the whole job.

## 3 — What to Look For

Hunt for these defect classes. The pipeline-phase list mirrors [bug-fix.prompt.md](bug-fix.prompt.md)'s isolation steps, so a finding drops straight into the fix workflow.

1. **Pipeline-phase correctness.** Trace a construct through the pipeline and find the phase that mishandles it:
    - **Lexer** (`core/analysis/lexer/`) — wrong tokenisation, especially string interpolation (`StringStart` / `InterpolationStart` / `StringEnd` sequences), numeric and escape literals, and multi-byte UTF-8 column widths.
    - **Parser** (`core/analysis/parser/`) — malformed AST, or a **recursive branch missing its `make_recursion_guard`** (every recursing branch needs one; recursing lookahead helpers need an explicit depth bound, e.g. `skip_type_at`). Missing guards surface under fuzzing as native stack overflows.
    - **Include resolver** (`core/runtime/include/`) — wrong relative resolution, missing deduplication, or a bypassed security check (circular include, `..` traversal, symlink).
    - **Type checker** (`core/analysis/types/`) — a wrong or missing type error. It collects `vector<Diagnostic>` and never throws, so a phase here that throws or drops a diagnostic is suspect.
    - **Linter** (`core/analysis/linter/`) — a false-positive or false-negative warning.
    - **Compiler** (`core/runtime/compiler/`) — wrong bytecode or variable→slot resolution. In particular the **value-block scratch-slot invariant**: any path that leaves operand-stack temporaries live *before* compiling a locals-declaring sub-expression (a `match`/`if` used as a value, a `container[index] = …` target, prior call args, a binary op's left operand, dict keys, pipe stages, interpolation parts) must bracket it with `reserve_scratch_slots` / `release_scratch_slots`, or `GetLocal`/`SetLocal` land on the wrong stack slots and corrupt state.
    - **Optimizer** (`core/runtime/compiler/optimizer.*`) — an unsound transform or value-block stack-slot corruption; the optimizer sees only a `Chunk` (no local scopes or liveness), so any pass that assumes them is wrong.
    - **VM** (`core/runtime/vm/`) — wrong dispatch, a stack-depth imbalance, or wrong opcode semantics.
    - **Standard library** (`core/runtime/stdlib/`) — wrong built-in behaviour, a signature that disagrees with the shared catalog, or the wrong error style.
2. **Edge cases and boundaries.** Empty input, off-by-one, integer overflow, boundary values, negative computed lengths (the project **clamps** — e.g. `String.truncate` clamps to 0 — so a *throw* there is the bug), and floating-point pitfalls (double equality, NaN / ±0.0 / infinity, `integer` vs `number` confusion, a whole `number` intended to print with a trailing `.0`).
3. **Error handling and results.** A dropped `[[nodiscard]]`, an ignored `result<T>` / `optional` / error code, the wrong exit code (0 success, 1 runtime, 2 type, 3 syntax, 4 compile, 5 usage) or wrong error category for a failure, an infallible function returning `result<T>` (or a fallible one returning a plain value) against [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md), and a `catch (...)` that swallows a real fault.
4. **Resource and memory safety.** A dangling `std::string_view` into a moved or reallocated `std::vector<std::string>` (a documented pitfall — constant-pool and param-name keys), use-after-move, missing RAII, leaks, unbounded recursion on nested ADTs (guarded by the `RecursionGuard` depth counters in `value.cpp`), and an unbounded `reserve(count)` driven by an untrusted count.
5. **Concurrency.** Data races on shared VM or channel state, an atomic with too-weak ordering (a hook/pause flag that should be release/acquire), `notify_one()` called while still holding the lock, `task_scope` lifetime violations (orphaned children outliving the scope), and channel deep-copy assumptions.
6. **Security.** Include-path traversal or symlink escape, unsafe `.lumc` bytecode deserialization (validate every count/offset/index and **bounds each `reserve` against the remaining input before reserving**), ReDoS in regex handling, and sandbox bypass — an OS-touching path (Console, Csv, FileSystem, Http, KeyValueStore, Process, Socket, Xml, or a file-I/O helper) missing its `if (!sandbox)` guard.
7. **Hash and equality invariants.** `ValueHash` must respect `ValueEqual` (content-based deep hashing with a depth limit, never pointer-based); equal `integer` and `number` (`3` and `3.0`) must hash equal; dictionary hashing must be order-independent. A violation shows up as broken deduplication or O(n) lookups.
8. **Cross-platform latent bugs.** Code that compiles under MSVC but breaks Linux/clang: a missing `NOMINMAX` before a Windows header, a constructor member-initializer list out of declaration order (`-Wreorder`), the float `NAN` macro where `std::numeric_limits<double>::quiet_NaN()` is needed (`-Wdouble-promotion`), LP64/LLP64 literal ambiguity (`42LL` vs `static_cast<std::int64_t>(42)`), or a missing `<cstdint>` / `<utility>` include.
9. **Stale or inverted logic.** Unreachable branches, inverted conditions, copy-paste errors (the wrong variable or bound used), and a comment that contradicts the code — one of the two is the bug.

## 4 — How to Gather Evidence

Use the workspace search and file-reading tools; parallelize independent read-only exploration. Do **not** build or run.

- **Trace an input.** Read the phase source and walk a representative program through it; the defect usually sits where your mental model and the code diverge.
- **Search for the smell signatures** in §3: a `string_view` field keyed into a `std::vector<std::string>`; a `static_cast<int>` or other narrowing with no range check (candidate `narrow_int` / `clamp_to_int`); a `reserve(` fed by a deserialized or parsed count; `memory_order_relaxed` on a hook or pause flag; a `parse_*` branch in `core/analysis/parser/` with no `make_recursion_guard`; a `catch (...)` that drops the error.
- **Grep intent markers** — `TODO`, `FIXME`, `HACK`, `BUG`, `XXX` — they frequently annotate known-fragile code.
- **Read the tests and fuzz targets** for the phase (`tests/`, `fuzz/`). A construct with thin coverage is where latent bugs survive, and a missing fuzz target for a parser or trust-boundary stdlib parser is itself a signal — record the coverage gap in the finding.

## 5 — Prioritize

Rank every candidate so the fixer picks the highest-value item first. Rate each on:

- **Severity** — impact if the defect is real. **Critical**: crash, memory corruption, deadlock, security or sandbox bypass, silent miscompilation, or data loss. **High**: wrong result on common input, or a resource-exhaustion denial of service. **Medium**: wrong behaviour on an edge case, or a wrong error category/message. **Low**: cosmetic, or a narrow case with an easy workaround.
- **Confidence** — how sure it is a genuine bug given you did not run it. **High**: mechanism traced end-to-end and a triggering input is describable. **Medium**: clearly suspect, but the trigger path needs confirmation. **Low**: a plausible smell that needs the fix step's live repro — say so.
- **Reachability** — whether it sits on a hot path or is reachable from ordinary user input or untrusted input (source files, `.lumc`, stdlib parser input). Higher reachability raises priority.
- **Effort** — rough fix size (Small / Medium / Large), independent of severity.

Rank by severity weighted by confidence, tie-broken by reachability then effort — a Critical or High finding with High confidence goes to the top. A high-severity, low-confidence item still belongs on the list, with "confirm the repro first" recorded as the fixer's prerequisite rather than a reason to drop it.

## 6 — What to Exclude

- **No fixes.** Reproducing and fixing is [bug-fix.prompt.md](bug-fix.prompt.md)'s job — do not edit code. The ranked report is the whole deliverable.
- **Not structural smells, style, or drift.** Behaviour-preserving structure belongs in [refactor-audit.prompt.md](refactor-audit.prompt.md); formatting and style in [code-review.prompt.md](code-review.prompt.md) / [lint-and-format.prompt.md](lint-and-format.prompt.md); artefact drift (code vs CMake, runtime vs type checker, docs vs code) in [consistency-check.prompt.md](consistency-check.prompt.md). Note and cross-reference; do not restate here.
- **Respect deliberate decisions that look like bugs.** Several are documented in [learnings.instructions.md](../../instructions/learnings.instructions.md) and must **not** be reported as defects:
    - The two optimizer passes removed as unsound at the bytecode level — **constant propagation** (`GetLocal`→`Constant` for single-assigned locals) and multiply→shift **strength reduction**. Their absence is intentional; do not flag it or propose re-adding them.
    - Clamping a negative computed length to 0 (e.g. `String.truncate`) instead of throwing — a deliberate beginner-friendly choice, mirroring saturating arithmetic.
    - A whole `number` printing with a trailing `.0` (`format_number`) — intended; converting to `integer` (`Converter.to_integer`) is the documented way to drop it.
    - Semicolons tokenised but treated as whitespace — neither required nor forbidden, by design.
    - The sparse `Chunk` source map (one `(byte_offset, SourceLocation)` entry per opcode, binary-searched) — a deliberate ~60% memory trade-off, not missing data.
    - The Windows `0xC0000409` `__fastfail` fuzz exit with no crash artifact, and the clang-cl catch-funclet `int3` / `STATUS_BREAKPOINT` trap — both are toolchain codegen artifacts, **not** Luma bugs (the funclet trap already carries an in-code workaround; do not re-flag it).
- **No hallucinated findings.** Every entry needs a location you have opened and read.

## 7 — Output Format

Produce the report in two parts.

First, a summary table ordered by priority for quick scanning:

```markdown
| ID  | Suspected bug                            | Category          | Severity | Confidence | Effort |
| --- | ---------------------------------------- | ----------------- | -------- | ---------- | ------ |
| B01 | `.lumc` source-map count OOMs `reserve`  | Security          | Critical | High       | Small  |
| B02 | `parse_unary` prefix branch missing guard| Pipeline (parser) | High     | Medium     | Small  |
| B03 | …                                        | …                 | …        | …          | …      |
```

Then, one detailed entry per candidate:

```markdown
### B01 — <Short, symptom-oriented title>

- **Category:** <one of the §3 classes>
- **Severity:** <Critical | High | Medium | Low>
- **Confidence:** <High | Medium | Low>
- **Location:** `path/to/file.cpp` (lines A–B), `path/to/other.hpp` (lines C–D)
- **Symptom / trigger:** <the observable failure and the input or conditions that provoke it>
- **Root-cause hypothesis:** <the mechanism — why the code is wrong, traced through the phase>
- **Suggested fix direction:** <a sketch, not a full patch; name the project idiom that applies>
- **Regression test idea:** <the test that would prove the fix, at the level bug-fix.prompt.md adds one>
- **Effort:** <Small | Medium | Large>
- **Handoff goal:** "<one-line goal string ready to paste into bug-fix.prompt.md>"
```

Close with a short note on what you would fix first and why (highest severity weighted by confidence, most reachable). Make each **Handoff goal** specific enough that [bug-fix.prompt.md](bug-fix.prompt.md) can act on it without re-discovering the problem.
