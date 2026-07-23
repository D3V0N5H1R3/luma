// Standard library tests: Decimal module (exact base-10 arithmetic).
//
// These exercise the function-based Decimal API end-to-end through the VM.
// Decimal values are opaque, so results are checked by their canonical
// to_string() form or via Decimal.equals / Decimal.compare, which are
// scale-insensitive.

#include <cmath>
#include <string>

#include "stdlib_test_helpers.hpp"

namespace {

// A Luma expression that parses a decimal literal, unwrapping the result.
std::string dec(const std::string& literal) {
    return "Result.unwrap(Decimal.from_string(\"" + literal + "\"))";
}

// Evaluate Decimal.to_string(expr) and return the resulting C++ string.
std::string to_str(const std::string& expr) {
    return eval("Decimal.to_string(" + expr + ")").as_string();
}

// A Luma expression that divides two decimal expressions at the given scale,
// unwrapping the result.
std::string divi(const std::string& a, const std::string& b, int scale) {
    return "Result.unwrap(Decimal.divide(" + a + ", " + b + ", " + std::to_string(scale) + "))";
}

// Whether two decimal expressions are equal (scale-insensitive).
bool dequals(const std::string& a, const std::string& b) {
    return eval("Decimal.equals(" + a + ", " + b + ")").as_bool();
}

} // namespace

// ─── Construction ───

LUMA_TEST(decimal_from_string_valid) {
    ASSERT_EQ(to_str(dec("0.1")), std::string("0.1"));
    ASSERT_EQ(to_str(dec("12.34")), std::string("12.34"));
    ASSERT_EQ(to_str(dec("-5")), std::string("-5"));
    ASSERT_EQ(to_str(dec("0")), std::string("0"));
    ASSERT_EQ(to_str(dec("1000000")), std::string("1000000"));
    // Leading/trailing forms normalise to a canonical value.
    ASSERT_TRUE(dequals(dec("-0"), dec("0")));

    // from_string returns a successful result.
    const auto ok = eval("Decimal.from_string(\"3.14\")");
    ASSERT_RESULT_SUCCESS(ok);
    ASSERT_TRUE(ok.as_result()->owned_inner->is_decimal());
}

LUMA_TEST(decimal_from_string_invalid) {
    // Non-numeric and malformed inputs fail rather than crash.
    ASSERT_RESULT_FAILURE(eval("Decimal.from_string(\"abc\")"));
    ASSERT_RESULT_FAILURE(eval("Decimal.from_string(\"\")"));
    ASSERT_RESULT_FAILURE(eval("Decimal.from_string(\"1.2.3\")"));
    ASSERT_RESULT_FAILURE(eval("Decimal.from_string(\"--1\")"));
    ASSERT_RESULT_FAILURE(eval("Decimal.from_string(\"1 2\")"));
    ASSERT_RESULT_FAILURE(eval("Decimal.from_string(\".\")"));
}

LUMA_TEST(decimal_from_number_exact_roundtrip) {
    // 0.1 has no exact binary form, but shortest round-trip recovers "0.1".
    ASSERT_EQ(to_str("Decimal.from_number(0.1)"), std::string("0.1"));
    ASSERT_EQ(to_str("Decimal.from_number(1.5)"), std::string("1.5"));
    // A whole number keeps no fractional digits.
    ASSERT_EQ(to_str("Decimal.from_number(2.0)"), std::string("2"));
    ASSERT_EQ(to_str("Decimal.from_number(-0.25)"), std::string("-0.25"));
}

LUMA_TEST(decimal_from_number_non_finite_throws) {
    // NaN and infinity have no decimal representation — a programmer error.
    ASSERT_THROWS(eval("Decimal.from_number(Math.infinity)"));
    ASSERT_THROWS(eval("Decimal.from_number(-Math.infinity)"));
}

LUMA_TEST(decimal_from_integer) {
    ASSERT_EQ(to_str("Decimal.from_integer(42)"), std::string("42"));
    ASSERT_EQ(to_str("Decimal.from_integer(-7)"), std::string("-7"));
    ASSERT_EQ(to_str("Decimal.from_integer(0)"), std::string("0"));
}

// ─── Arithmetic ───

LUMA_TEST(decimal_add_is_exact) {
    // The headline: IEEE-754 gets this wrong, decimal does not.
    ASSERT_TRUE(dequals("Decimal.add(" + dec("0.1") + ", " + dec("0.2") + ")", dec("0.3")));
    ASSERT_EQ(to_str("Decimal.add(" + dec("0.1") + ", " + dec("0.2") + ")"), std::string("0.3"));

    // Mixed scales.
    ASSERT_EQ(to_str("Decimal.add(" + dec("1.5") + ", " + dec("2.25") + ")"), std::string("3.75"));
    // Negative operands. Arithmetic preserves scale, so the value (not its
    // formatting) is what matters here: -1.5 + 0.5 == -1.
    ASSERT_TRUE(dequals("Decimal.add(" + dec("-1.5") + ", " + dec("0.5") + ")", dec("-1")));

    // Arbitrary precision — well beyond 64-bit integers.
    ASSERT_EQ(to_str("Decimal.add(" + dec("99999999999999999999") + ", " + dec("1") + ")"),
              std::string("100000000000000000000"));
}

