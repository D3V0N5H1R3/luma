#ifndef LUMA_CONCURRENCY_CHANNEL_HPP
#define LUMA_CONCURRENCY_CHANNEL_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>

#include "runtime/concurrency/channel_buffer.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// Channel provides a thread-safe FIFO queue for passing Values between tasks.
// Supports both unbounded (capacity = 0) and buffered (capacity > 0) modes.
// Storage strategy is selected at construction via ChannelBuffer subclasses.
class Channel {
public:
    // capacity = 0 means unbounded (queue grows up to max_channel_queue_size).
    explicit Channel(std::size_t capacity = 0);

    ~Channel() noexcept = default;

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&&) = delete;
    Channel& operator=(Channel&&) = delete;

    // Send a value into the channel.  Blocks if buffered and full.
    // Throws ChannelClosedError if the channel is closed.
    void send(Value value);

    // Receive a value.  Blocks until a value is available.
    // Throws ChannelClosedError if the channel is closed and drained.
    [[nodiscard]] Value receive();

    // Receive with timeout.  Blocks for at most timeout_ms milliseconds.
    // Returns the value on success, std::nullopt on timeout.
    // Throws ChannelClosedError if the channel is closed and drained.
    struct ReceiveTimeoutResult {
        std::optional<Value> value;
        bool timed_out{false};
    };

    [[nodiscard]] ReceiveTimeoutResult receive_timeout(std::chrono::milliseconds timeout_ms);

    // Send with timeout.  Blocks for at most timeout_ms milliseconds
    // when a buffered channel is full.
    // Returns timed_out = true if the deadline elapsed before the value could
    // be enqueued, timed_out = false once it was sent.
    // Throws ChannelClosedError if the channel is closed.
    struct SendTimeoutResult {
        bool timed_out{false};
    };

    [[nodiscard]] SendTimeoutResult send_timeout(Value value, std::chrono::milliseconds timeout_ms);

    // Non-blocking receive.  Returns the value if available.
    // Throws ChannelEmptyError if the buffer is empty.
    // Throws ChannelClosedError if the channel is closed and drained.
    [[nodiscard]] Value try_receive();

    // Non-blocking send.  Enqueues the value if possible.
    // Throws ChannelFullError if the buffer is full.
    // Throws ChannelClosedError if the channel is closed.
    void try_send(Value value);

    // Close the channel.  After closing, send throws ChannelClosedError
    // and receive throws ChannelClosedError once the buffer is drained.
    void close() noexcept;

    // Query whether the channel has been closed.
    // The closed_ flag is atomic, so no mutex is needed for a standalone read.
    // This is a non-authoritative hint: callers must still handle
    // ChannelClosedError from send() and receive(), which are the authoritative
    // signals.
    [[nodiscard]] bool is_closed() const noexcept;

    // Number of values currently buffered.
    [[nodiscard]] std::size_t length() const;

    // Whether the channel currently holds no buffered values.  Keeps the
    // emptiness predicate encapsulated so callers need not compare length().
    [[nodiscard]] bool is_empty() const;

private:
    std::unique_ptr<ChannelBuffer> buffer_;

    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;
    std::atomic<bool> closed_{false};

    // Throw ChannelClosedError if the channel has been closed.
    // Precondition: mutex_ is held by the caller.
    void throw_if_closed() const;

    // Condition-variable wait predicates shared by the blocking and timeout
    // paths.  Receivers wake when a value is available or the channel is
    // closed; senders wake when the buffer has room or the channel is closed.
    // Precondition: mutex_ is held by the caller.
    [[nodiscard]] bool has_value_or_closed() const;
    [[nodiscard]] bool has_room_or_closed() const;

    // Notify a waiting sender after a value is consumed (bounded channels
    // only).  Call after releasing mutex_, mirroring the send path, so a woken
    // sender does not immediately block on the lock the consumer still holds.
    void notify_senders_if_needed();
};

} // namespace luma

#endif // LUMA_CONCURRENCY_CHANNEL_HPP
