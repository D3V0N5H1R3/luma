#include "runtime/concurrency/task_scope.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <future>

#include "runtime/concurrency/task_completion.hpp"
#include "runtime/concurrency/thread_pool.hpp"

namespace luma {

namespace {

// Upper bound on one wait in join_all().  Spawned tasks wake the joiner the
// moment they complete through their TaskCompletion; this only bounds how long
// a child without a completion notifier could go unnoticed.
constexpr auto k_join_poll_interval = std::chrono::milliseconds{10};

} // anonymous namespace

TaskScope::TaskScope(TaskScope* parent, std::shared_ptr<const CancellationToken> parent_token)
    : parent_{parent}, token_{std::make_shared<CancellationToken>(std::move(parent_token))} {}

void TaskScope::add_child(std::shared_ptr<TaskValue> task) {
    const std::scoped_lock lock{mutex_};

    children_.push_back(std::move(task));
}

// Fail-fast policy: children are collected in completion order.  The first
// child that fails with a genuine error cancels every remaining sibling
// (cooperative cancellation) at once — not after the children spawned before
// it have finished — and that error is re-thrown after all children have
// completed or been cancelled.  A cancelled child yields `none` and is not a
// failure: Task.cancel(t) on one child must not fail or cancel the scope.
std::vector<Value> TaskScope::join_all() {
    // Move children out under the lock — join_all() is terminal and no more
    // children will be added after this point.
    std::vector<std::shared_ptr<TaskValue>> snapshot;

    {
        const std::scoped_lock lock{mutex_};

        snapshot = std::move(children_);
    }

    std::vector<Value> results(snapshot.size());
    std::vector<bool> collected(snapshot.size(), false);
    std::size_t remaining = snapshot.size();
    std::exception_ptr first_error;

    std::vector<std::shared_ptr<TaskCompletion>> completions;
    completions.reserve(snapshot.size());

    for (const auto& child : snapshot) {
        if (child->completion) {
            completions.push_back(child->completion);
        }
    }

    const CompletionSubscription subscription{std::move(completions)};

    // Joining blocks on other pool work; let the pool compensate if this is a
    // worker thread (a task that opened its own task_scope).
    const ThreadPool::BlockingRegion blocking;

    while (remaining > 0) {
        bool progressed = false;

        for (std::size_t i{0}; i < snapshot.size(); ++i) {
            if (collected[i]) {
                continue;
            }

            const auto& child = snapshot[i];

            if (child->future.valid() &&
                child->future.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
                continue;
            }

            collected[i] = true;
            --remaining;
            progressed = true;

            if (!child->future.valid()) {
                // Already consumed (should not happen in normal flow).
                continue;
            }

            try {
                results[i] = child->future.get();
            } catch (const CancelledException&) { // NOLINT(bugprone-empty-catch)
                // A cancelled child contributes `none`; cancellation is not a
                // failure and must not cancel its siblings.
            } catch (...) {
                if (!first_error) {
                    first_error = std::current_exception();
                    cancel_all();
                }
            }
        }

        if (remaining > 0 && !progressed) {
            subscription.wait_for(k_join_poll_interval);
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
