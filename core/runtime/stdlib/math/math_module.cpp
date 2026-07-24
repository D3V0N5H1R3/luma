#include "runtime/stdlib/math/math_module.hpp"

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

namespace luma {

namespace {

constexpr double pi{std::numbers::pi};

// Maximum input for int64_t factorial (21! overflows).
constexpr std::int64_t k_max_factorial_input = 20;

// Builds a top-level Sign choice value from a strcmp-style sign: a negative sign
// is Negative, zero is Zero, and a positive sign is Positive.  The runtime short
// name "Sign" matches the top-level Ordering pattern (make_ordering); the variant
// names must match the Sign choice in core/analysis/types/stdlib_type_arities.cpp.
[[nodiscard]] Value make_sign_choice(int sign) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "Sign";

    if (sign < 0) {
        cv->variant = "Negative";
    } else if (sign > 0) {
        cv->variant = "Positive";
    } else {
        cv->variant = "Zero";
    }

    return Value{std::move(cv)};
}

[[nodiscard]] std::int64_t compute_gcd(std::int64_t first, std::int64_t second) {
    while (second != 0) {
        auto temp = second;

        second = first % second;
        first = temp;
    }

    return first;
}

// Reject INT64_MIN operands before compute_gcd applies std::abs (std::abs of
// INT64_MIN is undefined behaviour).  Shared by greatest_common_divisor and
// least_common_multiple.  Returns a structured failure Value when either
// operand is INT64_MIN, or std::nullopt when both are safe.
[[nodiscard]] std::optional<Value> guard_gcd_operands(std::int64_t first, std::int64_t second,
                                                      std::string_view function) {
    if (first == std::numeric_limits<std::int64_t>::min() ||
        second == std::numeric_limits<std::int64_t>::min()) {
        return make_failure_value(error_msg("Math", function, "integer overflow"));
    }

    return std::nullopt;
}

} // namespace

