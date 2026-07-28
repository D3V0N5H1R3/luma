#include "runtime/concurrency/channel.hpp"

#include <string_view>

#include "runtime/interpreter/runtime_exceptions.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

namespace {

// Error messages shared across the send/receive/try_* paths.  Kept as
// single-source constants so the wording cannot drift between call sites.
constexpr std::string_view channel_closed_message{"channel is closed"};
constexpr std::string_view channel_drained_message{"channel is closed and drained"};
constexpr std::string_view channel_full_message{"channel buffer is full"};

// Create the appropriate channel buffer for the given capacity.
// capacity = 0 → unbounded (grows up to max_channel_queue_size).
// capacity > 0 → bounded ring buffer.
[[nodiscard]] std::unique_ptr<ChannelBuffer> make_channel_buffer(std::size_t capacity) {
    if (capacity == 0) {
        return std::make_unique<UnboundedChannelBuffer>();
    }
    return std::make_unique<BoundedChannelBuffer>(capacity);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Channel::Channel(std::size_t capacity) : buffer_{make_channel_buffer(capacity)} {}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void Channel::throw_if_closed() const {
    if (closed_.load(std::memory_order_relaxed)) {
        throw ChannelClosedError{channel_closed_message};
    }
}

bool Channel::has_value_or_closed() const {
    return !buffer_->is_empty() || closed_.load(std::memory_order_relaxed);
}

bool Channel::has_room_or_closed() const {
    return closed_.load(std::memory_order_relaxed) || !buffer_->is_full();
}

void Channel::notify_senders_if_needed() {
    if (buffer_->is_bounded()) {
        not_full_cv_.notify_one();
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Channel::send(Value value) {
    {
        std::unique_lock lock{mutex_};

        throw_if_closed();

        if (buffer_->is_bounded()) {
            not_full_cv_.wait(lock, [this] { return has_room_or_closed(); });

            throw_if_closed();
        } else {
            if (buffer_->is_full()) {
                throw ChannelFullError{channel_full_message};
            }
        }

        buffer_->push(std::move(value));
    }

    // Notify outside the lock to avoid waking a receiver that immediately
    // blocks on the mutex the sender still holds.
    not_empty_cv_.notify_one();
}

Value Channel::receive() {
    std::unique_lock lock{mutex_};

    not_empty_cv_.wait(lock, [this] { return has_value_or_closed(); });

    if (buffer_->is_empty()) {
        throw ChannelClosedError{channel_drained_message};
    }

    Value value{buffer_->pop()};

    lock.unlock();

    // Notify outside the lock, mirroring the send path: a woken sender cannot
    // progress until the lock is released anyway (bounded channels only).
    notify_senders_if_needed();

    return value;
}

// send_timeout and receive_timeout both use a CV wait_for pattern but differ
// in which CV they wait on (not_empty_cv_ vs not_full_cv_), the predicate
// (buffer empty vs buffer full), and post-wait actions (pop vs push).
// These differences make a shared wait_with_timeout() helper impractical —
// the abstraction would need to parameterise the CV, predicate, and
// post-wait action, adding complexity without reducing duplication.

Channel::ReceiveTimeoutResult Channel::receive_timeout(std::chrono::milliseconds timeout_ms) {
    std::unique_lock lock{mutex_};

    const bool signalled =
        not_empty_cv_.wait_for(lock, timeout_ms, [this] { return has_value_or_closed(); });

    if (!signalled) {
        return {.value = std::nullopt, .timed_out = true}; // timed out
    }

    if (buffer_->is_empty()) {
        throw ChannelClosedError{channel_drained_message};
    }

    Value value{buffer_->pop()};

    lock.unlock();

    // Notify outside the lock, mirroring the send path (bounded channels only).
    notify_senders_if_needed();

    return {.value = std::move(value), .timed_out = false};
}

Channel::SendTimeoutResult Channel::send_timeout(Value value,
                                                 std::chrono::milliseconds timeout_ms) {
    {
        std::unique_lock lock{mutex_};

        throw_if_closed();

        if (buffer_->is_bounded()) {
            const bool signalled =
                not_full_cv_.wait_for(lock, timeout_ms, [this] { return has_room_or_closed(); });

            if (!signalled) {
                return {.timed_out = true}; // timed out
            }

            throw_if_closed();
        } else {
            if (buffer_->is_full()) {
                throw ChannelFullError{channel_full_message};
            }
        }

        buffer_->push(std::move(value));
    }

    // Notify outside the lock to avoid unnecessary contention.
    not_empty_cv_.notify_one();

    return {.timed_out = false};
}

Value Channel::try_receive() {
    std::unique_lock lock{mutex_};

    if (buffer_->is_empty()) {
        if (closed_.load(std::memory_order_relaxed)) {
            throw ChannelClosedError{channel_drained_message};
        }
        throw ChannelEmptyError{"channel buffer is empty"};
    }

    Value value{buffer_->pop()};

    lock.unlock();

    // Notify outside the lock, mirroring the send path (bounded channels only).
    notify_senders_if_needed();

    return value;
}

void Channel::try_send(Value value) {
    {
        const std::scoped_lock lock{mutex_};

        throw_if_closed();

        if (buffer_->is_full()) {
            throw ChannelFullError{channel_full_message};
        }

        buffer_->push(std::move(value));
    }

    not_empty_cv_.notify_one();
}

void Channel::close() noexcept {
    {
        const std::scoped_lock lock{mutex_};

        closed_.store(true, std::memory_order_relaxed);
    }

    not_empty_cv_.notify_all();
    not_full_cv_.notify_all();
}

bool Channel::is_closed() const noexcept {
    // Relaxed is sufficient: this is a non-authoritative hint (send()/receive()
    // throwing ChannelClosedError are the authoritative signals).  All ordering
    // is established by the mutex-guarded paths; a lone acquire here would have
    // no paired release to synchronise with, and it matches the relaxed loads
    // used under mutex_ elsewhere in this class.
    return closed_.load(std::memory_order_relaxed);
}

std::size_t Channel::length() const {
    const std::scoped_lock lock{mutex_};

    return buffer_->size();
}

bool Channel::is_empty() const {
    const std::scoped_lock lock{mutex_};

    return buffer_->is_empty();
}

bool Channel::is_full() const {
    const std::scoped_lock lock{mutex_};

    // An unbounded channel is never reported full (it only blocks at the safety
    // cap, which is not the user-visible "at capacity" condition).
    return buffer_->is_bounded() && buffer_->is_full();
}

std::optional<std::size_t> Channel::capacity() const {
    const std::scoped_lock lock{mutex_};

    if (!buffer_->is_bounded()) {
        return std::nullopt;
    }

    return buffer_->capacity();
}

} // namespace luma
