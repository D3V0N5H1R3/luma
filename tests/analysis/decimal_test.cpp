// Unit tests for core/common/decimal.hpp — the exact base-10 Decimal type.

#include <limits>
#include <optional>
#include <string>

#include "common/decimal.hpp"
#include "test_framework.hpp"

using namespace luma;

namespace {

// Parses a literal, asserting success, for concise test setup.
Decimal dec(std::string_view text) {
    auto parsed = Decimal::parse(text);
    ASSERT_TRUE(parsed.has_value());
    return *parsed;
}

// Multiplies two Decimals, asserting the product fits the digit cap, for
// concise test setup (multiply is fallible: it rejects over-cap products).
Decimal mul(const Decimal& a, const Decimal& b) {
    auto product = a.multiply(b);
    ASSERT_TRUE(product.has_value());
    return *product;
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Parsing and round-tripping
// ═══════════════════════════════════════════════════════════

static void test_parse_basic() {
    ASSERT_EQ(dec("0").to_string(), std::string("0"));
    ASSERT_EQ(dec("42").to_string(), std::string("42"));
    ASSERT_EQ(dec("-42").to_string(), std::string("-42"));
    ASSERT_EQ(dec("123.45").to_string(), std::string("123.45"));
    ASSERT_EQ(dec("-0.001").to_string(), std::string("-0.001"));
}

static void test_parse_preserves_scale() {
    ASSERT_EQ(dec("1.50").to_string(), std::string("1.50"));
    ASSERT_EQ(dec("0.00").to_string(), std::string("0.00"));
    ASSERT_EQ(dec("100").to_string(), std::string("100"));
}

static void test_parse_leading_and_trailing_forms() {
    ASSERT_EQ(dec(".5").to_string(), std::string("0.5"));
    ASSERT_EQ(dec("5.").to_string(), std::string("5"));
    ASSERT_EQ(dec("+7").to_string(), std::string("7"));
    ASSERT_EQ(dec("007").to_string(), std::string("7"));
    ASSERT_EQ(dec("-0").to_string(), std::string("0")); // negative zero normalises
}

static void test_parse_exponent() {
    ASSERT_EQ(dec("1e3").to_string(), std::string("1000"));
    ASSERT_EQ(dec("1.5e2").to_string(), std::string("150"));
    ASSERT_EQ(dec("1.5e-2").to_string(), std::string("0.015"));
    ASSERT_EQ(dec("2500e-2").to_string(), std::string("25.00"));
    ASSERT_EQ(dec("-3.14E1").to_string(), std::string("-31.4"));
}

static void test_parse_rejects_invalid() {
    ASSERT_FALSE(Decimal::parse("").has_value());
    ASSERT_FALSE(Decimal::parse(".").has_value());
    ASSERT_FALSE(Decimal::parse("+").has_value());
    ASSERT_FALSE(Decimal::parse("abc").has_value());
    ASSERT_FALSE(Decimal::parse("1.2.3").has_value());
    ASSERT_FALSE(Decimal::parse("1e").has_value());
    ASSERT_FALSE(Decimal::parse("1 000").has_value());
    ASSERT_FALSE(Decimal::parse(" 1").has_value());
    ASSERT_FALSE(Decimal::parse("1,5").has_value());
    ASSERT_FALSE(Decimal::parse("1e999999999999").has_value());
}

// ═══════════════════════════════════════════════════════════
// The headline promise: exact base-10 addition
// ═══════════════════════════════════════════════════════════

static void test_exact_addition() {
    // The single most common floating-point surprise, now exact.
    ASSERT_EQ(dec("0.1").add(dec("0.2")).to_string(), std::string("0.3"));
    ASSERT_EQ(dec("0.1").add(dec("0.2")).compare(dec("0.3")), 0);
}

static void test_addition_scales() {
    ASSERT_EQ(dec("1.5").add(dec("2.25")).to_string(), std::string("3.75"));
    ASSERT_EQ(dec("100").add(dec("0.001")).to_string(), std::string("100.001"));
    ASSERT_EQ(dec("-5").add(dec("3")).to_string(), std::string("-2"));
    ASSERT_EQ(dec("5").add(dec("-5")).to_string(), std::string("0")); // magnitudes cancel
}

static void test_subtraction() {
    ASSERT_EQ(dec("0.3").subtract(dec("0.1")).to_string(), std::string("0.2"));
    ASSERT_EQ(dec("1").subtract(dec("0.9")).to_string(), std::string("0.1"));
    ASSERT_EQ(dec("5").subtract(dec("8")).to_string(), std::string("-3"));
    ASSERT_EQ(dec("-5").subtract(dec("-8")).to_string(), std::string("3"));
}

static void test_multiplication() {
    ASSERT_EQ(mul(dec("0.1"), dec("0.1")).to_string(), std::string("0.01"));
    ASSERT_EQ(mul(dec("1.5"), dec("1.5")).to_string(), std::string("2.25"));
    ASSERT_EQ(mul(dec("-2"), dec("3")).to_string(), std::string("-6"));
    ASSERT_EQ(mul(dec("-2"), dec("-3")).to_string(), std::string("6"));
    ASSERT_EQ(mul(dec("12345678901234567890"), dec("10")).to_string(),
              std::string("123456789012345678900"));
}

static void test_multiply_by_zero() {
    ASSERT_EQ(mul(dec("999.99"), dec("0")).to_string(), std::string("0.00"));
    ASSERT_TRUE(mul(dec("999.99"), dec("0")).is_zero());
}

static void test_multiply_overflow_guarded() {
    // 1e-1000000 parses to coefficient {1} at scale 1'000'000 (exactly the cap).
    // Squaring it would double the scale to 2'000'000 — historically this
    // overflowed the signed `int` scale field to a negative value (undefined
    // behaviour) and corrupted every downstream operation. multiply must reject
    // it (std::nullopt) rather than produce a broken Decimal.
    const Decimal tiny = dec("1e-1000000");
    ASSERT_FALSE(tiny.multiply(tiny).has_value());

    // A product whose coefficient would exceed the digit cap is also rejected,
    // before the O(n*m) schoolbook multiply runs.
    const Decimal big = dec(std::string(600'000, '9'));
    ASSERT_FALSE(big.multiply(big).has_value());

    // A product that stays within the cap still succeeds.
    ASSERT_TRUE(dec("123456789").multiply(dec("987654321")).has_value());
}

static void test_parse_rejects_oversized_coefficient() {
    // The plain-mantissa path must enforce the digit cap too, not only the
    // exponent/scale path: a coefficient longer than k_max_digits is rejected,
    // while a coefficient exactly at the cap is accepted.
    const std::string too_long(Decimal::k_max_digits + 1, '9');
    ASSERT_FALSE(Decimal::parse(too_long).has_value());

    const std::string at_cap(Decimal::k_max_digits, '9');
    ASSERT_TRUE(Decimal::parse(at_cap).has_value());
}

// ═══════════════════════════════════════════════════════════
// Division with target scale + rounding
// ═══════════════════════════════════════════════════════════

static void test_divide_exact() {
    ASSERT_EQ(dec("10").divide(dec("4"), 2, RoundingMode::HalfUp)->to_string(),
              std::string("2.50"));
    ASSERT_EQ(dec("1").divide(dec("8"), 3, RoundingMode::HalfUp)->to_string(),
              std::string("0.125"));
}

static void test_divide_rounding() {
    // 1/3 to 4 places = 0.3333
    ASSERT_EQ(dec("1").divide(dec("3"), 4, RoundingMode::HalfUp)->to_string(),
              std::string("0.3333"));
    // 2/3 to 4 places = 0.6667 (rounded up)
    ASSERT_EQ(dec("2").divide(dec("3"), 4, RoundingMode::HalfUp)->to_string(),
              std::string("0.6667"));
    // 10/3 to 0 places = 3
    ASSERT_EQ(dec("10").divide(dec("3"), 0, RoundingMode::HalfUp)->to_string(), std::string("3"));
}

static void test_divide_by_zero_fails() {
    ASSERT_FALSE(dec("1").divide(dec("0"), 2, RoundingMode::HalfUp).has_value());
    ASSERT_FALSE(dec("0").divide(dec("0"), 2, RoundingMode::HalfUp).has_value());
}

static void test_divide_negative_scale_fails() {
    ASSERT_FALSE(dec("1").divide(dec("2"), -1, RoundingMode::HalfUp).has_value());
}

// ═══════════════════════════════════════════════════════════
// Rounding modes — the "medium risk" area, tested carefully
// ═══════════════════════════════════════════════════════════

static void test_round_half_up() {
    ASSERT_EQ(dec("2.5").round(0, RoundingMode::HalfUp).to_string(), std::string("3"));
    ASSERT_EQ(dec("3.5").round(0, RoundingMode::HalfUp).to_string(), std::string("4"));
    ASSERT_EQ(dec("-2.5").round(0, RoundingMode::HalfUp).to_string(), std::string("-3"));
    ASSERT_EQ(dec("2.4").round(0, RoundingMode::HalfUp).to_string(), std::string("2"));
    ASSERT_EQ(dec("1.005").round(2, RoundingMode::HalfUp).to_string(), std::string("1.01"));
}

static void test_round_half_even() {
    ASSERT_EQ(dec("2.5").round(0, RoundingMode::HalfEven).to_string(), std::string("2"));
    ASSERT_EQ(dec("3.5").round(0, RoundingMode::HalfEven).to_string(), std::string("4"));
    ASSERT_EQ(dec("2.55").round(1, RoundingMode::HalfEven).to_string(), std::string("2.6"));
    ASSERT_EQ(dec("2.65").round(1, RoundingMode::HalfEven).to_string(), std::string("2.6"));
    // Not exactly half: rounds to nearest regardless of parity.
    ASSERT_EQ(dec("2.6501").round(1, RoundingMode::HalfEven).to_string(), std::string("2.7"));
}

static void test_round_half_down() {
    ASSERT_EQ(dec("2.5").round(0, RoundingMode::HalfDown).to_string(), std::string("2"));
    ASSERT_EQ(dec("2.51").round(0, RoundingMode::HalfDown).to_string(), std::string("3"));
    ASSERT_EQ(dec("-2.5").round(0, RoundingMode::HalfDown).to_string(), std::string("-2"));
}

static void test_round_up_down() {
    ASSERT_EQ(dec("2.01").round(0, RoundingMode::Up).to_string(), std::string("3"));
    ASSERT_EQ(dec("-2.01").round(0, RoundingMode::Up).to_string(), std::string("-3"));
    ASSERT_EQ(dec("2.99").round(0, RoundingMode::Down).to_string(), std::string("2"));
    ASSERT_EQ(dec("-2.99").round(0, RoundingMode::Down).to_string(), std::string("-2"));
    ASSERT_EQ(dec("2.00").round(0, RoundingMode::Up).to_string(),
              std::string("2")); // exact: no change
}

static void test_round_ceiling_floor() {
    ASSERT_EQ(dec("2.01").round(0, RoundingMode::Ceiling).to_string(), std::string("3"));
    ASSERT_EQ(dec("-2.99").round(0, RoundingMode::Ceiling).to_string(), std::string("-2"));
    ASSERT_EQ(dec("2.99").round(0, RoundingMode::Floor).to_string(), std::string("2"));
    ASSERT_EQ(dec("-2.01").round(0, RoundingMode::Floor).to_string(), std::string("-3"));
}

static void test_round_carry_propagation() {
    ASSERT_EQ(dec("9.99").round(1, RoundingMode::HalfUp).to_string(), std::string("10.0"));
    ASSERT_EQ(dec("99.999").round(2, RoundingMode::Up).to_string(), std::string("100.00"));
}

static void test_round_pads_when_increasing_scale() {
    ASSERT_EQ(dec("1.5").round(3, RoundingMode::HalfUp).to_string(), std::string("1.500"));
    ASSERT_EQ(dec("2").round(2, RoundingMode::HalfUp).to_string(), std::string("2.00"));
}

// Regression: values whose coefficient has fewer digits than their scale (e.g.
// 0.01 stores one digit at scale 2). Dropping more digits than are stored must
// treat the high-order positions as implicit zeros, not read out of bounds.
static void test_round_small_magnitude() {
    ASSERT_EQ(dec("0.01").round(0, RoundingMode::HalfUp).to_string(), std::string("0"));
    ASSERT_EQ(dec("0.06").round(0, RoundingMode::HalfUp).to_string(), std::string("0"));
    ASSERT_EQ(dec("0.6").round(0, RoundingMode::HalfUp).to_string(), std::string("1"));
    ASSERT_EQ(dec("0.005").round(1, RoundingMode::HalfUp).to_string(), std::string("0.0"));
    ASSERT_EQ(dec("0.05").round(1, RoundingMode::HalfUp).to_string(), std::string("0.1"));
    ASSERT_EQ(dec("0.005").round(2, RoundingMode::HalfUp).to_string(), std::string("0.01"));
    ASSERT_EQ(dec("0.001").round(0, RoundingMode::Up).to_string(), std::string("1"));
    // Sign-aware directed rounding on sub-one magnitudes.
    ASSERT_EQ(dec("-0.01").round(0, RoundingMode::Floor).to_string(), std::string("-1"));
    ASSERT_EQ(dec("-0.01").round(0, RoundingMode::Ceiling).to_string(), std::string("0"));
}

// ═══════════════════════════════════════════════════════════
// Comparison (scale-insensitive) and sign helpers
// ═══════════════════════════════════════════════════════════

static void test_compare_scale_insensitive() {
    ASSERT_EQ(dec("1.5").compare(dec("1.50")), 0);
    ASSERT_EQ(dec("1.50").compare(dec("1.5")), 0);
    ASSERT_TRUE(dec("1.5").equals(dec("1.500")));
}

static void test_compare_ordering() {
    ASSERT_EQ(dec("1").compare(dec("2")), -1);
    ASSERT_EQ(dec("2").compare(dec("1")), 1);
    ASSERT_EQ(dec("-1").compare(dec("1")), -1);
    ASSERT_EQ(dec("-2").compare(dec("-1")), -1);
    ASSERT_EQ(dec("0").compare(dec("0.0")), 0);
    ASSERT_EQ(dec("0.09").compare(dec("0.1")), -1);
}

static void test_sign_and_zero() {
    ASSERT_EQ(dec("0").sign(), 0);
    ASSERT_EQ(dec("5").sign(), 1);
    ASSERT_EQ(dec("-5").sign(), -1);
    ASSERT_TRUE(dec("0.00").is_zero());
    ASSERT_FALSE(dec("0.01").is_zero());
    ASSERT_TRUE(dec("-3").is_negative());
    ASSERT_FALSE(dec("0").is_negative());
}

static void test_negate_and_absolute() {
    ASSERT_EQ(dec("5").negate().to_string(), std::string("-5"));
    ASSERT_EQ(dec("-5").negate().to_string(), std::string("5"));
    ASSERT_EQ(dec("0").negate().to_string(), std::string("0")); // never -0
    ASSERT_EQ(dec("-7.5").absolute().to_string(), std::string("7.5"));
    ASSERT_EQ(dec("7.5").absolute().to_string(), std::string("7.5"));
}

// ═══════════════════════════════════════════════════════════
// Conversions
// ═══════════════════════════════════════════════════════════

static void test_from_int() {
    ASSERT_EQ(Decimal(0).to_string(), std::string("0"));
    ASSERT_EQ(Decimal(42).to_string(), std::string("42"));
    ASSERT_EQ(Decimal(-42).to_string(), std::string("-42"));
    // INT64_MIN must not overflow when negated.
    ASSERT_EQ(Decimal(-9223372036854775807LL - 1).to_string(), std::string("-9223372036854775808"));
}

static void test_from_double_shortest() {
    // The shortest round-trip form avoids the raw binary expansion of 0.1.
    ASSERT_EQ(Decimal::from_double(0.1)->to_string(), std::string("0.1"));
    ASSERT_EQ(Decimal::from_double(0.3)->to_string(), std::string("0.3"));
    ASSERT_EQ(Decimal::from_double(1.5)->to_string(), std::string("1.5"));
    ASSERT_EQ(Decimal::from_double(-2.25)->to_string(), std::string("-2.25"));
    ASSERT_EQ(Decimal::from_double(0.0)->to_string(), std::string("0"));
}

static void test_from_double_rejects_non_finite() {
    ASSERT_FALSE(Decimal::from_double(std::nan("")).has_value());
    ASSERT_FALSE(Decimal::from_double(std::numeric_limits<double>::infinity()).has_value());
    ASSERT_FALSE(Decimal::from_double(-std::numeric_limits<double>::infinity()).has_value());
}

static void test_to_double() {
    ASSERT_NEAR(dec("0.5").to_double(), 0.5, 1e-12);
    ASSERT_NEAR(dec("123.456").to_double(), 123.456, 1e-9);
    ASSERT_NEAR(dec("-2").to_double(), -2.0, 1e-12);
}

// ═══════════════════════════════════════════════════════════
// Hash / equality invariant
// ═══════════════════════════════════════════════════════════

static void test_hash_matches_equality() {
    // Equal values with different scales must hash identically.
    ASSERT_EQ(dec("1.5").hash(), dec("1.50").hash());
    ASSERT_EQ(dec("0").hash(), dec("0.00").hash());
    ASSERT_EQ(dec("100").hash(), dec("1e2").hash());
    ASSERT_EQ(dec("-0").hash(), dec("0").hash());
}

static void test_rounding_mode_names() {
    ASSERT_TRUE(parse_rounding_mode("half_up").has_value());
    ASSERT_TRUE(parse_rounding_mode("half_even").has_value());
    ASSERT_TRUE(parse_rounding_mode("half_down").has_value());
    ASSERT_TRUE(parse_rounding_mode("up").has_value());
    ASSERT_TRUE(parse_rounding_mode("down").has_value());
    ASSERT_TRUE(parse_rounding_mode("ceiling").has_value());
    ASSERT_TRUE(parse_rounding_mode("floor").has_value());
    ASSERT_FALSE(parse_rounding_mode("nearest").has_value());
    ASSERT_FALSE(parse_rounding_mode("HALF_UP").has_value());
}

static void test_rounding_mode_from_variant() {
    // Every PascalCase Decimal.RoundingMode variant maps to its enum value.
    ASSERT_TRUE(rounding_mode_from_variant("HalfUp") == RoundingMode::HalfUp);
    ASSERT_TRUE(rounding_mode_from_variant("HalfDown") == RoundingMode::HalfDown);
    ASSERT_TRUE(rounding_mode_from_variant("HalfEven") == RoundingMode::HalfEven);
    ASSERT_TRUE(rounding_mode_from_variant("Up") == RoundingMode::Up);
    ASSERT_TRUE(rounding_mode_from_variant("Down") == RoundingMode::Down);
    ASSERT_TRUE(rounding_mode_from_variant("Ceiling") == RoundingMode::Ceiling);
    ASSERT_TRUE(rounding_mode_from_variant("Floor") == RoundingMode::Floor);

    // The lowercase string names are NOT valid variant names, and vice versa.
    ASSERT_FALSE(rounding_mode_from_variant("half_up").has_value());
    ASSERT_FALSE(rounding_mode_from_variant("halfup").has_value());
    ASSERT_FALSE(rounding_mode_from_variant("HalfUpp").has_value());
    ASSERT_FALSE(rounding_mode_from_variant("").has_value());
}

// ═══════════════════════════════════════════════════════════
// A realistic money scenario
// ═══════════════════════════════════════════════════════════

static void test_money_arithmetic() {
    // Splitting $10.00 three ways with banker's rounding on each share.
    Decimal total = dec("10.00");
    Decimal share = *total.divide(dec("3"), 2, RoundingMode::HalfEven); // 3.33
    ASSERT_EQ(share.to_string(), std::string("3.33"));
    Decimal remainder = total.subtract(mul(share, dec("3")));
    ASSERT_EQ(remainder.to_string(), std::string("0.01"));

    // Tax: $19.99 * 8.25% rounded to cents.
    Decimal tax = mul(dec("19.99"), dec("0.0825")).round(2, RoundingMode::HalfUp);
    ASSERT_EQ(tax.to_string(), std::string("1.65"));
}

int main() {
    RUN(test_parse_basic);
    RUN(test_parse_preserves_scale);
    RUN(test_parse_leading_and_trailing_forms);
    RUN(test_parse_exponent);
    RUN(test_parse_rejects_invalid);
    RUN(test_parse_rejects_oversized_coefficient);

    RUN(test_exact_addition);
    RUN(test_addition_scales);
    RUN(test_subtraction);
    RUN(test_multiplication);
    RUN(test_multiply_by_zero);
    RUN(test_multiply_overflow_guarded);

    RUN(test_divide_exact);
    RUN(test_divide_rounding);
    RUN(test_divide_by_zero_fails);
    RUN(test_divide_negative_scale_fails);

    RUN(test_round_half_up);
    RUN(test_round_half_even);
    RUN(test_round_half_down);
    RUN(test_round_up_down);
    RUN(test_round_ceiling_floor);
    RUN(test_round_carry_propagation);
    RUN(test_round_pads_when_increasing_scale);
    RUN(test_round_small_magnitude);

    RUN(test_compare_scale_insensitive);
    RUN(test_compare_ordering);
    RUN(test_sign_and_zero);
    RUN(test_negate_and_absolute);

    RUN(test_from_int);
    RUN(test_from_double_shortest);
    RUN(test_from_double_rejects_non_finite);
    RUN(test_to_double);

    RUN(test_hash_matches_equality);
    RUN(test_rounding_mode_names);
    RUN(test_rounding_mode_from_variant);
    RUN(test_money_arithmetic);

    return SUMMARY();
}
