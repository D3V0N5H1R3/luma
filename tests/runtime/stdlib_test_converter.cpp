// Standard library tests: Converter.

#include "stdlib_test_helpers.hpp"

// ─────────────────────────────────────────────────────────────────
// to_boolean
// ─────────────────────────────────────────────────────────────────

static void test_converter_to_boolean() {
    ASSERT_EVAL_BOOL(R"(Converter.to_boolean("true"))", true);
    ASSERT_EVAL_BOOL(R"(Converter.to_boolean("false"))", false);
}

static void test_converter_to_boolean_invalid_fails() {
    ASSERT_EVAL_FAILURE(R"(Converter.to_boolean("maybe"))");
    ASSERT_EVAL_FAILURE(R"(Converter.to_boolean("True"))");
    ASSERT_EVAL_FAILURE(R"(Converter.to_boolean(""))");
}

static void test_converter_to_boolean_wrong_type_throws() {
    ASSERT_THROWS(eval("Converter.to_boolean(42)"));
    ASSERT_THROWS(eval("Converter.to_boolean(true)"));
}

// ─────────────────────────────────────────────────────────────────
// to_string
// ─────────────────────────────────────────────────────────────────

static void test_converter_to_string() {
    ASSERT_EQ(eval("Converter.to_string(42)").as_string(), "42");
    ASSERT_EQ(eval("Converter.to_string(-7)").as_string(), "-7");
    ASSERT_EQ(eval("Converter.to_string(true)").as_string(), "true");
    ASSERT_EQ(eval("Converter.to_string(3.14)").as_string(), "3.14");
    ASSERT_EQ(eval(R"(Converter.to_string("hi"))").as_string(), "hi");
    ASSERT_EQ(eval("Converter.to_string([1, 2, 3])").as_string(), "[1, 2, 3]");
    ASSERT_EQ(eval("Converter.to_string(none)").as_string(), "none");
}

// ─────────────────────────────────────────────────────────────────
// to_integer
// ─────────────────────────────────────────────────────────────────

static void test_converter_to_integer_from_number() {
    ASSERT_EVAL_INT("Converter.to_integer(3.9)", 3);
    ASSERT_EVAL_INT("Converter.to_integer(-3.9)", -3);
    ASSERT_EVAL_INT("Converter.to_integer(3.0)", 3);
}

static void test_converter_to_integer_from_integer() {
    ASSERT_EVAL_INT("Converter.to_integer(42)", 42);
    ASSERT_EVAL_INT("Converter.to_integer(-42)", -42);
}

static void test_converter_to_integer_from_string() {
    ASSERT_EVAL_INT(R"(Converter.to_integer("42"))", 42);
    ASSERT_EVAL_INT(R"(Converter.to_integer("-42"))", -42);
}

static void test_converter_to_integer_invalid_string_fails() {
    ASSERT_EVAL_FAILURE(R"(Converter.to_integer("hello"))");
    // Partial parses must be rejected: stoll stops at '.', leaving trailing input.
    ASSERT_EVAL_FAILURE(R"(Converter.to_integer("3.14"))");
    ASSERT_EVAL_FAILURE(R"(Converter.to_integer("12abc"))");
    ASSERT_EVAL_FAILURE(R"(Converter.to_integer(""))");
    // Out-of-range string must fail rather than overflow.
    ASSERT_EVAL_FAILURE(R"(Converter.to_integer("99999999999999999999999"))");
}

static void test_converter_to_integer_overflow() {
    // Values beyond int64 range return failure rather than invoking UB.
    ASSERT_EVAL_FAILURE("Converter.to_integer(1.0e19)");
    ASSERT_EVAL_FAILURE("Converter.to_integer(-1.0e19)");
    ASSERT_EVAL_INT("Converter.to_integer(1.5)", 1);
}

static void test_converter_to_integer_wrong_type_throws() {
    ASSERT_THROWS(eval("Converter.to_integer(true)"));
    ASSERT_THROWS(eval("Converter.to_integer([1, 2])"));
}

// ─────────────────────────────────────────────────────────────────
// to_number
// ─────────────────────────────────────────────────────────────────

static void test_converter_to_number_from_numeric() {
    ASSERT_EVAL_NUM("Converter.to_number(42)", 42.0);
    ASSERT_EVAL_NUM("Converter.to_number(-7)", -7.0);
    ASSERT_EVAL_NUM("Converter.to_number(2.5)", 2.5);
}