LUMA_TEST(decimal_subtract) {
    ASSERT_EQ(to_str("Decimal.subtract(" + dec("0.3") + ", " + dec("0.1") + ")"),
              std::string("0.2"));
    ASSERT_EQ(to_str("Decimal.subtract(" + dec("1") + ", " + dec("0.9") + ")"), std::string("0.1"));
    ASSERT_EQ(to_str("Decimal.subtract(" + dec("5") + ", " + dec("8") + ")"), std::string("-3"));
}

LUMA_TEST(decimal_multiply) {
    ASSERT_EQ(to_str("Decimal.multiply(" + dec("0.1") + ", " + dec("0.2") + ")"),
              std::string("0.02"));
    ASSERT_EQ(to_str("Decimal.multiply(" + dec("1.11") + ", " + dec("1.11") + ")"),
              std::string("1.2321"));
    ASSERT_EQ(to_str("Decimal.multiply(" + dec("-3") + ", " + dec("4") + ")"), std::string("-12"));
    // Multiplying by zero yields zero.
    ASSERT_TRUE(eval("Decimal.is_zero(Decimal.multiply(" + dec("123.45") + ", " + dec("0") + "))")
                    .as_bool());
}

// ─── Division ───

LUMA_TEST(decimal_divide_with_scale) {
    // 10 / 3 rounded (half-up) to 4 places.
    ASSERT_TRUE(dequals(divi(dec("10"), dec("3"), 4), dec("3.3333")));
    // Exact division.
    ASSERT_TRUE(dequals(divi(dec("1"), dec("8"), 3), dec("0.125")));
    ASSERT_TRUE(dequals(divi(dec("10"), dec("4"), 2), dec("2.5")));
    // Half-up rounding of the last retained digit: 2/3 = 0.667 at scale 3.
    ASSERT_TRUE(dequals(divi(dec("2"), dec("3"), 3), dec("0.667")));
}

LUMA_TEST(decimal_divide_failures) {
    // Division by zero is a failure, not a crash.
    ASSERT_RESULT_FAILURE(eval("Decimal.divide(" + dec("10") + ", " + dec("0") + ", 2)"));
    // A negative scale is rejected.
    ASSERT_RESULT_FAILURE(eval("Decimal.divide(" + dec("10") + ", " + dec("3") + ", -1)"));
}

// ─── Rounding ───

LUMA_TEST(decimal_round_positive_modes) {
    const std::string v = dec("2.345");
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"half_up\")"), std::string("2.35"));
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"half_even\")"), std::string("2.34"));
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"half_down\")"), std::string("2.34"));
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"up\")"), std::string("2.35"));
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"down\")"), std::string("2.34"));
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"floor\")"), std::string("2.34"));
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"ceiling\")"), std::string("2.35"));
}

LUMA_TEST(decimal_round_negative_modes) {
    const std::string v = dec("-2.345");
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"up\")"), std::string("-2.35"));
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"down\")"), std::string("-2.34"));
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"floor\")"), std::string("-2.35"));
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"ceiling\")"), std::string("-2.34"));
    ASSERT_EQ(to_str("Decimal.round(" + v + ", 2, \"half_up\")"), std::string("-2.35"));
}

LUMA_TEST(decimal_round_banker_ties) {
    // Half-even (banker's) rounding breaks ties toward the even neighbour.
    ASSERT_EQ(to_str("Decimal.round(" + dec("2.5") + ", 0, \"half_even\")"), std::string("2"));
    ASSERT_EQ(to_str("Decimal.round(" + dec("3.5") + ", 0, \"half_even\")"), std::string("4"));
    ASSERT_EQ(to_str("Decimal.round(" + dec("2.5") + ", 0, \"half_up\")"), std::string("3"));
}

LUMA_TEST(decimal_multiply_overflow_throws) {
    // 1e-1000000 has scale 1'000'000 (the internal digit cap). Squaring it would
    // double the scale past the cap, which historically overflowed the scale
    // field (undefined behaviour); it must now raise a runtime error instead of
    // silently corrupting the value.
    ASSERT_THROWS(eval("Decimal.multiply(" + dec("1e-1000000") + ", " + dec("1e-1000000") + ")"));
}

LUMA_TEST(decimal_round_edge_cases) {
    // Rounding to more places than the value has leaves it unchanged.
    ASSERT_TRUE(dequals("Decimal.round(" + dec("1.5") + ", 5, \"half_up\")", dec("1.5")));
    // Negative places clamp to zero (round to an integer).
    ASSERT_TRUE(dequals("Decimal.round(" + dec("12.6") + ", -3, \"half_up\")", dec("13")));
    // An unknown rounding mode is a programmer error.
    ASSERT_THROWS(eval("Decimal.round(" + dec("1.5") + ", 2, \"bogus\")"));
}

