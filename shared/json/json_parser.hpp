#ifndef LUMA_JSON_PARSER_HPP
#define LUMA_JSON_PARSER_HPP

#include "json/json.hpp"

// NOTE: parse_error.hpp lives at the shared/ root (resolved via the shared/
// include directory, not from json/ or protocol/) and defines luma::ParseError
// and luma::JsonParseError — base types shared by both the JSON parser and the
// protocol transport layer.  It sits at the root rather than under json/ or
// protocol/ because both depend on it equally; moving it to common/ would
// provide little practical benefit given its small consumer count
// (json_parser, transport_exceptions, protocol_test).
#include <cstddef>
#include <string_view>

#include "parse_error.hpp"

namespace luma::json {

// Recursive-descent JSON parser with configurable nesting depth.
// Enforces RFC 8259 syntax rules including surrogate pair handling,
// control character rejection, and leading-zero prohibition.
class JsonParser {
public:
    explicit JsonParser(std::string_view input, std::size_t max_depth);

    [[nodiscard]] JsonValue parse();

private:
    [[nodiscard]] char peek() const;
    char advance();
    void expect(char c);
    void skip_whitespace();

    [[nodiscard]] JsonValue parse_value();
    [[nodiscard]] char parse_escape_sequence(char esc) const;
    [[nodiscard]] std::string parse_unicode_escape();
    [[nodiscard]] std::string parse_string();
    void parse_integer_digits();
    [[nodiscard]] bool parse_fractional_part();
    [[nodiscard]] bool parse_exponent_part();
    [[nodiscard]] JsonValue parse_number();

    // Parse the already-scanned numeric substring as a double, throwing a
    // JsonParseError on overflow or (unreachable, given the grammar has already
    // validated the text) malformed input.  Shared by parse_number()'s
    // floating-point branch and its integer-overflow fallback.
    [[nodiscard]] JsonValue parse_double_literal(std::string_view num_str) const;
    void parse_literal(std::string_view literal);
    [[nodiscard]] JsonValue parse_bool();
    [[nodiscard]] JsonValue parse_null();
    [[nodiscard]] JsonValue parse_array();
    [[nodiscard]] JsonValue parse_object();

    template <typename Collection, typename ParseElement>
    [[nodiscard]] JsonValue parse_collection(const char* depth_label, char open, char close,
                                             const char* unterminated_msg, Collection initial,
                                             ParseElement parse_element);

    // Helper: returns true if the character at the current position is
    // an ASCII decimal digit ('0'–'9').
    [[nodiscard]] bool is_digit_at_pos() const noexcept;

    // Advance past a run of consecutive ASCII decimal digits, stopping at the
    // first non-digit or end of input.
    void skip_digits() noexcept;

    std::string_view input_;
    std::size_t pos_;
    int depth_;
    int max_nesting_depth_;
};

} // namespace luma::json

#endif // LUMA_JSON_PARSER_HPP
