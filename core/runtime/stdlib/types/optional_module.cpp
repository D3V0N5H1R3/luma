#include "runtime/stdlib/types/optional_module.hpp"

#include <format>
#include <span>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

// Both Optional.or and Optional.unwrap_or return the fallback when the optional
// is none, and the value itself when it is some.  A present optional is stored
// as the bare value (none == null), so the same logic serves both the
// optional-returning `or` and the value-returning `unwrap_or`.
[[nodiscard]] inline Value unwrap_or_impl(const Value& opt, const Value& default_val) {
    return opt.is_null() ? default_val : opt;
}

// Build the single-element argument vector passed to an Optional combinator
// callback.  The callback receives the unwrapped value, which for a present
// optional is the value itself.  Mirrors result_module's inner_args helper.
[[nodiscard]] inline std::vector<Value> callback_args(const Value& value) {
    return {value};
}

// Shared implementation for Optional.flat_map and its documented alias
// Optional.and_then.  When the optional is none, propagate none; otherwise
// invoke the callback with the unwrapped value and return its result, which is
// itself optional<U> — a plain value for some, or none.
[[nodiscard]] inline Value flat_map_impl(std::span<const Value> args, SourceLocation loc) {
    if (args[0].is_null()) {
        return Value{NullValue{}};
    }

    auto call_args = callback_args(args[0]);

    return invoke_callable(args[1], call_args, loc);
}

} // namespace

void register_optional_ns(const EnvPtr& env) {
    ModuleBuilder{"Optional", env}
        .func("is_some", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            return Value{args[0].is_some()};
        })
        .func("is_none", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            return Value{args[0].is_null()};
        })
        .func("unwrap", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (args[0].is_null()) {
                throw RuntimeError{error_msg("Optional", "unwrap", "called on none"), loc,
                                   "use Optional.unwrap_or() for a default or check with "
                                   "Optional.is_some() before unwrapping"};
            }

            return args[0];
        })
        .func("expect", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (args[0].is_null()) {
                (void)expect_string(args[1], "Optional.expect", loc);

                throw RuntimeError{args[1].as_string(), loc,
                                   "the optional was none when a value was expected"};
            }

            return args[0];
        })
        .func("and", 2)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            // some(b) when a is present, otherwise none.
            return args[0].is_null() ? Value{NullValue{}} : args[1];
        })
        .func("xor", 2)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            const bool a_some = !args[0].is_null();
            const bool b_some = !args[1].is_null();

            if (a_some == b_some) {
                return Value{NullValue{}};
            }

            return a_some ? args[0] : args[1];
        })
        .func("unwrap_or", 2)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            return unwrap_or_impl(args[0], args[1]);
        })
        .func("map", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (args[0].is_null()) {
                return Value{NullValue{}};
            }

            auto call_args = callback_args(args[0]);
            auto result = invoke_callable(args[1], call_args, loc);

            if (result.is_null()) {
                throw RuntimeError{
                    error_msg("Optional", "map",
                              "callback must not return none; "
                              "use Optional.flat_map for callbacks that may return none"),
                    loc,
                    "change the callback to return a plain value, "
                    "or use Optional.flat_map if none is a valid output"};
            }

            return result;
        })
        .func("flat_map", 2)
        .raw_body(flat_map_impl)
        .func("filter", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (args[0].is_null()) {
                return Value{NullValue{}};
            }

            auto call_args = callback_args(args[0]);

            const auto result = invoke_callable(args[1], call_args, loc);

            if (!result.is_bool()) {
                throw RuntimeError{error_msg("Optional", "filter",
                                             std::format("predicate must return boolean, got '{}'",
                                                         result.display_type_name())),
                                   loc,
                                   "return true or false from the predicate, "
                                   "e.g. (x) -> x > 0"};
            }

            if (result.as_bool()) {
                return args[0];
            }

            return Value{NullValue{}};
        })
        .func("or", 2)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            return unwrap_or_impl(args[0], args[1]);
        })
        .func("tap", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_null()) {
                auto call_args = callback_args(args[0]);

                (void)invoke_callable(args[1], call_args, loc);
            }

            return args[0];
        })
        .func("flatten", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            // optional<optional<T>> -> optional<T>.
            // If outer is none, return none.  Otherwise return the inner value
            // (which is itself either some(T) or none).
            if (args[0].is_null()) {
                return Value{NullValue{}};
            }

            return args[0];
        })
        .func("to_result", 2)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            if (args[0].is_null()) {
                return Value{ResultValue::failure(args[1])};
            }

            return make_success_value(args[0]);
        })
        .func("zip", 2)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            if (args[0].is_null() || args[1].is_null()) {
                return Value{NullValue{}};
            }

            return make_tuple_pair(args[0], args[1]);
        })
        .func("and_then", 2)
        .raw_body(flat_map_impl)
        .func("contains", 2)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            if (args[0].is_null()) {
                return Value{false};
            }

            return Value{args[0].equals(args[1])};
        });
}

} // namespace luma
