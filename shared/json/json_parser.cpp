#include "json/json_parser.hpp"

#include <charconv>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>

#include "common/depth_guard.hpp"
#include "common/escape.hpp"
#include "common/resource_limits.hpp"
#include "common/utf8.hpp"

namespace luma::json {

// ─── Cached error messages ─────────────────────────────────────────
// Frequently used parse error strings, cached as constexpr to avoid
// repeated allocations on malformed input.
namespace {
constexpr const char* k_unexpected_eof = "unexpected end of input";
constexpr const char* k_unterminated_string = "unterminated string";
constexpr const char* k_unescaped_control = "unescaped control character";
constexpr const char* k_incomplete_unicode = "incomplete unicode escape";
constexpr const char* k_invalid_unicode = "invalid unicode escape";
constexpr const char* k_invalid_number = "invalid number";
constexpr const char* k_number_out_of_range = "number out of range";
constexpr const char* k_unterminated_array = "unterminated array";
constexpr const char* k_unterminated_object = "unterminated object";
constexpr const char* k_invalid_value = "invalid value";
constexpr const char* k_unexpected_trailing = "unexpected trailing content";
constexpr const char* k_expected_digit = "expected digit in number";
constexpr const char* k_leading_zeros = "leading zeros are not allowed";
constexpr const char* k_expected_digit_decimal = "expected digit after decimal point";
constexpr const char* k_expected_digit_exponent = "expected digit in exponent";
constexpr const char* k_expected_string_key = "expected string key in object";
constexpr const char* k_eof_in_escape = "unexpected end of input in string escape";
constexpr const char* k_expected_low_surrogate = "expected low surrogate after high surrogate";
constexpr const char* k_invalid_low_surrogate = "invalid low surrogate";
constexpr const char* k_unexpected_low_surrogate =
    "unexpected low surrogate without preceding high surrogate";
constexpr const char* k_array_limit = "array element count exceeds limit";
constexpr const char* k_object_limit = "object key count exceeds limit";
constexpr const char* k_duplicate_key = "duplicate key in object";

// UTF-16 surrogate pair range boundaries (RFC 2781).
constexpr std::uint32_t k_high_surrogate_min = 0xD800;
constexpr std::uint32_t k_high_surrogate_max = 0xDBFF;
constexpr std::uint32_t k_low_surrogate_min = 0xDC00;
constexpr std::uint32_t k_low_surrogate_max = 0xDFFF;

// Maximum value for max_depth parameter before it overflows int.
constexpr std::size_t k_max_depth_limit = static_cast<std::size_t>(std::numeric_limits<int>::max());
} // namespace

namespace {

// Parse exactly 4 hex digits into a Unicode code point.
// Returns std::nullopt if any character is not a valid hex digit.
[[nodiscard]] std::optional<unsigned int> parse_hex_codepoint(const char* str, std::size_t len) {
    if (len < 4) {
        return std::nullopt;
    }

    unsigned int codepoint{0};
    auto [ptr, ec] = std::from_chars(str, str + 4, codepoint, 16);
    if (ec != std::errc{} || ptr != str + 4) {
        return std::nullopt;
    }

    return codepoint;
}

// Combine a UTF-16 high surrogate (0xD800–0xDBFF) with a low surrogate
// (0xDC00–0xDFFF) into the final Unicode code point.
// `low_str` / `low_len` point to the 4 hex digits of the low surrogate.
// Returns std::nullopt if the low surrogate is invalid.
[[nodiscard]] std::optional<unsigned int>
handle_surrogate_pair(unsigned int high_surrogate, const char* low_str, std::size_t low_len) {
    const auto low = parse_hex_codepoint(low_str, low_len);
    if (!low.has_value() || *low < k_low_surrogate_min || *low > k_low_surrogate_max) {
        return std::nullopt;
    }

    return static_cast<unsigned int>(luma::decode_surrogate_pair(high_surrogate, *low));
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════

JsonParser::JsonParser(std::string_view input, std::size_t max_depth)
    : input_{input},
      pos_{0},
      depth_{0},
      max_nesting_depth_{static_cast<int>(std::min(max_depth, k_max_depth_limit))} {}

// ═══════════════════════════════════════════════════════════
// Public interface
// ═══════════════════════════════════════════════════════════

JsonValue JsonParser::parse() {
    skip_whitespace();

    auto value = parse_value();

    skip_whitespace();

    if (pos_ < input_.size()) {
        throw JsonParseError{pos_, k_unexpected_trailing};
    }

    return value;
}

// ═══════════════════════════════════════════════════════════
// Lexer helpers
// ═══════════════════════════════════════════════════════════

char JsonParser::peek() const {
    if (pos_ >= input_.size()) {
        throw JsonParseError{pos_, k_unexpected_eof};
    }

    return input_[pos_];
}

char JsonParser::advance() {
    if (pos_ >= input_.size()) {
        throw JsonParseError{pos_, k_unexpected_eof};
    }

    return input_[pos_++];
}

void JsonParser::expect(char c) {
    if (advance() != c) {
        throw JsonParseError{pos_ - 1, std::format("expected '{}'", c)};
    }
}

void JsonParser::skip_whitespace() {
    while (pos_ < input_.size()) {
        const char c = input_[pos_];

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++pos_;
        } else {
            break;
        }
    }
}

bool JsonParser::is_digit_at_pos() const noexcept {
    return pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9';
}

void JsonParser::skip_digits() noexcept {
    while (is_digit_at_pos()) {
        ++pos_;
    }
}

// ═══════════════════════════════════════════════════════════
// Value parsing
// ═══════════════════════════════════════════════════════════

JsonValue JsonParser::parse_value() {
    skip_whitespace();

    if (pos_ >= input_.size()) {
        throw JsonParseError{pos_, k_unexpected_eof};
    }

    const char c = peek();

    switch (c) {
        case '"':
            return JsonValue(parse_string());
        case '{':
            return parse_object();
        case '[':
            return parse_array();
        case 't':
        case 'f':
            return parse_bool();
        case 'n':
            return parse_null();
        default:
            if (c == '-' || (c >= '0' && c <= '9')) {
                return parse_number();
            }

            throw JsonParseError{pos_, std::format("unexpected character '{}'", c)};
    }
}

// ═══════════════════════════════════════════════════════════
// String parsing
// ═══════════════════════════════════════════════════════════

char JsonParser::parse_escape_sequence(char esc) const {
    switch (esc) {
        case '"':
            return '"';
        case '\\':
            return '\\';
        case '/':
            return '/';
        case 'b':
            return '\b';
        case 'f':
            return '\f';
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        default:
            throw JsonParseError{pos_, std::format("invalid escape '\\{}'", esc)};
    }
}

std::string JsonParser::parse_unicode_escape() {
    if (pos_ + 4 > input_.size()) {
        throw JsonParseError{pos_, k_incomplete_unicode};
    }

    const auto codepoint = parse_hex_codepoint(input_.data() + pos_, 4);
    pos_ += 4;

    if (!codepoint.has_value()) {
        throw JsonParseError{pos_, k_invalid_unicode};
    }

    unsigned int final_codepoint = *codepoint;

    // Handle UTF-16 surrogate pairs (RFC 8259).
    if (final_codepoint >= k_high_surrogate_min && final_codepoint <= k_high_surrogate_max) {
        if (pos_ + 6 > input_.size() || input_[pos_] != '\\' || input_[pos_ + 1] != 'u') {
            throw JsonParseError{pos_, k_expected_low_surrogate};
        }

        pos_ += 2; // skip \u

        const auto combined = handle_surrogate_pair(final_codepoint, input_.data() + pos_, 4);
        pos_ += 4;

        if (!combined.has_value()) {
            throw JsonParseError{pos_, k_invalid_low_surrogate};
        }

        final_codepoint = *combined;
    } else if (final_codepoint >= k_low_surrogate_min && final_codepoint <= k_low_surrogate_max) {
        throw JsonParseError{pos_, k_unexpected_low_surrogate};
    }

    return luma::utf8_encode(final_codepoint);
}

std::string JsonParser::parse_string() {
    expect('"');

    std::string result;
    result.reserve(std::min(input_.size() - pos_, static_cast<std::size_t>(64)));

    while (pos_ < input_.size()) {
        const char c = advance();

        if (c == '"') {
            return result;
        }

        if (c == '\\') {
            if (pos_ >= input_.size()) {
                throw JsonParseError{pos_, k_eof_in_escape};
            }

            const char esc = advance();

            if (esc == 'u') {
                result += parse_unicode_escape();
            } else {
                result += parse_escape_sequence(esc);
            }
        } else {
            // RFC 8259 §7: control characters (U+0000–U+001F) must be escaped.
            if (static_cast<unsigned char>(c) <= k_ascii_control_max) {
                throw JsonParseError{pos_, k_unescaped_control};
            }

            // Bytes >= 0x80 (UTF-8 lead/continuation bytes) are copied through
            // verbatim; multi-byte sequences are not validated for
            // well-formedness.  This parser consumes the LSP/DAP wire protocol
            // from a trusted local editor peer, so a malformed sequence is a
            // client bug, not an attack surface, and round-trips unchanged
            // rather than being rejected or silently replaced.
            result += c;
        }
    }

    throw JsonParseError{pos_, k_unterminated_string};
}

// ═══════════════════════════════════════════════════════════
// Number parsing
// ═══════════════════════════════════════════════════════════

void JsonParser::parse_integer_digits() {
    // RFC 7159: at least one digit is required in the integer part.
    if (!is_digit_at_pos()) {
        throw JsonParseError{pos_, k_expected_digit};
    }

    if (input_[pos_] == '0') {
        ++pos_;
        // RFC 7159: leading zeros are not allowed (e.g. "007", "00").
        if (is_digit_at_pos()) {
            throw JsonParseError{pos_, k_leading_zeros};
        }
    } else {
        skip_digits();
    }
}

bool JsonParser::parse_fractional_part() {
    if (pos_ >= input_.size() || input_[pos_] != '.') {
        return false;
    }

    ++pos_;

    // RFC 7159: at least one digit is required after the decimal point.
    if (!is_digit_at_pos()) {
        throw JsonParseError{pos_, k_expected_digit_decimal};
    }

    skip_digits();

    return true;
}

bool JsonParser::parse_exponent_part() {
    if (pos_ >= input_.size() || (input_[pos_] != 'e' && input_[pos_] != 'E')) {
        return false;
    }

    ++pos_;

    if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) {
        ++pos_;
    }

