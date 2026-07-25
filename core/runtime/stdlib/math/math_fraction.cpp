#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/overflow.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/math/math_module.hpp"

namespace luma {

namespace {

// A rational in canonical form: reduced to lowest terms with a strictly
// positive denominator (the sign lives entirely in the numerator).  Zero is
// stored as 0/1.  Both fields are guaranteed to fit in int64 — INT64_MIN is
// rejected on the way in because negating or taking its absolute value is
// undefined behaviour.
struct Frac {
    std::int64_t numerator;
    std::int64_t denominator;
};

// Absolute value of a value already known not to be INT64_MIN.
[[nodiscard]] std::int64_t abs_i64(std::int64_t v) {
    return v < 0 ? -v : v;
}

// Euclidean GCD over non-negative operands (never touches std::abs, so it is
// safe for the full int64 magnitude range once INT64_MIN has been excluded).
[[nodiscard]] std::int64_t gcd_pos(std::int64_t a, std::int64_t b) {
    while (b != 0) {
        const auto t = a % b;
        a = b;
        b = t;
    }

    return a;
}

// Reduce num/den to canonical form.  Requires den != 0 and neither operand
// equal to INT64_MIN (both guaranteed by read_frac / the constructor guard).
[[nodiscard]] Frac normalize(std::int64_t num, std::int64_t den) {
    if (den < 0) {
        num = -num;
        den = -den;
    }

    const auto divisor = gcd_pos(abs_i64(num), den);

    // gcd(0, den) == den, so a zero numerator collapses to 0/1; every other
    // case has divisor >= 1.
    return Frac{num / divisor, den / divisor};
}

// Build a Math.Fraction record value from an already-canonical Frac.  The short
// runtime type_name "Fraction" matches the "Math.Fraction" record registered in
// core/analysis/types/stdlib_type_arities.cpp.
[[nodiscard]] Value make_fraction(const Frac& f) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Fraction";
    rec->fields.emplace_back("numerator", Value{f.numerator});
    rec->fields.emplace_back("denominator", Value{f.denominator});

