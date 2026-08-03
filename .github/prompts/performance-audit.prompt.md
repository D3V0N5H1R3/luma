---
description: "Analyse the project read-only and produce a prioritized, actionable list of performance optimization opportunities — without changing any code"
agent: "agent"
tools: ["search", "read"]
argument-hint: "Optional scope, e.g. 'core/runtime/vm/' or 'the whole interpreter'"
version: 1
lastUpdated: "2026-08-01"
---

# Performance Audit

Survey the codebase — the interpreter core (lexer → VM), the standard library (`core/runtime/stdlib/`), the language server (`luma_lsp`), the debugger (`luma_dap`), or the shared libraries (`shared/`) — and produce a **prioritized list of performance optimization opportunities**. This prompt is the discovery counterpart to [optimize.prompt.md](optimize.prompt.md): this one *finds and ranks* candidate optimizations; that one *implements* a single chosen item end-to-end, proving the speedup with a benchmark and keeping the test suite green.

This is a **read-only analysis**. Make no code changes, and no build is required. The deliverable is a ranked report, not a diff — every finding carries a **confidence** rating because a candidate is only confirmed once [optimize.prompt.md](optimize.prompt.md) measures it against a live baseline.

> **Scope vs sibling prompts:** This audit hunts for **behaviour-preserving speed and memory wins** — algorithmic complexity, redundant allocations and copies, hot-path dispatch overhead, and poor data-structure choices. It is a *wide, subsystem-scoped, benchmark-oriented* hunt, unlike [code-review.prompt.md](code-review.prompt.md), which is a *deep, file-scoped* review that notes performance pitfalls among bugs, security, and style. [refactor-audit.prompt.md](refactor-audit.prompt.md) finds structural improvements whose goal is clarity rather than speed; [bug-search.prompt.md](bug-search.prompt.md) finds correctness defects; [consistency-check.prompt.md](consistency-check.prompt.md) finds drift between artefacts. When a candidate is really a bug, a smell, or a consistency gap, note it briefly and cross-reference the right prompt rather than restating it here.

## 1 — Understand the Intended Cost Model

Before judging what is slow, learn the performance model the code is meant to respect, so you flag genuine wins rather than deliberate trade-offs:

- [Luma_Performance_Guide.md](../../documents/Luma_Performance_Guide.md) — the canonical cost model: immutability and deep-copy semantics (§1), the collection complexity table (§2), the O(n²) string-building pitfall (§3), the resource-limit table (§6, mirroring `core/common/resource_limits.hpp`), and the interpreter optimisations already in place (§7 — the sparse `constexpr` opcode-lookup array, `TypeInfo::to_string_cached()`, thread-pool queue limits, depth-limited `ValueHash`).
- [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) — §6 (Processing Pipeline) and §7 (Bytecode Compiler and VM Internals) for the execution model, plus §4.16 for the `ResourceLimits` design. The hot path is the VM dispatch loop; the compile-time path runs once per program.
- [learnings.instructions.md](../../instructions/learnings.instructions.md) — established patterns and, crucially, the **deliberate performance decisions that look like inefficiencies but are intentional** (see §6).
- The per-language style guide for whatever you audit: [cpp.instructions.md](../../instructions/cpp.instructions.md) (first-party C++), [typescript.instructions.md](../../instructions/typescript.instructions.md) (VS Code), [rust.instructions.md](../../instructions/rust.instructions.md) (Zed), [javascript.instructions.md](../../instructions/javascript.instructions.md) and [css.instructions.md](../../instructions/css.instructions.md) (GraphicalUi front-end), and [python.instructions.md](../../instructions/python.instructions.md) (`scripts/`).

If the audit targets a specific subsystem, also skim its design doc to learn the behaviour that must be preserved: [Luma_User_Manual.md](../../documents/Luma_User_Manual.md) (lexer→VM semantics), [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md) (per-module operations and their documented costs), [Luma_Language_Server.md](../../documents/Luma_Language_Server.md), or [Luma_Debugger.md](../../documents/Luma_Debugger.md).

## 2 — Scope and Ground Rules

- **Default scope** is the whole first-party tree, weighted toward the runtime hot path (`core/runtime/vm/`, `core/runtime/interpreter/`, `core/runtime/compiler/`, `core/runtime/stdlib/`, and `core/common/`). If the invocation names a directory or file, restrict the audit to it and its immediate collaborators.
- **In scope:** first-party sources under `core/`, `shared/`, `language-server/`, `debugger/`, and the GraphicalUi front-end under `external/gui-framework/`, plus the Luma-level cost of stdlib operations exercised by `benchmarks/`.
- **Out of scope:** vendored code (`external/` except `external/gui-framework/`), generated code, and build outputs (`build/`, `build-fuzz/`). Do not propose optimizations there.
- **Weight by reachability.** An inefficiency on the VM dispatch loop, a per-opcode handler, a `Value` copy, or a frequently called stdlib built-in matters far more than the same inefficiency in a compile-once analysis phase or a one-shot CLI path. Say where on the hot/cold spectrum each finding sits.
- **Verify every location.** Read each file you cite — never report a hotspot or line range you have not confirmed in the source. A hallucinated location wastes the implementer's time.
- **Make no changes.** Do not edit, format, build, or run tests. Producing the ranked list is the whole job.

