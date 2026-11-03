#ifndef LUMA_CONCURRENCY_CANCELLATION_TOKEN_HPP
#define LUMA_CONCURRENCY_CANCELLATION_TOKEN_HPP

#include <atomic>
#include <memory>
#include <stdexcept>
#include <utility>

namespace luma {

// CancelledException is thrown when a cooperative cancellation check detects
// that the token has been cancelled.  It is caught by task_scope to
// distinguish deliberate cancellation from genuine errors.
class CancelledException : public std::runtime_error {
public:
    explicit CancelledException(const char* message) : std::runtime_error{message} {}
};

// CancellationToken provides cooperative cancellation for spawned tasks.
//
// Tokens form a tree: every spawned task owns its own token whose parent is
// the enclosing task_scope's token, and a task_scope opened inside a task
// chains its token to that task's token.  A token counts as cancelled when
// its own flag or any ancestor's flag is set, so:
//   - Task.cancel(t) cancels only `t` (its own token);
//   - TaskScope::cancel_all() cancels every task in the scope (scope token);
//   - cancelling a task also cancels the task_scopes it has open.
// Tasks check the token at natural yield points (loop back-edges, function
// calls, await, and blocking channel waits).
class CancellationToken {
public:
    explicit CancellationToken(std::shared_ptr<const CancellationToken> parent = nullptr)
        : parent_{std::move(parent)} {}

    CancellationToken(const CancellationToken&) = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;
    CancellationToken(CancellationToken&&) = delete;
    CancellationToken& operator=(CancellationToken&&) = delete;
    ~CancellationToken() = default;

    void cancel() noexcept {
        cancelled_.store(true, std::memory_order_release);
    }

    // True when this token or any ancestor has been cancelled.  The parent
    // chain is as deep as the task/scope nesting, so the walk is short.
    [[nodiscard]] bool is_cancelled() const noexcept {
        for (const auto* token = this; token != nullptr; token = token->parent_.get()) {
            if (token->cancelled_.load(std::memory_order_acquire)) {
                return true;
            }
        }

        return false;
    }

    // Convenience: throw CancelledException if cancelled.
    void throw_if_cancelled() const {
        if (is_cancelled()) {
            throw CancelledException{"task cancelled"};
        }
    }

private:
    std::shared_ptr<const CancellationToken> parent_;
    std::atomic<bool> cancelled_{false};
};

} // namespace luma

#endif // LUMA_CONCURRENCY_CANCELLATION_TOKEN_HPP