void register_math_ns(const EnvPtr& env) {
    // Constants.
    env->define("Math.pi", Value{pi}, false);
    env->define("Math.e", Value{std::numbers::e}, false);
    env->define("Math.tau", Value{2.0 * pi}, false);
    env->define("Math.infinity", Value{std::numeric_limits<double>::infinity()}, false);

    ModuleBuilder{"Math", env}
        .checked_unary_to_int("floor", [](double x) { return std::floor(x); })
        .checked_unary_to_int("ceil", [](double x) { return std::ceil(x); })
        .checked_unary_to_int("round", [](double x) { return std::round(x); })
        .func("absolute", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto val = expect_numeric(args[0], "Math.absolute", loc);

            if (args[0].is_integer()) {
                auto v = args[0].as_integer();

                if (v == std::numeric_limits<std::int64_t>::min()) {
                    return make_failure_value(error_msg("Math", "absolute", "integer overflow"));
                }

                return make_success_value(Value{std::abs(v)});
            }

            return make_success_value(Value{std::abs(val)});
        })
        .func("power", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto r = std::pow(expect_numeric(args[0], "Math.power", loc),
                              expect_numeric(args[1], "Math.power", loc));

            if (!stdlib::is_valid_numeric(r)) {
                return make_failure_value(
                    error_msg("Math", "power", "result is not a real number"));
            }

            return make_success_value(Value{r});
        })
        .func("square_root", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto x = expect_numeric(args[0], "Math.square_root", loc);

            if (x < 0) {
                return make_failure_value(error_msg("Math", "square_root", "negative argument"));
            }

            return make_success_value(Value{std::sqrt(x)});
        })
        .positive_unary("log_e", [](double x) { return std::log(x); })
        .positive_unary("log_2", [](double x) { return std::log2(x); })
        .positive_unary("log_10", [](double x) { return std::log10(x); })
        .func("factorial", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto n = expect_integer(args[0], "Math.factorial", loc);

            if (n < 0) {
                return make_failure_value(error_msg("Math", "factorial", "negative number"));
            }

            if (n > k_max_factorial_input) {
                return make_failure_value(
                    error_msg("Math", "factorial",
                              std::format("overflow, n must be <= {}", k_max_factorial_input)));
            }

            std::int64_t result{1};

            for (std::int64_t i{2}; i <= n; ++i) {
                result *= i;
            }

            return make_success_value(Value{result});
        })
        .func("greatest_common_divisor", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto first = expect_integer(args[0], "Math.greatest_common_divisor", loc);
            auto second = expect_integer(args[1], "Math.greatest_common_divisor", loc);

            if (auto fail = guard_gcd_operands(first, second, "greatest_common_divisor")) {
                return *std::move(fail);
            }

            return make_success_value(Value{compute_gcd(std::abs(first), std::abs(second))});
        })
        .func("least_common_multiple", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto first = expect_integer(args[0], "Math.least_common_multiple", loc);
            auto second = expect_integer(args[1], "Math.least_common_multiple", loc);

            if (auto fail = guard_gcd_operands(first, second, "least_common_multiple")) {
                return *std::move(fail);
            }

            auto abs_first = std::abs(first);
            auto abs_second = std::abs(second);

            if (abs_first == 0 || abs_second == 0) {
                return make_success_value(Value{std::int64_t{0}});
            }

            auto divisor = compute_gcd(abs_first, abs_second);
            auto quotient = abs_first / divisor;

            if (quotient > std::numeric_limits<std::int64_t>::max() / abs_second) {
                return make_failure_value(
                    error_msg("Math", "least_common_multiple", "integer overflow"));
            }

            return make_success_value(Value{quotient * abs_second});
        })
        .func("is_prime", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto n = expect_integer(args[0], "Math.is_prime", loc);

            if (n < 2) {
                return Value{false};
            }

            if (n < 4) {
                return Value{true};
            }

            if (n % 2 == 0 || n % 3 == 0) {
                return Value{false};
            }

            // `i <= n / i` is the overflow-safe form of `i * i <= n`.  For a
            // large prime near INT64_MAX, computing `i * i` overflows (UB) right
            // at the intended sqrt(n) termination point and can wrap so the loop
            // never ends.
            for (std::int64_t i{5}; i <= n / i; i += 6) {
                if (n % i == 0 || n % (i + 2) == 0) {
                    return Value{false};
                }
            }

            return Value{true};
        })
        .func("clamp", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto val = expect_numeric(args[0], "Math.clamp", loc);
            auto lo = expect_numeric(args[1], "Math.clamp", loc);
            auto hi = expect_numeric(args[2], "Math.clamp", loc);

            if (lo > hi) {
                return make_failure_value(error_msg("Math", "clamp", "'lo' must not exceed 'hi'"));
            }

            return make_success_value(Value{std::clamp(val, lo, hi)});
        })
        .func("lerp", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_numeric(args[0], "Math.lerp", loc);
            auto b = expect_numeric(args[1], "Math.lerp", loc);
            auto t = expect_numeric(args[2], "Math.lerp", loc);

            if (t < 0.0 || t > 1.0) {
                return make_failure_value(
                    error_msg("Math", "lerp", "interpolation factor 't' must be in [0, 1]"));
            }

            return make_success_value(Value{a + ((b - a) * t)});
        })
        .func("sign", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto x = expect_numeric(args[0], "Math.sign", loc);

            std::int64_t sign = 0;
            if (x > 0) {
                sign = 1;
            } else if (x < 0) {
                sign = -1;
            }

            return Value{sign};
        })
        .func("sign_of", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            // The exhaustive, self-documenting counterpart to Math.sign: match on
            // Sign.Negative / Sign.Zero / Sign.Positive instead of the magic
            // -1 / 0 / 1.  NaN has no sign, so — like Math.sign — it is treated as
            // Zero (neither < nor > 0), keeping the choice total.
            const auto x = expect_numeric(args[0], "Math.sign_of", loc);

            int sign = 0;
            if (x > 0) {
                sign = 1;
            } else if (x < 0) {
                sign = -1;
            }

            return make_sign_choice(sign);
        })
        .checked_unary("sine", [](double x) { return std::sin(x); })
        .checked_unary("cosine", [](double x) { return std::cos(x); })
        .func("degrees", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{expect_numeric(args[0], "Math.degrees", loc) * 180.0 / pi};
        })
        .func("radians", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{expect_numeric(args[0], "Math.radians", loc) * pi / 180.0};
        });

    register_math_analysis(env);
    register_math_statistics(env);
    register_math_transcendental(env);
}

} // namespace luma
