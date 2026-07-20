#include "runtime/stdlib/concurrency/channel_module.hpp"

#include <chrono>
#include <cstdint>
#include <format>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "runtime/concurrency/channel.hpp"
#include "runtime/interpreter/runtime_exceptions.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/concurrency/concurrency_constants.hpp"

namespace luma {

namespace {

// Validate a non-negative timeout argument shared by send_timeout and
// receive_timeout, returning the value as a chrono duration.  Throws a
// qualified RuntimeError if the argument is not an integer or is negative.
[[nodiscard]] std::chrono::milliseconds expect_timeout_ms(const Value& v, std::string_view function,
                                                          const SourceLocation& loc) {
    const auto ms = expect_integer(v, format_function_name("Channel", function), loc);

    if (ms < 0) {
        throw RuntimeError{ErrorMessages::must_be_non_negative("Channel", function, "timeout_ms"),
                           loc, "use 0 for no timeout or a positive number of milliseconds"};
    }

    return std::chrono::milliseconds{ms};
}

} // namespace

void register_channel_ns(const EnvPtr& env) {
    ModuleBuilder{"Channel", env} // Channel.new() -> channel
        .func("new", 0)
        .raw_body([]([[maybe_unused]] std::span<const Value> args,
                     [[maybe_unused]] SourceLocation loc) -> Value {
            auto ch = std::make_shared<Channel>();
            return Value{std::make_shared<ChannelValue>(std::move(ch))};
        })
        // Channel.new_buffered(integer capacity) -> channel
        .func("new_buffered", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto cap = expect_integer(args[0], "Channel.new_buffered", loc);

            if (cap <= 0) {
                throw RuntimeError{
                    ErrorMessages::must_be_positive("Channel", "new_buffered", "capacity", cap),
                    loc, "pass a positive integer as the buffer capacity"};
            }

            if (static_cast<std::size_t>(cap) > ResourceLimits::max_channel_queue_size) {
                throw RuntimeError{error_msg("Channel", "new_buffered",
                                             std::format("capacity {} exceeds maximum ({})", cap,
                                                         ResourceLimits::max_channel_queue_size)),
                                   loc, "use a smaller buffer capacity"};
            }

            auto ch = std::make_shared<Channel>(static_cast<std::size_t>(cap));
            return Value{std::make_shared<ChannelValue>(std::move(ch))};
        })
        // Channel.send(channel<T> ch, T value) -> boolean
        // Sends a value to the channel. Returns false if the channel is closed.
        .func("send", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& ch = expect_channel(args[0], "Channel.send", loc)->channel;
            try {
                ch->send(args[1].deep_copy());
                return Value{true};
            } catch (const ChannelClosedError&) {
                return Value{false};
            } catch (const ChannelFullError&) {
                // An unbounded channel throws ChannelFullError only when it hits
                // the safety cap that bounds memory growth.  Surface it as a
                // clear, catchable error rather than letting it escape the
                // native call — returning false here would be indistinguishable
                // from a closed channel and silently drop the value.
                throw RuntimeError{
                    error_msg("Channel", "send",
                              "channel buffer is full (reached the maximum queue size)"),
                    loc,
                    "receive values faster, use a bounded channel for back-pressure, "
                    "or send fewer values"};
            }
        })
        // Channel.try_send(channel<T> ch, T value) -> boolean
        .func("try_send", 2)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& ch = expect_channel(args[0], "Channel.try_send", loc)->channel;
            try {
                ch->try_send(args[1].deep_copy());
                return Value{true};
            } catch (const ChannelClosedError&) {
                return Value{false};
            } catch (const ChannelFullError&) {
                return Value{false};
            }
        })
        // Channel.send_timeout(channel<T> ch, T value, integer timeout_ms) -> result<boolean>
        // success(true) if sent, failure("timeout") after timeout_ms ms,
        // failure("channel closed") if channel was closed.
        .func("send_timeout", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& cv = expect_channel(args[0], "Channel.send_timeout", loc);
            const auto timeout = expect_timeout_ms(args[2], "send_timeout", loc);

            const auto& ch = cv->channel;

            try {
                auto result = ch->send_timeout(args[1].deep_copy(), timeout);

                if (result.timed_out) {
                    return failure_msg("Channel", "send_timeout", "timeout", error_codes::timeout);
                }

                return make_success_value(Value{true});
            } catch (const ChannelClosedError&) {
                return failure_msg("Channel", "send_timeout", "channel closed",
                                   error_codes::channel_closed);
            }
        })
        // Channel.receive(channel<T> ch) -> result<T>
        .func("receive", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& ch = expect_channel(args[0], "Channel.receive", loc)->channel;

            try {
                auto val = ch->receive();
                return make_success_value(std::move(val));
            } catch (const ChannelClosedError&) {
                return failure_msg("Channel", "receive", "channel closed",
                                   error_codes::channel_closed);
            }
        })
        // Channel.try_receive(channel<T> ch) -> result<T>
        .func("try_receive", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& ch = expect_channel(args[0], "Channel.try_receive", loc)->channel;

            try {
                auto val = ch->try_receive();
                return make_success_value(std::move(val));
            } catch (const ChannelEmptyError&) {
                return failure_msg("Channel", "try_receive", "no value available",
                                   error_codes::empty_container);
            } catch (const ChannelClosedError&) {
                return failure_msg("Channel", "try_receive", "channel closed",
                                   error_codes::channel_closed);
            }
        })
        // Channel.receive_timeout(channel<T> ch, integer timeout_ms) -> result<T>
        // success(value) on success, failure("timeout") after timeout_ms ms,
        // failure("channel closed") if closed.
        .func("receive_timeout", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& cv = expect_channel(args[0], "Channel.receive_timeout", loc);
            const auto timeout = expect_timeout_ms(args[1], "receive_timeout", loc);

            const auto& ch = cv->channel;

            try {
                auto result = ch->receive_timeout(timeout);

                if (result.timed_out) {
                    return failure_msg("Channel", "receive_timeout", "timeout",
                                       error_codes::timeout);
                }

                return make_success_value(std::move(*result.value));
            } catch (const ChannelClosedError&) {
                return failure_msg("Channel", "receive_timeout", "channel closed",
                                   error_codes::channel_closed);
            }
        })
        // Channel.close(channel ch) -> null
        .func("close", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            expect_channel(args[0], "Channel.close", loc)->channel->close();
            return NullValue{};
        })
        // Channel.is_closed(channel ch) -> boolean
        .func("is_closed", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            return Value{expect_channel(args[0], "Channel.is_closed", loc)->channel->is_closed()};
        })
        // Channel.length(channel ch) -> integer
        .func("length", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            return Value{static_cast<std::int64_t>(
                expect_channel(args[0], "Channel.length", loc)->channel->length())};
        })
        // Channel.is_empty(channel ch) -> boolean
        .func("is_empty", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            return Value{expect_channel(args[0], "Channel.is_empty", loc)->channel->is_empty()};
        })
        // Channel.receive_all(channel<T> ch) -> array<T>
        .func("receive_all", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& ch = expect_channel(args[0], "Channel.receive_all", loc)->channel;
            auto arr = std::make_shared<ArrayValue>();

            while (true) {
                try {
                    auto val = ch->try_receive();
                    arr->elements->push_back(std::move(val));
                } catch (const ChannelError&) {
                    // Buffer drained (empty) or channel closed — stop collecting.
                    break;
                }
            }

            return Value{std::move(arr)};
        })
        // Channel.select(channels: array<channel<T>>) -> result<(integer, T)>
        // Waits for data on any of the given channels and returns (index, value).
        .func("select", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            const auto& arr = expect_array(args[0], "Channel.select", loc);

            if (auto failure =
                    check_non_empty_elements(*arr->elements, "Channel", "select", "channel",
                                             [](const Value& elem) { return elem.is_channel(); })) {
                return std::move(*failure);
            }

            BackoffTimer backoff;

            while (true) {
                bool any_open = false;

                for (std::size_t i = 0; i < arr->elements->size(); ++i) {
                    const auto& ch = (*arr->elements)[i].as_channel();

                    if (ch->channel->is_closed() && ch->channel->is_empty()) {
                        continue;
                    }

                    any_open = true;

                    try {
                        auto val = ch->channel->try_receive();
                        auto tuple = std::make_shared<TupleValue>();
                        tuple->elements.emplace_back(static_cast<std::int64_t>(i));
                        tuple->elements.push_back(std::move(val));
                        return make_success_value(Value{std::move(tuple)});
                    } catch (const ChannelError&) { // NOLINT(bugprone-empty-catch)
                        // No value on this channel (empty or closed while
                        // checking); try the next one.
                    }
                }

                if (!any_open) {
                    return failure_msg("Channel", "select", "all channels are closed",
                                       error_codes::channel_closed);
                }

                backoff.sleep();
            }
        });
}

} // namespace luma
