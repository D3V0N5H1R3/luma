---
description: "Diagnose and fix a bug in the Luma debugger (DAP)"
agent: "agent"
argument-hint: "Bug description, e.g. 'step out skips the caller frame inside task_scope'"
---

# Bug Fix — Debugger

Diagnose and fix a bug in the Luma debugger (`luma_dap`). The debugger embeds the Luma VM and runs the target program with instrumentation hooks, so most bugs are wrong breakpoints, stepping, stack traces, variable inspection, expression evaluation, or concurrency mapping. Follow a structured approach:

1. **Reproduce** the bug with a minimal Luma program and the DAP request sequence that misbehaves (set breakpoints, continue, step over/into/out, reverse step, stack trace, scopes, variables, evaluate, threads). Confirm the failure, ideally as a C++ test in `debugger/tests/`.
2. **Isolate the component** where the bug occurs by tracing through the debugger:
    - Protocol transport (`dap_transport.*`, `dap_tcp_transport.*`, `dap_protocol_handler.hpp`, `shared/protocol/`) — Content-Length framing or request dispatch wrong?
    - Request handling & capability negotiation (`dap_server.*`, `dap_handler_context.*`, `dap_lifecycle_handler.hpp`, `dap_execution_handler.hpp`, `dap_breakpoint_handler.hpp`, `dap_inspection_handler.hpp`, `dap_feature_manager.hpp`, `dap_response_builders.hpp`, `dap_error_handler.hpp`) — request routed to the wrong handler group, a capability not advertised or enabled, or a malformed response?
    - Session orchestration (`debug_session.*`, `dap_session_types.hpp`, `debug_session_state.hpp`, `dap_lock_ordering.hpp`) — the session state machine or lock ordering wrong?
    - Execution engine (`debug_execution_engine.*`, `debug_execution_control.cpp`, `debug_execution_hooks.cpp`, `debug_execution_lifecycle.cpp`) — program launch, hook installation, or continue/step/pause wrong?
    - Breakpoints (`breakpoint_manager.*`, `compiled_breakpoint.*`, `line_breakpoint_manager.*`, `function_breakpoint_manager.*`, `data_breakpoint_manager.*`, `exception_breakpoint_settings.hpp`, `dap_breakpoint_validator.*`) — pending vs resolved entries, conditions, hit counts, logpoints, or exception filters (caught/uncaught) wrong?
    - Source location mapping (`source_manager_locator.*`, `i_source_locator.hpp`) — wrong file or line resolution for breakpoint binding or stack frames?
    - Thread state (`thread_state_manager.*`) — per-thread paused state or task-to-thread mapping wrong?
    - Variable inspection (`variable_inspector.*`, `variable_reference_registry.hpp`, `custom_visualizer.*`) — scope expansion, generational reference invalidation, or visualizers wrong?
    - Expression evaluation (`expression_evaluator.*`, `expression_compiler.*`) — frame-context resolution (local → upvalue → global → scratch VM) wrong?
    - VM instrumentation (`vm_hook_registry.*`, `vm_debug_adapter.*`) — hook installation or VM introspection (stack trace, locals, upvalues) wrong?
    - Reverse debugging (`time_travel.*`) — snapshot capture interval or state restore wrong?
    - Hot reload (`hot_reloader.*`) — source-change watching wrong?
    - JSON field extraction (`dap_helpers.hpp`) — null-safe defaults or `narrow_int` overflow handling wrong?
    - The specific handler-group implementation in `debugger/source/` (e.g. `dap_server_breakpoints.cpp`, `dap_server_execution.cpp`, `dap_server_inspection.cpp`, which implement the `Dap*Handler` classes).
3. Read [Luma_Debugger.md](../../documents/Luma_Debugger.md) and the relevant source files to understand the current behaviour.
4. **Fix** the root cause with the smallest correct change. If the root cause lies in the embedded VM, compiler, or a runtime `Value` rather than the debugger's own instrumentation, fix it there via [bug-fix.prompt.md](bug-fix.prompt.md). Watch for two debugger pitfalls: follow the documented `DebugSession` lock ordering (thread_states → ThreadState → config → exception) to avoid deadlocks, and invoke session-terminating callbacks only *after* releasing the transport send mutex, since the disconnect callback joins the VM thread, which may itself be blocked on that mutex.
5. **Add a regression test** in the appropriate `debugger/tests/dap_test_*.cpp` file (e.g. `dap_test_breakpoints.cpp`, `dap_test_variable_inspector.cpp`, `dap_test_protocol.cpp`), or in `dap_integration_test.cpp` for an end-to-end protocol flow.
6. **Verify.** For a fast inner loop, build and run just the debugger tests — they all carry the CTest `dap` label (including the end-to-end `dap_integration_test`):

    ```bash
    cmake --build --preset default
    ctest --preset default -L dap
    ```

    Then run the full suite once (`ctest --preset default`) to confirm nothing else broke. See [build-and-test.prompt.md](build-and-test.prompt.md) for the canonical build-and-test workflow.
