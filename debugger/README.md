# Luma DAP Debugger

A standalone [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) (DAP) server that enables interactive debugging of Luma programs in any DAP-capable editor.

## Features

The debugger implements a broad subset of the Debug Adapter Protocol.

### Core

| Feature              | Description                                               |
| -------------------- | --------------------------------------------------------- |
| Breakpoints          | Set breakpoints by source file and line number            |
| Step In / Over / Out | Step through code one statement at a time                 |
| Pause / Continue     | Pause a running program or resume execution               |
| Stack traces         | View the call stack with file names and line numbers      |
| Variable inspection  | Inspect local, closure, and global variables per frame    |
| Set variable         | Modify a variable's value while execution is paused       |
| Expression eval      | Evaluate expressions in the context of the current frame  |
| Console completions  | Name completions for expressions in the debug console     |
| Output capture       | `print` output is forwarded to the editor's debug console |
| Stop on entry        | Optionally pause at the first statement of `@main`        |

### Advanced breakpoints

| Feature               | Description                                           |
| --------------------- | ----------------------------------------------------- |
| Conditional           | Pause only when a per-breakpoint expression is `true` |
| Hit count             | Pause after N hits (`5`, `>3`, `>=10`, `==5`)         |
| Log points            | Emit an interpolated message instead of pausing       |
| Function breakpoints  | Break on entry to a named function                    |
| Data breakpoints      | Break when a watched variable's value changes         |
| Exception breakpoints | Break on `caught` and/or `uncaught` exceptions        |

### Specialised

| Feature            | Description                                               |
| ------------------ | --------------------------------------------------------- |
| Reverse debugging  | Step backwards through recorded VM snapshots (`stepBack`) |
| Concurrency        | Map each `task_scope` task to its own DAP thread          |
| Hot reload         | Detect source edits and reload via `luma/hotReload`       |
| Custom visualizers | Per-type value formatting loaded from a JSON config       |
| Remote debugging   | Attach over TCP with an optional authentication token     |

## Architecture

The editor launches `luma_dap` and exchanges Content-Length framed JSON messages with it over stdio, or over a TCP socket for remote debugging.

```text
┌───────────────────────────────────────────────┐
│        Editor - VS Code / Zed        │
│                  DAP client                   │
└───────────────────────────────────────────────┘
    ▼ requests          responses + events ▲
  Content-Length framed JSON over stdio or TCP
┌───────────────────────────────────────────────┐
│                    luma_dap                   │
│                                               │
│      Protocol thread    Execution thread      │
│    read/dispatch/     runs the Luma VM with   │
│    write messages     a pausing debug hook    │
│                                               │
│    shared state: mutex + condition_variable   │
└───────────────────────────────────────────────┘
```

Internally, the debugger runs two threads:

- **Protocol thread** — reads DAP requests from the transport, dispatches them, and writes responses and events back.
- **Execution thread** — runs the Luma VM with a debug hook that pauses at breakpoints and step targets.

The threads synchronise via `std::mutex` and `std::condition_variable`; the mutex ordering rules are documented in `dap_lock_ordering.hpp`.

## Module Layout

All sources live under `debugger/source/` and link against `luma_core`. Header and implementation pairs are shown as `name.hpp/cpp`.

### Entry and transport

| File                        | Responsibility                                                        |
| --------------------------- | --------------------------------------------------------------------- |
| `main.cpp`                  | Entry point: arg parsing (`--port`, `--auth-token`), stdio/TCP wiring |
| `dap_transport.hpp/cpp`     | Thread-safe Content-Length framed stdio transport                     |
| `dap_tcp_transport.hpp/cpp` | TCP transport for remote debugging                                    |
| `dap_types.hpp/cpp`         | DAP protocol constants and serialisation helpers                      |

### Protocol and dispatch

| File                        | Responsibility                                     |
| --------------------------- | -------------------------------------------------- |
| `dap_protocol_handler.hpp`  | Message framing, JSON parsing, request dispatch    |
| `dap_server.hpp/cpp`        | Builds the dispatch table and wires handler groups |
| `dap_feature_manager.hpp`   | Capability negotiation for `initialize`            |
| `dap_callback_types.hpp`    | Shared event/output callback aliases               |
| `dap_error_handler.hpp`     | Error-reporting conventions and helpers            |
| `dap_helpers.hpp`           | Shared DAP helper utilities                        |
| `dap_response_builders.hpp` | DAP response construction helpers                  |
| `dap_lock_ordering.hpp`     | Documents mutex ordering to prevent deadlocks      |

### Request handlers

