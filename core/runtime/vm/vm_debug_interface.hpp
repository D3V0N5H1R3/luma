// ─────────────────────────────────────────────────────────────────────────────
// VM Debug Interface
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: own the VM↔debugger integration state and mediate all
// thread-safe access to the DAP callback set.  This is the "VMDebugInterface"
// seam of TODO(refactor/V1) — extracted from VM's former nested DebugContext
// so the debug concern is a first-class, independently documented component,
// mirroring the VMStack / VMExceptionManager / VMGlobalCache composition.
//
// State owned:
//   callbacks        — hook functions set by the debugger:
//                      DebugHook          (line-change / step notifications)
//                      PauseCallback      (blocks until the debugger resumes)
//                      ExceptionHook      (notifies on caught/uncaught exceptions)
//                      DataBreakpointHook (variable-write watchpoints)
//                      TaskSpawnHook / TaskExitHook (concurrency events)
//   last_line        — most recently reported source line (suppresses
//                      duplicate notifications on the same line)
//   last_file        — most recently reported source file index
//   pause_requested  — atomic flag set by the protocol thread to request a
//                      pause at the next line change (thread-safe)
//
// Locking discipline: callbacks are read from the execution thread and written
// from the debugger's protocol thread, so all callback access goes through the
// set_*/copy_* helpers, which hold callbacks_mutex (exclusive for writes,
// shared for reads).  Copies are taken under the lock and invoked after it is
// released, so a blocking callback never holds the mutex.
//
// Coupling note: the frame/stack-touching NOTIFICATION logic
// (VM::check_debug_hooks(), VM::notify_*_data_breakpoint()) stays on VM — it
// needs the call frames, value stack, and instruction pointer to report state
// to the debugger.  This component owns only the state and the thread-safe
// accessors; it never touches the VM's execution state.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_VM_VM_DEBUG_INTERFACE_HPP
#define LUMA_VM_VM_DEBUG_INTERFACE_HPP

#include <atomic>
#include <shared_mutex>
#include <utility>

#include "runtime/vm/vm_debug_types.hpp"

namespace luma {

/// Owns the VM's debugger-integration state and guards concurrent access to the
/// callback set.  A non-copyable, non-movable component (it holds a shared_mutex
/// and an atomic); the VM transfers its data members field-by-field under lock
/// in VM::transfer_state() rather than moving the object as a whole.
class VMDebugInterface {
public:
    DebugCallbacks callbacks;
    int last_line{-1};
    int last_file{-1};
    std::atomic<bool> pause_requested{false};
    mutable std::shared_mutex callbacks_mutex;

    /// Set a single callback field under the exclusive lock.
    template <typename Field, typename Callback>
    void set_callback(Field DebugCallbacks::*field, Callback callback) {
        const std::unique_lock lock(callbacks_mutex);
        callbacks.*field = std::move(callback);
    }

    /// Replace all callbacks under the exclusive lock.
    void set_all(DebugCallbacks new_callbacks) {
        const std::unique_lock lock(callbacks_mutex);
        callbacks = std::move(new_callbacks);
    }

    /// Copy a single callback field under a shared lock.  The caller invokes the
    /// returned copy after the lock is released, to avoid holding the mutex
    /// during a potentially blocking callback.
    template <typename Field>
    [[nodiscard]] auto copy_hook(Field DebugCallbacks::*field) const -> Field {
        const std::shared_lock lock(callbacks_mutex);
        return callbacks.*field;
    }

    /// Copy all callbacks under a shared lock.
    [[nodiscard]] DebugCallbacks copy_all() const {
        const std::shared_lock lock(callbacks_mutex);
        return callbacks;
    }
};

} // namespace luma

#endif // LUMA_VM_VM_DEBUG_INTERFACE_HPP
