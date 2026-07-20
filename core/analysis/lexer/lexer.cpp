#include "analysis/lexer/lexer.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"
#include "common/utf8.hpp"

namespace luma {

// Cap on the number of hard lexical errors before the scanner stops emitting and
// gives up.  On badly broken or adversarial input every unrecognised byte — and
// every invalid escape inside a string literal — emits a diagnostic, so an
// uncapped scan accumulates O(input) diagnostics up to the 64 MB source cap.  The
// parser applies the same guard once it takes over (parser_limits::k_max_errors).
// Only hard errors gate the scan; warnings (stray semicolons, confusables) must
// never truncate an otherwise valid file.
constexpr std::size_t k_max_lex_errors{100};

// ──────────── Diagnostic helpers ────────────

void Lexer::emit_syntax_error(std::string message, SourceLocation loc, std::string_view hint,
                              DiagnosticCode code) {
    // Stop emitting once the hard-error cap is reached.  tokenize() also checks the
    // cap between tokens, but a single token can emit many errors before it returns
    // (e.g. a long string literal full of invalid escape sequences), so gating here
    // as well keeps the lexer's diagnostic output bounded on adversarial input
    // regardless of which scan path produced the error.
    if (error_count() >= k_max_lex_errors) {
        return;
    }

    emit_error(loc, std::move(message), hint, code);
}

// ──────────── Public ────────────

std::vector<Token> Lexer::tokenize() {
    tokens_.clear();

    // Reserve up front to avoid repeated geometric reallocations of the token
    // vector — each of which move-constructs every Token scanned so far (a
    // heavyweight type: std::string lexeme + optional literal + location).
    // The average Luma token spans several source bytes, so dividing the
    // source length by this factor is a safe over-reserve-free estimate that
    // eliminates reallocations for typical inputs.
    constexpr std::size_t k_avg_bytes_per_token{4};
    tokens_.reserve(cursor_.source().size() / k_avg_bytes_per_token + 1);

    // Give up after too many hard lexical errors so a badly broken or adversarial
    // file cannot flood the token/diagnostic stream; the rationale and cap live on
    // k_max_lex_errors, and emit_syntax_error enforces the same cap within a single
    // token.
    while (!is_at_end()) {
        skip_whitespace_and_comments();

        if (is_at_end()) {
            break;
        }

        if (error_count() >= k_max_lex_errors) {
            break;
        }

        scan_token();
    }

    add_token(TokenType::EndOfFile, "");

    // Flush accumulated diagnostics to the external collector.
    for (auto& diagnostic : take_diagnostics()) {
        collector_.emit(std::move(diagnostic));
    }

    return std::move(tokens_);
}

// ──────────── Token helpers ────────────

bool Lexer::is_at_end() const {
    return cursor_.is_at_end();
}

char Lexer::current() const {
    return cursor_.current();
}

char Lexer::peek_next() const {
    return cursor_.peek_next();
}

char Lexer::advance() {
    return cursor_.advance();
}

bool Lexer::match(char expected) {
    return cursor_.match(expected);
}

SourceLocation Lexer::current_location() const {
    return cursor_.location(file_id_);
}

void Lexer::add_token(TokenType type, std::string_view lexeme) {
    tokens_.push_back({.type = type,
                       .lexeme = std::string{lexeme},
                       .literal = std::nullopt,
                       .location = current_location()});
}

void Lexer::add_token(TokenType type, std::string_view lexeme, TokenLiteral literal) {
    tokens_.push_back({.type = type,
                       .lexeme = std::string{lexeme},
                       .literal = std::move(literal),
                       .location = current_location()});
}

void Lexer::skip_whitespace_and_comments() {
    bool semicolon_warned{false};

    while (!is_at_end()) {
        const char character = current();

        if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
            advance();
        } else if (character == ';') {
            if (!semicolon_warned) {
                emit_warning(current_location(), "unnecessary semicolon",
                             "Luma does not use semicolons — you can safely remove them");

                semicolon_warned = true;
            }
            advance();
        } else if (character == '#') {
            while (!is_at_end() && current() != '\n') {
                advance();
            }
        } else {
            break;
        }
    }
}

