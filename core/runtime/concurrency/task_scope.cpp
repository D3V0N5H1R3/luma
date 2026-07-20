#include "runtime/concurrency/task_scope.hpp"

#include <exception>

namespace luma {

namespace {

// Outcome of awaiting a child task: it was either cancelled as part of the
// scope's fail-fast shutdown, or it failed with a genuine error that must be
// captured and re-thrown.
enum class ChildOutcome {
    Cancelled,
    Failed
};

// Handle a failed task by cancelling remaining siblings (if not already
// cancelled) and capturing the first genuine (non-cancellation) error.
void handle_task_failure(std::exception_ptr& first_error, TaskScope& scope, ChildOutcome outcome) {
    if (!first_error) {
        if (outcome == ChildOutcome::Failed) {
            first_error = std::current_exception();
        }
        scope.cancel_all();
    }
}

} // anonymous namespace

TaskScope::TaskScope(TaskScope* parent)
    : parent_{parent}, token_{std::make_shared<CancellationToken>()} {}

void TaskScope::add_child(std::shared_ptr<TaskValue> task) {
    const std::scoped_lock lock{mutex_};

    children_.push_back(std::move(task));
}

// Fail-fast policy: if any task in the scope throws an exception,
// all remaining tasks are cancelled via cooperative cancellation
// (Task.is_cancelled()) and the first exception is re-thrown after
// all tasks have completed or been cancelled.
std::vector<Value> TaskScope::join_all() {
    // Move children out under the lock — join_all() is terminal and no more
    // children will be added after this point.
    std::vector<std::shared_ptr<TaskValue>> snapshot;

    {
        const std::scoped_lock lock{mutex_};

        snapshot = std::move(children_);
    }

    std::vector<Value> results;
    results.reserve(snapshot.size());

    std::exception_ptr first_error;

    for (const auto& child : snapshot) {
        if (!child->future.valid()) {
            // Already consumed (should not happen in normal flow).
            results.emplace_back(NullValue{});

            continue;
        }

        try {
            results.push_back(child->future.get());
        } catch (const CancelledException&) {
            // Cancelled children are expected after cancel_all().
            // Do not propagate cancellation as an error — just
            // cancel remaining siblings and stop collecting.
            handle_task_failure(first_error, *this, ChildOutcome::Cancelled);
            results.emplace_back(NullValue{});
        } catch (...) {
            handle_task_failure(first_error, *this, ChildOutcome::Failed);
            results.emplace_back(NullValue{});
        }
    }

    if (first_error) {
        std::rethrow_exception(first_error);
    }

    return results;
}

void TaskScope::cancel_all() {
    token_->cancel();
}

const std::shared_ptr<CancellationToken>& TaskScope::token() const noexcept {
    return token_;
}

TaskScope* TaskScope::parent() const noexcept {
    return parent_;
}

} // namespace luma
