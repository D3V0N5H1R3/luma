# Luma — Debugger

This document describes the design goals, scope, and architecture of the Luma debugger, as well as the implementation strategy, module decomposition, data flow, and DAP protocol handling.

---

## Table of Contents

1. [Overview](#1--overview)
2. [Goals](#2--goals)
3. [Non-Goals](#3--non-goals)
4. [Architecture](#4--architecture)
5. [Supported DAP Requests](#5--supported-dap-requests)
6. [Breakpoints](#6--breakpoints)
7. [Stepping](#7--stepping)
8. [Variable Inspection](#8--variable-inspection)
9. [Concurrency Support](#9--concurrency-support)
10. [Exception Handling](#10--exception-handling)
11. [Platform Support](#11--platform-support)
12. [Usage](#12--usage)
13. [Editor Integration](#13--editor-integration)
14. [File Layout](#14--file-layout)
15. [Module Responsibilities](#15--module-responsibilities)
16. [Data Flow](#16--data-flow)
17. [VM Instrumentation](#17--vm-instrumentation)
18. [Local Variable Names](#18--local-variable-names)
19. [Output Capture](#19--output-capture)
20. [Error Handling](#20--error-handling)
21. [CMake Integration](#21--cmake-integration)
22. [Testing Strategy](#22--testing-strategy)
23. [Future Extensions](#23--future-extensions)

- [See Also](#see-also)

---

## 1 — Overview

The Luma debugger implements the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) (DAP) to provide interactive debugging of Luma programs in editors. The debugger is a standalone C++ executable that communicates over standard input/output using the DAP base protocol (Content-Length framed JSON messages).

A single binary serves Visual Studio Code, Zed, and any other DAP-capable editor. It runs on Windows, Ubuntu, and macOS without platform-specific code beyond what the existing Luma interpreter already handles.

The debugger embeds the Luma VM directly. It compiles and executes the target program internally, with instrumentation hooks that enable pausing, stepping, and inspecting program state.

---

## 2 — Goals

- Allow users to set breakpoints by source file and line number.
- Pause execution on breakpoints and on unhandled exceptions.
- Support stepping: step over, step into, step out, and continue.
- Display the call stack with source locations.
- Inspect local and global variables at each stack frame.
- Evaluate expressions in the context of a paused frame.
- Support debugging of concurrent programs (`task_scope` blocks) by mapping each task to a DAP thread.
- Reuse the existing Luma lexer, parser, type checker, compiler, and VM without duplication.
- Keep the debugger self-contained: external dependencies only as exceptions for functionality beyond the C++20 standard library (see `instructions/cpp.instructions.md` §7).

---

## 3 — Non-Goals

The following features remain out of scope for the current implementation:

- Memory or performance profiling.
- Integration with the language server (the debugger and LSP run as independent processes).

---

## 4 — Architecture

```text
┌──────────────────────────────────────────────────────────────┐
│                         Editor                               │
│            (Visual Studio Code / Zed)                        │
│                                                              │
│   DAP Client  ←──  JSON over stdio  ──→  luma_dap           │
└──────────────────────────────────────────────────────────────┘
```

The editor spawns `luma_dap` as a child process and communicates via standard input/output using the DAP base protocol (`Content-Length` headers followed by JSON messages).

Internally, the debugger runs two threads:

```text
┌─────────────────────────────────────────────────────────────┐
│                       luma_dap                               │
│                                                             │
│  ┌──────────────────┐          ┌─────────────────────────┐ │
│  │  Protocol Thread │          │   Execution Thread      │ │
│  │                  │          │                         │ │
│  │  Read requests   │──cmds──▸ │  VM::run_dispatch()     │ │
│  │  Write responses │◂─events──│  Breakpoint checking    │ │
│  │  Write events    │          │  Step tracking          │ │
│  └──────────────────┘          └─────────────────────────┘ │
│                                                             │
│  Shared state (mutex-protected):                            │
│  • Breakpoint set                                           │
│  • Pause flag + condition variable                          │
│  • Variable inspection results                              │
└─────────────────────────────────────────────────────────────┘
```

### Pipeline

When the editor sends a `launch` request, the debugger runs the full compilation pipeline and then begins VM execution on the execution thread:

```text
Source file → Lexer → Parser → Include Resolver → Type Checker → Compiler → VM (instrumented)
```

The protocol thread remains responsive to DAP requests while the program runs.

---

## 5 — Supported DAP Requests

### Lifecycle

| Request             | Direction       | Purpose                                           |
| ------------------- | --------------- | ------------------------------------------------- |
| `initialize`        | Client → Server | Negotiate capabilities and protocol options       |
| `launch`            | Client → Server | Compile and start executing the target program    |
| `configurationDone` | Client → Server | Signal that all configuration requests are done   |
| `terminate`         | Client → Server | Request graceful program termination              |
| `disconnect`        | Client → Server | Stop execution and terminate the debugger process |

### Execution Control

| Request    | Direction       | Purpose                                            |
| ---------- | --------------- | -------------------------------------------------- |
| `continue` | Client → Server | Resume execution until the next breakpoint or exit |
| `next`     | Client → Server | Step over — execute one source line                |
| `stepIn`   | Client → Server | Step into — enter function calls                   |
| `stepOut`  | Client → Server | Step out — run until the current function returns  |
| `pause`    | Client → Server | Pause execution immediately                        |

### Breakpoints

| Request                   | Direction       | Purpose                                          |
| ------------------------- | --------------- | ------------------------------------------------ |
| `setBreakpoints`          | Client → Server | Set breakpoints for a source file (replaces all) |
| `setExceptionBreakpoints` | Client → Server | Configure which exceptions trigger a pause       |

### State Inspection

| Request         | Direction       | Purpose                                             |
| --------------- | --------------- | --------------------------------------------------- |
| `threads`       | Client → Server | List active threads (main + tasks)                  |
| `stackTrace`    | Client → Server | Return the call stack for a given thread            |
| `scopes`        | Client → Server | Return variable scopes (local, closure, global)     |
| `variables`     | Client → Server | Return variables within a scope or structured value |
| `evaluate`      | Client → Server | Evaluate an expression in a paused frame's context  |
| `loadedSources` | Client → Server | Return all source files loaded by the program       |

### Modification

| Request       | Direction       | Purpose                                       |
| ------------- | --------------- | --------------------------------------------- |
| `setVariable` | Client → Server | Modify a variable's value while paused        |
| `completions` | Client → Server | Return name completions for the debug console |

### Events (Server → Client)

| Event         | Direction       | Purpose                                         |
| ------------- | --------------- | ----------------------------------------------- |
| `initialized` | Server → Client | Debugger is ready to accept breakpoint requests |
| `stopped`     | Server → Client | Execution paused (breakpoint, step, exception)  |
| `continued`   | Server → Client | Execution resumed                               |
| `thread`      | Server → Client | Thread started or exited                        |
| `terminated`  | Server → Client | Program finished executing                      |
| `output`      | Server → Client | Program produced console output                 |
| `exited`      | Server → Client | Program exited with a status code               |

---

## 6 — Breakpoints

The debugger maintains a set of verified breakpoints per source file. When the editor sends `setBreakpoints`, the debugger:

1. Clears all existing breakpoints for the specified file.
2. For each requested line, finds the nearest line that contains executable bytecode (using the chunk's `source_map`).
3. Stores the verified breakpoint as a `(file_id, line)` pair.
4. Returns the verified breakpoint locations (which may differ from the requested lines).

At runtime, the VM checks after each instruction whether the source location has changed to a new line. If the new `(file_id, line)` matches a breakpoint, execution pauses.

### Conditional Breakpoints

Each breakpoint may have a `condition` expression. When the breakpoint line is reached, the debugger evaluates the condition in the current frame's context. Execution pauses only if the condition evaluates to `true`.

### Hit Count Breakpoints

Each breakpoint may have a `hitCondition` expression that controls how many hits are required before pausing. Supported formats: bare number (`5`), comparison operators (`>3`, `>=10`, `==5`). The hit count is tracked per breakpoint and incremented on every hit regardless of whether the condition passes.

When both `condition` and `hitCondition` are specified, the hit condition is evaluated first. If the hit condition is not met, the breakpoint is skipped entirely. If met, the condition is then evaluated.

### Log Points

A breakpoint with a `logMessage` attribute acts as a non-breaking tracepoint. Instead of pausing, the debugger interpolates `{expression}` placeholders in the log message and emits the result as a `console` output event. If the breakpoint also has condition/hitCondition, those are evaluated first.

### Breakpoint Line Snapping

Requested breakpoint lines are snapped to the nearest executable line using the compiled chunk's source map. If no executable line exists at or after the requested line, the breakpoint is placed at the closest preceding executable line.

---

## 7 — Stepping

The debugger tracks a `StepMode` and a reference frame depth:

| Mode   | Behaviour                                                            |
| ------ | -------------------------------------------------------------------- |
| `None` | Run freely (only stop on breakpoints or exceptions)                  |
| `Over` | Pause when the source line changes and frame depth ≤ reference depth |
| `Into` | Pause when the source line changes (regardless of depth)             |
| `Out`  | Pause when frame depth < reference depth                             |

The reference depth is captured at the moment the step command is issued.

---

## 8 — Variable Inspection

When execution is paused, the debugger exposes three scopes per stack frame:

| Scope   | Contents                                                    |
| ------- | ----------------------------------------------------------- |
| Local   | Named local variables in the current function's stack slots |
| Closure | Captured upvalues from enclosing scopes                     |
| Global  | Top-level global bindings                                   |

Variable values are formatted as human-readable strings. Structured types (arrays, dictionaries, records, tuples) are presented as expandable containers, allowing the editor to request their children via nested `variables` requests.

The `CompiledFunction` already stores `param_names`, and the compiler can be extended to emit a local variable table mapping slot indices to names and source ranges.

---

## 9 — Concurrency Support

Luma's `task_scope` blocks spawn concurrent tasks, each running its own VM instance on the thread pool. The debugger maps each task to a DAP thread:

| DAP Thread ID | Luma Concept                    |
| ------------- | ------------------------------- |
| 1             | Main thread (top-level program) |
| 2+            | Spawned tasks                   |

When a task hits a breakpoint, only that thread pauses. Other threads continue executing unless the user explicitly pauses them. The `stopped` event includes the thread ID, so the editor shows the correct call stack.

> **In depth.** For a hands-on walkthrough of debugging concurrent programs — tasks, channels, deadlocks, and common pitfalls — see the [Concurrent Debugging Guide](Luma_Concurrent_Debugging_Guide.md).

---

## 10 — Exception Handling

The debugger supports exception breakpoints via the `setExceptionBreakpoints` request. Two filters are available:

| Filter     | Behaviour                                                        |
| ---------- | ---------------------------------------------------------------- |
| `caught`   | Pause on exceptions caught by a `try`/`catch` block in user code |
| `uncaught` | Pause on unhandled exceptions (enabled by default)               |

When an exception matches an active filter, the debugger pauses execution and sends a `stopped` event with `reason: "exception"`. The exception message is included in the event's `text` and `description` fields.

When the VM throws and no exception filter is active, or if the exception propagates past all frames, the debugger still pauses on the unhandled exception and allows the user to inspect the final state.

The `stopped` event includes `allThreadsStopped: true` for both exception and breakpoint stops.

---

## 11 — Platform Support

The debugger is written against the C++20 standard library and communicates over `stdin`/`stdout`. Threading uses `std::thread`, `std::mutex`, and `std::condition_variable`.

The one place platform-specific handling is required is the shared stdio transport: on Windows the binary sets `stdin`, `stdout`, and `stderr` to binary mode (`_setmode`) so `\r\n` translation cannot corrupt `Content-Length` framing, and the transport guards its input polling behind `_WIN32` / POSIX branches. This handling is shared with the language server and documented authoritatively in [Luma_Language_Server.md §23](Luma_Language_Server.md#23--platform-specific-handling).

The debugger binary is named `luma_dap` (or `luma_dap.exe` on Windows).

---

## 12 — Usage

### Building

The `luma_dap` target is part of the main CMake build. No extra flags are needed:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

This produces `build/luma_dap` on Linux and macOS, or `build\Release\luma_dap.exe` on Windows with MSVC.

### Running

The debugger reads DAP messages from **stdin** and writes responses/events to **stdout**. Program output (`print`) is captured and sent as `output` events rather than written to the debugger's stdout.

Editors start the debugger automatically via launch configurations. For manual testing:

```bash
build/luma_dap              # Linux / macOS
build\Release\luma_dap.exe  # Windows (MSVC)
```

The debugger blocks on stdin, waiting for DAP messages. Send an `initialize` request followed by a `launch` request to begin a session. Send `disconnect` to stop.

---

## 13 — Editor Integration

### Visual Studio Code

The existing VS Code extension (`extensions/vscode/`) gains a debug adapter contribution in `package.json`:

```json
{
    "contributes": {
        "debuggers": [
            {
                "type": "luma",
                "label": "Luma Debug",
                "program": "${extensionPath}/bin/luma_dap",
                "languages": ["luma"],
                "configurationAttributes": {
                    "launch": {
                        "required": ["program"],
                        "properties": {
                            "program": {
                                "type": "string",
                                "description": "Path to the Luma program to debug."
                            },
                            "stopOnEntry": {
                                "type": "boolean",
                                "description": "Pause on the first executable line.",
                                "default": false
                            }
                        }
                    }
                }
            }
        ]
    }
}
```

A concrete `launch.json` example that uses these attributes is maintained in [CONTRIBUTING.md](../CONTRIBUTING.md#editor-integration), the canonical reference for editor configuration.

### Zed

The Zed extension adds a debug adapter entry to `extension.toml` pointing to the `luma_dap` binary, using `"custom"` transport with stdio.

---

## 14 — File Layout

```text
debugger/source/
├── breakpoint_manager.hpp             # BreakpointManager class declaration
├── breakpoint_manager.cpp             # Breakpoint storage and hit testing
├── breakpoint_shared_context.hpp      # Shared breakpoint response building
├── breakpoint_shared_context.cpp      # Shared breakpoint response building impl
├── compiled_breakpoint.hpp            # CompiledBreakpoint for bytecode-level breakpoints
├── compiled_breakpoint.cpp            # Compiled breakpoint implementation
├── custom_visualizer.hpp              # CustomVisualizer class declaration
├── custom_visualizer.cpp              # Custom variable display formatting
├── dap_breakpoint_handler.hpp         # Breakpoint request handler group
├── dap_breakpoint_validator.hpp       # BreakpointValidator class declaration
├── dap_breakpoint_validator.cpp       # Breakpoint validation logic
├── dap_callback_types.hpp             # DAP callback type definitions
├── dap_error_handler.hpp              # Error handling utilities
├── dap_execution_handler.hpp          # Execution control handler group
├── dap_feature_manager.hpp            # Feature capability manager
├── dap_handler_base.hpp               # DapHandler base class for handler groups
├── dap_handler_context.hpp            # Handler context class declaration
├── dap_handler_context.cpp            # Handler context implementation
├── dap_handler_types.hpp              # Handler data types (ExecutionResult, PostResponseAction)
├── dap_helpers.hpp                    # Shared DAP helper utilities
├── dap_inspection_handler.hpp         # Inspection request handler group
├── dap_lifecycle_handler.hpp          # Lifecycle request handler group
├── dap_lock_ordering.hpp              # Lock ordering declarations
├── dap_protocol_handler.hpp           # Protocol handler interface
├── dap_response_builders.hpp          # DAP response construction helpers
├── dap_server.hpp                     # DapServer class declaration
├── dap_server.cpp                     # DapServer: lifecycle, dispatch, response building
├── dap_server_breakpoints.cpp         # Breakpoint-related request handlers
├── dap_server_execution.cpp           # Execution control request handlers
├── dap_server_lifecycle.cpp           # Lifecycle request handlers (initialize/launch/disconnect/restart)
├── dap_server_inspection.cpp          # Variable/stack inspection request handlers
├── dap_session_types.hpp              # Session type definitions (ThreadId, ThreadState)
├── dap_tcp_transport.hpp              # TCP transport class declaration
├── dap_tcp_transport.cpp              # TCP transport for remote debugging
├── dap_transport.hpp                  # Transport class declaration (Content-Length framing)
├── dap_transport.cpp                  # Read/write DAP messages over stdio
├── dap_types.hpp                      # DAP protocol type definitions and constants
├── dap_types.cpp                      # DAP type serialisation helpers
├── data_breakpoint_manager.hpp        # Data breakpoint management
├── data_breakpoint_manager.cpp        # Data breakpoint management implementation
├── debug_execution_control.cpp        # Execution control logic (continue, step, pause)
├── debug_execution_engine.hpp         # DebugExecutionEngine class declaration
├── debug_execution_engine.cpp         # VM execution with debug hooks
├── debug_execution_hooks.cpp          # VM debug hook installation and callbacks
├── debug_execution_lifecycle.cpp      # Execution lifecycle management
├── debug_session.hpp                  # DebugSession class declaration (owns the VM)
├── debug_session.cpp                  # DebugSession: compile, execute, pause/resume, inspect
├── debug_session_state.hpp            # WatchCache — caches evaluated watch results
├── debug_stream_utils.hpp             # Stream utility helpers
├── debugger_config.hpp                # Centralised constexpr constants
├── debugger_messages.hpp              # Centralised error/diagnostic message strings
├── diagnostic_log.hpp                 # report_or_log: callback-or-stderr diagnostics
├── exception_breakpoint_settings.hpp  # Exception breakpoint configuration
├── expression_compiler.hpp            # ExpressionCompiler class declaration
├── expression_compiler.cpp            # Compile watch expressions to bytecode
├── expression_evaluator.hpp           # ExpressionEvaluator class declaration
├── expression_evaluator.cpp           # Runtime expression evaluation for watch and hover
├── function_breakpoint_manager.hpp    # Function breakpoint detail types
├── function_breakpoint_manager.cpp    # Function breakpoint resolution
├── hot_reloader.hpp                   # HotReloader class declaration
├── hot_reloader.cpp                   # Hot code reload (edit and continue)
├── i_filesystem_monitor.hpp           # Filesystem monitoring interface
├── i_source_locator.hpp               # Source file locator interface
├── i_vm_control.hpp                   # VM control interface for debugging
├── i_vm_introspection.hpp             # VM introspection interface
├── line_breakpoint_manager.hpp        # Line breakpoint detail types
├── line_breakpoint_manager.cpp        # Line breakpoint resolution
├── main.cpp                           # Entry point: launch protocol thread, start message loop
├── source_manager_locator.hpp         # Source file locator class declaration
├── source_manager_locator.cpp         # Source file locator implementation
├── thread_state_manager.hpp           # ThreadStateManager class declaration
├── thread_state_manager.cpp           # Multi-thread debug state coordination
├── time_travel.hpp                    # TimeTravel class declaration
├── time_travel.cpp                    # Reverse debugging (step backwards)
├── variable_inspector.hpp             # VariableInspector class declaration
├── variable_inspector.cpp             # Variable scope inspection for debug views
├── variable_reference_registry.hpp    # Variable reference ID management
├── vm_assert.hpp                      # VM assertion macro and lifecycle documentation
├── vm_debug_adapter.hpp               # VM debug adapter class declaration
├── vm_debug_adapter.cpp               # VM debug adapter implementation
├── vm_hook_registry.hpp               # VmHookRegistry class declaration
└── vm_hook_registry.cpp               # VM execution hook registration and dispatch
```

All files live under `debugger/source/` at the repository root, parallel to `language-server/source/`, `core/`, and `tests/`. The debugger links against `luma_core` (lexer, parser, type checker, compiler, VM, stdlib) to execute programs.

---

## 15 — Module Responsibilities

### `main.cpp` — Entry Point

Initialises the transport and server, then enters the message loop:

```cpp
int main() {
    luma::dap::Transport transport;
    luma::dap::DapServer server(transport);
    return server.run();
}
```

On Windows, sets `stdin`, `stdout`, and `stderr` to binary mode to prevent `\r\n` translation.

### `dap_transport.hpp` — Message Framing (Shared Transport)

`dap_transport.hpp` re-exports the shared stdio transport under the debugger's namespace: `using luma::dap::Transport = luma::protocol::StdioTransport;`. The Content-Length framing, thread-safe writes, optional read timeout, and shared JSON value type (`luma::json::JsonValue` from `shared/json/`) are exactly those the language server uses.

That shared transport and JSON type are documented authoritatively in the [Language Server](Luma_Language_Server.md) design document — see §15 (Module Responsibilities) and §23 (Platform-Specific Handling). A companion `dap_tcp_transport` provides the same framing over a TCP socket for remote debugging.

### `dap_types.hpp` / `dap_types.cpp` — Protocol Types

Defines C++ types for DAP structures and provides serialisation to/from `JsonValue`:

```cpp
namespace luma::dap {

// ─── Stop reasons ───
constexpr std::string_view kStopReasonBreakpoint = "breakpoint";
constexpr std::string_view kStopReasonStep       = "step";
constexpr std::string_view kStopReasonException  = "exception";
constexpr std::string_view kStopReasonPause      = "pause";
constexpr std::string_view kStopReasonEntry      = "entry";

// ─── Output categories ───
constexpr std::string_view kOutputConsole  = "console";
constexpr std::string_view kOutputStdout   = "stdout";
constexpr std::string_view kOutputStderr   = "stderr";

// ─── Types ───

// Incoming breakpoint request from the editor.
struct BreakpointRequest {
    int line{0};
    std::string condition;
    std::string hit_condition;
    std::string log_message;
};

struct Source {
    std::string name;       // File name (e.g., "main.luma")
    std::string path;       // Absolute file path
};

struct Breakpoint {
    int id{0};
    bool verified{false};
    Source source;
    int line{0};            // Verified line (may differ from requested)
    std::string message;    // Reason if not verified
};

struct StackFrame {
    int id{0};              // Unique frame ID
    std::string name;       // Function name
    Source source;
    int line{0};
    int column{0};
};

struct Scope {
    std::string name;       // "Local", "Closure", "Global"
    int variables_reference{0};
    bool expensive{false};  // True for globals (may be large)
};

struct Variable {
    std::string name;
    std::string value;              // Display string
    std::string type;               // Type name
    int variables_reference{0};     // Non-zero if expandable
    int named_variables{0};         // Number of named children
    int indexed_variables{0};       // Number of indexed children
};

enum class StepMode { None, Over, Into, Out };

// Serialisation helpers.
[[nodiscard]] JsonValue serialise_source(const Source& src);
[[nodiscard]] JsonValue serialise_breakpoint(const Breakpoint& bp);
[[nodiscard]] JsonValue serialise_stack_frame(const StackFrame& frame);
[[nodiscard]] JsonValue serialise_scope(const Scope& scope);
[[nodiscard]] JsonValue serialise_variable(const Variable& var);

} // namespace luma::dap
```

### `dap_server.hpp` / `dap_server.cpp` — Protocol Dispatch

Manages the DAP message loop, dispatches requests and sends responses/events:

```cpp
namespace luma::dap {

class DapServer {
public:
    explicit DapServer(Transport& transport);

    // Run the message loop until disconnect. Returns exit code.
    [[nodiscard]] int run();

private:
    // Handler type: takes arguments, returns response body.
    using Handler = std::function<JsonValue(const JsonValue&)>;

    // Build the command → handler dispatch table.
    void init_dispatch_table();

    // ─── Lifecycle ───
    [[nodiscard]] JsonValue handle_initialize(const JsonValue& args);
    [[nodiscard]] JsonValue handle_launch(const JsonValue& args);
    [[nodiscard]] JsonValue handle_disconnect(const JsonValue& args);
    [[nodiscard]] JsonValue handle_terminate(const JsonValue& args);

    // ─── Execution control ───
    [[nodiscard]] JsonValue handle_continue(const JsonValue& args);
    [[nodiscard]] JsonValue handle_next(const JsonValue& args);
    [[nodiscard]] JsonValue handle_step_in(const JsonValue& args);
    [[nodiscard]] JsonValue handle_step_out(const JsonValue& args);
    [[nodiscard]] JsonValue handle_pause(const JsonValue& args);

    // ─── Breakpoints ───
    [[nodiscard]] JsonValue handle_set_breakpoints(const JsonValue& args);
    [[nodiscard]] JsonValue handle_set_exception_breakpoints(const JsonValue& args);

    // ─── State inspection ───
    [[nodiscard]] JsonValue handle_threads(const JsonValue& args);
    [[nodiscard]] JsonValue handle_stack_trace(const JsonValue& args);
    [[nodiscard]] JsonValue handle_scopes(const JsonValue& args);
    [[nodiscard]] JsonValue handle_variables(const JsonValue& args);
    [[nodiscard]] JsonValue handle_evaluate(const JsonValue& args);

    // ─── Modification ───
    [[nodiscard]] JsonValue handle_set_variable(const JsonValue& args);
    [[nodiscard]] JsonValue handle_completions(const JsonValue& args);
    [[nodiscard]] JsonValue handle_loaded_sources(const JsonValue& args);

    // ─── Response/event helpers ───
    void send_response(int request_seq, const std::string& command,
                       const JsonValue& body, bool success = true,
                       const std::string& message = "");
    void send_event(const std::string& event, const JsonValue& body);

    // ─── State ───
    Transport& transport_;
    std::unique_ptr<DebugSession> session_;
    int sequence_number_{1};
    bool disconnected_{false};
    std::unordered_map<std::string, Handler> dispatch_table_;
    std::unordered_map<std::string, std::vector<BreakpointRequest>> pending_breakpoints_;
};

} // namespace luma::dap
```

The dispatch table maps DAP command strings to handler methods:

```cpp
void DapServer::handle_request(const std::string& command,
                               const JsonValue& arguments,
                               int seq) {
    if (command == "initialize")     { /* ... */ }
    else if (command == "launch")    { /* ... */ }
    else if (command == "continue")  { /* ... */ }
    // ...
}
```

### `debug_session.hpp` / `debug_session.cpp` — VM Execution and Debug State

The core debugging logic. Owns the VM instance, manages breakpoints, and coordinates the execution thread:

```cpp
namespace luma::dap {

class DebugSession {
public:
    using OutputFn = std::function<void(const std::string& category,
                                             const std::string& text)>;
    using EventCallback = std::function<void(const std::string& event,
                                            const JsonValue& body)>;

    explicit DebugSession(EventCallback event_cb, OutputFn output_cb);

    // ─── Lifecycle ───
    // Compile and start executing the program on a background thread.
    // Returns an error message on compilation failure, or empty on success.
    [[nodiscard]] std::string launch(const std::string& program_path,
                                     bool stop_on_entry);

    // Stop execution and join the background thread.
    void terminate();

    // ─── Breakpoints ───
    [[nodiscard]] std::vector<Breakpoint> set_breakpoints(
        const std::string& path, const std::vector<BreakpointRequest>& breakpoints);
    void set_exception_breakpoints(const std::vector<std::string>& filters);

    // ─── Execution control ───
    void continue_execution(int thread_id);
    void step_over(int thread_id);
    void step_into(int thread_id);
    void step_out(int thread_id);
    void pause(int thread_id);

    // ─── State inspection (only valid when paused) ───
    [[nodiscard]] std::vector<StackFrame> get_stack_trace(int thread_id) const;
    [[nodiscard]] std::vector<Scope> get_scopes(int frame_id) const;
    [[nodiscard]] std::vector<Variable> get_variables(int reference) const;
    [[nodiscard]] Variable evaluate(int frame_id,
                                    const std::string& expression) const;

    // ─── Modification ───
    [[nodiscard]] Variable set_variable(int variables_reference,
                                        const std::string& name,
                                        const std::string& value) const;

    // ─── Completions ───
    [[nodiscard]] std::vector<std::pair<std::string, std::string>>
    get_completions(int frame_id, const std::string& text) const;

    // ─── Sources ───
    [[nodiscard]] std::vector<Source> get_loaded_sources() const;

    // ─── Thread listing ───
    [[nodiscard]] std::vector<std::pair<int, std::string>> get_threads() const;

private:
    // ─── Debug hook (called by instrumented VM after each line change) ───
    // Returns true if execution should pause.
    bool should_break(FileId file_id, int line, std::size_t frame_depth);

    // Block the execution thread until resumed by a continue/step command.
    // Returns true to continue execution, false to terminate.
    [[nodiscard]] bool wait_for_resume();

    // ─── Variable reference helpers ───
    [[nodiscard]] int alloc_ref(VariableRefEntry entry) const;
    void clear_refs() const;
    [[nodiscard]] Variable make_variable(const std::string& name, const Value& val) const;

    // ─── Breakpoint helpers ───
    // Returns a reference into the internal source-map cache to avoid copying
    // the executable-line set on every breakpoint query; valid while the
    // breakpoint mutex is held.
    [[nodiscard]] const std::set<int>& collect_executable_lines(FileId file_id) const;
    [[nodiscard]] int snap_line(int requested, const std::set<int>& executable) const;
    [[nodiscard]] static Value parse_value(const std::string& str);
    bool on_exception(const std::string& message, bool is_caught);

    // ─── State ───
    EventCallback event_callback_;
    OutputFn output_callback_;

    std::jthread execution_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> terminated_{false};
    std::atomic<bool> terminate_requested_{false};

    // Per-thread debugging state (decomposed into sub-structs).
    // See dap_session_types.hpp for StepState / PendingEvents definitions.
    struct ThreadState {
        int thread_id{0};
        std::string name;
        VM* vm{nullptr};                                    // GUARDED_BY(mutex)

        mutable std::mutex mutex;
        std::condition_variable cv;
        bool is_paused{false};                              // GUARDED_BY(mutex)
        bool is_exception_terminated{false};                // GUARDED_BY(mutex)
        StepState step;                                     // GUARDED_BY(mutex)
        PendingEvents pending;                              // GUARDED_BY(mutex)
    };

    // Breakpoints: file_id → (line → BreakpointInfo).
    struct BreakpointInfo {
        int id{0};
        int line{0};
        std::string condition;
        std::string hit_condition;
        std::string log_message;
        int hit_count{0};
    };

    mutable std::mutex breakpoint_mutex_;
    std::unordered_map<int, std::map<int, BreakpointInfo>> breakpoints_;

    // Exception breakpoint filters.
    std::atomic<bool> break_on_caught_{false};
    std::atomic<bool> break_on_uncaught_{false};
    std::string pending_exception_message_;
    int pending_hit_breakpoint_id_{0};

    // Compiled program.
    std::unique_ptr<SourceManager> source_manager_;
    std::shared_ptr<std::vector<CompiledFunction>> compiled_functions_;
    std::shared_ptr<CompiledFunction> compiled_top_level_;

    // VM instance (created on execution thread).
    std::unique_ptr<VM> vm_;

    // Variable reference registry.
    mutable std::unordered_map<int, VariableRefEntry> ref_registry_;
    mutable int next_ref_id_{1};
    int next_breakpoint_id_{1};
};

} // namespace luma::dap
```

### Additional Modules

The following modules were added to support advanced debugging features:

| Module                            | Responsibility                                                                                                                |
| --------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| `compiled_breakpoint`             | Compile/validate cache for breakpoint condition/log expressions; runtime evaluation is performed by `debug_execution_engine`. |
| `custom_visualizer`               | User-configurable display formatting for complex values (arrays, dictionaries, records).                                      |
| `dap_response_builders.hpp`       | Helpers to construct well-formed DAP response and event JSON.                                                                 |
| `dap_session_types.hpp`           | Shared type definitions used across handler files.                                                                            |
| `debug_execution_engine`          | Drives the VM with debug hooks installed; separates execution logic from session state.                                       |
| `debug_session_state`             | WatchCache — caches evaluated watch expressions. Session state is managed directly by DapServer.                              |
| `debug_stream_utils.hpp`          | Stream capture and redirection utilities for output events.                                                                   |
| `expression_compiler`             | Compiles watch/hover expressions to bytecode for evaluation in a paused frame.                                                |
| `hot_reloader`                    | Hot code reload: recompile changed source and patch the running VM without restarting.                                        |
| `thread_state_manager`            | Coordinates debug state across multiple concurrent task threads.                                                              |
| `time_travel`                     | Reverse debugging: records execution snapshots and restores an earlier snapshot's value stack on step-back.                   |
| `variable_reference_registry.hpp` | Allocates and tracks variable reference IDs across inspection requests.                                                       |
| `vm_hook_registry`                | Central registry for VM execution hooks (breakpoints, stepping, coverage).                                                    |

---

## 16 — Data Flow

### Launch Sequence

1. Editor sends `initialize` request.
2. Debugger responds with capabilities (supports breakpoints, stepping, variable inspection).
3. Debugger sends `initialized` event.
4. Editor sends `setBreakpoints` for each file with breakpoints.
5. Editor sends `launch` with the program path.
6. Debugger compiles the program. On failure, responds with an error.
7. On success, spawns the execution thread and begins VM dispatch.
8. If `stopOnEntry` is true, pauses before the first instruction and sends `stopped` event.

### Breakpoint Hit

1. Execution thread: VM executes an instruction that moves to a new source line.
2. `should_break()` checks the new `(file_id, line)` against the breakpoint set.
3. Match found → sets `is_paused = true`, sends `stopped` event via callback.
4. Execution thread blocks on `cv`.
5. Protocol thread receives `stackTrace`, `scopes`, `variables` requests and services them from the paused VM state.
6. Protocol thread receives `continue` → sets `step.mode = StepMode::None`, signals `cv`.
7. Execution thread wakes and resumes dispatch.

### Program Termination

1. VM's `run_dispatch()` returns (program finished or unhandled exception).
2. Execution thread sets `terminated_ = true`.
3. Sends `terminated` event, then `exited` event with exit code.
4. Protocol thread receives `disconnect` and exits cleanly.

---

## 17 — VM Instrumentation

The existing `VM::run_dispatch()` loop requires minimal modification. A debug hook is inserted at the point where the source location changes:

```cpp
Value VM::run_dispatch() {
    // ... existing setup ...
    for (;;) {
        // ─── Debug hook (new) ───
        if (debug_hook_) {
            auto loc = current_location();
            if (loc.line != last_debug_line_ || loc.file_id != last_debug_file_) {
                last_debug_line_ = loc.line;
                last_debug_file_ = loc.file_id;
                if (debug_hook_(loc.file_id, loc.line, frames_.size())) {
                    // Pause requested — block until resumed.
                    debug_pause_callback_();
                }
            }
        }

        // ─── Existing dispatch ───
        auto op = static_cast<Op>(read_byte());
        switch (op) {
            // ... existing cases ...
        }
    }
}
```

The hook is a `std::function` set by `DebugSession`. When no debugger is attached (normal execution via `luma` CLI), the hook is null and the `if` branch is never taken — zero overhead for non-debug runs.

### Required VM Interface Additions

```cpp
class VM {
public:
    // Debug hook type: (file_id, line, frame_depth) → should_pause
    using DebugHook = std::function<bool(FileId, int, std::size_t)>;
    using PauseCallback = std::function<void()>;

    // Set debug hooks (called by DebugSession before execution).
    void set_debug_hook(DebugHook hook);
    void set_pause_callback(PauseCallback callback);

    // Expose state for inspection while paused.
    [[nodiscard]] const std::vector<CallFrame>& frames() const;
    [[nodiscard]] const std::vector<Value>& stack() const;

private:
    DebugHook debug_hook_;
    PauseCallback debug_pause_callback_;
    int last_debug_line_{-1};
    int last_debug_file_{-1};
};
```

---

## 18 — Local Variable Names

The compiler currently stores parameter names in `CompiledFunction::param_names` but does not retain names for local variables. To support variable inspection, the compiler emits a local variable table:

```cpp
struct LocalVarInfo {
    std::string name;
    int slot;               // Stack slot relative to frame base
    std::size_t start_ip;   // First instruction where the variable is live
    std::size_t end_ip;     // Last instruction where the variable is live
};
```

This table is stored in `CompiledFunction` alongside the existing `param_names`. The debugger uses it to map stack slot values to named variables in the `variables` response.

---

## 19 — Output Capture

Program output via `print` and related stdlib functions normally writes to `stdout`. When running under the debugger, stdout is reserved for DAP protocol messages.

The debugger redirects program output by injecting a custom output handler into the VM's environment. The handler routes output text to `DebugSession::output_callback_`, which sends DAP `output` events to the editor.

---

## 20 — Error Handling

| Condition                   | Behaviour                                                     |
| --------------------------- | ------------------------------------------------------------- |
| Compilation error           | `launch` response with `success: false` and error message     |
| Unhandled runtime exception | `stopped` event with `reason: "exception"` and exception text |
| Malformed DAP message       | Ignored (logged to stderr if available)                       |
| Unknown request command     | Error response with `"command not supported"` message         |
| Inspection while running    | Error response with `"not paused"` message                    |
| `disconnect` while running  | Terminates VM execution, joins thread, exits                  |

---

## 21 — CMake Integration

The `luma_dap` target is added to the top-level `CMakeLists.txt`:

```cmake
# ─── Debugger (DAP) ───
add_executable(luma_dap
    debugger/main.cpp
    debugger/dap_server.cpp
    debugger/dap_transport.cpp
    debugger/dap_types.cpp
    debugger/debug_session.cpp
    shared/json/json.cpp          # Shared JSON implementation
)
target_link_libraries(luma_dap PRIVATE luma_core)
target_include_directories(luma_dap PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/server)
```

The debugger links `luma_core` (which contains the lexer, parser, type checker, compiler, VM, and stdlib) and reuses the JSON implementation from `shared/json/json.cpp`.

---

## 22 — Testing Strategy

### Unit Tests

A `dap_test` target exercises the debug components in isolation:

- **Protocol serialisation:** Verify that DAP types serialise to correct JSON with all required fields.
- **Response/event structure:** Verify that `send_response` and `send_event` produce spec-compliant messages.
- **Capabilities:** Verify the `initialize` response includes all advertised capabilities and exception breakpoint filters.
- **Variable formatting:** Verify that Luma `Value` types are formatted correctly for DAP responses.

### Integration Tests

End-to-end tests that spawn `luma_dap` as a subprocess, send DAP messages via stdin, and verify responses:

- Launch a simple program with a breakpoint, verify `stopped` event.
- Step through a function call, verify stack trace changes.
- Inspect variables at a breakpoint, verify correct names and values.
- Continue past a breakpoint, verify `terminated` event.
- Launch a program with a compile error, verify error response.
- Set conditional and hit count breakpoints, verify correct firing.
- Configure exception breakpoints, verify exception stops.

### Test Programs

Small Luma programs in `examples/debug/` designed for debugger testing:

```text
examples/debug/
├── breakpoint_basic.luma      # Simple breakpoint on a print statement
├── closure_variables.luma     # Captured upvalues for closure-scope inspection
├── concurrent_tasks.luma      # task_scope with multiple tasks
├── conditional_loop.luma      # Loop with conditional logic for stepping
├── data_breakpoint.luma       # Variable mutation for data breakpoint testing
├── exception_caught.luma      # try/catch for the 'caught' exception filter
├── exception_unhandled.luma   # Triggers an unhandled exception
├── function_breakpoint.luma   # Named function for function breakpoint testing
├── long_loop.luma             # Long-running loop for pause testing
├── recursive_stack.luma       # Deep recursion for call-stack and step-out testing
├── set_variable.luma          # Various variable types for setVariable testing
├── step_into_function.luma    # Function call for step-in testing
├── step_over_loop.luma        # Loop for step-over testing
├── structured_values.luma     # Records and choice types for expandable variables
└── variables_basic.luma       # Various variable types for inspection
```

---

## 23 — Future Extensions

Once the current implementation is stable, the following features could be added:

- **Attach mode:** Connect to an already-running Luma program (TCP transport layer is already implemented).
- **Inline values:** Report variable values inline in the editor (DAP `InlineValue` capability).
- **Disassembly view:** Show compiled bytecode instructions.

---

## See Also

- [Concurrent Debugging Guide](Luma_Concurrent_Debugging_Guide.md) — debugging tasks and channels
- [Contributing](../CONTRIBUTING.md) — configuring editors to launch the debugger
- [Language Server](Luma_Language_Server.md) — the companion LSP language server
- [Software Architecture](Luma_Software_Architecture.md) — how the debug adapter fits into the interpreter
