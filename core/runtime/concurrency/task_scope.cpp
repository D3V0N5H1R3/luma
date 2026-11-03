#include "runtime/concurrency/task_scope.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <future>
#include <optional>

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
std::vector<Value> TaskScope::join_all(JoinMode mode) {
    // Move children out under the lock — join_all() is terminal and no more
    // children will be added after this point.
    std::vector<std::shared_ptr<TaskValue>> snapshot;

    {
        const std::scoped_lock lock{mutex_};

        snapshot = std::move(children_);
    }

    // Teardown: the scope's token is already cancelled; the loop below
    // resolves children that never started as cancelled (see abandoned).
    std::vector<Value> results(snapshot.size());
    std::vector<bool> collected(snapshot.size(), false);
    std::size_t remaining = snapshot.size();
    std::exception_ptr first_error;

    // Once the scope is cancelled (Teardown, a failed child's fail-fast
    // cancel_all, or cancellation of the task running this join — the scope
    // token chains to it), a child that no worker has started is resolved as
    // cancelled right here: it would only have observed its cancelled token
    // and stopped.  The join then depends only on started children, which are
    // cancelled too and reach a cancellation point — never on a free worker,
    // which there may not be when every worker is blocked.  Checked on every
    // poll, so a join nested inside a task that gets cancelled stops waiting
    // for its queued children (otherwise a teardown waiting on that task hung).
    bool abandoned = false;

    const auto abandon_unstarted_if_cancelled = [&] {
        if (abandoned || (mode != JoinMode::Teardown && !token_->is_cancelled())) {
            return;
        }

        abandoned = true;

        for (std::size_t i{0}; i < snapshot.size(); ++i) {
            if (!collected[i] && snapshot[i]->completion) {
                (void)snapshot[i]->completion->abandon_if_not_started();
            }
        }
    };

    std::vector<std::shared_ptr<TaskCompletion>> completions;
    completions.reserve(snapshot.size());

    for (const auto& child : snapshot) {
        if (child->completion) {
            completions.push_back(child->completion);
        }
    }

    const CompletionSubscription subscription{std::move(completions)};

    // Joining blocks on other pool work; let the pool compensate if this is a
    // worker thread (a task that opened its own task_scope).  The region is
    // entered only once the join actually has to wait, so joining children
    // that are already done never trips the pool's nested-blocking check.
    std::optional<ThreadPool::BlockingRegion> blocking;

    while (remaining > 0) {
        abandon_unstarted_if_cancelled();

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
            if (!blocking) {
                // A cancelled scope waits only for started, cancelled children,
                // which something will finish: no stall check for that wait.
                if (abandoned) {
                    blocking.emplace(ThreadPool::BlockingRegion::Wait::External);
                } else {
                    blocking.emplace();
                }
            }

            if (!abandoned) {
                try {
                    blocking->verify_not_deadlocked();
                } catch (...) {
                    // Nested-blocking error: nothing could ever run the queued
                    // children.  Hand them back so the caller's teardown join
                    // (after cancel_all) still joins them.
                    const std::scoped_lock lock{mutex_};
                    children_ = std::move(snapshot);
                    throw;
                }
            }

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
