// ─────────────────────────────────────────────────────────────────────────────
// Lexer Module
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Tokenize Luma source code into a stream of tokens.
//
// Key Types:
//   - Token: A single lexical unit with location information.
//   - Lexer: Main lexer class that produces tokens from source text.
//
// Dependencies:
//   - analysis/diagnostics: For collecting lexer diagnostics.
//   - analysis/source: For tracking source locations.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_LEXER_LEXER_HPP
#define LUMA_LEXER_LEXER_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/diagnostics/diagnostic_emitter.hpp"
#include "analysis/lexer/lexer_cursor.hpp"
#include "analysis/lexer/token.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"
#include "common/utf8.hpp"

namespace luma {

class Lexer : public DiagnosticEmitter {
public:
    explicit Lexer(std::string source, DiagnosticCollector& diagnostics, FileId file_id = 0)
        : DiagnosticEmitter(DiagnosticCategory::Syntax, DiagnosticSource::Syntax),
          collector_{diagnostics},
          file_id_{file_id},
          cursor_{std::move(source)} {}

    [[nodiscard]] std::vector<Token> tokenize();

private:
    [[nodiscard]] bool is_at_end() const;
    [[nodiscard]] char current() const;
    [[nodiscard]] char peek_next() const;
    char advance();
    [[nodiscard]] bool match(char expected);
    [[nodiscard]] SourceLocation current_location() const;
    void add_token(TokenType type, std::string_view lexeme);
    void add_token(TokenType type, std::string_view lexeme, TokenLiteral literal);
    void skip_whitespace_and_comments();
    void scan_token();
    // Attempt to close an interpolation when '}' is encountered inside
    // an interpolation context.  Returns true if the brace closed an
    // interpolation (and string scanning has resumed), false if the brace
    // is a normal expression-level brace.
    [[nodiscard]] bool try_close_interpolation();
    void scan_operator(char character, SourceLocation location);

    // Helpers for common operator patterns in scan_operator.
    void emit_compound_assign(TokenType single, std::string_view single_lex, TokenType compound,
                              std::string_view compound_lex);
    void emit_double_then_compound(char second, TokenType single, std::string_view single_lex,
                                   TokenType doubled, std::string_view doubled_lex,
                                   TokenType assign, std::string_view assign_lex);

    // Table-driven matchers for regular-structure operators (true if handled).
    [[nodiscard]] bool try_scan_compound_assign(char character);
    [[nodiscard]] bool try_scan_double_or_assign(char character);

    // Multi-character operator emitters extracted from scan_operator.
    void emit_minus_op();
    void emit_less_op();
    void emit_greater_op();
    void emit_pipe_op();
    void emit_question_op();
    void emit_dot_op();

    // ASCII-only digit check (0–9).  Unicode digits (e.g. Arabic-Indic)
    // are intentionally excluded — Luma numeric literals use ASCII only.
    [[nodiscard]] static constexpr bool is_digit(char character) noexcept {
        return character >= '0' && character <= '9';
    }

    // ASCII letter or underscore, plus any non-ASCII UTF-8 byte
    // (>= k_utf8_ascii_max), so Unicode letters are accepted in identifiers.
    [[nodiscard]] static constexpr bool is_alpha(char character) noexcept {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               character == '_' || static_cast<unsigned char>(character) >= k_utf8_ascii_max;
    }

    // ASCII-only alphanumeric check, extended with is_alpha's Unicode
    // multibyte acceptance.
    [[nodiscard]] static constexpr bool is_alnum(char character) noexcept {
        return is_digit(character) || is_alpha(character);
    }

    [[nodiscard]] bool is_prefixed_literal(char prefix_char) const;

    void scan_number();
    void scan_prefixed_literal(bool (*is_valid_digit)(char), int base, std::string_view kind_name,
                               std::string_view hint_example, std::size_t start);
    [[nodiscard]] bool has_valid_exponent() const;
    void consume_exponent();

    // Parse an integer literal, emitting a diagnostic on failure and returning
    // a sentinel value (0) so lexing can continue.
    [[nodiscard]] TokenLiteral parse_integer_literal(const std::string& lexeme, int base = 10);
    // Parse a floating-point literal, emitting a diagnostic on failure and
    // returning a sentinel value (0.0) so lexing can continue.
    [[nodiscard]] TokenLiteral parse_float_literal(const std::string& lexeme);