static void test_converter_to_number_from_string() {
    const auto ok = eval(R"(Converter.to_number("3.14"))");

    ASSERT_RESULT_SUCCESS(ok);
    ASSERT_TRUE(ok.as_result()->owned_inner->as_number() > 3.13 &&
                ok.as_result()->owned_inner->as_number() < 3.15);

    ASSERT_EVAL_NUM(R"(Converter.to_number("-2.5"))", -2.5);
    ASSERT_EVAL_NUM(R"(Converter.to_number("1e3"))", 1000.0);
}

static void test_converter_to_number_invalid_string_fails() {
    ASSERT_EVAL_FAILURE(R"(Converter.to_number("hello"))");
    ASSERT_EVAL_FAILURE(R"(Converter.to_number("3.1.4"))");
    ASSERT_EVAL_FAILURE(R"(Converter.to_number(""))");
}

// Regression: non-finite parses (NaN / ±Inf) must be rejected, not silently
// accepted.  std::stod parses "inf"/"nan" successfully, so the is_valid_numeric
// guard is what makes these fail — matching the stdlib's own numeric-validity
// contract and the overflow path ("1e999" also fails), removing the prior
// self-inconsistency.
static void test_converter_to_number_rejects_non_finite() {
    ASSERT_EVAL_FAILURE(R"(Converter.to_number("inf"))");
    ASSERT_EVAL_FAILURE(R"(Converter.to_number("-inf"))");
    ASSERT_EVAL_FAILURE(R"(Converter.to_number("infinity"))");
    ASSERT_EVAL_FAILURE(R"(Converter.to_number("nan"))");
    ASSERT_EVAL_FAILURE(R"(Converter.to_number("NaN"))");

    // A finite value still parses — the guard must not reject valid numbers.
    ASSERT_EVAL_NUM(R"(Converter.to_number("1.5"))", 1.5);
}

static void test_converter_to_number_wrong_type_throws() {
    ASSERT_THROWS(eval("Converter.to_number(true)"));
    ASSERT_THROWS(eval("Converter.to_number([1])"));
}

// ─────────────────────────────────────────────────────────────────
// to_hexadecimal / from_hexadecimal
// ─────────────────────────────────────────────────────────────────

static void test_converter_to_hexadecimal() {
    ASSERT_EQ(eval("Converter.to_hexadecimal(0)").as_string(), "0");
    ASSERT_EQ(eval("Converter.to_hexadecimal(255)").as_string(), "ff");
    ASSERT_EQ(eval("Converter.to_hexadecimal(4294967295)").as_string(), "ffffffff");
}

static void test_converter_to_hexadecimal_wrong_type_throws() {
    ASSERT_THROWS(eval(R"(Converter.to_hexadecimal("ff"))"));
}

static void test_converter_from_hexadecimal() {
    ASSERT_EVAL_INT(R"(Converter.from_hexadecimal("ff"))", 255);
    ASSERT_EVAL_INT(R"(Converter.from_hexadecimal("FF"))", 255);
    ASSERT_EVAL_INT(R"(Converter.from_hexadecimal("0x1f"))", 31);
}

static void test_converter_from_hexadecimal_roundtrip() {
    ASSERT_EVAL_INT(R"(Converter.from_hexadecimal(Converter.to_hexadecimal(48879)))", 48879);
}

static void test_converter_from_hexadecimal_invalid_fails() {
    ASSERT_EVAL_FAILURE(R"(Converter.from_hexadecimal("xyz"))");
    ASSERT_EVAL_FAILURE(R"(Converter.from_hexadecimal(""))");
    ASSERT_EVAL_FAILURE(R"(Converter.from_hexadecimal("ffg"))");
}

static void test_converter_from_hexadecimal_wrong_type_throws() {
    ASSERT_THROWS(eval("Converter.from_hexadecimal(255)"));
}

// ─────────────────────────────────────────────────────────────────
// to_binary / from_binary
// ─────────────────────────────────────────────────────────────────

static void test_converter_to_binary() {
    ASSERT_EQ(eval("Converter.to_binary(0)").as_string(), "0");
    ASSERT_EQ(eval("Converter.to_binary(10)").as_string(), "1010");
    ASSERT_EQ(eval("Converter.to_binary(255)").as_string(), "11111111");
}

static void test_converter_to_binary_negative() {
    ASSERT_EQ(eval("Converter.to_binary(-10)").as_string(), "-1010");
}

