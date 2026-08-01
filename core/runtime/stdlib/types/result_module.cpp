#include "runtime/stdlib/types/result_module.hpp"

#include <concepts>
#include <format>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

// Build a single-element argument vector from the inner value of a result.
// Replaces the repeated `std::vector<Value> call_args{*r->owned_inner}` pattern.
[[nodiscard]] inline std::vector<Value> inner_args(const std::shared_ptr<ResultValue>& r) {
    return {*r->owned_inner};
}

// Invoke a combinator callback with the result's inner value as its sole
// argument.  Collapses the inner_args + invoke_callable pair repeated by every
// value-transforming combinator (map, recover, bimap, fold, flat_map, ...).
[[nodiscard]] inline Value invoke_on_inner(const Value& callback,
                                           const std::shared_ptr<ResultValue>& r,
                                           const SourceLocation& loc) {
    auto call_args = inner_args(r);
    return invoke_callable(callback, call_args, loc);
}

// Enforce the flat_map / or_else contract that the callback returns a result
// value, throwing a RuntimeError that names the offending combinator otherwise.
inline void require_result_callback(const Value& produced, std::string_view name,
                                    const SourceLocation& loc) {
    if (!produced.is_result()) {
        throw RuntimeError{std::format("{}: callback must return a result value", name), loc,
                           "the callback should return success() or failure()"};
    }
}

// Read a failure-metadata string field (error_code or source_function).  A
// success value carries no error metadata, so it reports an empty string.
[[nodiscard]] inline Value failure_metadata(const std::shared_ptr<ResultValue>& r,
                                            const std::string& field) {
    if (r->is_success) {
        return Value{std::string{}};
    }
    return Value{field};
}

// Build a NativeFunction for a typed map_* variant that checks the inner
// value type at compile time (if constexpr) rather than via a runtime
// std::function.  Arity is validated by the ModuleBuilder wrapper, so the body
// only guards the receiver and inner-value types.
// T selects the inner-type guard:
//   double       → is_number() || is_integer()
//   std::string  → is_string()
//   bool         → is_bool()
//   std::int64_t → is_integer()
template <typename T> [[nodiscard]] NativeFunction make_typed_map(std::string name) {
    return [name = std::move(name)](std::span<const Value> args, SourceLocation loc) -> Value {
        const auto& r = expect_result(args[0], name, loc);

        if (!r->is_success) {
            return args[0];
        }

        // Compile-time type dispatch: dead branches are discarded.
        if constexpr (std::same_as<T, double>) {
            if (!r->owned_inner->is_number() && !r->owned_inner->is_integer()) {
                return args[0];
            }
        } else if constexpr (std::same_as<T, std::string>) {
            if (!r->owned_inner->is_string()) {
                return args[0];
            }
        } else if constexpr (std::same_as<T, bool>) {
            if (!r->owned_inner->is_bool()) {
                return args[0];
            }
        } else if constexpr (std::same_as<T, std::int64_t>) {
            if (!r->owned_inner->is_integer()) {
                return args[0];
            }
        }

        return make_success_value(invoke_on_inner(args[1], r, loc));
    };
}

} // namespace

