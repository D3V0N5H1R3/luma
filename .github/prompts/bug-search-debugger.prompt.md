---
description: "Analyse the Luma debugger (DAP) read-only and produce a prioritized, actionable list of suspected bugs — without changing any code"
agent: "agent"
tools: ["search", "read"]
argument-hint: "Optional scope, e.g. 'breakpoint_manager.cpp' or 'the whole debugger'"
version: 1
lastUpdated: "2026-08-01"
---

# Bug Search — Debugger

Survey the Luma debugger (`luma_dap`) and produce a **prioritized list of suspected bugs**. This prompt is the discovery counterpart to [bug-fix-debugger.prompt.md](bug-fix-debugger.prompt.md): this one *finds and ranks* candidate defects; that one *reproduces, root-causes, and fixes* a single chosen item with a regression test and the suite green.

The debugger embeds the Luma VM and runs the target program with instrumentation hooks, so most defects are wrong breakpoints, stepping, stack traces, variable inspection, expression evaluation, or concurrency mapping — plus the deadlock-shaped bugs that come with its two-thread design. This is a **read-only hunt**: make no code changes, and no build is required — confirming each candidate with a live repro is the first step of [bug-fix-debugger.prompt.md](bug-fix-debugger.prompt.md), which is why every finding carries a **confidence** rating. The deliverable is a ranked report, not a diff.

> **Scope vs sibling prompts:** This hunt covers the debugger. If the fault is in another subsystem, use the matching hunt instead: [bug-search.prompt.md](bug-search.prompt.md) for the interpreter core and standard library, [bug-search-language-server.prompt.md](bug-search-language-server.prompt.md) for the language server (`luma_lsp`), or [bug-search-editor-extension.prompt.md](bug-search-editor-extension.prompt.md) for the editor extensions. Because the debugger embeds the VM, a wrong variable display or evaluation may root-cause to the VM, the compiler, or a runtime `Value` — that is [bug-search.prompt.md](bug-search.prompt.md)'s territory. Stay on defects: [code-review.prompt.md](code-review.prompt.md) is a *deep, file-scoped* review that also weighs style and maintainability, while [refactor-audit.prompt.md](refactor-audit.prompt.md) and [consistency-check.prompt.md](consistency-check.prompt.md) own structure and drift — note and cross-reference such candidates rather than restating them here.

## 1 — Understand the Intended Behaviour

Before judging what looks wrong, learn the behaviour the debugger is meant to have:

- [Luma_Debugger.md](../../documents/Luma_Debugger.md) — the debugger's architecture, its capabilities, and the concurrency and lock-ordering rules any fix must preserve.
- [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) — especially §7 (Bytecode Compiler and VM Internals): the VM the debugger instruments.
- [learnings.instructions.md](../../instructions/learnings.instructions.md) — the **Debugger (DAP)** section (component responsibilities, the documented lock ordering, the send-mutex/disconnect rule, and the synthesized-source evaluation caveat) plus **C++ Pitfalls Discovered**, and the deliberate decisions in §6.
- [cpp.instructions.md](../../instructions/cpp.instructions.md) — the C++ idioms whose violation is frequently the defect.

## 2 — Scope and Ground Rules

- **Default scope** is the whole debugger: `debugger/source/` and the shared transport it consumes (`shared/protocol/`). If the invocation names a file, restrict the hunt to it and its immediate collaborators.
- **In scope to flag, not to fix here:** when a symptom (a wrong variable value, a failed evaluation) roots in the embedded VM, the compiler, or a `Value`, record it and point the handoff at [bug-search.prompt.md](bug-search.prompt.md) / [bug-fix.prompt.md](bug-fix.prompt.md) — the debugger's own instrumentation is this prompt's fix territory.
- **Out of scope:** the language server (`language-server/`), the editor extensions (`extensions/`), vendored code (`external/`), generated code, and build outputs. Point findings there at the matching hunt.
- **Verify every location.** Read each file you cite — never report a defect or line range you have not confirmed in the source.
- **Make no changes.** Do not edit, format, build, or run tests. Producing the ranked list is the whole job.

## 3 — What to Look For

Hunt for these defect classes. The component list mirrors [bug-fix-debugger.prompt.md](bug-fix-debugger.prompt.md)'s isolation steps, so a finding drops straight into the fix workflow.

