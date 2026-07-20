#ifndef LUMA_CONCURRENCY_CANCELLATION_TOKEN_HPP
#define LUMA_CONCURRENCY_CANCELLATION_TOKEN_HPP

#include <atomic>
#include <stdexcept>

namespace luma {

// CancelledException is thrown when a cooperative cancellation check detects
// that the token has been cancelled.  It is caught by task_scope to
// distinguish deliberate cancellation from genuine errors.
class CancelledException : public std::runtime_error {
public:
    explicit CancelledException(const char* message) : std::runtime_error{message} {}
};

// CancellationToken provides cooperative cancellation for spawned tasks.
// A task_scope owns a token and shares it with every child task.  When the
// scope decides to cancel (e.g. because a sibling failed), it sets the
// flag and every child checks it at natural yield points.
class CancellationToken {
public:
    CancellationToken() = default;

    CancellationToken(const CancellationToken&) = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;

    void cancel() noexcept {
        cancelled_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

    // Convenience: throw CancelledException if cancelled.
    void throw_if_cancelled() const {
        if (is_cancelled()) {
            throw CancelledException{"task cancelled"};
        }
    }

private:
    std::atomic<bool> cancelled_{false};
};

} // namespace luma

#endif // LUMA_CONCURRENCY_CANCELLATION_TOKEN_HPP
