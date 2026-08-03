---
description: "Optimize Luma code for speed or memory — interpreter, language server, debugger, or stdlib — proving the win with a benchmark while keeping all tests green"
agent: "agent"
argument-hint: "Optimization goal, e.g. 'cut the per-opcode Value copy in the VM arithmetic handlers'"
version: 1
lastUpdated: "2026-08-01"
---

# Optimize

Implement a single **performance optimization** anywhere in the Luma project — the interpreter core (lexer → VM), the standard library (`core/runtime/stdlib/`), the language server (`luma_lsp`), the debugger (`luma_dap`), or the shared libraries (`shared/`). This prompt is the execution counterpart to [performance-audit.prompt.md](performance-audit.prompt.md): that one *finds and ranks* hotspots; this one *implements* one chosen item, **proving the speedup with a benchmark** and keeping observable behaviour identical and the test suite green.

The cardinal rule: **an optimization preserves behaviour and is validated by measurement.** A change that is not backed by a before/after benchmark number (or an unambiguous complexity improvement on a proven-hot path) is not done — and if the measurement shows no gain, revert it. Prioritise correctness first, then measured speed.

> **Scope vs sibling prompts:** This prompt makes correct code *faster or leaner*. If the goal is really to fix wrong behaviour, use [bug-fix.prompt.md](bug-fix.prompt.md); if it is to restructure for clarity without a speed target, use [refactor.prompt.md](refactor.prompt.md). The two share this prompt's discipline of keeping the suite green, but only this one gates the change on a benchmark.

## Workflow

1. **Learn the cost model and the behaviour to preserve.** Read [Luma_Performance_Guide.md](../../documents/Luma_Performance_Guide.md) — immutability and deep-copy costs (§1), the collection complexity table (§2), string building (§3), the resource limits (§6, mirroring `core/common/resource_limits.hpp`), and the optimisations already in place (§7). Then read [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) §6–§7 for the pipeline and VM internals, and — **if the optimization touches a specific subsystem** — the matching design doc for the behaviour that must not change:
    - Lexer, parser, type checker, compiler, or VM → [Luma_User_Manual.md](../../documents/Luma_User_Manual.md) for the language semantics.
    - Standard library (`core/runtime/stdlib/`) → [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md), plus [Luma_GraphicalUi_Guide.md](../../documents/Luma_GraphicalUi_Guide.md) for the GraphicalUi module.
    - Error-handling paths → [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md) for the conventions to preserve.
    - Language server (`language-server/source/`) → [Luma_Language_Server.md](../../documents/Luma_Language_Server.md); debugger (`debugger/source/`) → [Luma_Debugger.md](../../documents/Luma_Debugger.md).
    - Crucially, read [learnings.instructions.md](../../instructions/learnings.instructions.md) §6 for the **deliberate performance decisions that must not be undone** (see Constraints below).
2. **Read the code and confirm the hotspot.** Read the source involved and every caller and dependency — search for all references so no call site is missed. Confirm the path is genuinely hot: name the benchmark that exercises it, or the user-facing loop that reaches it. If the path is not on any benchmark, first check whether one *should* exist (see step 4).
3. **Establish a green, measured baseline.** Build in Release (`cmake --preset default && cmake --build --preset default`) and run the full test suite (`ctest --preset default`) to confirm a green starting point; record any pre-existing failures so they stay distinguishable from regressions. Then capture the **performance baseline** so the win is provable:

    ```bash
    # Release build is essential — never benchmark a Debug build.
    build/luma benchmarks/suite.luma | tee baseline.txt
    python3 scripts/parse_benchmark_results.py baseline.txt -o baseline.json
    ```

    On Windows the binary is `build\Release\luma.exe`. If the relevant path has no benchmark, add one first (step 4) so there is something to measure.
