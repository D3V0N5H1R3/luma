// ─────────────────────────────────────────────────────────────────────────────
// VM Task Manager
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: own the VM's structured-concurrency state — the LIFO stack of
// active task_scope { } blocks, the thread pool, the monotonic task-id counter,
// and the thread-local "innermost active scope" pointer.  This is the
// "VMTaskManager" seam of TODO(refactor/V1), consolidating state that was
// previously split across VM's nested ConcurrencyContext struct, the separate
// VM::current_scope_ thread-local, and VM::pool() in vm_calls.cpp.
//
// State owned:
//   task_scopes    — LIFO stack of active task_scope { } blocks.  Op::TaskScopeBegin
//                    pushes a scope; Op::TaskScopeEnd (and exception unwinding)
//                    joins/cancels and pops it.
//   owned_pool     — thread pool owned by a top-level (root) VM.
//   shared_pool    — non-owning pointer used by child VMs spawned into an
//                    existing pool.
//   next_task_id   — monotonically increasing task ID for debugger correlation.
//   current_scope  — thread-local pointer to the innermost active TaskScope for
//                    this thread.  Spawned tasks read it to inherit the parent's
//                    cancellation token.  It is thread-global (one per thread,
//                    not per VM/manager instance), so it is a static member and
//                    is deliberately NOT transferred by VM's move operations.
//
// Coupling note: the task opcode handlers (handle_spawn / handle_await /
// handle_task_scope_begin / handle_task_scope_end / unwind_task_scopes_to) stay
// on VM because they push/pop the value stack, raise runtime_error, and drive
// the careful exception-safe join/cancel sequencing.  This component owns only
// the state and the thread-pool lazy-init; it never touches the value stack.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_VM_VM_TASK_MANAGER_HPP
#define LUMA_VM_VM_TASK_MANAGER_HPP

#include <atomic>
#include <memory>
#include <vector>

#include "runtime/concurrency/task_scope.hpp"
#include "runtime/concurrency/thread_pool.hpp"
#include "runtime/vm/vm_constants.hpp"

namespace luma {

/// Owns the VM's task-scope, thread-pool, and task-id state.  Instance fields
/// are transferred field-by-field by VM's move operations (the type is not
/// movable as a whole because std::atomic is non-movable); the static
/// thread-local current_scope is shared across the thread and left untouched.
class VMTaskManager {
public:
    /// LIFO stack of active task_scope { } blocks.
    std::vector<std::unique_ptr<TaskScope>> task_scopes;

    /// Thread pool owned by a top-level (root) VM.
    std::unique_ptr<ThreadPool> owned_pool;

    /// Non-owning pointer used by child VMs spawned into an existing pool.
    ThreadPool* shared_pool{nullptr};

    /// Monotonically increasing task ID for debugger correlation.
    std::atomic<int> next_task_id{VMConstants::k_initial_task_id};

    /// Thread-local pointer to the innermost active TaskScope for this thread.
    /// Defined in vm.cpp.  Gives spawned tasks access to the active cancellation
    /// token without threading it through every call.
    static thread_local TaskScope* current_scope;

    /// Lazily create and return the thread pool.  A root VM creates and owns the
    /// pool on first use; a child VM constructed with an existing pool returns
    /// that shared pool.
    [[nodiscard]] ThreadPool& pool() {
        if (shared_pool == nullptr) {
            owned_pool = std::make_unique<ThreadPool>();
            shared_pool = owned_pool.get();
        }

        return *shared_pool;
    }
};

} // namespace luma

#endif // LUMA_VM_VM_TASK_MANAGER_HPP