## 3 — What to Look For

Hunt for these performance smells. Each maps to an optimization the project has precedent for, and each must **preserve observable behaviour** — this is speed and memory, never a change in results.

1. **Algorithmic complexity.** Quadratic (or worse) work where linear is possible: a linear scan nested inside a loop over the same data, repeated `find`/`contains` on a `std::vector` that should be a set or map lookup, an accidental O(n²) from rebuilding a container each iteration. The interpreter runs user loops millions of times, so a hot-path complexity win compounds.
2. **Redundant allocations and copies.** The immutability model means `Value`s are deep-copied on mutation by design — but *unnecessary* copies are fair game: a `Value`/`std::string`/container passed by value where `const&` suffices, a temporary built and discarded each iteration, a `std::string` where a `std::string_view` into stable storage would do (watch the documented dangling-view pitfall), or a missing `reserve()` before a known-size fill. Prefer moving over copying.
3. **Hot-path dispatch overhead.** Per-instruction work in the VM loop that could hoist out or precompute: repeated map lookups keyed by name that a resolved slot index would replace, a switch chain where the project already uses a table (opcode dispatch via `vm_dispatch_table.cpp`, AST dispatch via `ast_dispatch.hpp`, binary-operator opcodes via the `constexpr` sparse array), or work repeated per call that could be computed once at compile time.
4. **Recomputation that should be cached or hoisted.** A pure result recomputed inside a loop, a value derived every call that a member could memoize (the `TypeInfo::to_string_cached()` precedent), or a lookup performed twice where one would serve. Only cache when the recomputation is genuinely hot and the cache cannot go stale.
5. **Poor data-structure choice.** Using an `Array` (O(n) `contains`) where a `Set` (O(1)) fits the access pattern, an ad-hoc string-keyed `std::unordered_map<std::string, …>` instead of `StringMap`/`StringHash` (heterogeneous lookup that avoids constructing a key string), a bespoke cache instead of `LruCache`, or a container whose complexity table entry (Performance Guide §2) does not match how the code uses it.
6. **String building and formatting.** O(n²) incremental concatenation instead of a single-pass `String.join`/builder (Performance Guide §3), repeated re-formatting of the same value, or interpolation on a hot path that could be assembled once.
7. **Refcount and indirection churn.** `std::shared_ptr` copied (atomic refcount bump) where a reference or raw non-owning pointer suffices on a hot path, gratuitous virtual dispatch or pointer chasing in the inner loop, or an `std::function` call where a direct call or template would inline.
8. **Startup and latency.** Work done eagerly at startup that could be lazy (the stdlib already uses a `LazyLoader`), or bytecode recompiled when a cached `.lumc` would serve — measurable via `bench_startup`.
9. **Memory footprint.** An over-large `Value` or node struct, a container held at full capacity long after it shrank, or storage that could be sparser — but respect the deliberate footprint trade-offs in §6 before flagging.

## 4 — How to Gather Evidence

Use the workspace search and file-reading tools first; the benchmark suite is the ground truth for *where* time is actually spent. Parallelize independent read-only exploration. Do **not** build or run the profiler yourself — the audit is static — but read the benchmark results and code to reason about cost.

- **Map hot paths to benchmarks.** `benchmarks/` has one `bench_<topic>.luma` per area (arithmetic, strings, collections, control_flow, functions, …) driven by `suite.luma` and the `time_it` harness. Read the benchmark that exercises the code you are auditing; a topic with a slow or missing benchmark is itself a signal — record the coverage gap in the finding. [benchmarks/DIRECTORY.md](../../benchmarks/DIRECTORY.md) documents the harness, the `parse_benchmark_results.py` output shape, and the `compare_benchmark_results.py` regression thresholds (10% CPU-bound, 50% for noisy I/O and concurrency cases).
- **Trace the hot path.** Read the VM dispatch loop and the handlers for the most-executed opcodes, then walk a representative program through them; the cost usually concentrates in a handful of per-instruction operations.
- **Search for the smell signatures** in §3: a `Value`/`std::string`/container parameter taken by value (candidate `const&` or move), a `std::unordered_map<std::string,` (candidate `StringMap`), a `.find(` or `std::find` inside a loop (candidate hoist or better container), a `reserve(` that is missing before a sized fill, a `shared_ptr` copied on a hot path, or `+`-concatenation building a string in a loop.
- **Read the complexity table.** Cross-check each container's actual usage against Performance Guide §2 — a mismatch (frequent `contains` on an `Array`) is a concrete finding.

## 5 — Prioritize

Rank every candidate so the implementer picks the highest-value item first. Weigh:

