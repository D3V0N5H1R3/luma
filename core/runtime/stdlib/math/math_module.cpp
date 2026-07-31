#include "runtime/stdlib/math/math_module.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "analysis/errors/error.hpp"
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

// Convert a Math.Angle choice value — Radians(number) | Degrees(number) — to a
// magnitude in radians.  Throws when the value is not a well-formed angle (only
// reachable by bypassing the type checker).  Used by Math.to_radians /
// Math.to_degrees / Math.sin_of.
[[nodiscard]] double angle_to_radians(const Value& value, std::string_view func,
                                      const SourceLocation& loc) {
    if (!value.is_choice()) {
        throw RuntimeError{std::string{func} + ": expected a Math.Angle value", loc,
                           "build one with Math.Angle.Radians(x) or Math.Angle.Degrees(x)"};
    }

    const auto& choice = *value.as_choice();

    if (choice.fields.empty() || !(choice.fields[0].is_integer() || choice.fields[0].is_number())) {
        throw RuntimeError{std::string{func} + ": malformed Math.Angle payload", loc,
                           "build one with Math.Angle.Radians(x) or Math.Angle.Degrees(x)"};
    }

    const double magnitude = choice.fields[0].to_numeric();

    if (choice.variant == "Radians") {
        return magnitude;
    }

    if (choice.variant == "Degrees") {
        return magnitude * pi / 180.0;
    }

    throw RuntimeError{std::string{func} + ": unknown Math.Angle variant '" + choice.variant + "'",
                       loc, "use Math.Angle.Radians or Math.Angle.Degrees"};
}

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

// The { min, max } bounds of a Math.Interval record, as doubles.
struct IntervalBounds {
    double min;
    double max;
};

// Read a Math.Interval record's min/max fields.  Throws when the argument is not
// a record; missing fields default to 0.0 (the type checker guarantees the shape
// for well-typed programs, so this only guards raw/hand-built values).  Mirrors
// DateTime's read_interval_bounds.
[[nodiscard]] IntervalBounds read_interval(const Value& value, std::string_view func,
                                           const SourceLocation& loc) {
    if (!value.is_record()) {
        throw RuntimeError{std::string{func} + ": expected a Math.Interval record", loc,
                           "build one with Math.interval(min, max)"};
    }

    const auto& rec = value.as_record();
    const auto field = [&rec](std::string_view name) -> double {
        const Value* v = rec->find_field(name);
        return v != nullptr ? v->to_numeric() : 0.0;
    };

    return IntervalBounds{.min = field("min"), .max = field("max")};
}

// A Math.Rect record's fields, as doubles.  width/height are non-negative (the
// constructor clamps them).
struct RectBounds {
    double x;
    double y;
    double width;
    double height;
};

// Build a Math.Rect record value (type_name "Rect").  width/height are clamped to
// be non-negative so a degenerate rectangle is empty rather than inside-out,
// keeping contains/intersects/area well-defined (the beginner-friendly "clamp
// over throw" convention, like String.truncate).
[[nodiscard]] Value make_rect(const RectBounds& r) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Rect";
    rec->fields.emplace_back("x", Value{r.x});
    rec->fields.emplace_back("y", Value{r.y});
    rec->fields.emplace_back("width", Value{std::max(r.width, 0.0)});
    rec->fields.emplace_back("height", Value{std::max(r.height, 0.0)});

    return Value{std::move(rec)};
}

// Build a Math.Vector2 record value (type_name "Vector2"), matching make_vec2 in
// math_vectors.cpp — used by Math.rect_center.
[[nodiscard]] Value make_rect_vec2(double x, double y) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Vector2";
    rec->fields.emplace_back("x", Value{x});
    rec->fields.emplace_back("y", Value{y});

    return Value{std::move(rec)};
}

// Read a Math.Rect record's x/y/width/height fields.  Throws when the argument is
// not a record; missing fields default to 0.0.  width/height are clamped to be
// non-negative so a hand-built record can never carry inside-out extents into a
// derivation.  Mirrors read_interval.
[[nodiscard]] RectBounds read_rect(const Value& value, std::string_view func,
                                   const SourceLocation& loc) {
    if (!value.is_record()) {
        throw RuntimeError{std::string{func} + ": expected a Math.Rect record", loc,
                           "build one with Math.rect(x, y, width, height)"};
    }

    const auto& rec = value.as_record();
    const auto field = [&rec](std::string_view name) -> double {
        const Value* v = rec->find_field(name);
        return v != nullptr ? v->to_numeric() : 0.0;
    };

    return RectBounds{.x = field("x"),
                      .y = field("y"),
                      .width = std::max(field("width"), 0.0),
                      .height = std::max(field("height"), 0.0)};
}