    // RFC 7159: at least one digit is required in the exponent.
    if (!is_digit_at_pos()) {
        throw JsonParseError{pos_, k_expected_digit_exponent};
    }

    skip_digits();

    return true;
}

JsonValue JsonParser::parse_double_literal(std::string_view num_str) const {
    double value{0.0};
    auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), value);

    if (ec == std::errc::result_out_of_range) {
        throw JsonParseError{pos_, k_number_out_of_range};
    }

    if (ec != std::errc{}) {
        throw JsonParseError{pos_, k_invalid_number};
    }

    return JsonValue(value);
}

JsonValue JsonParser::parse_number() {
    const std::size_t start = pos_;

    if (peek() == '-') {
        ++pos_;
    }

    parse_integer_digits();

    const bool has_fraction = parse_fractional_part();
    const bool has_exponent = parse_exponent_part();
    const bool is_float = has_fraction || has_exponent;

    const auto num_str = input_.substr(start, pos_ - start);

    if (is_float) {
        return parse_double_literal(num_str);
    }

    int64_t value{0};

    auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), value);

    if (ec == std::errc::result_out_of_range) {
        // Integer overflow: fall back to double.  The grammar has already
        // validated num_str as a pure integer literal (no fraction or exponent),
        // so the double parse can only succeed or overflow — never fail as
        // malformed.
        return parse_double_literal(num_str);
    }

    if (ec != std::errc{}) {
        throw JsonParseError{pos_, k_invalid_number};
    }

    return JsonValue(value);
}