    return Value{std::move(rec)};
}

// Read and canonicalise a Math.Fraction argument.  Throws a RuntimeError when
// the value is not a fraction-shaped record, its denominator is zero, or either
// field is INT64_MIN (which cannot be reduced without overflow).  Accepting a
// hand-built record and re-normalising it keeps every operation robust even for
// values that did not come from Math.fraction().
[[nodiscard]] Frac read_frac(const Value& value, std::string_view func, const SourceLocation& loc) {
    if (!value.is_record()) {
        throw RuntimeError{std::string{func} + ": expected a Math.Fraction record", loc,
                           "build one with Math.fraction(numerator, denominator)"};
    }

    const auto& rec = value.as_record();
    const Value* num_field = rec->find_field("numerator");
    const Value* den_field = rec->find_field("denominator");

    if (num_field == nullptr || !num_field->is_integer() || den_field == nullptr ||
        !den_field->is_integer()) {
        throw RuntimeError{std::string{func} + ": expected a Math.Fraction record", loc,
                           "build one with Math.fraction(numerator, denominator)"};
    }

    const auto num = num_field->as_integer();
    const auto den = den_field->as_integer();

    if (den == 0) {
        throw RuntimeError{std::string{func} + ": fraction has a zero denominator", loc,
                           "build fractions with Math.fraction(numerator, denominator)"};
    }

    constexpr auto min64 = std::numeric_limits<std::int64_t>::min();
    if (num == min64 || den == min64) {
        throw RuntimeError{std::string{func} + ": fraction operand is out of range", loc};
    }

    return normalize(num, den);
}

// Checked int64 arithmetic — throws a RuntimeError on overflow, mirroring the
// VM's behaviour for native integer `+`, `-`, and `*`.  A result of exactly
// INT64_MIN is also rejected as overflow: it cannot be reduced by normalize()
// (whose abs_i64 / unary negation on INT64_MIN would be signed-overflow UB),
// matching the constructor's rejection of INT64_MIN inputs.
[[nodiscard]] std::int64_t checked_add(std::int64_t a, std::int64_t b, std::string_view func,
                                       const SourceLocation& loc) {
    if (would_overflow_add(a, b) || a + b == std::numeric_limits<std::int64_t>::min()) {
        throw RuntimeError{std::string{func} + ": integer overflow", loc};
    }

    return a + b;
}

[[nodiscard]] std::int64_t checked_sub(std::int64_t a, std::int64_t b, std::string_view func,
                                       const SourceLocation& loc) {
    if (would_overflow_sub(a, b) || a - b == std::numeric_limits<std::int64_t>::min()) {
        throw RuntimeError{std::string{func} + ": integer overflow", loc};
    }

    return a - b;
}

[[nodiscard]] std::int64_t checked_mul(std::int64_t a, std::int64_t b, std::string_view func,
                                       const SourceLocation& loc) {
    if (would_overflow_mul(a, b)) {
        throw RuntimeError{std::string{func} + ": integer overflow", loc};
    }

    if (a * b == std::numeric_limits<std::int64_t>::min()) {
        throw RuntimeError{std::string{func} + ": integer overflow", loc};
    }

    return a * b;
}

// Exact ordering of two positive fractions a/b and c/d (all operands > 0) using
// the continued-fraction (Euclidean) algorithm.  This never multiplies the
// numerators and denominators together, so it cannot overflow — matching the
// portability stance of core/common/decimal.hpp (no compiler __int128).
[[nodiscard]] int compare_positive(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d) {
    while (true) {
        const auto q1 = a / b;
        const auto q2 = c / d;

        if (q1 != q2) {
            return q1 < q2 ? -1 : 1;
        }

        const auto r1 = a % b;
        const auto r2 = c % d;

        if (r1 == 0 && r2 == 0) {
            return 0;
        }
        if (r1 == 0) {
            return -1;
        }
        if (r2 == 0) {
            return 1;
        }

        // Both fractional parts are non-zero: compare their reciprocals
        // b/r1 and d/r2, which flips the ordering — so continue with the
        // swapped pairs (d/r2 vs b/r1).
        const auto next_a = d;
        const auto next_b = r2;
        const auto next_c = b;
        const auto next_d = r1;

        a = next_a;
        b = next_b;
        c = next_c;
        d = next_d;
    }
}

// Numeric sign of x - y for two canonical fractions.
[[nodiscard]] int compare_fractions(const Frac& x, const Frac& y) {
    if (x.numerator == y.numerator && x.denominator == y.denominator) {
        return 0;
    }

    const int sx = (x.numerator > 0) - (x.numerator < 0);
    const int sy = (y.numerator > 0) - (y.numerator < 0);

    if (sx != sy) {
        return sx < sy ? -1 : 1;
    }

    if (sx == 0) {
        return 0;
    }

    const int mag =
        compare_positive(abs_i64(x.numerator), x.denominator, abs_i64(y.numerator), y.denominator);

    // For negative values the larger magnitude is the smaller number.
    return sx > 0 ? mag : -mag;
}

// Build a top-level Ordering choice value (type_name "Ordering") from a
// strcmp-style sign, matching Order.of and the Ordering variants registered by
// register_stdlib_postamble.
[[nodiscard]] Value make_ordering(int sign) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "Ordering";

    if (sign < 0) {
        cv->variant = "Less";
    } else if (sign > 0) {
        cv->variant = "Greater";
    } else {
        cv->variant = "Equal";
    }

    return Value{std::move(cv)};
}

} // namespace