// A point read from a Math.Vector2 record's x/y fields.
struct Vec2Point {
    double x;
    double y;
};

// A Math.Circle record's fields, as doubles.  radius is non-negative (the
// constructor clamps it).
struct CircleData {
    double cx;
    double cy;
    double radius;
};

// Read a Math.Vector2 record's x/y fields.  Throws when the argument is not a
// record; missing fields default to 0.0.  Shared by the circle constructor and
// circle_contains (whose point argument is a Math.Vector2).
[[nodiscard]] Vec2Point read_vec2_point(const Value& value, std::string_view func,
                                        const SourceLocation& loc) {
    if (!value.is_record()) {
        throw RuntimeError{std::string{func} + ": expected a Math.Vector2 record", loc,
                           "build one with Math.vector2(x, y)"};
    }

    const auto& rec = value.as_record();
    const auto field = [&rec](std::string_view name) -> double {
        const Value* v = rec->find_field(name);
        return v != nullptr ? v->to_numeric() : 0.0;
    };

    return Vec2Point{.x = field("x"), .y = field("y")};
}

// Build a Math.Circle record value (type_name "Circle") from a centre and radius.
// radius is clamped to be non-negative so a degenerate circle is a point rather
// than inside-out, keeping contains/intersects well-defined (the beginner-friendly
// "clamp over throw" convention, like Math.rect).
[[nodiscard]] Value make_circle(double cx, double cy, double radius) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Circle";
    rec->fields.emplace_back("center", make_rect_vec2(cx, cy));
    rec->fields.emplace_back("radius", Value{std::max(radius, 0.0)});

    return Value{std::move(rec)};
}

// Read a Math.Circle record's centre and radius.  Throws when the argument is not
// a record; a missing centre defaults to the origin and a missing radius to 0.0.
// radius is clamped non-negative so a hand-built record can never carry an
// inside-out radius into a derivation.  Mirrors read_rect.
[[nodiscard]] CircleData read_circle(const Value& value, std::string_view func,
                                     const SourceLocation& loc) {
    if (!value.is_record()) {
        throw RuntimeError{std::string{func} + ": expected a Math.Circle record", loc,
                           "build one with Math.circle(center, radius)"};
    }

    const auto& rec = value.as_record();
    const Value* center = rec->find_field("center");
    const Value* radius = rec->find_field("radius");

    Vec2Point c{.x = 0.0, .y = 0.0};

    if (center != nullptr) {
        c = read_vec2_point(*center, func, loc);
    }

    const double r = radius != nullptr ? std::max(radius->to_numeric(), 0.0) : 0.0;

    return CircleData{.cx = c.x, .cy = c.y, .radius = r};
}

} // namespace

