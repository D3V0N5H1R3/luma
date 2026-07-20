// ─────────────────────────────────────────────────────────────────────────────
// VM Debug Hook Type Definitions
// ─────────────────────────────────────────────────────────────────────────────
// Extracted from vm.hpp to keep the main header focused on execution concerns.
// These type aliases define the callback signatures used by the debugger to
// integrate with the VM.
//
// The VM supports 6 hook types for debugger integration:
//
//   DebugHook          — fires on each source-line change during execution.
//                        Receives (file_id, line, frame_depth) and returns
//                        true to request a pause.  Used for stepping and
//                        breakpoint evaluation.
//   PauseCallback      — fires when DebugHook requests a pause.  The VM
//                        blocks inside this callback until the debugger
//                        resumes.  Returns true to continue, false to stop.
//   ExceptionHook      — fires when a RuntimeError is about to be caught
//                        by try/catch.  Receives (message, is_caught) and
//                        returns true to pause (exception breakpoints).
//   DataBreakpointHook — fires when a named variable is written.  Receives
//                        the variable name and returns true to pause.
//   TaskSpawnHook      — fires when a new task is spawned.  Receives the
//                        child VM and task ID.  Used to track threads.
//   TaskExitHook       — fires when a task completes.  Receives the task
//                        ID.  Used to clean up thread tracking.
//
// Hooks are registered by DebugExecutionEngine via VmHookRegistry
// (debugger/source/vm_hook_registry.hpp).
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_VM_VM_DEBUG_TYPES_HPP
#define LUMA_VM_VM_DEBUG_TYPES_HPP

#include <cstddef>
#include <functional>
#include <string>

namespace luma {

class VM;

// Debug hook type: (file_id, line, frame_depth) → should_pause.
using DebugHook = std::function<bool(int, int, std::size_t)>;

// Pause callback: called when the debug hook requests a pause.
// The VM blocks inside this callback until the debugger resumes execution.
// Returns true to continue execution, false to terminate.
using PauseCallback = std::function<bool()>;

// Exception hook: called when a RuntimeError is about to be caught
// by a try/catch block.  The debugger can use this to implement
// exception breakpoints.  Signature: (message, is_caught) → should_pause.
using ExceptionHook = std::function<bool(const std::string&, bool)>;

// Data breakpoint hook: called when a named variable is written.
// Returns true if the write should trigger a pause.
using DataBreakpointHook = std::function<bool(const std::string&)>;

// Task hooks: called when a task is spawned or completes.
// Used by the debugger to track threads.
using TaskSpawnHook = std::function<void(VM&, int)>;
using TaskExitHook = std::function<void(int)>;

// Consolidated debug callback struct grouping all hook types.
struct DebugCallbacks {
    DebugHook debug_hook;
    PauseCallback pause_callback;
    ExceptionHook exception_hook;
    DataBreakpointHook data_breakpoint_hook;
    TaskSpawnHook task_spawn_hook;
    TaskExitHook task_exit_hook;
};

} // namespace luma

#endif // LUMA_VM_VM_DEBUG_TYPES_HPP