    void scan_identifier();
    void scan_annotation();
    [[nodiscard]] static TokenType keyword_type(std::string_view word);

    struct StringScanResult {
        std::string text;
        bool has_interpolation{false}; // true if ended with ${
        bool is_terminated{false};     // true if ended with closing quote
    };

    [[nodiscard]] StringScanResult scan_string_content();
    [[nodiscard]] std::optional<char> scan_escape_sequence();
    [[nodiscard]] StringScanResult scan_interpolation(std::string text);
    void emit_string_tokens(bool is_continuation);
    // Emit the closing token of a string literal: StringEnd for an interpolation
    // continuation, otherwise a standalone StringLiteral carrying `text` as its
    // literal payload.  Shared by single-line and triple-quoted string scanning.
    void add_terminal_string_token(bool is_continuation, const std::string& text);
    // Choose the opening token for the text segment that precedes a "${"
    // interpolation inside a triple-quoted string.
    [[nodiscard]] TokenType triple_quoted_segment_open_type(bool is_continuation) const;
    void scan_string();
    void scan_string_continuation();
    void scan_triple_quoted_string();
    void scan_triple_quoted_string_continuation();
    void scan_triple_quoted_string_body(bool is_continuation);
    [[nodiscard]] char scan_escape_char();

    // Triple-quoted string helpers to reduce nesting depth.
    [[nodiscard]] bool try_consume_triple_quote(std::string& raw);

    // Shared helper: emit a syntax-category error diagnostic.
    // Reorders parameters relative to emit_error for caller convenience
    // (message first, location second).
    void emit_syntax_error(std::string message, SourceLocation loc, std::string_view hint = {},
                           DiagnosticCode code = DiagnosticCode::None);

    // Shared helper: emit interpolation start tokens and push interpolation state.
    void begin_interpolation(bool is_triple_quoted);
    // Shared helper: emit a diagnostic if interpolation depth is exceeded.
    // Returns false and emits a diagnostic if the limit is reached.
    [[nodiscard]] bool check_interpolation_depth();

    DiagnosticCollector& collector_;
    FileId file_id_{0};
    std::vector<Token> tokens_;

    // Source position (1-based) of the first character of the string segment
    // currently being scanned.  String tokens store a PROCESSED lexeme, so their
    // true source start cannot be reconstructed from the lexeme; the string
    // scanners stamp this onto each emitted string token's `start_location`.
    // Set at the opening quote (fresh string) or immediately after a closing
    // interpolation brace (continuation segment).
    SourceLocation string_segment_start_{};

    // Source buffer + scan position (byte offset, line, column) and the
    // low-level character-stream primitives.  The Lexer's is_at_end/current/
    // peek_next/advance/match/current_location methods delegate here.
    LexerCursor cursor_;

    // ─── String interpolation state machine ──────────────────────────────────
    //
    // The lexer operates in two alternating modes: string mode and expression
    // mode.  A stack of InterpolationState entries tracks active interpolations,
    // with the innermost (most-recently-entered) at the back.
    //
    // Transitions:
    //   String mode  → Expression mode:
    //     Triggered by "${" inside a string literal.
    //     begin_interpolation() pushes a new InterpolationState{brace_depth=1}
    //     onto interpolation_state_ and emits InterpolationStart.  The lexer
    //     then returns to the main scan loop in expression mode.
    //
    //   Expression mode → String mode:
    //     scan_token() decrements brace_depth on every '}' while inside an
    //     interpolation.  When brace_depth reaches 0 the entry is popped,
    //     InterpolationEnd is emitted, and scanning resumes in the string
    //     that was interrupted (single- or triple-quoted, per is_triple_quoted).
    //
    //   Nested interpolations (e.g. "${f("${x}")}"):
    //     Each "${" encountered during expression mode pushes another entry.
    //     Each matched '}' pops only the innermost entry when its depth hits 0,
    //     so nesting unwinds one level at a time.  Non-interpolation '{' and '}'
    //     inside expressions increment/decrement brace_depth without popping.
    //
    // Entry limit:
    //     check_interpolation_depth() enforces ResourceLimits::max_interpolation_depth
    //     and emits a diagnostic if exceeded.
    struct InterpolationState {
        int brace_depth{0};
        bool is_triple_quoted{false};
    };

    std::vector<InterpolationState> interpolation_state_;
};

} // namespace luma

#endif // LUMA_LEXER_LEXER_HPP