| File                          | Responsibility                                               |
| ----------------------------- | ------------------------------------------------------------ |
| `dap_handler_base.hpp`        | Common base class for handler groups                         |
| `dap_handler_context.hpp/cpp` | Shared handler context (transport, session, state)           |
| `dap_handler_types.hpp`       | Handler data types (`ExecutionResult`, `PostResponseAction`) |
| `dap_lifecycle_handler.hpp`   | initialize, launch, restart, terminate, disconnect           |
| `dap_execution_handler.hpp`   | continue, step, pause, hot reload, concurrency state         |
| `dap_breakpoint_handler.hpp`  | Set and clear breakpoints of every kind                      |
| `dap_inspection_handler.hpp`  | threads, stack, scopes, variables, evaluate, completions     |
| `dap_server_breakpoints.cpp`  | Breakpoint request handler implementations                   |
| `dap_server_execution.cpp`    | Execution-control request handler implementations            |
| `dap_server_inspection.cpp`   | Inspection request handler implementations                   |

### Session and execution

| File                             | Responsibility                                        |
| -------------------------------- | ----------------------------------------------------- |
| `debug_session.hpp/cpp`          | Session lifecycle; owns the VM; compile/execute/pause |
| `debug_session_state.hpp`        | Cache of evaluated watch results                      |
| `dap_session_types.hpp`          | Session and thread type definitions                   |
| `thread_state_manager.hpp/cpp`   | Multi-thread debug state coordination                 |
| `debug_execution_engine.hpp/cpp` | Drives VM execution with debug hooks                  |
| `debug_execution_control.cpp`    | continue / step / pause control logic                 |
| `debug_execution_lifecycle.cpp`  | Execution start/stop lifecycle management             |
| `debug_execution_hooks.cpp`      | Debug hook installation and callbacks                 |
| `vm_hook_registry.hpp/cpp`       | VM execution hook registration and dispatch           |
| `vm_debug_adapter.hpp/cpp`       | VM adapter for debug introspection                    |
| `debug_stream_utils.hpp`         | Output capture and redirection utilities              |
| `vm_assert.hpp`                  | Assertion macro for an active VM pointer              |

### Breakpoints

| File                                  | Responsibility                                       |
| ------------------------------------- | ---------------------------------------------------- |
| `breakpoint_manager.hpp/cpp`          | Breakpoint storage and hit testing                   |
| `breakpoint_shared_context.hpp/cpp`   | Shared breakpoint hit snapshot and response building |
| `line_breakpoint_manager.hpp/cpp`     | Line breakpoint resolution and line snapping         |
| `function_breakpoint_manager.hpp/cpp` | Function breakpoint resolution                       |
| `data_breakpoint_manager.hpp/cpp`     | Data breakpoint (watchpoint) management              |
| `exception_breakpoint_settings.hpp`   | Atomic caught/uncaught filter flags                  |
| `compiled_breakpoint.hpp/cpp`         | Compile/validate cache for breakpoint conditions     |
| `dap_breakpoint_validator.hpp/cpp`    | Condition / hit-count / log-message validation       |

### Inspection and evaluation

| File                              | Responsibility                               |
| --------------------------------- | -------------------------------------------- |
| `variable_inspector.hpp/cpp`      | Variable scope inspection for debug views    |
| `variable_reference_registry.hpp` | Generational variable reference IDs          |
| `expression_compiler.hpp/cpp`     | Compile watch expressions to bytecode        |
| `expression_evaluator.hpp/cpp`    | Runtime evaluation for watch, hover, console |
| `custom_visualizer.hpp/cpp`       | Configurable per-type value formatting       |

### Advanced features

| File                   | Responsibility                           |
| ---------------------- | ---------------------------------------- |
| `time_travel.hpp/cpp`  | Reverse debugging via VM state snapshots |
| `hot_reloader.hpp/cpp` | Source-change detection for hot reload   |

### Source and interfaces

| File                             | Responsibility                                |
| -------------------------------- | --------------------------------------------- |
| `source_manager_locator.hpp/cpp` | Source file locator for breakpoint resolution |
| `i_source_locator.hpp`           | Source-lookup abstraction                     |
| `i_vm_control.hpp`               | VM execution-control abstraction              |
| `i_vm_introspection.hpp`         | Read-only VM state abstraction                |
| `i_filesystem_monitor.hpp`       | Filesystem-query abstraction (hot reload)     |

### Configuration and diagnostics

