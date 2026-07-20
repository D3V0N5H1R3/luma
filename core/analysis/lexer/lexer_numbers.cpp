#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_emitter.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/lexer/token.hpp"
#include "analysis/source/source_location.hpp"

namespace luma {

// ──────────── Number kind (internal to this translation unit) ────────────

namespace {

// Distinguishes integer literals from floating-point (number) literals.
// Used to select appropriate error messages and fallback sentinel values.
enum class NumberKind {
    Integer,
    Floating
};

[[nodiscard]] constexpr std::string_view number_kind_label(NumberKind kind) noexcept {
    return kind == NumberKind::Integer ? "integer" : "number";
}

} // namespace

// ──────────── Character classification ────────────

bool Lexer::is_prefixed_literal(char prefix_char) const {
    return current() == '0' && cursor_.position() + 1 < cursor_.source().size() &&
           (cursor_.source()[cursor_.position() + 1] == prefix_char ||
            cursor_.source()[cursor_.position() + 1] ==
                static_cast<char>(prefix_char - ('a' - 'A')));
}

// ──────────── Number literal parsing ────────────

namespace {

// Shared implementation for parsing numeric literals with diagnostic-as-error
// pattern: on failure, a diagnostic is emitted and a sentinel value is returned
// so lexing can continue.
template <typename Parser, typename Sentinel>
[[nodiscard]] TokenLiteral parse_number(const std::string& lexeme, Parser parser, Sentinel sentinel,
                                        NumberKind kind, DiagnosticEmitter& emitter,
                                        SourceLocation loc) {
    const auto label = number_kind_label(kind);
    const bool is_integer = (kind == NumberKind::Integer);

    try {
        return TokenLiteral{parser(lexeme)};
    } catch (const std::out_of_range&) {
        emitter.emit_error(loc, std::format("{} literal out of range: {}", label, lexeme),
                           is_integer ? "use 'number' type for values beyond integer range"
                                      : "the value exceeds the maximum representable number",
                           DiagnosticCode::InvalidNumber);
        return TokenLiteral{sentinel};
    } catch (const std::invalid_argument&) {
        emitter.emit_error(loc, std::format("invalid {} literal: {}", label, lexeme),
                           is_integer ? "check the number format — e.g. 42 or 0xFF"
                                      : "check the number format — e.g. 3.14 or 1e10",
                           DiagnosticCode::InvalidNumber);
        return TokenLiteral{sentinel};
    }
}

} // namespace

TokenLiteral Lexer::parse_integer_literal(const std::string& lexeme, int base) {
    return parse_number(
        lexeme, [base](const std::string& s) { return std::stoll(s, nullptr, base); },
        std::int64_t{0}, NumberKind::Integer, *this, current_location());
}

TokenLiteral Lexer::parse_float_literal(const std::string& lexeme) {
    return parse_number(
        lexeme, [](const std::string& s) { return std::stod(s); }, 0.0, NumberKind::Floating, *this,
        current_location());
}

bool Lexer::has_valid_exponent() const {
    if (is_at_end() || (current() != 'e' && current() != 'E')) {
        return false;
    }

    std::size_t look{cursor_.position() + 1};

    if (look < cursor_.source().size() &&
        (cursor_.source()[look] == '+' || cursor_.source()[look] == '-')) {
        ++look;
    }

    return look < cursor_.source().size() && is_digit(cursor_.source()[look]);
}

void Lexer::consume_exponent() {
    advance(); // consume 'e'/'E'

    if (!is_at_end() && (current() == '+' || current() == '-')) {
        advance();
    }

    while (!is_at_end() && is_digit(current())) {
        advance();
    }
}

void Lexer::scan_prefixed_literal(bool (*is_valid_digit)(char), int base,
                                  std::string_view kind_name, std::string_view hint_example,
                                  std::size_t start) {
    advance(); // consume '0'
    advance(); // consume prefix letter

    const std::size_t digit_start{cursor_.position()};

    while (!is_at_end() && is_valid_digit(current())) {
        advance();
    }

    if (cursor_.position() == digit_start) {
        emit_syntax_error(std::format("{} literal has no digits", kind_name), current_location(),
                          std::format("add {} digits after '{}', e.g. {}", kind_name,
                                      cursor_.source().substr(start, 2), hint_example),
                          DiagnosticCode::InvalidNumber);
        return;
    }

    const auto lexeme = cursor_.source().substr(start, cursor_.position() - start);
    const auto digits = cursor_.source().substr(digit_start, cursor_.position() - digit_start);

    auto literal = parse_integer_literal(digits, base);
    add_token(TokenType::IntegerLiteral, lexeme, std::move(literal));
}

void Lexer::scan_number() {
    const std::size_t start{cursor_.position()};

    // Hex literal: 0x... or 0X...
    if (is_prefixed_literal('x')) {
        scan_prefixed_literal(
            [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            },
            16, "hex", "0xFF", start);

        return;
    }

    // Binary literal: 0b... or 0B...
    if (is_prefixed_literal('b')) {
        scan_prefixed_literal([](char c) { return c == '0' || c == '1'; }, 2, "binary", "0b1010",
                              start);

        return;
    }

    // Decimal integer or floating-point number.
    bool is_float{false};

    while (!is_at_end() && is_digit(current())) {
        advance();
    }

    if (!is_at_end() && current() == '.' && peek_next() != '.') {
        is_float = true;

        advance();

        while (!is_at_end() && is_digit(current())) {
            advance();
        }
    }

    if (has_valid_exponent()) {
        is_float = true;
        consume_exponent();
    }

    const auto lexeme = cursor_.source().substr(start, cursor_.position() - start);

    if (is_float) {
        auto literal = parse_float_literal(lexeme);
        add_token(TokenType::NumberLiteral, lexeme, std::move(literal));
    } else {
        auto literal = parse_integer_literal(lexeme);
        add_token(TokenType::IntegerLiteral, lexeme, std::move(literal));
    }
}

} // namespace luma
