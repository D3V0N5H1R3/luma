#ifndef LUMA_CONCURRENCY_TASK_COMPLETION_HPP
#define LUMA_CONCURRENCY_TASK_COMPLETION_HPP

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace luma {

// A wake-up flag that one waiting thread blocks on while any number of tasks
// may signal it.  Task.race and Task.any subscribe one to every task they wait
// on, so whichever task finishes first wakes the waiter immediately instead of
// the waiter discovering it on its next poll.
class CompletionSignal {
public:
    void notify() {
        {
            const std::scoped_lock lock{mutex_};
            notified_ = true;
        }

        condition_.notify_all();
    }

    // Block until notified or until `timeout` elapses, then re-arm.  The
    // timeout is a safety net only: callers re-poll every task after waking.
    void wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        condition_.wait_for(lock, timeout, [this] { return notified_; });
        notified_ = false;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool notified_{false};
};

// Completion notifier shared by a spawned task's producer and every handle to
// that task (deep copies included).  The producer calls mark_complete() after
// it has satisfied the task's promise, so a woken subscriber always finds the
// task's future ready.
class TaskCompletion {
public:
    TaskCompletion() = default;

    TaskCompletion(const TaskCompletion&) = delete;
    TaskCompletion& operator=(const TaskCompletion&) = delete;

    void mark_complete() {
        std::vector<std::shared_ptr<CompletionSignal>> subscribers;

        {
            const std::scoped_lock lock{mutex_};
            complete_ = true;
            subscribers.swap(subscribers_);
        }

        for (const auto& subscriber : subscribers) {
            subscriber->notify();
        }
    }

    // Register `signal` to be notified on completion.  A task that has already
    // completed notifies immediately, so a subscriber can never miss it.
    void subscribe(std::shared_ptr<CompletionSignal> signal) {
        {
            const std::scoped_lock lock{mutex_};

            if (!complete_) {
                subscribers_.push_back(std::move(signal));
                return;
            }
        }

        signal->notify();
    }

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<CompletionSignal>> subscribers_;
    bool complete_{false};
};

} // namespace luma

#endif // LUMA_CONCURRENCY_TASK_COMPLETION_HPP