- **Impact** — expected speedup or memory saving, scaled by **reachability**. A constant-factor win on the VM inner loop or a hot stdlib built-in outranks an asymptotic win on a compile-once path. State the complexity change (e.g. O(n²)→O(n)) or the copy/allocation removed.
- **Confidence** — how sure the win is real given you did not profile. **High**: a clear complexity change or an obviously removable copy on a path a benchmark exercises. **Medium**: plausibly hot but the trigger path needs the benchmark to confirm. **Low**: a micro-optimization whose payoff only measurement can settle — say so, so the implementer measures first.
- **Risk** — blast radius and how mechanical vs semantic the change is. A localized `const&` or `reserve()` is low risk; reshaping a core data structure or the dispatch loop is high risk and demands strong test and benchmark coverage.
- **Effort** — rough size (Small / Medium / Large), independent of impact.

Synthesize these into the single **Priority** rating (High / Medium / Low) the report ranks by — Priority rises with impact and confidence and falls with risk and effort. Favour high-impact, low-risk, high-confidence, benchmark-backed items at the top. A high-impact but low-confidence item still belongs on the list, with "measure against the benchmark baseline first" recorded as the implementer's prerequisite rather than a reason to drop it.

## 6 — What to Exclude

- **No behaviour changes.** An optimization preserves observable behaviour and all results. Anything that fixes a defect belongs in [bug-fix.prompt.md](bug-fix.prompt.md); anything that only restructures for clarity belongs in [refactor-audit.prompt.md](refactor-audit.prompt.md). Note and cross-reference; do not restate here.
- **No unmeasured micro-optimization.** Per the project's implementation discipline, an optimization must pay for itself in a benchmark or a clear complexity argument. Do not propose speculative tweaks, hand-inlining, or cache layers whose benefit no benchmark can show — they add complexity and risk for no proven gain.
- **No new third-party runtime dependencies.** Optimizations use the standard library, OS APIs, and existing project utilities (`core/common/`, `shared/`) — not a new vendored library.
- **Respect deliberate performance decisions.** Several are documented in [learnings.instructions.md](../../instructions/learnings.instructions.md) and must **not** be proposed as optimizations:
    - The two optimizer passes removed as **unsound** at the bytecode level — constant propagation (`GetLocal`→`Constant` for single-assigned locals) and multiply→shift strength reduction. The optimizer sees only a `Chunk` (no local scopes or liveness), so do **not** suggest re-adding them for speed.
    - **Immutability and deep-copy semantics.** Collections copy on mutation by design for beginner-safe, shared-state-free behaviour. Do not propose making `Value` mutable or introducing shared mutable state to skip copies; only remove *gratuitous* copies that the semantics do not require.
    - Deliberate space/time trade-offs: the sparse `Chunk` source map (one entry per opcode, binary-searched — a ~60% memory saving, not missing data) and the LSP function-body ranges held **twice** (a map plus a sorted vector) for O(log n) enclosing-function lookup. Do not flag either as wasteful.
    - Beginner-friendly choices that cost a little: clamping a negative computed length to 0 instead of erroring, and a whole `number` printing with a trailing `.0`.
- **No hallucinated findings.** Every entry needs a location you have opened and read.

## 7 — Output Format

Produce the report in two parts.

First, a summary table ordered by priority for quick scanning:

```markdown
| ID  | Optimization                              | Category           | Priority | Confidence | Effort |
| --- | ----------------------------------------- | ------------------ | -------- | ---------- | ------ |
| P01 | Replace `Array.contains` loop with Set    | Data structure     | High     | High       | Small  |
| P02 | Take `Value` by `const&` in op handler    | Redundant copy     | High     | Medium     | Small  |
| P03 | …                                         | …                  | …        | …          | …      |
```

Then, one detailed entry per candidate:

```markdown
### P01 — <Short, action-oriented title>

- **Category:** <one of the §3 categories>
- **Priority:** <High | Medium | Low>
- **Confidence:** <High | Medium | Low>
- **Location:** `path/to/file.cpp` (lines A–B), `path/to/other.hpp` (lines C–D)
- **Hotness:** <hot path / warm / cold> — <why: the benchmark or call site that reaches it>
- **Cost now:** <the current complexity or per-iteration overhead — e.g. O(n²) scan, one Value copy per opcode>
- **Proposed optimization:** <high-level approach, naming the established project pattern or utility to apply>
- **Expected gain:** <the complexity change or copies/allocations removed, and the benchmark that should show it>
- **Effort / risk:** <Small | Medium | Large> effort, <Low | Medium | High> risk
- **Test / benchmark safety net:** <which tests cover the behaviour and which `bench_*.luma` measures the path; note "add a benchmark first" if the path is unmeasured>
- **Handoff goal:** "<one-line goal string ready to paste into optimize.prompt.md>"
```

Close with a short note on what you would optimize first and why (highest impact-to-risk, most reachable, benchmark-backed). Make each **Handoff goal** specific enough that [optimize.prompt.md](optimize.prompt.md) can act on it without re-discovering the hotspot.
