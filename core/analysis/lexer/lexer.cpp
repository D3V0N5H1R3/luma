#include "analysis/lexer/lexer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/common/utf8_validation.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/diagnostics/display_text.hpp"
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

        token_start_ = current_location();
        scan_token();
    }

    // A triple-quoted literal left open (unterminated inside an interpolation,
    // or cut short by the error cap) still gets its pieces dedented.
    while (!open_triple_quoted_.empty()) {
        finish_triple_quoted_string();
    }

    token_start_ = current_location();
    add_token(TokenType::EndOfFile, "");

    stamp_token_starts();

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

// Copy each token's start into its end-position `location`, so every AST node,
// source-map entry and diagnostic built from a token knows where the token
// begins.  Done once after scanning, because the string scanners overwrite
// `start_location` after add_token() for string segments.
void Lexer::stamp_token_starts() {
    for (auto& token : tokens_) {
        if (token.start_location.line > 0) {
            token.location.start_line = token.start_location.line;
            token.location.start_column = token.start_location.column;
        }
    }
}

void Lexer::add_token(TokenType type, std::string_view lexeme) {
    tokens_.push_back({.type = type,
                       .lexeme = std::string{lexeme},
                       .literal = std::nullopt,
                       .location = current_location(),
                       .start_location = token_start_,
                       .preceded_by_newline = std::exchange(newline_pending_, false)});
}

void Lexer::add_token(TokenType type, std::string_view lexeme, TokenLiteral literal) {
    tokens_.push_back({.type = type,
                       .lexeme = std::string{lexeme},
                       .literal = std::move(literal),
                       .location = current_location(),
                       .start_location = token_start_,
                       .preceded_by_newline = std::exchange(newline_pending_, false)});
}

void Lexer::skip_whitespace_and_comments() {
    bool semicolon_warned{false};

    while (!is_at_end()) {
        const char character = current();

        if (character == '\n') {
            newline_pending_ = true;
            advance();
        } else if (character == ' ' || character == '\t' || character == '\r') {
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

    // Numbers.  After '.' / '?.' a number is a tuple member index.
    if (is_digit(character)) {
        if (!tokens_.empty() && (tokens_.back().type == TokenType::Dot ||
                                 tokens_.back().type == TokenType::QuestionDot)) {
            scan_member_index();
        } else {
            scan_number();
        }

        return;
    }

    // Identifiers and keywords
    if (identifier_char_width(/*allow_digit=*/false) > 0) {
        scan_identifier();

        return;
    }

    // A non-ASCII byte that cannot start an identifier: malformed UTF-8 or an
    // invisible Unicode space/control character.
    if (static_cast<unsigned char>(character) >= k_utf8_ascii_max) {
        scan_invalid_non_ascii(location);

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

std::size_t Lexer::identifier_char_width(bool allow_digit) const {
    if (is_at_end()) {
        return 0;
    }

    const char character = current();

    if (is_ascii_alpha(character) || (allow_digit && is_digit(character))) {
        return 1;
    }

    if (static_cast<unsigned char>(character) < k_utf8_ascii_max) {
        return 0;
    }

    const std::string_view source{cursor_.source()};
    const std::size_t position{cursor_.position()};
    const std::size_t length = well_formed_utf8_length(source, position);

    if (length == 0 ||
        is_unicode_space_or_control(decode_well_formed_utf8(source, position, length))) {
        return 0;
    }

    return length;
}

void Lexer::consume_identifier_chars() {
    for (auto width = identifier_char_width(/*allow_digit=*/true); width > 0;
         width = identifier_char_width(/*allow_digit=*/true)) {
        for (std::size_t i{0}; i < width; ++i) {
            advance();
        }
    }
}

void Lexer::scan_invalid_non_ascii(SourceLocation location) {
    const std::string_view source{cursor_.source()};
    const std::size_t start{cursor_.position()};
    const std::size_t length = well_formed_utf8_length(source, start);

    if (length > 0) {
        // Well-formed, but whitespace or a control character (e.g. U+00A0).
        const auto code_point =
            static_cast<std::uint32_t>(decode_well_formed_utf8(source, start, length));

        for (std::size_t i{0}; i < length; ++i) {
            advance();
        }

        emit_syntax_error(std::format("unexpected character U+{:04X}", code_point), location,
                          "invisible or non-breaking Unicode spaces are not allowed in code — "
                          "replace it with an ordinary space",
                          DiagnosticCode::UnexpectedToken);
    } else {
        // Malformed UTF-8: report the offending byte and any stray continuation
        // bytes after it as one error rather than one per byte.
        advance();

        while (!is_at_end() && is_utf8_continuation(static_cast<unsigned char>(current()))) {
            advance();
        }

        emit_syntax_error(
            std::format("invalid UTF-8 byte sequence '{}'",
                        sanitize_for_terminal(source.substr(start, cursor_.position() - start))),
            location, "save the file as UTF-8", DiagnosticCode::UnexpectedToken);
    }

    add_token(TokenType::Error, source.substr(start, cursor_.position() - start));
}

void Lexer::scan_identifier() {
    const std::size_t start{cursor_.position()};

    consume_identifier_chars();

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
        emit_warning(token_start_,
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
            emit_warning(token_start_, std::format("'{}' is not a Luma keyword", word),
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

    // First character must be a letter or underscore; subsequent characters
    // may also include digits, consistent with identifier rules.
    if (!is_at_end() && identifier_char_width(/*allow_digit=*/false) > 0) {
        consume_identifier_chars();
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
