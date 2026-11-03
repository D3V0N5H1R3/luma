#ifndef LUMA_STDLIB_CONCURRENCY_CONSTANTS_HPP
#define LUMA_STDLIB_CONCURRENCY_CONSTANTS_HPP

// Shared constants and helpers for concurrency-related stdlib modules
// (Task, Channel).

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <thread>

#include "runtime/concurrency/cancellation_token.hpp"
#include "runtime/stdlib/common/native_function_fwd.hpp"

namespace luma {

// Cancellation token of the spawned task running on this thread (nullptr on the
// main thread), so blocking waits inside a cancelled task give up instead of
// blocking forever.
[[nodiscard]] inline const CancellationToken* active_task_token() noexcept {
    return detail::active_cancel_token != nullptr ? detail::active_cancel_token->get() : nullptr;
}

// Clamp a user-supplied millisecond timeout to [0, INT_MAX] ms (≈24.8 days),
// mirroring the Http module.  Larger values would overflow the clock
// arithmetic inside wait_for/sleep_for — on MSVC and libstdc++ the deadline
// wraps into the past, so `Task.timeout(t, 9223372036854775807)` timed out
// immediately.  Callers reject negative timeouts first where that is an error.
[[nodiscard]] constexpr std::chrono::milliseconds clamp_timeout_ms(std::int64_t ms) noexcept {
    return std::chrono::milliseconds{std::clamp<std::int64_t>(ms, 0, INT_MAX)};
}

// How often a cancellable sleep re-checks its token (matches Channel's poll).
inline constexpr auto k_cancel_poll_interval = std::chrono::milliseconds{10};

// Sleep for `duration` (clamped by the caller), waking every
// k_cancel_poll_interval to honour `cancel` (may be null): throws
// CancelledException once the token is cancelled.  The caller holds a
// ThreadPool::BlockingRegion for the duration.
inline void cancellable_sleep(std::chrono::milliseconds duration, const CancellationToken* cancel) {
    const auto deadline = std::chrono::steady_clock::now() + duration;

    while (true) {
        if (cancel != nullptr) {
            cancel->throw_if_cancelled();
        }

        const auto now = std::chrono::steady_clock::now();

        if (now >= deadline) {
            return;
        }

        const auto remaining = deadline - now;

        std::this_thread::sleep_for(
            cancel != nullptr
                ? std::min<std::chrono::steady_clock::duration>(remaining, k_cancel_poll_interval)
                : remaining);
    }
}

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