static void test_converter_to_binary_wrong_type_throws() {
    ASSERT_THROWS(eval(R"(Converter.to_binary("1010"))"));
}

static void test_converter_from_binary() {
    ASSERT_EVAL_INT(R"(Converter.from_binary("1010"))", 10);
    ASSERT_EVAL_INT(R"(Converter.from_binary("0"))", 0);
    ASSERT_EVAL_INT(R"(Converter.from_binary("11111111"))", 255);
}

static void test_converter_from_binary_roundtrip() {
    ASSERT_EVAL_INT(R"(Converter.from_binary(Converter.to_binary(48879)))", 48879);
}

static void test_converter_from_binary_invalid_fails() {
    ASSERT_EVAL_FAILURE(R"(Converter.from_binary("222"))");
    // stoll(base 2) does not accept a "0b" prefix.
    ASSERT_EVAL_FAILURE(R"(Converter.from_binary("0b101"))");
    ASSERT_EVAL_FAILURE(R"(Converter.from_binary("210"))");
    ASSERT_EVAL_FAILURE(R"(Converter.from_binary(""))");
}

static void test_converter_from_binary_wrong_type_throws() {
    ASSERT_THROWS(eval("Converter.from_binary(1010)"));
}

// ─────────────────────────────────────────────────────────────────
// ordinal
// ─────────────────────────────────────────────────────────────────

static void test_converter_ordinal_suffixes() {
    ASSERT_EQ(eval("Converter.ordinal(1)").as_string(), "1st");
    ASSERT_EQ(eval("Converter.ordinal(2)").as_string(), "2nd");
    ASSERT_EQ(eval("Converter.ordinal(3)").as_string(), "3rd");
    ASSERT_EQ(eval("Converter.ordinal(4)").as_string(), "4th");
    ASSERT_EQ(eval("Converter.ordinal(21)").as_string(), "21st");
    ASSERT_EQ(eval("Converter.ordinal(22)").as_string(), "22nd");
    ASSERT_EQ(eval("Converter.ordinal(23)").as_string(), "23rd");
    ASSERT_EQ(eval("Converter.ordinal(101)").as_string(), "101st");
}

static void test_converter_ordinal_teens() {
    // 11..13 are always "th" regardless of the final digit.
    ASSERT_EQ(eval("Converter.ordinal(11)").as_string(), "11th");
    ASSERT_EQ(eval("Converter.ordinal(12)").as_string(), "12th");
    ASSERT_EQ(eval("Converter.ordinal(13)").as_string(), "13th");
    ASSERT_EQ(eval("Converter.ordinal(111)").as_string(), "111th");
    ASSERT_EQ(eval("Converter.ordinal(112)").as_string(), "112th");
    ASSERT_EQ(eval("Converter.ordinal(113)").as_string(), "113th");
}

static void test_converter_ordinal_zero_and_negative() {
    ASSERT_EQ(eval("Converter.ordinal(0)").as_string(), "0th");
    ASSERT_EQ(eval("Converter.ordinal(-1)").as_string(), "-1st");
}

static void test_converter_ordinal_wrong_type_throws() {
    ASSERT_THROWS(eval(R"(Converter.ordinal("3"))"));
}

// ─────────────────────────────────────────────────────────────────
// to_roman / from_roman
// ─────────────────────────────────────────────────────────────────

static void test_converter_to_roman() {
    ASSERT_EVAL_STR("Converter.to_roman(1)", "I");
    ASSERT_EVAL_STR("Converter.to_roman(4)", "IV");
    ASSERT_EVAL_STR("Converter.to_roman(14)", "XIV");
    ASSERT_EVAL_STR("Converter.to_roman(2024)", "MMXXIV");
    ASSERT_EVAL_STR("Converter.to_roman(3999)", "MMMCMXCIX");
}

static void test_converter_to_roman_out_of_range_fails() {
    ASSERT_EVAL_FAILURE("Converter.to_roman(0)");
    ASSERT_EVAL_FAILURE("Converter.to_roman(-5)");
    ASSERT_EVAL_FAILURE("Converter.to_roman(4000)");
}

static void test_converter_to_roman_wrong_type_throws() {
    ASSERT_THROWS(eval(R"(Converter.to_roman("X"))"));
}