// ─── Comparison & equality ───

LUMA_TEST(decimal_compare) {
    ASSERT_EQ(eval("Decimal.compare(" + dec("1") + ", " + dec("2") + ")").as_integer(), -1);
    ASSERT_EQ(eval("Decimal.compare(" + dec("2") + ", " + dec("1") + ")").as_integer(), 1);
    ASSERT_EQ(eval("Decimal.compare(" + dec("2") + ", " + dec("2") + ")").as_integer(), 0);
    // Scale-insensitive.
    ASSERT_EQ(eval("Decimal.compare(" + dec("1.5") + ", " + dec("1.50") + ")").as_integer(), 0);
    ASSERT_EQ(eval("Decimal.compare(" + dec("-1") + ", " + dec("1") + ")").as_integer(), -1);
    // 0.1 + 0.2 compares equal to 0.3.
    ASSERT_EQ(eval("Decimal.compare(Decimal.add(" + dec("0.1") + ", " + dec("0.2") + "), " +
                   dec("0.3") + ")")
                  .as_integer(),
              0);
}

LUMA_TEST(decimal_equals_is_scale_insensitive) {
    ASSERT_TRUE(dequals(dec("1.5"), dec("1.50")));
    ASSERT_TRUE(dequals(dec("1.500"), dec("1.5")));
    ASSERT_TRUE(dequals("Decimal.add(" + dec("0.1") + ", " + dec("0.2") + ")", dec("0.3")));
    ASSERT_FALSE(dequals(dec("1"), dec("2")));
}

// ─── Predicates & sign ───

LUMA_TEST(decimal_predicates) {
    ASSERT_TRUE(eval("Decimal.is_zero(" + dec("0") + ")").as_bool());
    ASSERT_TRUE(eval("Decimal.is_zero(" + dec("0.00") + ")").as_bool());
    ASSERT_FALSE(eval("Decimal.is_zero(" + dec("0.01") + ")").as_bool());

    ASSERT_TRUE(eval("Decimal.is_negative(" + dec("-1") + ")").as_bool());
    ASSERT_FALSE(eval("Decimal.is_negative(" + dec("1") + ")").as_bool());
    // Zero is not negative.
    ASSERT_FALSE(eval("Decimal.is_negative(" + dec("0") + ")").as_bool());
}

LUMA_TEST(decimal_negate_and_absolute) {
    ASSERT_EQ(to_str("Decimal.negate(" + dec("1.5") + ")"), std::string("-1.5"));
    ASSERT_EQ(to_str("Decimal.negate(" + dec("-2.5") + ")"), std::string("2.5"));
    // Negating zero stays zero (no negative zero).
    ASSERT_TRUE(eval("Decimal.is_zero(Decimal.negate(" + dec("0") + "))").as_bool());
    ASSERT_FALSE(eval("Decimal.is_negative(Decimal.negate(" + dec("0") + "))").as_bool());

    ASSERT_EQ(to_str("Decimal.absolute(" + dec("-2.5") + ")"), std::string("2.5"));
    ASSERT_EQ(to_str("Decimal.absolute(" + dec("3") + ")"), std::string("3"));
}

LUMA_TEST(decimal_scale) {
    ASSERT_EQ(eval("Decimal.scale(" + dec("1.5") + ")").as_integer(), 1);
    ASSERT_EQ(eval("Decimal.scale(" + dec("0.125") + ")").as_integer(), 3);
    ASSERT_EQ(eval("Decimal.scale(" + dec("42") + ")").as_integer(), 0);
    ASSERT_EQ(eval("Decimal.scale(Decimal.from_integer(7))").as_integer(), 0);
}

// ─── Conversion ───

LUMA_TEST(decimal_to_string) {
    ASSERT_EQ(eval("Decimal.to_string(" + dec("123.456") + ")").as_string(),
              std::string("123.456"));
    ASSERT_EQ(eval("Decimal.to_string(" + dec("-0.5") + ")").as_string(), std::string("-0.5"));
}

LUMA_TEST(decimal_to_number) {
    ASSERT_EQ(eval("Decimal.to_number(" + dec("0.5") + ")").as_number(), 0.5);
    ASSERT_EQ(eval("Decimal.to_number(" + dec("-2.25") + ")").as_number(), -2.25);
    // 0.1 + 0.2 converts back to the nearest double to 0.3.
    const auto n =
        eval("Decimal.to_number(Decimal.add(" + dec("0.1") + ", " + dec("0.2") + "))").as_number();
    ASSERT_TRUE(std::fabs(n - 0.3) < 1e-12);
}

int main() {
    LUMA_RUN_ALL();
}