| File                    | Responsibility                                            |
| ----------------------- | --------------------------------------------------------- |
| `debugger_config.hpp`   | Centralised constexpr constants (limits, timeouts, sizes) |
| `debugger_messages.hpp` | Centralised error and diagnostic message strings          |
| `diagnostic_log.hpp`    | Callback-or-stderr diagnostic reporting helper            |

> For the authoritative per-file listing, see the [File Layout](../documents/Luma_Debugger.md#14--file-layout) section of the design document.

## Building

The debugger is built as part of the main CMake project. Using the CMake presets:

```bash
cmake --preset default
cmake --build --preset default
```

Or with a classic configure and build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

This produces `build/luma_dap` on Linux and macOS, or `build\Release\luma_dap.exe` on Windows with MSVC.

## Usage

In normal use you do not run `luma_dap` directly — your editor launches it automatically when you start a debug session. For remote debugging you can start it manually in TCP mode; see [Remote Debugging](#remote-debugging-tcp).

### VS Code

Add a debug configuration to `.vscode/launch.json`:

```jsonc
{
    "type": "luma",
    "request": "launch",
    "name": "Debug Luma Program",
    "program": "${file}",
    "stopOnEntry": false
}
```

### Remote Debugging (TCP)

Start the adapter in TCP mode to debug across a network or container boundary. It listens on the given port and accepts a single client:

```bash
luma_dap --port 4711 [--auth-token <token>]
```

`--port` switches from stdio to the TCP transport. `--auth-token` is optional; when set, the client must present the matching token before any other request is processed.

### Supported DAP Requests

#### Lifecycle

| Request             | Purpose                                        |
| ------------------- | ---------------------------------------------- |
| `initialize`        | Negotiate capabilities and protocol options    |
| `launch`            | Compile and start executing the target program |
| `configurationDone` | Signal that initial configuration is complete  |
| `restart`           | Restart the debug session                      |
| `terminate`         | Request graceful program termination           |
| `disconnect`        | Stop execution and terminate the debugger      |

#### Breakpoints

| Request                   | Purpose                                               |
| ------------------------- | ----------------------------------------------------- |
| `setBreakpoints`          | Set source-line breakpoints for a file (replaces all) |
| `setFunctionBreakpoints`  | Set breakpoints by function name                      |
| `setDataBreakpoints`      | Set data breakpoints (watchpoints)                    |
| `dataBreakpointInfo`      | Query whether a variable supports a data breakpoint   |
| `setExceptionBreakpoints` | Configure caught/uncaught exception filters           |
| `breakpointLocations`     | List valid breakpoint positions in a range            |

#### Execution control

| Request    | Purpose                                           |
| ---------- | ------------------------------------------------- |
| `continue` | Resume execution                                  |
| `next`     | Step over - execute one source line               |
| `stepIn`   | Step into - enter function calls                  |
| `stepOut`  | Step out - run until the current function returns |
| `stepBack` | Step backwards (reverse debugging)                |
| `pause`    | Pause execution immediately                       |

#### State inspection

| Request         | Purpose                                                     |
| --------------- | ----------------------------------------------------------- |
| `threads`       | List active threads (main and tasks)                        |
| `stackTrace`    | Return the call stack for a thread                          |
| `scopes`        | Return variable scopes (local, closure, global) for a frame |
| `variables`     | Return variables within a scope or structured value         |
| `evaluate`      | Evaluate an expression in the paused frame's context        |
| `setVariable`   | Modify a variable's value while paused                      |
| `completions`   | Return name completions for the debug console               |
| `stepInTargets` | List step-in targets for the current line                   |
| `exceptionInfo` | Return details about the current exception                  |
| `source`        | Return the contents of a source file                        |
| `loadedSources` | List all source files loaded by the program                 |

#### Custom Luma extensions

| Request                 | Purpose                                   |
| ----------------------- | ----------------------------------------- |
| `luma/hotReload`        | Reload the program after source edits     |
| `luma/concurrencyState` | Report task and channel concurrency state |

### Events (server to client)

| Event         | Meaning                                           |
| ------------- | ------------------------------------------------- |
| `initialized` | Debugger is ready to accept breakpoint requests   |
| `stopped`     | Execution paused (breakpoint, step, or exception) |
| `continued`   | Execution resumed                                 |
| `thread`      | A thread started or exited                        |
| `output`      | Program produced console output                   |
| `terminated`  | Program finished executing                        |
| `exited`      | Program exited with a status code                 |

## Design Document

See [Luma_Debugger.md](../documents/Luma_Debugger.md) for the full design document covering goals, non-goals, stepping semantics, variable inspection, concurrency support, and exception handling.