void register_math_fraction(const EnvPtr& env) {
    ModuleBuilder{"Math", env}
        .func("fraction", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto num = expect_integer(args[0], "Math.fraction", loc);
            const auto den = expect_integer(args[1], "Math.fraction", loc);

            if (den == 0) {
                return make_failure_value(
                    error_msg("Math", "fraction", "denominator cannot be zero"));
            }

            constexpr auto min64 = std::numeric_limits<std::int64_t>::min();
            if (num == min64 || den == min64) {
                return make_failure_value(error_msg("Math", "fraction", "operand is out of range"));
            }

            return make_success_value(make_fraction(normalize(num, den)));
        })
        .func("fraction_add", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_frac(args[0], "Math.fraction_add", loc);
            const auto b = read_frac(args[1], "Math.fraction_add", loc);

            const auto g = gcd_pos(a.denominator, b.denominator);
            const auto da_g = a.denominator / g;
            const auto db_g = b.denominator / g;

            const auto t1 = checked_mul(a.numerator, db_g, "Math.fraction_add", loc);
            const auto t2 = checked_mul(b.numerator, da_g, "Math.fraction_add", loc);
            const auto num = checked_add(t1, t2, "Math.fraction_add", loc);
            const auto den = checked_mul(da_g, b.denominator, "Math.fraction_add", loc);

            return make_fraction(normalize(num, den));
        })
        .func("fraction_subtract", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_frac(args[0], "Math.fraction_subtract", loc);
            const auto b = read_frac(args[1], "Math.fraction_subtract", loc);

            const auto g = gcd_pos(a.denominator, b.denominator);
            const auto da_g = a.denominator / g;
            const auto db_g = b.denominator / g;

            const auto t1 = checked_mul(a.numerator, db_g, "Math.fraction_subtract", loc);
            const auto t2 = checked_mul(b.numerator, da_g, "Math.fraction_subtract", loc);
            const auto num = checked_sub(t1, t2, "Math.fraction_subtract", loc);
            const auto den = checked_mul(da_g, b.denominator, "Math.fraction_subtract", loc);

            return make_fraction(normalize(num, den));
        })
        .func("fraction_multiply", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_frac(args[0], "Math.fraction_multiply", loc);
            const auto b = read_frac(args[1], "Math.fraction_multiply", loc);

            // Cross-reduce before multiplying to keep the products small.
            const auto g1 = gcd_pos(abs_i64(a.numerator), b.denominator);
            const auto g2 = gcd_pos(abs_i64(b.numerator), a.denominator);

            const auto num =
                checked_mul(a.numerator / g1, b.numerator / g2, "Math.fraction_multiply", loc);
            const auto den =
                checked_mul(a.denominator / g2, b.denominator / g1, "Math.fraction_multiply", loc);

            return make_fraction(normalize(num, den));
        })
        .func("fraction_divide", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_frac(args[0], "Math.fraction_divide", loc);
            const auto b = read_frac(args[1], "Math.fraction_divide", loc);

            if (b.numerator == 0) {
                return make_failure_value(
                    error_msg("Math", "fraction_divide", "cannot divide by zero"));
            }

            // a/b ÷ c/d = a*d / (b*c); cross-reduce first.
            const auto g1 = gcd_pos(abs_i64(a.numerator), abs_i64(b.numerator));
            const auto g2 = gcd_pos(a.denominator, b.denominator);

            const auto num =
                checked_mul(a.numerator / g1, b.denominator / g2, "Math.fraction_divide", loc);
            const auto den =
                checked_mul(a.denominator / g2, b.numerator / g1, "Math.fraction_divide", loc);

            return make_success_value(make_fraction(normalize(num, den)));
        })
        .func("fraction_to_number", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto f = read_frac(args[0], "Math.fraction_to_number", loc);

            return Value{static_cast<double>(f.numerator) / static_cast<double>(f.denominator)};
        })
        .func("fraction_compare", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = read_frac(args[0], "Math.fraction_compare", loc);
            const auto b = read_frac(args[1], "Math.fraction_compare", loc);

            return make_ordering(compare_fractions(a, b));
        });
}

} // namespace luma