// ═══════════════════════════════════════════════════════════
// Keyword parsing
// ═══════════════════════════════════════════════════════════

void JsonParser::parse_literal(std::string_view literal) {
    if (pos_ + literal.size() > input_.size() || input_.substr(pos_, literal.size()) != literal) {
        throw JsonParseError{pos_, k_invalid_value};
    }

    pos_ += literal.size();
}

JsonValue JsonParser::parse_bool() {
    if (peek() == 't') {
        parse_literal("true");

        return JsonValue(true);
    }

    parse_literal("false");

    return JsonValue(false);
}

JsonValue JsonParser::parse_null() {
    parse_literal("null");

    return {};
}

// ═══════════════════════════════════════════════════════════
// Array and object parsing
// ═══════════════════════════════════════════════════════════

// Generic collection parser for arrays and objects.  Handles the
// shared open-delimiter / loop / comma-or-close / close-delimiter
// pattern.  The ParseElement callback is responsible for parsing
// one element and appending it to the collection, and for reporting
// the appropriate size-limit error.
template <typename Collection, typename ParseElement>
JsonValue JsonParser::parse_collection(const char* depth_label, char open, char close,
                                       const char* unterminated_msg, Collection initial,
                                       ParseElement parse_element) {
    const DepthGuard guard{depth_, max_nesting_depth_, depth_label};

    expect(open);
    skip_whitespace();

    if (pos_ < input_.size() && peek() == close) {
        ++pos_;

        return JsonValue(std::move(initial));
    }

    while (true) {
        parse_element(initial);
        skip_whitespace();

        if (pos_ >= input_.size()) {
            throw JsonParseError{pos_, unterminated_msg};
        }

        if (peek() == close) {
            ++pos_;

            return JsonValue(std::move(initial));
        }

        expect(',');
        skip_whitespace();
    }
}

JsonValue JsonParser::parse_array() {
    return parse_collection("JSON array", '[', ']', k_unterminated_array, JsonValue::ArrayType{},
                            [this](JsonValue::ArrayType& elements) {
                                if (elements.size() >= ResourceLimits::max_json_elements) {
                                    throw JsonParseError{pos_, k_array_limit};
                                }
                                elements.push_back(parse_value());
                            });
}

JsonValue JsonParser::parse_object() {
    return parse_collection("JSON object", '{', '}', k_unterminated_object, JsonValue::ObjectType{},
                            [this](JsonValue::ObjectType& members) {
                                if (members.size() >= ResourceLimits::max_json_elements) {
                                    throw JsonParseError{pos_, k_object_limit};
                                }
                                skip_whitespace();
                                if (peek() != '"') {
                                    throw JsonParseError{pos_, k_expected_string_key};
                                }
                                auto key = parse_string();
                                skip_whitespace();
                                expect(':');
                                auto value = parse_value();
                                auto [it, inserted] =
                                    members.emplace(std::move(key), std::move(value));
                                if (!inserted) {
                                    throw JsonParseError{pos_, k_duplicate_key};
                                }
                            });
}

} // namespace luma::json
