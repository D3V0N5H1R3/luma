#ifndef LUMA_CONCURRENCY_TASK_SCOPE_HPP
#define LUMA_CONCURRENCY_TASK_SCOPE_HPP

#include <memory>
#include <mutex>
#include <vector>

#include "runtime/concurrency/cancellation_token.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// TaskScope implements structured concurrency.
//
// When a task_scope { ... } block is evaluated, a TaskScope is created.
// Every spawn inside the block registers its TaskValue as a child.
// At scope exit:
//   - join_all() blocks until every child completes.
//   - If any child threw, the first exception is captured, all remaining
//     children are cancelled, and the exception is re-thrown.
//   - Results are collected in spawn order and returned as an array.
//
// Scopes nest: a TaskScope records its parent so the VM can restore the
// enclosing scope when this one ends.  A scope opened inside a spawned task
// chains its cancellation token to that task's token, so cancelling the task
// also cancels the scope's children.
class TaskScope {
public:
    explicit TaskScope(TaskScope* parent = nullptr,
                       std::shared_ptr<const CancellationToken> parent_token = nullptr);

    ~TaskScope() = default;

    TaskScope(const TaskScope&) = delete;
    TaskScope& operator=(const TaskScope&) = delete;

    // Register a child task.  Called by evaluate_spawn when inside a scope.
    void add_child(std::shared_ptr<TaskValue> task);

    // How join_all() waits.
    enum class JoinMode {
        // Scope end: the wait counts as waiting on pool tasks, so the pool's
        // nested-blocking check may throw when no worker could ever run a
        // queued child.  The children are then kept for a teardown join.
        Normal,
        // Teardown after cancel_all() (an error is unwinding the scope):
        // children that have not started are resolved as cancelled without
        // running, and started ones are awaited without the nested-blocking
        // check, so every child is joined and the wait never throws for lack
        // of workers.
        Teardown
    };

    // Block until all children finish.  Returns an array of results
    // in registration order.  Fail-fast: children are collected in the order
    // they complete, and the first child that fails with a genuine error
    // (not a cancellation) immediately cancels its siblings; that error is
    // re-thrown once every child has finished.  A cancelled child contributes
    // `none` to the results and is not treated as a failure.
    //
    // NOTE: join_all() is terminal once it returns or rethrows a child's
    // error — the children vector is moved out.  If the wait itself fails
    // (JoinMode::Normal's nested-blocking error) the children are restored so
    // a Teardown join can still join them.
    [[nodiscard]] std::vector<Value> join_all(JoinMode mode = JoinMode::Normal);

    // Cancel every child that has not yet completed.
    void cancel_all();

    // Access the shared cancellation token for this scope.
    [[nodiscard]] const std::shared_ptr<CancellationToken>& token() const noexcept;

    // Parent scope (may be nullptr at the top level).
    [[nodiscard]] TaskScope* parent() const noexcept;

private:
    TaskScope* parent_;
    std::shared_ptr<CancellationToken> token_;
    std::vector<std::shared_ptr<TaskValue>> children_;
    std::mutex mutex_;
};

} // namespace luma

#endif // LUMA_CONCURRENCY_TASK_SCOPE_HPP