void Lexer::scan_token() {
    // Check if we are inside an interpolation and hit a closing brace.
    if (!interpolation_state_.empty() && current() == '}') {
        if (try_close_interpolation()) {
            return;
        }
    }

    const char character = current();
    const auto location = current_location();

    // Numbers
    if (is_digit(character)) {
        scan_number();

        return;
    }

    // Identifiers and keywords
    if (is_alpha(character) || character == '_') {
        scan_identifier();

        return;
    }

    // Strings
    if (character == '"') {
        // Record the opening quote as this string segment's source start before
        // consuming it; string tokens carry a processed lexeme, so their start
        // cannot be reconstructed from the lexeme width later.
        string_segment_start_ = location;

        advance();

        if (!is_at_end() && current() == '"' && peek_next() == '"') {
            advance();
            advance();
            scan_triple_quoted_string();
        } else {
            scan_string();
        }

        return;
    }

    // Annotation
    if (character == '@') {
        scan_annotation();

        return;
    }

    // Operators and punctuation
    advance();
    scan_operator(character, location);
}

// ──────────── Interpolation close ────────────

bool Lexer::try_close_interpolation() {
    auto& state = interpolation_state_.back();

    --state.brace_depth;

    if (state.brace_depth == 0) {
        const bool triple_quoted = state.is_triple_quoted;

        interpolation_state_.pop_back();

        advance();
        add_token(TokenType::InterpolationEnd, "}");

        // The continuation segment begins immediately after the closing brace;
        // record it as the source start for the StringMiddle/StringEnd token the
        // continuation scanners are about to emit.
        string_segment_start_ = current_location();

        if (triple_quoted) {
            scan_triple_quoted_string_continuation();
        } else {
            scan_string_continuation();
        }

        return true;
    }

    return false;
}

// ──────────── Identifiers and keywords ────────────

void Lexer::scan_identifier() {
    const std::size_t start{cursor_.position()};

    while (!is_at_end() && is_alnum(current())) {
        advance();
    }

    const std::string_view word{cursor_.source().data() + start, cursor_.position() - start};

    // Heuristic check for zero-width or confusable Unicode characters.
    // 0xE2 is the first byte of several problematic 3-byte UTF-8 sequences
    // (e.g. zero-width space U+200B, zero-width non-joiner U+200C).
    // This is a cheap approximation — it may false-positive on legitimate
    // characters starting with 0xE2 (e.g. em-dash U+2014, bullet U+2022)
    // and cannot detect homoglyphs between scripts (e.g. Cyrillic а vs
    // Latin a).  A full confusable detector would require a Unicode
    // confusables table, which is out of scope for the lexer.
    constexpr unsigned char k_potential_zwsp_marker = 0xE2;

    const bool has_confusable_marker = std::ranges::any_of(word, [](char character) {
        return static_cast<unsigned char>(character) == k_potential_zwsp_marker;
    });

    if (has_confusable_marker) {
        emit_warning(current_location(),
                     "identifier contains non-ASCII characters that may be confusable",
                     "check for zero-width or homoglyph characters");
    }

    const auto type = keyword_type(word);

    if (type == TokenType::BooleanLiteral) {
        add_token(type, word, TokenLiteral{word == "true"});
    } else {
        // Nudge users who reach for another language's variable-declaration
        // keywords toward Luma's own syntax.
        if (type == TokenType::Identifier && (word == "var" || word == "let")) {
            emit_warning(current_location(), std::format("'{}' is not a Luma keyword", word),
                         "use 'type name = value' or "
                         "'mutable type name = value' to declare variables");
        }

        add_token(type, word);
    }
}

TokenType Lexer::keyword_type(std::string_view word) {
    // Binary search the canonical, spelling-sorted keyword table (token_type.hpp)
    // — the single source of truth shared with the keyword classification
    // predicates.
    const auto it = std::ranges::lower_bound(k_keywords, word, {}, &KeywordSpelling::spelling);

    if (it != std::end(k_keywords) && it->spelling == word) {
        return it->type;
    }

    return TokenType::Identifier;
}

// ──────────── Annotations ────────────

void Lexer::scan_annotation() {
    const auto location = current_location();

    advance(); // consume '@'

    const std::size_t start{cursor_.position()};

    // First character must be a letter or underscore.
    if (!is_at_end() && (is_alpha(current()) || current() == '_')) {
        advance();

        // Subsequent characters may also include digits, consistent
        // with identifier rules.
        while (!is_at_end() && is_alnum(current())) {
            advance();
        }
    }

    if (cursor_.position() == start) {
        emit_syntax_error("expected annotation name after '@'", location,
                          "annotations must be a name like @main or @test",
                          DiagnosticCode::UnexpectedToken);
        add_token(TokenType::Error, "@");
        return;
    }

    add_token(TokenType::Annotation, cursor_.source().substr(start, cursor_.position() - start));
}

} // namespace luma