1. **Locking and shutdown — the highest-yield DAP bug class.** The two-thread design (protocol I/O thread + VM execution thread) makes deadlocks the signature failure:
    - Any mutex acquisition that violates the **documented lock ordering** — `thread_states_mutex_` → `ThreadState::mutex` → `config_mutex_` → leaf `exception_mutex_`. An out-of-order pair (or a path that takes two of these in the wrong sequence) is a latent deadlock.
    - A **session-terminating callback invoked while holding the transport `send_mutex_`.** `DapProtocolHandler::send_response` / `send_event` hold `send_mutex_` while writing; on a broken pipe they must set a flag and call `signal_disconnect()` only *after* the lock scope ends, because the disconnect callback joins the VM execution thread, which may itself be blocked on `send_mutex_` inside `send_event()` — signalling under the lock self-deadlocks.
2. **Component correctness, by handler group:**
    - **Transport and request handling** (`dap_transport*`, `dap_tcp_transport*`, `dap_server*`, `dap_*_handler.hpp`, `dap_response_builders.hpp`) — Content-Length framing, a request routed to the wrong handler group, a capability not advertised or enabled, or a malformed response.
    - **Session orchestration** (`debug_session*`, `dap_session_types.hpp`) — the atomic `SessionState` machine (`Idle`/`Running`/`Terminated`) or a re-introduced separate-boolean race.
    - **Execution engine** (`debug_execution_*`) — program launch, VM creation, hook installation, or continue/step/pause, including the configuration-phase synchronization that waits for `configurationDone`, plus the hot-reload source watcher it integrates (`hot_reloader.*`, the `luma/hotReload` handler).
    - **Breakpoints** (`breakpoint_manager*`, `compiled_breakpoint*`, `line_/function_/data_breakpoint_manager*`, `dap_breakpoint_validator.*`) — pending vs resolved entries, conditions, hit counts, logpoints, or exception filters (caught/uncaught). Note that `CompiledBreakpoint` compiles conditions to bytecode but does **not** evaluate them — evaluation runs in the engine against the live paused frame, so a condition that ignores frame locals is the bug.
    - **Source-location mapping** (`source_manager_locator*`, `i_source_locator.hpp`) — wrong file or line for breakpoint binding or a stack frame.
    - **Thread state** (`thread_state_manager*`) — per-thread paused state, the paused-thread counter/condition-variable signalling, or the task → DAP-thread mapping.
    - **Variable inspection** (`variable_inspector*`, `variable_reference_registry.hpp`, `custom_visualizer.*`) — scope expansion (max depth 32), **generational-reference invalidation** (a stale reference surviving a resume, or a live one wrongly purged), or a visualizer rule.
    - **Expression evaluation** (`expression_evaluator*`, `expression_compiler*`) — the frame-context strategy chain (direct local → closure upvalue → global env → scratch-VM compile-and-run). Watch **synthesized-source correctness**: the scratch VM compiles `function boolean __bp_eval__() { return <expr> }` and reads the `Value` that `VM::execute_function` returns; any change to that synthesis needs a test that compiles **and runs** a real compound expression (the class of bug that shipped precisely because no test ran the wrapper).
    - **VM instrumentation** (`vm_hook_registry*`, `vm_debug_adapter*`) — hook installation or introspection (stack trace, locals, upvalues), and atomic memory ordering on flags like `pause_requested` (writers need release, readers need acquire).
    - **Reverse debugging** (`time_travel*`) — the snapshot capture interval or the value-stack restore on step-back.
    - **JSON field extraction** (`dap_helpers.hpp`) — `narrow_int()` overflow handling or null-safe defaults (thread ID defaults to 1).
3. **Root cause in the embedded VM/compiler/Value.** A wrong variable display, stack trace, or evaluation may root below the debugger — flag it, but the fix belongs to [bug-search.prompt.md](bug-search.prompt.md) / [bug-fix.prompt.md](bug-fix.prompt.md).

## 4 — How to Gather Evidence

Use the workspace search and file-reading tools; parallelize independent read-only exploration. Do **not** build or run.

- **Audit lock order.** Grep for `lock_guard`, `unique_lock`, and `OrderedLockGuard` and check every path that holds two of `thread_states_mutex_`, `ThreadState::mutex`, `config_mutex_`, `exception_mutex_` against the documented order. Separately, find every `signal_disconnect()` / session-terminating callback and confirm it fires *outside* the `send_mutex_` scope.
- **Trace one DAP request sequence.** Walk `setBreakpoints → configurationDone → continue → stop → stackTrace → scopes → variables → evaluate` and look for the component where frame context, thread mapping, or reference generation breaks.
- **Follow synthesized source.** Find any path that builds Luma source to feed back through the compiler (evaluation, logpoints, compiled breakpoints) and confirm a test compiles **and runs** a representative example.
- **Check atomics.** Grep `memory_order` on hook and pause flags for relaxed where release/acquire is required.
- **Grep intent markers** — `TODO`, `FIXME`, `HACK`, `BUG`, `XXX` — and read the tests (`debugger/tests/dap_test_*.cpp`, `dap_integration_test.cpp`); a component with thin coverage is where latent bugs survive — record the gap.

