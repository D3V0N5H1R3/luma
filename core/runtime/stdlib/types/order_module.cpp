#include "runtime/stdlib/types/order_module.hpp"

#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/value_compare.hpp"
#include "runtime/stdlib/common/function_builder.hpp"

namespace luma {

namespace {

// The Ordering choice is top-level (no namespace), so a ChoiceValue built here
// uses type_name "Ordering" to match the variant constants that
// register_stdlib_postamble registers as Ordering.Less / Ordering.Equal /
// Ordering.Greater.
constexpr std::string_view k_ordering_type = "Ordering";

// Build an Ordering choice value from a strcmp-style sign: a negative sign is
// Less, zero is Equal, and a positive sign is Greater.
[[nodiscard]] Value make_ordering(int sign) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = std::string{k_ordering_type};

    if (sign < 0) {
        cv->variant = "Less";
    } else if (sign > 0) {
        cv->variant = "Greater";
    } else {
        cv->variant = "Equal";
    }

    return Value{std::move(cv)};
}

// Map an Ordering variant name to its numeric sign (Less = -1, Equal = 0,
// Greater = 1).  Returns nullopt when the name is not an Ordering variant.
[[nodiscard]] std::optional<int> ordering_sign_from_variant(std::string_view variant) {
    if (variant == "Less") {
        return -1;
    }
    if (variant == "Equal") {
        return 0;
    }
    if (variant == "Greater") {
        return 1;
    }

    return std::nullopt;
}

// Extract the numeric sign of an Ordering choice argument, throwing a
// RuntimeError when the argument is not a genuine Ordering variant.
[[nodiscard]] int expect_ordering(const Value& arg, std::string_view func_name,
                                  const SourceLocation& loc) {
    if (!arg.is_choice()) {
        throw RuntimeError{std::format("{}: expected an Ordering choice", func_name), loc,
                           "pass an Ordering variant, e.g. Ordering.Less"};
    }

    const auto sign = ordering_sign_from_variant(arg.as_choice()->variant);

    if (!sign.has_value()) {
        throw RuntimeError{
            std::format("{}: unknown ordering 'Ordering.{}'", func_name, arg.as_choice()->variant),
            loc, "use an Ordering variant: Less, Equal, or Greater"};
    }

    return *sign;
}

} // namespace

void register_order_ns(const EnvPtr& env) {
    ModuleBuilder builder{"Order", env};

    // Order.of(a, b) — compare two comparable primitives, returning an Ordering.
    // Reuses the shared comparator that Array.sort uses, so the
    // choice and numeric worlds agree on ordering (and on which types compare).
    builder.func("of", 2).raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
        const int sign = compare_values(args[0], args[1], loc, "Order.of");
        return make_ordering(sign);
    });

    // Order.reverse(o) — flip Less and Greater; Equal is unchanged.
    builder.func("reverse", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const int sign = expect_ordering(args[0], "Order.reverse", loc);
            return make_ordering(-sign);
        });

    // Order.then(first, second) — tie-break: return `first` unless it is Equal,
    // in which case fall back to `second`.  Chains comparisons for multi-key
    // sorting ("by last name, then first name").
    builder.func("then", 2).raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
        const int first = expect_ordering(args[0], "Order.then", loc);
        (void)expect_ordering(args[1], "Order.then", loc);

        return first != 0 ? args[0] : args[1];
    });

    // Order.to_number(o) — bridge to the numeric comparator Array.sort expects:
    // Less = -1, Equal = 0, Greater = 1.
    builder.func("to_number", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const int sign = expect_ordering(args[0], "Order.to_number", loc);
            return Value{static_cast<double>(sign)};
        });

    // Order.from_number(n) — bridge from a numeric comparator result to an
    // Ordering: negative = Less, zero = Equal, positive = Greater.
    builder.func("from_number", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_integer() && !args[0].is_number()) {
                throw RuntimeError{"Order.from_number: expected a number", loc,
                                   "pass a negative, zero, or positive number"};
            }

            const double n = args[0].to_numeric();

            if (std::isnan(n)) {
                throw RuntimeError{"Order.from_number: NaN is not an ordering", loc,
                                   "pass a negative, zero, or positive number"};
            }

            int sign = 0;
            if (n < 0.0) {
                sign = -1;
            } else if (n > 0.0) {
                sign = 1;
            }

            return make_ordering(sign);
        });

    // Boolean predicates reading an Ordering as a condition, so the everyday
    // comparison needs no match: Order.is_less(Order.of(a, b)) and friends.
    builder.func("is_less", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{expect_ordering(args[0], "Order.is_less", loc) < 0};
        });

    builder.func("is_equal", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{expect_ordering(args[0], "Order.is_equal", loc) == 0};
        });

    builder.func("is_greater", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{expect_ordering(args[0], "Order.is_greater", loc) > 0};
        });

    builder.func("is_less_or_equal", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{expect_ordering(args[0], "Order.is_less_or_equal", loc) <= 0};
        });

    builder.func("is_greater_or_equal", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{expect_ordering(args[0], "Order.is_greater_or_equal", loc) >= 0};
        });
}

} // namespace luma
