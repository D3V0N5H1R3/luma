#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

void register_channel_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("close", 1, "(channel: channel<T>)", R::void_type(), {p.channel_any}),
            m.fn("capacity", 1, "(channel: channel<T>)", R::optional(R::integer_type()),
                 {p.channel_any}),
            m.fn("consume", 2, "(channel: channel<T>, function: func(T) -> void)",
                 R::result_integer(), {p.channel_any, p.func}),
            m.fn("is_closed", 1, "(channel: channel<T>)", R::boolean_type(), {p.channel_any}),
            m.fn("is_empty", 1, "(channel: channel<T>)", R::boolean_type(), {p.channel_any}),
            m.fn("is_full", 1, "(channel: channel<T>)", R::boolean_type(), {p.channel_any}),
            m.fn("length", 1, "(channel: channel<T>)", R::integer_type(), {p.channel_any}),
            m.fn("new", 0, "()", R::channel_any()),
            m.fn("new_buffered", 1, "(capacity: integer)", R::channel_any(), {p.integer}),
            m.fn("poll", 1, "(channel: channel<T>)", R::optional_any(), {p.channel_any}),
            m.fn("receive", 1, "(channel: channel<T>)", R::result_any(), {p.channel_any}),
            m.fn("receive_all", 1, "(channel: channel<T>)", R::array_any(), {p.channel_any}),
            m.fn("receive_timeout", 2, "(channel: channel<T>, timeout_milliseconds: integer)",
                 R::result_any(), {p.channel_any, p.integer}),
            m.fn("select", 1, "(channels: array<channel<T>>)", R::result_any(), {p.array_any}),
            m.fn("send", 2, "(channel: channel<T>, value: T)", R::boolean_type(),
                 {p.channel_any, p.any}),
            m.fn("send_all", 2, "(channel: channel<T>, values: array<T>)", R::integer_type(),
                 {p.channel_any, p.array_any}),
            m.fn("send_timeout", 3,
                 "(channel: channel<T>, value: T, timeout_milliseconds: integer)",
                 R::result_boolean(), {p.channel_any, p.any, p.integer}),
            m.fn("try_receive", 1, "(channel: channel<T>)", R::result_any(), {p.channel_any}),
            m.fn("try_send", 2, "(channel: channel<T>, value: T)", R::boolean_type(),
                 {p.channel_any, p.any}),
        });
}

} // namespace luma::stdlib::detail
