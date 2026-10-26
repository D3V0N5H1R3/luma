#ifndef LUMA_STDLIB_CONCURRENCY_CONSTANTS_HPP
#define LUMA_STDLIB_CONCURRENCY_CONSTANTS_HPP

// Shared constants and helpers for concurrency-related stdlib modules
// (Task, Channel).

#include <algorithm>
#include <chrono>
#include <thread>

namespace luma {

// Exponential backoff bounds for polling loops (Channel.select).
inline constexpr auto k_backoff_min = std::chrono::milliseconds{1};
inline constexpr auto k_backoff_max = std::chrono::milliseconds{50};

// Stateful exponential-backoff sleeper for busy-poll loops.  Each call to
// sleep() blocks for the current interval, then doubles it up to k_backoff_max,
// so a loop that keeps finding no work progressively yields more CPU.  On
// Windows even the 1 ms minimum sleeps a full scheduler tick (~15.6 ms), so
// prefer a wake-up notification where one exists — Task.race and Task.any
// block on task completion (see runtime/concurrency/task_completion.hpp).
class BackoffTimer {
public:
    // Sleep for the current interval, then grow it towards k_backoff_max.
    void sleep() {
        std::this_thread::sleep_for(interval_);
        if (interval_ < k_backoff_max) {
            interval_ = std::min(interval_ * 2, k_backoff_max);
        }
    }

private:
    std::chrono::milliseconds interval_{k_backoff_min};
};

} // namespace luma

#endif // LUMA_STDLIB_CONCURRENCY_CONSTANTS_HPP