static void test_converter_from_roman() {
    ASSERT_EVAL_INT(R"(Converter.from_roman("XIV"))", 14);
    ASSERT_EVAL_INT(R"(Converter.from_roman("MCMXCIV"))", 1994);
    ASSERT_EVAL_INT(R"(Converter.from_roman("MMMCMXCIX"))", 3999);
    // Lowercase input is accepted (normalised before validation).
    ASSERT_EVAL_INT(R"(Converter.from_roman("iv"))", 4);
}

static void test_converter_from_roman_roundtrip() {
    ASSERT_EVAL_INT(R"(Converter.from_roman(Result.unwrap(Converter.to_roman(2727))))", 2727);
}

static void test_converter_from_roman_malformed_fails() {
    ASSERT_EVAL_FAILURE(R"(Converter.from_roman(""))");
    // Non-canonical numerals are rejected by the round-trip validation.
    ASSERT_EVAL_FAILURE(R"(Converter.from_roman("IIII"))");
    ASSERT_EVAL_FAILURE(R"(Converter.from_roman("VV"))");
    ASSERT_EVAL_FAILURE(R"(Converter.from_roman("ABC"))");
    ASSERT_EVAL_FAILURE(R"(Converter.from_roman("XIIX"))");
}

static void test_converter_from_roman_wrong_type_throws() {
    ASSERT_THROWS(eval("Converter.from_roman(14)"));
}

// ─────────────────────────────────────────────────────────────────
// number_to_words
// ─────────────────────────────────────────────────────────────────

static void test_converter_number_to_words() {
    ASSERT_EQ(eval("Converter.number_to_words(0)").as_string(), "zero");
    ASSERT_EQ(eval("Converter.number_to_words(42)").as_string(), "forty two");
    ASSERT_EQ(eval("Converter.number_to_words(100)").as_string(), "one hundred");
    ASSERT_EQ(eval("Converter.number_to_words(1234)").as_string(),
              "one thousand two hundred thirty four");
    ASSERT_EQ(eval("Converter.number_to_words(1000000)").as_string(), "one million");
    ASSERT_EQ(eval("Converter.number_to_words(1000000000)").as_string(), "one billion");
}

static void test_converter_number_to_words_negative() {
    ASSERT_EQ(eval("Converter.number_to_words(-5)").as_string(), "negative five");
    ASSERT_EQ(eval("Converter.number_to_words(-19)").as_string(), "negative nineteen");
}

static void test_converter_number_to_words_wrong_type_throws() {
    ASSERT_THROWS(eval(R"(Converter.number_to_words("5"))"));
}

// ─────────────────────────────────────────────────────────────────
// character_to_codepoint / codepoint_to_character
// ─────────────────────────────────────────────────────────────────

static void test_converter_character_to_codepoint_ascii() {
    ASSERT_EVAL_INT(R"(Converter.character_to_codepoint("A"))", 65);
    ASSERT_EVAL_INT(R"(Converter.character_to_codepoint("a"))", 97);
    ASSERT_EVAL_INT(R"(Converter.character_to_codepoint("0"))", 48);
}

static void test_converter_character_to_codepoint_empty_fails() {
    ASSERT_EVAL_FAILURE(R"(Converter.character_to_codepoint(""))");
}

static void test_converter_character_to_codepoint_wrong_type_throws() {
    ASSERT_THROWS(eval("Converter.character_to_codepoint(65)"));
}

static void test_converter_codepoint_to_character_ascii() {
    ASSERT_EVAL_STR("Converter.codepoint_to_character(65)", "A");
    ASSERT_EVAL_STR("Converter.codepoint_to_character(97)", "a");
}

static void test_converter_codepoint_to_character_out_of_range_fails() {
    ASSERT_EVAL_FAILURE("Converter.codepoint_to_character(-1)");
    // 0x110000 is one past the highest valid Unicode codepoint.
    ASSERT_EVAL_FAILURE("Converter.codepoint_to_character(1114112)");
}

static void test_converter_codepoint_to_character_surrogate_fails() {
    // Surrogate halves (U+D800..U+DFFF) are not valid Unicode scalar values and
    // cannot be encoded as UTF-8, so they must fail rather than emit invalid
    // bytes.
    ASSERT_EVAL_FAILURE("Converter.codepoint_to_character(55296)"); // 0xD800 (high surrogate)
    ASSERT_EVAL_FAILURE("Converter.codepoint_to_character(56320)"); // 0xDC00 (low surrogate)
    ASSERT_EVAL_FAILURE("Converter.codepoint_to_character(57343)"); // 0xDFFF (last surrogate)

    // The first codepoint past the surrogate range still encodes and round-trips.
    ASSERT_EVAL_INT(
        R"(Converter.character_to_codepoint(Result.unwrap(Converter.codepoint_to_character(57344))))",
        57344); // 0xE000
}

