#ifndef LUMA_CONCURRENCY_CHANNEL_BUFFER_HPP
#define LUMA_CONCURRENCY_CHANNEL_BUFFER_HPP

#include <cassert>
#include <cstddef>
#include <deque>
#include <utility>
#include <vector>

#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// Abstract interface for channel storage strategies.
// Implementations are NOT thread-safe; the owning Channel handles all locking.
class ChannelBuffer {
public:
    virtual ~ChannelBuffer() = default;

    ChannelBuffer() = default;
    ChannelBuffer(const ChannelBuffer&) = delete;
    ChannelBuffer& operator=(const ChannelBuffer&) = delete;
    ChannelBuffer(ChannelBuffer&&) = delete;
    ChannelBuffer& operator=(ChannelBuffer&&) = delete;

    virtual void push(Value value) = 0;
    [[nodiscard]] virtual Value pop() = 0;
    [[nodiscard]] virtual bool is_empty() const = 0;
    [[nodiscard]] virtual std::size_t size() const = 0;
    [[nodiscard]] virtual bool is_full() const = 0;

    // Configured fixed capacity of a bounded buffer, or 0 for an unbounded one.
    [[nodiscard]] virtual std::size_t capacity() const noexcept = 0;

    // Returns true if this is a bounded (fixed-capacity) channel. Bounded
    // channels block senders when full, providing back-pressure. Unbounded
    // channels grow up to max_channel_queue_size.
    //
    // This is a design-time capability query, NOT a runtime state check.
    // To check whether the buffer is currently at capacity, use is_full().
    [[nodiscard]] virtual bool is_bounded() const noexcept = 0;
};

// Fixed-capacity ring buffer for bounded (buffered) channels.
class BoundedChannelBuffer final : public ChannelBuffer {
public:
    explicit BoundedChannelBuffer(std::size_t capacity) : ring_(capacity), capacity_{capacity} {
        assert(capacity > 0 && "use unbounded channel for unlimited capacity");
    }

    void push(Value value) override {
        assert(!is_full() && "push called on full BoundedChannelBuffer");
        ring_[tail_] = std::move(value);
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
    }

    [[nodiscard]] Value pop() override {
        assert(!is_empty() && "pop called on empty BoundedChannelBuffer");
        Value value{std::move(ring_[head_])};
        ring_[head_] = Value{};
        head_ = (head_ + 1) % capacity_;
        --count_;
        return value;
    }

    [[nodiscard]] bool is_empty() const override {
        return count_ == 0;
    }

    [[nodiscard]] std::size_t size() const override {
        return count_;
    }

    [[nodiscard]] bool is_full() const override {
        return count_ >= capacity_;
    }

    [[nodiscard]] bool is_bounded() const noexcept override {
        return true;
    }

    [[nodiscard]] std::size_t capacity() const noexcept override {
        return capacity_;
    }

private:
    std::vector<Value> ring_;
    std::size_t capacity_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t count_{0};
};

// Dynamically-growing deque for unbounded channels, capped by
// ResourceLimits::max_channel_queue_size to prevent runaway growth.
class UnboundedChannelBuffer final : public ChannelBuffer {
public:
    void push(Value value) override {
        assert(!is_full() && "push called on full UnboundedChannelBuffer");
        deque_.push_back(std::move(value));
    }

    [[nodiscard]] Value pop() override {
        assert(!is_empty() && "pop called on empty UnboundedChannelBuffer");
        Value value{std::move(deque_.front())};
        deque_.pop_front();
        return value;
    }

    [[nodiscard]] bool is_empty() const override {
        return deque_.empty();
    }

    [[nodiscard]] std::size_t size() const override {
        return deque_.size();
    }

    [[nodiscard]] bool is_full() const override {
        return deque_.size() >= ResourceLimits::max_channel_queue_size;
    }

    [[nodiscard]] bool is_bounded() const noexcept override {
        return false;
    }

    [[nodiscard]] std::size_t capacity() const noexcept override {
        return 0;
    }

private:
    std::deque<Value> deque_;
};

} // namespace luma

#endif // LUMA_CONCURRENCY_CHANNEL_BUFFER_HPP