// Functions are registered in the same order as the stdlib catalog
// (shared/stdlib/stdlib_catalog_error_handling.cpp) so the two lists can be
// cross-referenced line by line when adding or auditing a function.
void register_result_ns(const EnvPtr& env) {
    ModuleBuilder{"Result", env}
        .func("collect", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& results = expect_array(args[0], "Result.collect", loc);

            auto arr = std::make_shared<ArrayValue>();

            for (const auto& elem : *results->elements) {
                if (!elem.is_result()) {
                    throw RuntimeError{error_msg("Result", "collect",
                                                 std::format("element is not a result, got '{}'",
                                                             elem.display_type_name())),
                                       loc, "pass an array of result values"};
                }

                const auto& r = elem.as_result();

                if (!r->is_success) {
                    return elem;
                }

                arr->elements->push_back(*r->owned_inner);
            }

            return make_success_value(Value{std::move(arr)});
        })
        .func("error", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.error", loc);

            if (r->is_success) {
                throw RuntimeError{
                    error_msg("Result", "error", "called on success value"), loc,
                    "use Result.is_failure() to check before calling Result.error()"};
            }

            return *r->owned_inner;
        })
        .func("expect", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.expect", loc);
            const auto& msg = expect_string(args[1], "Result.expect", loc);

            if (!r->is_success) {
                throw RuntimeError{std::format("{}: {}", msg, r->owned_inner->to_string()), loc,
                                   "provide a successful result or handle the failure "
                                   "with a match"};
            }

            return *r->owned_inner;
        })
        .func("filter", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.filter", loc);

            if (!r->is_success) {
                return args[0];
            }

            const auto pass = invoke_on_inner(args[1], r, loc);

            if (!pass.is_truthy()) {
                return Value{ResultValue::failure(args[2])};
            }

            return args[0];
        })
        .func("flat_map", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.flat_map", loc);

            if (!r->is_success) {
                return args[0];
            }

            auto mapped = invoke_on_inner(args[1], r, loc);

            require_result_callback(mapped, "Result.flat_map", loc);

            return mapped;
        })
        .func("flatten", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& outer = expect_result(args[0], "Result.flatten", loc);

            if (!outer->is_success) {
                return args[0];
            }

            if (outer->owned_inner->is_result()) {
                return *outer->owned_inner;
            }

            return args[0];
        })
        .func("is_failure", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{!expect_result(args[0], "Result.is_failure", loc)->is_success};
        })
        .func("is_success", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{expect_result(args[0], "Result.is_success", loc)->is_success};
        })
        // The map* family: map and map_failure apply an arbitrary callback,
        // while the typed map_boolean/map_integer/map_number/map_string variants
        // use make_typed_map to guard the inner value type at compile time (via
        // if constexpr).  Kept together in catalog order.
        .func("map", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.map", loc);

            if (!r->is_success) {
                return args[0];
            }

            return make_success_value(invoke_on_inner(args[1], r, loc));
        })
        .func("map_boolean", 2)
        .raw_body(make_typed_map<bool>("Result.map_boolean"))
        .func("map_failure", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.map_failure", loc);

            if (r->is_success) {
                return args[0];
            }

            return Value{ResultValue::failure(invoke_on_inner(args[1], r, loc))};
        })
        .func("map_integer", 2)
        .raw_body(make_typed_map<std::int64_t>("Result.map_integer"))
        .func("map_number", 2)
        .raw_body(make_typed_map<double>("Result.map_number"))
        .func("map_string", 2)
        .raw_body(make_typed_map<std::string>("Result.map_string"))
        .func("or", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (expect_result(args[0], "Result.or", loc)->is_success) {
                return args[0];
            }

            return args[1];
        })
        .func("or_else", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.or_else", loc);

            if (r->is_success) {
                return args[0];
            }

            auto mapped = invoke_on_inner(args[1], r, loc);

            require_result_callback(mapped, "Result.or_else", loc);

            return mapped;
        })
        .func("recover", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.recover", loc);

            if (r->is_success) {
                return args[0];
            }

            return make_success_value(invoke_on_inner(args[1], r, loc));
        })
        .func("tap", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.tap", loc);

            if (r->is_success) {
                static_cast<void>(invoke_on_inner(args[1], r, loc));
            }

            return args[0];
        })
        .func("to_optional", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.to_optional", loc);

            if (r->is_success) {
                return *r->owned_inner;
            }

            return Value{NullValue{}};
        })
        .func("unwrap", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.unwrap", loc);

            if (!r->is_success) {
                const std::string location_prefix =
                    r->has_failure_location ? std::format("[{}:{}] ", r->failure_location.line,
                                                          r->failure_location.column)
                                            : "";

                auto err = RuntimeError{
                    error_msg("Result", "unwrap",
                              std::format("called on fail: {}{}", location_prefix,
                                          r->owned_inner->to_string())),
                    loc, "use Result.unwrap_or() for a default or handle the failure with a match"};
                err.set_error_payload(*r->owned_inner);

                throw err;
            }

            return *r->owned_inner;
        })
        .func("unwrap_or", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.unwrap_or", loc);

            return r->is_success ? *r->owned_inner : args[1];
        })
        .func("zip", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r1 = expect_result(args[0], "Result.zip", loc);

            if (!r1->is_success) {
                return args[0];
            }

            const auto& r2 = expect_result(args[1], "Result.zip", loc);

            if (!r2->is_success) {
                return args[1];
            }

            return make_success_value(make_tuple_pair(*r1->owned_inner, *r2->owned_inner));
        })
        // bimap, fold, error_code and source_function are appended after the
        // alphabetical block, mirroring the catalog (they were added later).
        .func("map_both", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.map_both", loc);

            if (r->is_success) {
                return make_success_value(invoke_on_inner(args[1], r, loc));
            }

            return Value{ResultValue::failure(invoke_on_inner(args[2], r, loc))};
        })
        .func("fold", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.fold", loc);

            if (r->is_success) {
                return invoke_on_inner(args[1], r, loc);
            }

            return invoke_on_inner(args[2], r, loc);
        })
        .func("error_code", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.error_code", loc);

            return failure_metadata(r, r->error_code);
        })
        .func("source_function", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& r = expect_result(args[0], "Result.source_function", loc);

            return failure_metadata(r, r->source_function);
        });
}

} // namespace luma