4. **Add a benchmark if the path is unmeasured.** A hot path worth optimizing is a hot path worth guarding against regression. Follow [benchmarks/README.md](../../benchmarks/README.md): add a `bench_<topic>.luma` (or a case in an existing one), thread state through the `time_it` harness, and wire it into `suite.luma` in **both** the `include` list and `run_benchmarks()` — `scripts/check_benchmark_suite.py` fails CI if either is missing. Re-capture the baseline after adding it.
5. **Implement the optimization incrementally.** Change as little as possible to realise the win:
    - Preserve observable behaviour exactly — same results, same errors, same exit codes.
    - Follow the conventions of the language you edit: [cpp.instructions.md](../../instructions/cpp.instructions.md) for all first-party C++ (interpreter, LSP, DAP, `shared/`), [javascript.instructions.md](../../instructions/javascript.instructions.md) and [css.instructions.md](../../instructions/css.instructions.md) for the GraphicalUi front-end, [typescript.instructions.md](../../instructions/typescript.instructions.md) / [rust.instructions.md](../../instructions/rust.instructions.md) for the editor extensions.
    - Reach for the established mechanism rather than a bespoke one: a dispatch table (`vm_dispatch_table.cpp`, `ast_dispatch.hpp`, the `constexpr` binary-operator opcode array), `StringMap`/`StringHash` for heterogeneous string-keyed lookup, `LruCache` for caching, `narrow_int`/`clamp_to_int` for checked narrowing, a `const&` or `std::move` to drop a copy, `reserve()` before a sized fill, and a memoized member in the style of `TypeInfo::to_string_cached()`.
    - Keep each step small enough that the test suite stays a meaningful checkpoint; commit or stash each green checkpoint so any regression is easy to roll back.
6. **Verify behaviour after each significant change.** Rebuild and rerun `ctest --preset default` to catch regressions early — a clean Release compile is the first checkpoint, a green suite the second. The LSP and DAP suites run inside this same `ctest` invocation.
7. **Measure the win — and prove no regression elsewhere.** Re-run the benchmarks and compare against the baseline:

    ```bash
    build/luma benchmarks/suite.luma | tee current.txt
    python3 scripts/parse_benchmark_results.py current.txt -o current.json
    python3 scripts/compare_benchmark_results.py baseline.json current.json --threshold 10
    ```

    `compare_benchmark_results.py` fails a CPU-bound benchmark that regresses by more than 10% and applies a wider 50% `--io-threshold` to inherently noisy filesystem, key-value, `Process.run`, and concurrency cases. Confirm the **targeted** benchmark improved and that **no other** benchmark regressed past its threshold. Run the comparison on a quiet machine and repeat if a number looks like jitter — a per-iteration figure that does not move outside the noise band means the optimization did not pay off, so revert it rather than keep dead complexity.
8. **Update the wiring and docs if they moved.** If you renamed files or moved code, update `CMakeLists.txt` (sources are listed explicitly — no globbing) and every affected `#include`. If the optimization changes a documented cost — a complexity-table entry, a resource limit, or the §7 list of interpreter optimisations — update [Luma_Performance_Guide.md](../../documents/Luma_Performance_Guide.md) to match.
9. **Lint, format, and do a final sweep.** Lint and format every language you touched per [lint-and-format.prompt.md](lint-and-format.prompt.md) (lint first, then format) — the CI **Formatting** job fails on *any* clang-format diff, and the `clang-analyzer-*`, `bugprone-*`, and `concurrency-*` clang-tidy categories are escalated to build-failing errors. Then run the full suite one final time. For a deeper guardrail — especially when the change touched the lexer→VM pipeline or a stdlib parser — follow [full-test-sweep.prompt.md](full-test-sweep.prompt.md) (fuzz smoke tests and the benchmark run `ctest` does not cover) and run the example programs (`python scripts/run_luma_examples.py`) as a behaviour check the suite does not reach.

## Constraints

- **Behaviour is sacred.** If a benchmark speeds up but a test changes result, the optimization is wrong — fix or revert. Never delete, skip, or weaken a test to make a number look better.
- **Measure or revert.** No change ships without a before/after benchmark improvement or an unambiguous complexity win on a proven-hot path. Keeping unmeasured "it should be faster" complexity is a net loss.
- **Never re-add the unsound optimizer passes.** Constant propagation (`GetLocal`→`Constant`) and multiply→shift strength reduction were removed because the optimizer sees only a `Chunk` with no local scopes or liveness. Do not reintroduce them for speed (see [learnings.instructions.md](../../instructions/learnings.instructions.md) §6).
- **Do not undo deliberate trade-offs.** Immutability's deep-copy semantics (remove only *gratuitous* copies, never introduce shared mutable state), the sparse `Chunk` source map, and the LSP's double-stored function-body ranges are intentional. Optimize within them.
- **Ask first** before changing a public API or module boundary, altering the pipeline architecture, adding a third-party runtime dependency, or raising a resource limit to buy speed.
- **Release only.** Benchmark exclusively against a Release build; Debug numbers are meaningless for comparison.
