// json_module_parser.cpp — JSON parser (JSON text into a Value).
//
// Extracted from json_module.cpp for readability.  The path-navigation
// operations live in json_module_path.{hpp,cpp} and the Json module
// registration (deserialize/get/set/...) lives in json_module.cpp.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

#include "common/resource_limits.hpp"
#include "common/utf8.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/text/json_module.hpp"
#include "runtime/stdlib/text/parser_cursor.hpp"

namespace luma {

namespace {

// Convert an already-validated JSON numeric token to double.  std::stod throws
// std::out_of_range when the magnitude is outside double's range (e.g. 1e400 or
// the underflowing 1e-400); a valid JSON document must still parse, so fall back
// to std::strtod, which saturates to +/-inf or 0 rather than throwing.
[[nodiscard]] double parse_double_saturating(const std::string& num_str) {
    try {
        return std::stod(num_str);
    } catch (const std::out_of_range&) {
        return std::strtod(num_str.c_str(), nullptr);
    }
}

// Error-reporting policy for the JSON cursor: every scanner error surfaces as a
// std::runtime_error, matching the rest of the JSON parser.
struct JsonErrorPolicy {
    [[noreturn]] static void unexpected_end() {
        throw std::runtime_error{"unexpected end of JSON"};
    }

    [[noreturn]] static void expected(char c, std::size_t position) {
        throw std::runtime_error{std::format("expected '{}' at position {}", c, position)};
    }

    [[noreturn]] static void too_deep() {
        throw std::runtime_error{"JSON nesting too deep"};
    }
};

class JsonParser : public parser_detail::ParserCursor<JsonErrorPolicy> {
public:
    explicit JsonParser(std::string_view input)
        : parser_detail::ParserCursor<JsonErrorPolicy>{input} {}

    [[nodiscard]] Value parse() {
        skip_whitespace();

        auto result = parse_value();

        skip_whitespace();

        if (pos_ < input_.size()) {
            throw std::runtime_error{std::format("unexpected character at position {}", pos_)};
        }

        return result;
    }

private:
    using Cursor = parser_detail::ParserCursor<JsonErrorPolicy>;
    using Cursor::advance;
    using Cursor::depth_;
    using Cursor::enter_depth;
    using Cursor::expect;
    using Cursor::input_;
    using Cursor::peek;
    using Cursor::pos_;

    // Cumulative count of values parsed in this document, bounding total node
    // allocation independently of any single container's size limit.
    std::size_t element_count_{0};

    void skip_whitespace() {
        while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\t' ||
                                        input_[pos_] == '\n' || input_[pos_] == '\r')) {
            ++pos_;
        }
    }

    [[nodiscard]] Value parse_value() {
        // Runtime-configurable nesting bound (LUMA_LIMIT_MAX_JSON_NESTING_DEPTH,
        // tightened to 32 under --box), clamped to the compile-time cap that keeps
        // this recursive descent within the native stack.  The runtime limit can
        // therefore only tighten the bound, never loosen it past the safe ceiling.
        const auto depth_limit =
            std::min<std::size_t>(ResourceLimits::max_json_nesting_depth,
                                  static_cast<std::size_t>(CompileTimeLimits::max_json_depth));
        enter_depth(static_cast<int>(depth_limit));

        // Per-document element cap (LUMA_LIMIT_MAX_JSON_ELEMENTS; 10k under --box).
        // Counts every parsed value so one deserialize cannot allocate an unbounded
        // number of nodes even when each container stays under its own size limit.
        if (++element_count_ > ResourceLimits::max_json_elements) {
            throw std::runtime_error{"JSON document exceeds maximum element count"};
        }

        skip_whitespace();

        if (pos_ >= input_.size()) {
            throw std::runtime_error{"unexpected end of JSON"};
        }

        Value result;

        switch (peek()) {
            case '"':
                result = parse_string_value();
                break;
            case '{':
                result = parse_object();
                break;
            case '[':
                result = parse_array();
                break;
            case 't':
            case 'f':
                result = parse_bool();
                break;
            case 'n':
                result = parse_null();
                break;
            default:
                result = parse_number();
                break;
        }

        --depth_;

        return result;
    }

