// Unit tests for core/common/format_number.hpp.

#include <cmath>
#include <limits>
#include <string>

#include "common/format_number.hpp"
#include "test_framework.hpp"

using namespace luma;

// ═══════════════════════════════════════════════════════════
// Whole numbers — must retain ".0" suffix
// ═══════════════════════════════════════════════════════════

static void test_format_zero() {
    ASSERT_EQ(format_number(0.0), std::string("0.0"));
}

static void test_format_positive_integer() {
    ASSERT_EQ(format_number(1.0), std::string("1.0"));
    ASSERT_EQ(format_number(42.0), std::string("42.0"));
}

static void test_format_negative_integer() {
    ASSERT_EQ(format_number(-1.0), std::string("-1.0"));
    ASSERT_EQ(format_number(-100.0), std::string("-100.0"));
}

// ═══════════════════════════════════════════════════════════
// Fractional numbers — trailing zeros trimmed
// ═══════════════════════════════════════════════════════════

static void test_format_simple_fraction() {
    ASSERT_EQ(format_number(1.5), std::string("1.5"));
    ASSERT_EQ(format_number(-0.25), std::string("-0.25"));
}

static void test_format_trailing_zeros_trimmed() {
    // 3.10 should become "3.1"
    ASSERT_EQ(format_number(3.1), std::string("3.1"));
}

static void test_format_many_decimal_places() {
    ASSERT_EQ(format_number(0.123456), std::string("0.123456"));
}

// ═══════════════════════════════════════════════════════════
// Special values
// ═══════════════════════════════════════════════════════════

static void test_format_nan() {
    ASSERT_EQ(format_number(std::numeric_limits<double>::quiet_NaN()), std::string("NaN"));
}

static void test_format_positive_infinity() {
    ASSERT_EQ(format_number(std::numeric_limits<double>::infinity()), std::string("Infinity"));
}

static void test_format_negative_infinity() {
    ASSERT_EQ(format_number(-std::numeric_limits<double>::infinity()), std::string("-Infinity"));
}

// ═══════════════════════════════════════════════════════════
// Edge cases
// ═══════════════════════════════════════════════════════════

static void test_format_negative_zero() {
    // -0.0 should format as "-0.0"
    ASSERT_EQ(format_number(-0.0), std::string("-0.0"));
}

static void test_format_large_number() {
    ASSERT_EQ(format_number(1000000.0), std::string("1000000.0"));
}

static void test_format_small_fraction() {
    ASSERT_EQ(format_number(0.5), std::string("0.5"));
}

// ═══════════════════════════════════════════════════════════
// Extreme magnitudes — must stay on the locale-independent,
// trailing-zero-trimming path (regression: values whose fixed
// notation exceeds a small stack buffer previously fell back to
// the locale-dependent, untrimmed std::to_string).
// ═══════════════════════════════════════════════════════════

static void test_format_very_large_magnitude() {
    // 1e300 needs ~300 integer digits in fixed notation — far beyond any
    // small buffer. It is an integer-valued double, so it must trim to ".0",
    // never the six-zero "%f" tail ".000000" of the old std::to_string fallback.
    const std::string s = format_number(1e300);
    ASSERT_GT(s.size(), static_cast<std::size_t>(64)); // exceeds the old 64-byte buffer
    ASSERT_EQ(s.front(), '1');
    ASSERT_EQ(s.substr(s.size() - 2), std::string(".0"));
    ASSERT_EQ(s.find(','), std::string::npos); // locale-independent separator
    ASSERT_EQ(s.find('e'), std::string::npos); // fixed notation, not scientific
}

static void test_format_dbl_max() {
    const std::string s = format_number(std::numeric_limits<double>::max());
    ASSERT_GT(s.size(), static_cast<std::size_t>(64));
    ASSERT_EQ(s.substr(s.size() - 2), std::string(".0"));
    ASSERT_EQ(s.find(','), std::string::npos);
    ASSERT_EQ(s.find('e'), std::string::npos);
}

static void test_format_smallest_subnormal() {
    // The smallest positive subnormal must render as its true tiny value, not
    // collapse to "0.000000" (the old %f fallback) or "0.0".
    const std::string s = format_number(std::numeric_limits<double>::denorm_min());
    ASSERT_NE(s, std::string("0.000000"));
    ASSERT_NE(s, std::string("0.0"));
    ASSERT_EQ(s.rfind("0.0", 0), static_cast<std::size_t>(0));  // starts with "0.0"
    ASSERT_NE(s.find_first_of("123456789"), std::string::npos); // has a significant digit
    ASSERT_EQ(s.find(','), std::string::npos);
    ASSERT_EQ(s.find('e'), std::string::npos);
}

// ─── main ───

int main() {
    // Whole numbers.
    RUN(test_format_zero);
    RUN(test_format_positive_integer);
    RUN(test_format_negative_integer);

    // Fractional numbers.
    RUN(test_format_simple_fraction);
    RUN(test_format_trailing_zeros_trimmed);
    RUN(test_format_many_decimal_places);

    // Special values.
    RUN(test_format_nan);
    RUN(test_format_positive_infinity);
    RUN(test_format_negative_infinity);

    // Edge cases.
    RUN(test_format_negative_zero);
    RUN(test_format_large_number);
    RUN(test_format_small_fraction);

    // Extreme magnitudes.
    RUN(test_format_very_large_magnitude);
    RUN(test_format_dbl_max);
    RUN(test_format_smallest_subnormal);

    return SUMMARY();
}