## 5 — Prioritize

Rank every candidate so the fixer picks the highest-value item first. Rate each on:

- **Severity** — impact if the defect is real. **Critical**: a deadlock, a crash, a hang on disconnect, or session state corruption. **High**: a breakpoint that never binds, a step that skips or lands on the wrong frame, or an evaluation that returns the wrong value on common input. **Medium**: a wrong variable display or a wrong result on an edge case. **Low**: cosmetic, or a narrow case with an easy workaround.
- **Confidence** — how sure it is a genuine bug given you did not run it. **High**: mechanism traced end-to-end and a triggering request sequence is describable. **Medium**: clearly suspect, but the trigger path needs confirmation. **Low**: a plausible smell that needs the fix step's live repro — say so.
- **Reachability** — whether it fires on ordinary sessions (common breakpoint/step/evaluate flows, concurrent tasks) versus a rare shape. Higher reachability raises priority.
- **Effort** — rough fix size (Small / Medium / Large), independent of severity.

Rank by severity weighted by confidence, tie-broken by reachability then effort. A suspected deadlock (Critical) with even Medium confidence belongs near the top, with "confirm the repro first" recorded as the fixer's prerequisite.

## 6 — What to Exclude

- **No fixes.** Reproducing and fixing is [bug-fix-debugger.prompt.md](bug-fix-debugger.prompt.md)'s job — do not edit code.
- **Not structure, style, or drift.** Behaviour-preserving structure belongs in [refactor-audit.prompt.md](refactor-audit.prompt.md); style in [code-review.prompt.md](code-review.prompt.md) / [lint-and-format.prompt.md](lint-and-format.prompt.md); artefact drift (e.g. advertised capabilities in `dap_feature_manager.hpp` vs implemented handlers) in [consistency-check.prompt.md](consistency-check.prompt.md). Note and cross-reference.
- **Respect deliberate decisions that look like bugs.** Documented in [learnings.instructions.md](../../instructions/learnings.instructions.md):
    - `TimeTravel` restoring only the **value stack** on step-back — frame/IP restoration and forward replay are intentionally out of scope (the VM exposes no frame/IP restoration API); their absence is not a defect.
    - The documented non-goals are **memory/performance profiling** and **language-server integration** ([Luma_Debugger.md](../../documents/Luma_Debugger.md) §3) — do not report them as missing features. Hot code reload, by contrast, *is* implemented (`hot_reloader.*`, the `luma/hotReload` handler), so hot-reload defects are in scope.
    - `narrow_int()` **throwing** on overflow is the intended contract (ES.46-compliant), as is the thread-ID default of 1 for the main thread.
- **No hallucinated findings.** Every entry needs a location you have opened and read.

## 7 — Output Format

Produce the report in two parts.

First, a summary table ordered by priority for quick scanning:

```markdown
| ID  | Suspected bug                              | Category         | Severity | Confidence | Effort |
| --- | ------------------------------------------ | ---------------- | -------- | ---------- | ------ |
| B01 | `signal_disconnect` fires under send mutex | Locking/shutdown | Critical | High       | Small  |
| B02 | Step-out skips caller frame in `task_scope`| Execution engine | High     | Medium     | Medium |
| B03 | …                                          | …                | …        | …          | …      |
```

Then, one detailed entry per candidate:

```markdown
### B01 — <Short, symptom-oriented title>

- **Category:** <one of the §3 classes>
- **Severity:** <Critical | High | Medium | Low>
- **Confidence:** <High | Medium | Low>
- **Location:** `path/to/file.cpp` (lines A–B), `path/to/other.hpp` (lines C–D)
- **Symptom / trigger:** <the observable failure and the DAP request sequence that provokes it>
- **Root-cause hypothesis:** <the mechanism — why the code is wrong, traced through the component>
- **Suggested fix direction:** <a sketch, not a full patch; name the lock-ordering rule or idiom that applies>
- **Regression test idea:** <the `dap_test_*.cpp` or `dap_integration_test.cpp` case that would prove the fix>
- **Effort:** <Small | Medium | Large>
- **Handoff goal:** "<one-line goal string ready to paste into bug-fix-debugger.prompt.md>"
```

Close with a short note on what you would fix first and why (highest severity weighted by confidence, most reachable). Make each **Handoff goal** specific enough that [bug-fix-debugger.prompt.md](bug-fix-debugger.prompt.md) can act on it without re-discovering the problem.
