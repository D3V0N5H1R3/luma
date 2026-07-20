#include "runtime/stdlib/types/converter_module.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

#include "analysis/source/source_location.hpp"
#include "common/utf8.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/numeric_helpers.hpp"

namespace luma {

namespace {

// Absolute value of a signed 64-bit integer as an unsigned 64-bit magnitude.
// Correct even for INT64_MIN, whose magnitude (2^63) is not representable as an
// int64_t: the negation is computed with well-defined unsigned arithmetic.
[[nodiscard]] std::uint64_t abs_to_u64(std::int64_t n) noexcept {
    return (n < 0) ? ~static_cast<std::uint64_t>(n) + 1ULL : static_cast<std::uint64_t>(n);
}

constexpr std::array<std::pair<int, const char*>, 13> roman_table{{{1000, "M"},
                                                                   {900, "CM"},
                                                                   {500, "D"},
                                                                   {400, "CD"},
                                                                   {100, "C"},
                                                                   {90, "XC"},
                                                                   {50, "L"},
                                                                   {40, "XL"},
                                                                   {10, "X"},
                                                                   {9, "IX"},
                                                                   {5, "V"},
                                                                   {4, "IV"},
                                                                   {1, "I"}}};

// Map a single Roman numeral character to its integer value.
[[nodiscard]] int roman_char_value(char c) {
    static const std::unordered_map<char, int> lookup = {
        {'I', 1}, {'V', 5}, {'X', 10}, {'L', 50}, {'C', 100}, {'D', 500}, {'M', 1000}};
    const auto upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const auto it = lookup.find(upper);
    return it != lookup.end() ? it->second : 0;
}

[[nodiscard]] std::string int_to_roman(int n) {
    std::string result{};

    for (const auto& [val, sym] : roman_table) {
        while (n >= val) {
            result += sym;

            n -= val;
        }
    }

    return result;
}

// Scale names for number_to_words, indexed so that scale_names[i] corresponds
// to the divisor 10^(3*(i+1)).  E.g. scale_names[0] = "thousand" (10^3).
constexpr std::array<const char*, 6> scale_names{
    {"thousand", "million", "billion", "trillion", "quadrillion", "quintillion"}};

constexpr std::array<const char*, 20> ones_words{
    {"",         "one",     "two",     "three",     "four",     "five",    "six",
     "seven",    "eight",   "nine",    "ten",       "eleven",   "twelve",  "thirteen",
     "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"}};

constexpr std::array<const char*, 10> tens_words{
    {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"}};

// Convert a non-negative integer to English words.
[[nodiscard]] std::string int_to_words(std::int64_t num) {
    if (num < 0) {
        return "negative " + int_to_words(-num);
    }

    if (num < 20) {
        return std::string(ones_words[static_cast<std::size_t>(num)]);
    }

    if (num < 100) {
        const auto r = num % 10;

        return std::string(tens_words[static_cast<std::size_t>(num / 10)]) +
               ((r != 0) ? " " + std::string(ones_words[static_cast<std::size_t>(r)]) : "");
    }

    if (num < 1000) {
        return std::string(ones_words[static_cast<std::size_t>(num / 100)]) + " hundred" +
               (((num % 100) != 0) ? " " + int_to_words(num % 100) : "");
    }

    // Walk the scale table from largest to smallest to find the right grouping.
    for (std::size_t i{scale_names.size()}; i-- > 0;) {
        std::int64_t divisor{1};

        for (std::size_t j{0}; j <= i; ++j) {
            divisor *= 1000;
        }

        if (num >= divisor) {
            return int_to_words(num / divisor) + " " + scale_names[i] +
                   (((num % divisor) != 0) ? " " + int_to_words(num % divisor) : "");
        }
    }

    return {};
}

// Parse `s` as an integer in `base`, requiring the whole string to be consumed.
// Returns success(value), or failure(err_msg) on a parse error or trailing input.
[[nodiscard]] Value parse_in_base(const std::string& s, int base, std::string_view err_msg) {
    try {
        std::size_t pos{0};

        const auto value = std::stoll(s, &pos, base);

        if (pos == s.size()) {
            return make_success_value(Value{static_cast<std::int64_t>(value)});
        }
    } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
        // Conversion failed — fall through to return failure.
    }

    return make_failure_value(std::string{err_msg});
}

} // namespace

void register_converter_ns(const EnvPtr& env) {
    ModuleBuilder{"Converter", env}
        .func("to_boolean", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Converter.to_boolean", loc);

            const auto& s = args[0].as_string();

            if (s == "true") {
                return make_success_value(Value{true});
            }

            if (s == "false") {
                return make_success_value(Value{false});
            }

            return make_failure_value(error_msg("Converter", "to_boolean",
                                                std::format("cannot convert '{}' to boolean", s)));
        })
        .func("to_string", 1)
        .raw_body([](std::span<const Value> args, [[maybe_unused]] SourceLocation loc) -> Value {
            return Value{args[0].to_string()};
        })
        .func("to_integer", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (args[0].is_integer()) {
                return make_success_value(Value{args[0].as_integer()});
            }

            if (args[0].is_number()) {
                // safe_to_int64 rejects NaN, ±Inf, and out-of-range values,
                // avoiding the undefined behaviour of casting such doubles to
                // int64_t.
                if (const auto i = stdlib::safe_to_int64(args[0].as_number())) {
                    return make_success_value(Value{*i});
                }

                return make_failure_value(
                    error_msg("Converter", "to_integer", "value out of integer range"));
            }

            if (args[0].is_string()) {
                const auto& s = args[0].as_string();

                try {
                    std::size_t pos{0};

                    const auto val = std::stoll(s, &pos);

                    if (pos == s.size()) {
                        return make_success_value(Value{static_cast<std::int64_t>(val)});
                    }
                } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
                    // Conversion failed — fall through to return failure.
                }

                return make_failure_value(error_msg(
                    "Converter", "to_integer", std::format("cannot convert '{}' to integer", s)));
            }