static void test_converter_codepoint_to_character_wrong_type_throws() {
    ASSERT_THROWS(eval(R"(Converter.codepoint_to_character("A"))"));
}

static void test_converter_codepoint_roundtrip_multibyte() {
    // Round-trip through both directions so the shared UTF-8 encode and decode
    // helpers agree on 2-, 3-, and 4-byte sequences without embedding raw UTF-8
    // bytes in this source file.
    ASSERT_EVAL_INT(
        R"(Converter.character_to_codepoint(Result.unwrap(Converter.codepoint_to_character(233))))",
        233); // é  (2-byte)
    ASSERT_EVAL_INT(
        R"(Converter.character_to_codepoint(Result.unwrap(Converter.codepoint_to_character(9733))))",
        9733); // ★ (3-byte)
    ASSERT_EVAL_INT(
        R"(Converter.character_to_codepoint(Result.unwrap(Converter.codepoint_to_character(128512))))",
        128512); // 😀 (4-byte)
}

int main() {
    // to_boolean
    RUN(test_converter_to_boolean);
    RUN(test_converter_to_boolean_invalid_fails);
    RUN(test_converter_to_boolean_wrong_type_throws);

    // to_string
    RUN(test_converter_to_string);

    // to_integer
    RUN(test_converter_to_integer_from_number);
    RUN(test_converter_to_integer_from_integer);
    RUN(test_converter_to_integer_from_string);
    RUN(test_converter_to_integer_invalid_string_fails);
    RUN(test_converter_to_integer_overflow);
    RUN(test_converter_to_integer_wrong_type_throws);

    // to_number
    RUN(test_converter_to_number_from_numeric);
    RUN(test_converter_to_number_from_string);
    RUN(test_converter_to_number_invalid_string_fails);
    RUN(test_converter_to_number_rejects_non_finite);
    RUN(test_converter_to_number_wrong_type_throws);

    // to_hexadecimal / from_hexadecimal
    RUN(test_converter_to_hexadecimal);
    RUN(test_converter_to_hexadecimal_wrong_type_throws);
    RUN(test_converter_from_hexadecimal);
    RUN(test_converter_from_hexadecimal_roundtrip);
    RUN(test_converter_from_hexadecimal_invalid_fails);
    RUN(test_converter_from_hexadecimal_wrong_type_throws);

    // to_binary / from_binary
    RUN(test_converter_to_binary);
    RUN(test_converter_to_binary_negative);
    RUN(test_converter_to_binary_wrong_type_throws);
    RUN(test_converter_from_binary);
    RUN(test_converter_from_binary_roundtrip);
    RUN(test_converter_from_binary_invalid_fails);
    RUN(test_converter_from_binary_wrong_type_throws);

    // ordinal
    RUN(test_converter_ordinal_suffixes);
    RUN(test_converter_ordinal_teens);
    RUN(test_converter_ordinal_zero_and_negative);
    RUN(test_converter_ordinal_wrong_type_throws);

    // to_roman / from_roman
    RUN(test_converter_to_roman);
    RUN(test_converter_to_roman_out_of_range_fails);
    RUN(test_converter_to_roman_wrong_type_throws);
    RUN(test_converter_from_roman);
    RUN(test_converter_from_roman_roundtrip);
    RUN(test_converter_from_roman_malformed_fails);
    RUN(test_converter_from_roman_wrong_type_throws);

    // number_to_words
    RUN(test_converter_number_to_words);
    RUN(test_converter_number_to_words_negative);
    RUN(test_converter_number_to_words_wrong_type_throws);

    // character_to_codepoint / codepoint_to_character
    RUN(test_converter_character_to_codepoint_ascii);
    RUN(test_converter_character_to_codepoint_empty_fails);
    RUN(test_converter_character_to_codepoint_wrong_type_throws);
    RUN(test_converter_codepoint_to_character_ascii);
    RUN(test_converter_codepoint_to_character_out_of_range_fails);
    RUN(test_converter_codepoint_to_character_surrogate_fails);
    RUN(test_converter_codepoint_to_character_wrong_type_throws);
    RUN(test_converter_codepoint_roundtrip_multibyte);

    return SUMMARY();
}