void register_math_ns(const EnvPtr& env) {
    // Constants.
    env->define("Math.pi", Value{pi}, false);
    env->define("Math.e", Value{std::numbers::e}, false);
    env->define("Math.tau", Value{2.0 * pi}, false);
    env->define("Math.infinity", Value{std::numeric_limits<double>::infinity()}, false);
    env->define("Math.nan", Value{std::numeric_limits<double>::quiet_NaN()}, false);
    env->define("Math.epsilon", Value{std::numeric_limits<double>::epsilon()}, false);
    env->define("Math.max_integer", Value{std::numeric_limits<std::int64_t>::max()}, false);
    env->define("Math.min_integer", Value{std::numeric_limits<std::int64_t>::min()}, false);
    // Largest finite and smallest positive normal double, mirroring max_/min_integer.
    env->define("Math.max_number", Value{std::numeric_limits<double>::max()}, false);
    env->define("Math.min_number", Value{std::numeric_limits<double>::min()}, false);
    // Mathematical constants sourced from <numbers>, exactly as pi/e above.
    env->define("Math.sqrt2", Value{std::numbers::sqrt2}, false);
    env->define("Math.golden_ratio", Value{std::numbers::phi}, false);
    env->define("Math.ln2", Value{std::numbers::ln2}, false);
    env->define("Math.ln10", Value{std::numbers::ln10}, false);

    ModuleBuilder{"Math", env}
        .checked_unary_to_int("floor", [](double x) { return std::floor(x); })
        .checked_unary_to_int("ceil", [](double x) { return std::ceil(x); })
        .checked_unary_to_int("round", [](double x) { return std::round(x); })
        // Math.round_to(x: number, places: integer) -> result<number>
        // Round x to `places` decimal places, returning a number (unlike
        // Math.round, which returns an integer).  Fails if places is negative or
        // exceeds the precision a 64-bit double can represent.
        .func("round_to", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = expect_numeric(args[0], "Math.round_to", loc);
            const auto places = expect_integer(args[1], "Math.round_to", loc);

            constexpr std::int64_t k_max_places{15};

            if (places < 0) {
                return make_failure_value(
                    error_msg("Math", "round_to", "places must not be negative"));
            }
            if (places > k_max_places) {
                return make_failure_value(error_msg(
                    "Math", "round_to", std::format("places must not exceed {}", k_max_places)));
            }
            if (!std::isfinite(x)) {
                return make_failure_value(error_msg("Math", "round_to", "value must be finite"));
            }

            const double factor = std::pow(10.0, static_cast<double>(places));
            const double result = std::round(x * factor) / factor;

            // A finite x with large places can overflow x * factor to ±inf,
            // yielding inf/NaN — reject it rather than wrap a non-real number in
            // a success result (mirrors Math.power's is_valid_numeric guard).
            if (!stdlib::is_valid_numeric(result)) {
                return make_failure_value(
                    error_msg("Math", "round_to", "result is not a finite number"));
            }

            return make_success_value(Value{result});
        })
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
        .func("combinations", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto n = expect_integer(args[0], "Math.combinations", loc);
            auto k = expect_integer(args[1], "Math.combinations", loc);

            if (n < 0 || k < 0) {
                return make_failure_value(error_msg("Math", "combinations", "negative number"));
            }

            if (k > n) {
                return make_failure_value(error_msg("Math", "combinations", "k must not exceed n"));
            }

            // C(n, k) == C(n, n - k); pick the smaller k to minimise iterations.
            if (k > n - k) {
                k = n - k;
            }

            std::int64_t result{1};

            for (std::int64_t i{1}; i <= k; ++i) {
                // result = result * (n - k + i) / i, kept exact by cancelling the
                // gcd first: after dividing both terms by gcd(numerator, i) the
                // reduced divisor exactly divides the running result, so no
                // intermediate fraction and no false overflow of a representable
                // result.
                std::int64_t numerator = n - k + i;
                std::int64_t divisor = i;
                const std::int64_t common = std::gcd(numerator, divisor);

                numerator /= common;
                divisor /= common;
                result /= divisor;

                if (would_overflow_mul(result, numerator)) {
                    return make_failure_value(
                        error_msg("Math", "combinations", "integer overflow"));
                }

                result *= numerator;
            }

            return make_success_value(Value{result});
        })
        .func("permutations", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto n = expect_integer(args[0], "Math.permutations", loc);
            auto k = expect_integer(args[1], "Math.permutations", loc);

            if (n < 0 || k < 0) {
                return make_failure_value(error_msg("Math", "permutations", "negative number"));
            }

            if (k > n) {
                return make_failure_value(error_msg("Math", "permutations", "k must not exceed n"));
            }

            std::int64_t result{1};

            for (std::int64_t i{0}; i < k; ++i) {
                const std::int64_t factor = n - i;

                if (would_overflow_mul(result, factor)) {
                    return make_failure_value(
                        error_msg("Math", "permutations", "integer overflow"));
                }

                result *= factor;
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
        .func("is_even", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "Math.is_even", loc);
            // C++ truncates toward zero, so n % 2 is 0 for every even integer
            // including negatives (e.g. -4 % 2 == 0).
            return Value{n % 2 == 0};
        })
        .func("digit_sum", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto n = expect_integer(args[0], "Math.digit_sum", loc);

            std::int64_t sum{0};

            // Accumulate on the negative side so INT64_MIN (whose magnitude has no
            // positive representation) is handled without overflow.
            if (n > 0) {
                n = -n;
            }

            while (n != 0) {
                sum += -(n % 10);
                n /= 10;
            }

            return Value{sum};
        })
        .func("digit_count", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto n = expect_integer(args[0], "Math.digit_count", loc);

            if (n == 0) {
                return Value{static_cast<std::int64_t>(1)};
            }

            std::int64_t count{0};

            // Divide on the negative side so INT64_MIN is counted without overflow.
            if (n > 0) {
                n = -n;
            }

            while (n != 0) {
                ++count;
                n /= 10;
            }

            return Value{count};
        })
        .func("is_odd", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "Math.is_odd", loc);
            // For negatives n % 2 is -1 (e.g. -3 % 2 == -1), so compare against 0.
            return Value{n % 2 != 0};
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
        })
        // ── Math.Angle (unit-safe angle) ─────────────────────────────────────
        // Optional convenience wrappers around the Math.Angle choice
        // (Radians(number) | Degrees(number)).  They make an easy-to-confuse
        // quantity self-documenting; the bare number-radians trig APIs stay
        // primary.
        .func("to_radians", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{angle_to_radians(args[0], "Math.to_radians", loc)};
        })
        .func("to_degrees", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{angle_to_radians(args[0], "Math.to_degrees", loc) * 180.0 / pi};
        })
        .func("sin_of", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{std::sin(angle_to_radians(args[0], "Math.sin_of", loc))};
        })
        // ── Math.Interval ────────────────────────────────────────────────────
        // A closed numeric range [min, max].  Mirrors DateTime.Interval: a
        // validating constructor (fails when min > max) plus contains/clamp/
        // length/overlap, so beginners get a named, reusable range instead of
        // hand-rolled `x >= lo && x <= hi`.
        .func("interval", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const double min = expect_numeric(args[0], "Math.interval", loc);
            const double max = expect_numeric(args[1], "Math.interval", loc);

            if (max < min) {
                return make_failure_value(error_msg("Math", "interval", "max must be >= min"));
            }

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "Interval";
            rec->fields.emplace_back("min", Value{min});
            rec->fields.emplace_back("max", Value{max});

            return make_success_value(Value{std::move(rec)});
        })
        .func("interval_contains", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto iv = read_interval(args[0], "Math.interval_contains", loc);
            const double x = expect_numeric(args[1], "Math.interval_contains", loc);

            // Closed interval: both endpoints count as inside.
            return Value{x >= iv.min && x <= iv.max};
        })
        .func("interval_clamp", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto iv = read_interval(args[0], "Math.interval_clamp", loc);
            const double x = expect_numeric(args[1], "Math.interval_clamp", loc);

            return Value{std::clamp(x, iv.min, iv.max)};
        })
        .func("interval_length", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto iv = read_interval(args[0], "Math.interval_length", loc);

            return Value{iv.max - iv.min};
        })
        .func("intervals_overlap", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_interval(args[0], "Math.intervals_overlap", loc);
            const auto b = read_interval(args[1], "Math.intervals_overlap", loc);

            // Closed intervals, consistent with interval_contains: touching
            // endpoints (a.max == b.min) count as overlapping at that point.
            return Value{a.min <= b.max && b.min <= a.max};
        })
        // ── Math.Rect ────────────────────────────────────────────────────────
        // An axis-aligned rectangle { x, y, width, height }, the 2D analogue of
        // Math.Interval.  A total constructor (dimensions clamped non-negative)
        // plus contains/intersects/intersection/union/center/area, so beginners
        // get named layout and hit-testing instead of hand-rolled overlap
        // arithmetic.  Edges are half-open ([x, x+width)) — consistent with
        // DOMRect hit-testing — so a zero-size rect contains nothing.
        .func("rect", 4)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const double x = expect_numeric(args[0], "Math.rect", loc);
            const double y = expect_numeric(args[1], "Math.rect", loc);
            const double width = expect_numeric(args[2], "Math.rect", loc);
            const double height = expect_numeric(args[3], "Math.rect", loc);

            return make_rect(RectBounds{.x = x, .y = y, .width = width, .height = height});
        })
        .func("rect_contains", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto r = read_rect(args[0], "Math.rect_contains", loc);
            const double px = expect_numeric(args[1], "Math.rect_contains", loc);
            const double py = expect_numeric(args[2], "Math.rect_contains", loc);

            // Half-open: the min edges are inside, the max edges are outside.
            return Value{px >= r.x && px < r.x + r.width && py >= r.y && py < r.y + r.height};
        })
        .func("rect_intersects", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_rect(args[0], "Math.rect_intersects", loc);
            const auto b = read_rect(args[1], "Math.rect_intersects", loc);

            // Half-open overlap: touching edges (a.x + a.width == b.x) do not
            // count as intersecting, consistent with rect_contains.
            return Value{a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
                         b.y < a.y + a.height};
        })
        .func("rect_intersection", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_rect(args[0], "Math.rect_intersection", loc);
            const auto b = read_rect(args[1], "Math.rect_intersection", loc);

            const double left = std::max(a.x, b.x);
            const double top = std::max(a.y, b.y);
            const double right = std::min(a.x + a.width, b.x + b.width);
            const double bottom = std::min(a.y + a.height, b.y + b.height);

            // No positive-area overlap → none.  The one fallible operation
            // returns optional<Math.Rect> (none is Value{NullValue{}}).
            if (right <= left || bottom <= top) {
                return Value{NullValue{}};
            }

            return make_rect(
                RectBounds{.x = left, .y = top, .width = right - left, .height = bottom - top});
        })
        .func("rect_union", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_rect(args[0], "Math.rect_union", loc);
            const auto b = read_rect(args[1], "Math.rect_union", loc);

            const double left = std::min(a.x, b.x);
            const double top = std::min(a.y, b.y);
            const double right = std::max(a.x + a.width, b.x + b.width);
            const double bottom = std::max(a.y + a.height, b.y + b.height);

            return make_rect(
                RectBounds{.x = left, .y = top, .width = right - left, .height = bottom - top});
        })
        .func("rect_center", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto r = read_rect(args[0], "Math.rect_center", loc);

            return make_rect_vec2(r.x + r.width / 2.0, r.y + r.height / 2.0);
        })
        .func("rect_area", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto r = read_rect(args[0], "Math.rect_area", loc);

            return Value{r.width * r.height};
        })
        // ── Math.Circle ──────────────────────────────────────────────────────
        // A 2D circle { center: Math.Vector2, radius }, the disk companion to
        // Math.Rect.  A total constructor (radius clamped non-negative) plus
        // contains/intersects/circle_rect_intersects, so beginners get the common
        // circle collision tests without hand-writing the distance-squared
        // comparison.  All predicates use inclusive (closed-disk) boundaries.
        .func("circle", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto center = read_vec2_point(args[0], "Math.circle", loc);
            const double radius = expect_numeric(args[1], "Math.circle", loc);

            return make_circle(center.x, center.y, radius);
        })
        .func("circle_contains", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_circle(args[0], "Math.circle_contains", loc);
            const auto p = read_vec2_point(args[1], "Math.circle_contains", loc);

            // Closed disk: a point exactly on the boundary counts as contained.
            const double dx = p.x - c.cx;
            const double dy = p.y - c.cy;

            return Value{dx * dx + dy * dy <= c.radius * c.radius};
        })
        .func("circle_intersects", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_circle(args[0], "Math.circle_intersects", loc);
            const auto b = read_circle(args[1], "Math.circle_intersects", loc);

            // Two disks overlap (or touch) when the distance between centres is at
            // most the sum of the radii — compared squared to avoid a sqrt.
            const double dx = a.cx - b.cx;
            const double dy = a.cy - b.cy;
            const double sum = a.radius + b.radius;

            return Value{dx * dx + dy * dy <= sum * sum};
        })
        .func("circle_rect_intersects", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto c = read_circle(args[0], "Math.circle_rect_intersects", loc);
            const auto r = read_rect(args[1], "Math.circle_rect_intersects", loc);

            // Distance from the circle centre to the closest point on the rect: if
            // that is within the radius, the shapes overlap.  Edges are inclusive,
            // matching the closed-disk contains predicate.
            const double closest_x = std::clamp(c.cx, r.x, r.x + r.width);
            const double closest_y = std::clamp(c.cy, r.y, r.y + r.height);
            const double dx = c.cx - closest_x;
            const double dy = c.cy - closest_y;

            return Value{dx * dx + dy * dy <= c.radius * c.radius};
        });

    register_math_analysis(env);
    register_math_transcendental(env);
    register_math_fraction(env);
    register_math_complex(env);
    register_math_vectors(env);
}

} // namespace luma