            throw RuntimeError{error_msg("Converter", "to_integer",
                                         std::format("expected integer, number, or string, got {}",
                                                     args[0].display_type_name())),
                               loc, "pass a numeric value or a string containing an integer"};
        })
        .func("to_number", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (args[0].is_integer()) {
                return make_success_value(Value{static_cast<double>(args[0].as_integer())});
            }

            if (args[0].is_number()) {
                return make_success_value(Value{args[0].as_number()});
            }

            if (args[0].is_string()) {
                const auto& s = args[0].as_string();

                try {
                    std::size_t pos{0};

                    const auto val = std::stod(s, &pos);

                    if (pos == s.size() && stdlib::is_valid_numeric(val)) {
                        return make_success_value(Value{val});
                    }
                } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
                    // Conversion failed — fall through to return failure.
                }

                return make_failure_value(error_msg(
                    "Converter", "to_number", std::format("cannot convert '{}' to number", s)));
            }

            throw RuntimeError{error_msg("Converter", "to_number",
                                         std::format("expected integer, number, or string, got {}",
                                                     args[0].display_type_name())),
                               loc, "pass a numeric value or a string containing a number"};
        })
        .func("to_hexadecimal", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "Converter.to_hexadecimal", loc);
            return Value{std::format("{:x}", n)};
        })
        .func("from_hexadecimal", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Converter.from_hexadecimal", loc);
            return parse_in_base(args[0].as_string(), 16,
                                 error_msg("Converter", "from_hexadecimal", "invalid hex string"));
        })
        .func("to_binary", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "Converter.to_binary", loc);

            if (n == 0) {
                return Value{std::string{"0"}};
            }

            std::string result{};

            auto abs_n = abs_to_u64(n);

            while (abs_n > 0) {
                result += (abs_n & 1) ? '1' : '0';

                abs_n >>= 1;
            }

            std::ranges::reverse(result);

            if (n < 0) {
                result = "-" + result;
            }

            return Value{std::move(result)};
        })
        .func("from_binary", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Converter.from_binary", loc);
            return parse_in_base(args[0].as_string(), 2,
                                 error_msg("Converter", "from_binary", "invalid binary string"));
        })
        .func("ordinal", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "Converter.ordinal", loc);

            // Last two decimal digits of the magnitude (0..99); a small signed
            // int keeps the comparisons below sign-clean.  abs_to_u64 handles
            // INT64_MIN, whose magnitude does not fit in int64_t.
            const auto last_two_digits = static_cast<int>(abs_to_u64(n) % 100);

            std::string suffix{"th"};

            if (last_two_digits < 11 || last_two_digits > 13) {
                switch (last_two_digits % 10) {
                    case 1:
                        suffix = "st";
                        break;
                    case 2:
                        suffix = "nd";
                        break;
                    case 3:
                        suffix = "rd";
                        break;
                    default:
                        break;
                }
            }

            return Value{std::to_string(n) + suffix};
        })
        .func("to_roman", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto raw = expect_integer(args[0], "Converter.to_roman", loc);

            if (raw <= 0 || raw > 3999) {
                return make_failure_value(
                    error_msg("Converter", "to_roman", "value out of range (1-3999)"));
            }

            const auto n = static_cast<int>(raw);
            return make_success_value(Value{int_to_roman(n)});
        })
        .func("from_roman", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Converter.from_roman", loc);

            const auto& s = args[0].as_string();

            if (s.empty()) {
                return make_failure_value(
                    error_msg("Converter", "from_roman", "invalid roman numeral"));
            }

            std::int64_t result{0};

            for (std::size_t i{0}; i < s.size(); ++i) {
                const auto val = roman_char_value(s[i]);

                if (val == 0) {
                    return make_failure_value(
                        error_msg("Converter", "from_roman", "invalid roman numeral"));
                }

                if (i + 1 < s.size() && val < roman_char_value(s[i + 1])) {
                    result -= val;
                } else {
                    result += val;
                }
            }

            // Round-trip validation: convert back and compare to reject
            // malformed numerals like "IIII" or "VV".
            if (result < 1 || result > 3999) {
                return make_failure_value(
                    error_msg("Converter", "from_roman", "invalid roman numeral"));
            }

            const auto canonical = int_to_roman(static_cast<int>(result));

            // Compare uppercased input against canonical form.
            std::string upper_input{};
            upper_input.reserve(s.size());

            for (const char c : s) {
                upper_input += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }

            if (upper_input != canonical) {
                return make_failure_value(
                    error_msg("Converter", "from_roman", "invalid roman numeral"));
            }

            return make_success_value(Value{result});
        })
        .func("number_to_words", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "Converter.number_to_words", loc);

            if (n == 0) {
                return Value{std::string{"zero"}};
            }

            if (n == std::numeric_limits<std::int64_t>::min()) {
                return Value{std::string{
                    "negative nine quintillion two hundred twenty three quadrillion "
                    "three hundred seventy two trillion thirty six billion "
                    "eight hundred fifty four million seven hundred seventy five thousand "
                    "eight hundred eight"}};
            }

            return Value{int_to_words(n)};
        })
        .func("character_to_codepoint", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Converter.character_to_codepoint", loc);

            const auto& s = args[0].as_string();

            if (s.empty()) {
                return make_failure_value(
                    error_msg("Converter", "character_to_codepoint", "empty string"));
            }

            // Decode the first UTF-8 character to a Unicode codepoint.  Invalid
            // or truncated sequences fall back to the raw lead byte, matching the
            // shared decoder used elsewhere in the runtime.
            const auto codepoint = utf8_decode_at(s, 0);

            return make_success_value(Value{static_cast<std::int64_t>(codepoint)});
        })
        .func("codepoint_to_character", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto cp = expect_integer(args[0], "Converter.codepoint_to_character", loc);

            if (cp < 0 || cp > 0x10FFFF) {
                return make_failure_value(
                    error_msg("Converter", "codepoint_to_character",
                              "codepoint out of valid Unicode range (0..0x10FFFF)"));
            }

            // utf8_encode yields an empty string for surrogate halves
            // (U+D800..U+DFFF), which are not valid Unicode scalar values and
            // cannot be encoded as UTF-8.  The range check above already
            // excludes values above U+10FFFF, so an empty result here means the
            // codepoint is a surrogate.
            auto encoded = utf8_encode(static_cast<std::uint32_t>(cp));

            if (encoded.empty()) {
                return make_failure_value(
                    error_msg("Converter", "codepoint_to_character",
                              "codepoint is a UTF-16 surrogate half and cannot be encoded"));
            }

            return make_success_value(Value{std::move(encoded)});
        });
}

} // namespace luma
