// Math module — number classification, inverse trigonometry, and min/max /
// remainder helpers.  Registered via register_math_analysis().

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iterator>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "common/overflow.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/numeric_helpers.hpp"
#include "runtime/stdlib/math/math_module.hpp"

namespace luma {

namespace {

// Validate that x lies in the inverse-trig domain [-1, 1].  Shared by arc_sine
// and arc_cosine.  Returns a structured failure Value when x is out of range,
// or std::nullopt when it is valid.
[[nodiscard]] std::optional<Value> check_unit_domain(double x, std::string_view function) {
    if (x < -1.0 || x > 1.0) {
        return make_failure_value(error_msg("Math", function, "argument out of domain [-1, 1]"));
    }

    return std::nullopt;
}

} // namespace

// Number classification, inverse trigonometry, and min/max/remainder helpers.
void register_math_analysis(const EnvPtr& env) {
    ModuleBuilder{"Math", env}
        .func("is_not_a_number", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{std::isnan(expect_numeric(args[0], "Math.is_not_a_number", loc))};
        })
        .func("is_infinite", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{std::isinf(expect_numeric(args[0], "Math.is_infinite", loc))};
        })
        .func("is_finite", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{std::isfinite(expect_numeric(args[0], "Math.is_finite", loc))};
        })
        // approximately_equal accepts 2 or 3 arguments, so uses native() directly.
        .native(
            "approximately_equal",
            [](std::span<const Value> args, SourceLocation loc) -> Value {
                if (args.size() < 2 || args.size() > 3) {
                    throw RuntimeError{
                        std::format("Math.approximately_equal expects 2 or 3 argument(s), got {}",
                                    args.size()),
                        loc};
                }

                auto a = expect_numeric(args[0], "Math.approximately_equal", loc);
                auto b = expect_numeric(args[1], "Math.approximately_equal", loc);
                auto epsilon = (args.size() == 3)
                                   ? expect_numeric(args[2], "Math.approximately_equal", loc)
                                   : 1e-9;

                if (epsilon < 0.0) {
                    throw RuntimeError{
                        error_msg("Math", "approximately_equal", "epsilon must not be negative"),
                        loc};
                }

                return Value{std::abs(a - b) <= epsilon};
            })
        .func("arc_cosine", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto x = expect_numeric(args[0], "Math.arc_cosine", loc);

            if (auto fail = check_unit_domain(x, "arc_cosine")) {
                return *std::move(fail);
            }

            return make_success_value(Value{std::acos(x)});
        })
        .func("arc_sine", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto x = expect_numeric(args[0], "Math.arc_sine", loc);

            if (auto fail = check_unit_domain(x, "arc_sine")) {
                return *std::move(fail);
            }

            return make_success_value(Value{std::asin(x)});
        })
        .checked_unary("arc_tangent", [](double x) { return std::atan(x); })
        .checked_unary("exponential", [](double x) { return std::exp(x); })
        .func("maximum", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_numeric(args[0], "Math.maximum", loc);
            auto b = expect_numeric(args[1], "Math.maximum", loc);

            return a >= b ? args[0] : args[1];
        })
        .func("minimum", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_numeric(args[0], "Math.minimum", loc);
            auto b = expect_numeric(args[1], "Math.minimum", loc);

            return a <= b ? args[0] : args[1];
        })
        .func("remainder", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_numeric(args[0], "Math.remainder", loc);
            (void)expect_numeric(args[1], "Math.remainder", loc);

            if (args[0].is_integer() && args[1].is_integer()) {
                auto b = args[1].as_integer();

                if (b == 0) {
                    return make_failure_value(error_msg("Math", "remainder", "division by zero"));
                }

                if (b == -1) {
                    return make_success_value(Value{std::int64_t{0}});
                }

                return make_success_value(Value{args[0].as_integer() % b});
            }

            auto divisor = args[1].to_numeric();

            if (divisor == 0.0) {
                return make_failure_value(error_msg("Math", "remainder", "division by zero"));
            }

            return make_success_value(Value{std::fmod(args[0].to_numeric(), divisor)});
        })
        .checked_unary("tangent", [](double x) { return std::tan(x); })
        .checked_unary_to_int("truncate", [](double x) { return std::trunc(x); })
        // sum is a fundamental aggregate over a numeric array — it stays in Math
        // (the descriptive/inferential statistics live in the Statistics module).
        .func("sum", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Math.sum", loc)->elements;

            bool all_int{true};
            std::int64_t int_sum{0};
            double dbl_sum{0.0};

            for (const auto& elem : elems) {
                if (elem.is_integer()) {
                    if (all_int && would_overflow_add(int_sum, elem.as_integer())) {
                        all_int = false;
                    }
                    if (all_int) {
                        int_sum += elem.as_integer();
                    }
                    dbl_sum += static_cast<double>(elem.as_integer());
                } else if (elem.is_number()) {
                    all_int = false;

                    dbl_sum += elem.as_number();
                } else {
                    return make_failure_value(error_msg("Math", "sum", "non-numeric element"));
                }
            }

            return make_success_value(all_int ? Value{int_sum} : Value{dbl_sum});
        });
}

} // namespace luma