    // Parse a 4-digit hex sequence starting at pos_+1, advance pos_ by 4
    // (so pos_ lands on the last hex digit, matching the outer ++pos_ contract).
    [[nodiscard]] unsigned int parse_hex_quad() {
        if (pos_ + 4 >= input_.size()) {
            throw std::runtime_error{"incomplete unicode escape"};
        }

        unsigned int cp{0};

        for (std::size_t i = 1; i <= 4; ++i) {
            const char h = input_[pos_ + i];

            cp <<= 4;

            if (h >= '0' && h <= '9') {
                cp += static_cast<unsigned int>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
                cp += static_cast<unsigned int>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
                cp += static_cast<unsigned int>(h - 'A' + 10);
            } else {
                throw std::runtime_error{"invalid unicode escape"};
            }
        }

        pos_ += 4;

        return cp;
    }

    [[nodiscard]] std::string parse_string() {
        expect('"');
        std::string result;

        while (pos_ < input_.size() && input_[pos_] != '"') {
            if (input_[pos_] == '\\') {
                ++pos_;

                if (pos_ >= input_.size()) {
                    throw std::runtime_error{"unterminated escape"};
                }

                switch (input_[pos_]) {
                    case '"':
                        result += '"';
                        break;
                    case '\\':
                        result += '\\';
                        break;
                    case '/':
                        result += '/';
                        break;
                    case 'b':
                        result += '\b';
                        break;
                    case 'f':
                        result += '\f';
                        break;
                    case 'n':
                        result += '\n';
                        break;
                    case 'r':
                        result += '\r';
                        break;
                    case 't':
                        result += '\t';
                        break;
                    case 'u': {
                        // parse_hex_quad reads pos_+1..pos_+4, advances pos_ by 4
                        // so pos_ lands on the last hex char; outer ++pos_ moves past it.
                        auto cp = parse_hex_quad();

                        // Handle UTF-16 surrogate pairs (\uD800-\uDBFF followed by \uDC00-\uDFFF).
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // High surrogate — expect a low surrogate next.
                            if (pos_ + 2 < input_.size() && input_[pos_ + 1] == '\\' &&
                                input_[pos_ + 2] == 'u') {
                                pos_ += 2; // skip past '\u' — parse_hex_quad reads from pos_+1

                                const auto low = parse_hex_quad();

                                if (low >= 0xDC00 && low <= 0xDFFF) {
                                    cp = static_cast<unsigned int>(
                                        luma::decode_surrogate_pair(cp, low));
                                } else {
                                    throw std::runtime_error{
                                        "high surrogate not followed by low surrogate"};
                                }
                            } else {
                                throw std::runtime_error{
                                    "high surrogate not followed by low surrogate"};
                            }
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            throw std::runtime_error{
                                "unexpected low surrogate without high surrogate"};
                        }

                        result += luma::utf8_encode(cp);

                        break;
                    }
                    default:
                        throw std::runtime_error{
                            std::format("invalid escape '\\{}'", input_[pos_])};
                }
            } else {
                result += input_[pos_];
            }

            ++pos_;
        }

        expect('"');

