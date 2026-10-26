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

    // Block until all children finish.  Returns an array of results
    // in registration order.  Fail-fast: children are collected in the order
    // they complete, and the first child that fails with a genuine error
    // (not a cancellation) immediately cancels its siblings; that error is
    // re-thrown once every child has finished.  A cancelled child contributes
    // `none` to the results and is not treated as a failure.
    //
    // NOTE: join_all() is terminal — the children vector is moved out
    // during iteration.  Do not add more children after calling this.
    [[nodiscard]] std::vector<Value> join_all();

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