        return result;
    }

    [[nodiscard]] Value parse_string_value() {
        return Value{parse_string()};
    }

    [[nodiscard]] Value parse_number() {
        auto start = pos_;

        bool is_float{false};

        if (pos_ < input_.size() && input_[pos_] == '-') {
            ++pos_;
        }

        if (pos_ >= input_.size() ||
            (std::isdigit(static_cast<unsigned char>(input_[pos_])) == 0)) {
            throw std::runtime_error{std::format("invalid number at position {}", start)};
        }

        // Reject leading zeros: after an optional '-', a leading '0' must not
        // be followed by another digit (JSON spec RFC 8259 §6).
        if (input_[pos_] == '0' && pos_ + 1 < input_.size() &&
            (std::isdigit(static_cast<unsigned char>(input_[pos_ + 1])) != 0)) {
            throw std::runtime_error{
                std::format("leading zeros not allowed at position {}", start)};
        }

        while (pos_ < input_.size() &&
               (std::isdigit(static_cast<unsigned char>(input_[pos_])) != 0)) {
            ++pos_;
        }

        if (pos_ < input_.size() && input_[pos_] == '.') {
            is_float = true;

            ++pos_;

            // A decimal point must be followed by at least one digit.
            if (pos_ >= input_.size() ||
                (std::isdigit(static_cast<unsigned char>(input_[pos_])) == 0)) {
                throw std::runtime_error{
                    std::format("expected digit after decimal point at position {}", pos_)};
            }

            while (pos_ < input_.size() &&
                   (std::isdigit(static_cast<unsigned char>(input_[pos_])) != 0)) {
                ++pos_;
            }
        }

        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            is_float = true;

            ++pos_;

            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) {
                ++pos_;
            }

            // An exponent must have at least one digit.
            if (pos_ >= input_.size() ||
                (std::isdigit(static_cast<unsigned char>(input_[pos_])) == 0)) {
                throw std::runtime_error{
                    std::format("expected digit in exponent at position {}", pos_)};
            }

            while (pos_ < input_.size() &&
                   (std::isdigit(static_cast<unsigned char>(input_[pos_])) != 0)) {
                ++pos_;
            }
        }

        auto num_str = std::string{input_.substr(start, pos_ - start)};

        if (is_float) {
            return Value{parse_double_saturating(num_str)};
        }

        try {
            return Value{static_cast<std::int64_t>(std::stoll(num_str))};
        } catch (const std::out_of_range&) {
            // Integer literal too large for int64 — represent it as a double.
            return Value{parse_double_saturating(num_str)};
        }
    }

    [[nodiscard]] Value parse_bool() {
        if (input_.substr(pos_, 4) == "true") {
            pos_ += 4;

            return Value{true};
        }

        if (input_.substr(pos_, 5) == "false") {
            pos_ += 5;

            return Value{false};
        }

        throw std::runtime_error{std::format("unexpected token at position {}", pos_)};
    }

    [[nodiscard]] Value parse_null() {
        if (input_.substr(pos_, 4) == "null") {
            pos_ += 4;

            return Value{NullValue{}};
        }

        throw std::runtime_error{std::format("unexpected token at position {}", pos_)};
    }

    [[nodiscard]] Value parse_array() {
        expect('[');
        skip_whitespace();

        auto arr = std::make_shared<ArrayValue>();

        if (pos_ < input_.size() && input_[pos_] == ']') {
            ++pos_;

            return Value{std::move(arr)};
        }

        while (true) {
            if (arr->elements->size() >= ResourceLimits::max_array_size) {
                throw std::runtime_error{"JSON array exceeds maximum size"};
            }

            arr->elements->push_back(parse_value());

            skip_whitespace();

            if (pos_ < input_.size() && input_[pos_] == ',') {
                ++pos_;

                skip_whitespace();
            } else {
                break;
            }
        }

        expect(']');

        return Value{std::move(arr)};
    }

    [[nodiscard]] Value parse_object() {
        expect('{');
        skip_whitespace();

        auto dict = std::make_shared<DictionaryValue>();

        if (pos_ < input_.size() && input_[pos_] == '}') {
            ++pos_;

            return Value{std::move(dict)};
        }

        // Pre-build the empty hash index so each set() below is O(1), keeping
        // object parsing O(n) in the number of members rather than O(n^2).
        dict->rebuild_index();

        while (true) {
            skip_whitespace();

            if (peek() != '"') {
                throw std::runtime_error{std::format("expected string key at position {}", pos_)};
            }

            auto key = parse_string();

            skip_whitespace();
            expect(':');

            auto val = parse_value();

            dict->set(key, std::move(val));

            if (dict->entries.size() > ResourceLimits::max_dictionary_size) {
                throw std::runtime_error{"JSON object exceeds maximum size"};
            }

            skip_whitespace();

            if (pos_ < input_.size() && input_[pos_] == ',') {
                ++pos_;
            } else {
                break;
            }
        }

        expect('}');

        return Value{std::move(dict)};
    }
};

} // namespace

// Internal — exposed for the fuzz_json_stdlib trust-boundary target.  Mirrors
// the parse path used by Json.deserialize: construct the parser over the raw
// text and run it, propagating any std::runtime_error on malformed input.
// Every Json native body routes its parse through this shim.
Value json_parse_string(std::string_view input) {
    JsonParser parser{input};

    return parser.parse();
}

} // namespace luma
