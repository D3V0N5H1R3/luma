#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/lexer/lexer.hpp"
#include "common/resource_limits.hpp"
#include "common/string_utils.hpp"

namespace luma {

namespace {

constexpr std::string_view k_unterminated_string = "unterminated string";
constexpr std::string_view k_close_quote_hint = "add a closing '\"' to end the string";
constexpr std::string_view k_newline_in_string_hint =
    "single-line strings cannot contain newlines — "
    R"(use a triple-quoted string ("""...""") for multi-line text)";
constexpr std::string_view k_close_triple_quote_hint =
    R"(add closing '"""' to end the multi-line string)";
constexpr std::string_view k_interpolation_depth_exceeded =
    "maximum string interpolation nesting depth exceeded";
constexpr std::string_view k_interpolation_depth_hint =
    "reduce the nesting of interpolated expressions, or extract "
    "inner values into variables";

// ── Dedentation helpers ──
//
// Pure functions with no Lexer state dependency.

[[nodiscard]] constexpr bool is_space_or_tab(char character) {
    return character == ' ' || character == '\t';
}

[[nodiscard]] bool is_blank(std::string_view line) {
    return std::ranges::all_of(line, is_space_or_tab);
}

[[nodiscard]] int find_min_indent(const std::vector<std::string>& lines) {
    int min_indent{std::numeric_limits<int>::max()};

    for (const auto& line : lines) {
        if (is_blank(line)) {
            continue;
        }

        const auto first_non_space = std::ranges::find_if_not(line, is_space_or_tab);
        const int indent = static_cast<int>(first_non_space - line.begin());

        min_indent = std::min(min_indent, indent);
    }

    if (min_indent == std::numeric_limits<int>::max()) {
        return 0;
    }

    return min_indent;
}

[[nodiscard]] std::string rebuild_dedented(const std::vector<std::string>& lines, int min_indent) {
    std::string result{};
    bool first_line{true};

    for (const auto& line : lines) {
        if (!first_line) {
            result += '\n';
        }

        first_line = false;

        if (static_cast<int>(line.size()) > min_indent) {
            result += line.substr(static_cast<std::size_t>(min_indent));
        }
    }

    return result;
}

void trim_blank_lines(std::vector<std::string>& lines) {
    // Trim leading blank lines.
    {
        const auto it =
            std::ranges::find_if(lines, [](const std::string& line) { return !is_blank(line); });

        lines.erase(lines.begin(), it);
    }

    // Trim trailing blank lines.
    {
        const auto it = std::ranges::find_if(
            lines.rbegin(), lines.rend(), [](const std::string& line) { return !is_blank(line); });

        lines.erase(it.base(), lines.end());
    }
}

[[nodiscard]] std::string dedent(std::string_view text) {
    auto lines = split_lines(text);

    trim_blank_lines(lines);

    if (lines.empty()) {
        return "";
    }

    const int min_indent = find_min_indent(lines);

    return rebuild_dedented(lines, min_indent);
}

} // namespace

// ──────────── Interpolation helpers ────────────

bool Lexer::check_interpolation_depth() {
    if (static_cast<int>(interpolation_state_.size()) >= ResourceLimits::max_interpolation_depth) {
        emit_syntax_error(std::string{k_interpolation_depth_exceeded}, current_location(),
                          k_interpolation_depth_hint, DiagnosticCode::UnexpectedToken);
        return false;
    }
    return true;
}

void Lexer::begin_interpolation(bool is_triple_quoted) {
    // Enforce the nesting limit before emitting InterpolationStart or consuming
    // "${". If the token were added first, an over-limit level would leave a start
    // token with no matching state on interpolation_state_; a later '}' would then
    // close an outer level prematurely, desynchronising the token stream from the
    // state stack. Returning here leaves the cursor on '$' so the main loop
    // re-scans "${" as ordinary operators — the '{' still increments the enclosing
    // level's brace_depth, which the matching '}' decrements, so outer levels stay
    // balanced during error recovery.
    if (!check_interpolation_depth()) {
        return;
    }

    advance(); // $
    advance(); // {
    add_token(TokenType::InterpolationStart, "${");

    // Push an interpolation level; scan_token() pops it when the matching '}' arrives.
    interpolation_state_.push_back({.brace_depth = 1, .is_triple_quoted = is_triple_quoted});
}

// ──────────── String scanning ────────────

std::optional<char> Lexer::scan_escape_sequence() {
    advance(); // consume '\'

    if (is_at_end()) {
        emit_syntax_error(std::string{k_unterminated_string}, current_location(),
                          k_close_quote_hint, DiagnosticCode::UnterminatedString);
        return std::nullopt;
    }

    return scan_escape_char();
}

Lexer::StringScanResult Lexer::scan_interpolation(std::string text) {
    return {.text = std::move(text), .has_interpolation = true, .is_terminated = false};
}

Lexer::StringScanResult Lexer::scan_string_content() {
    std::string text{};

    while (!is_at_end() && current() != '"') {
        if (current() == '\\') {
            const auto escaped_char = scan_escape_sequence();
            if (!escaped_char.has_value()) {
                return {
                    .text = std::move(text), .has_interpolation = false, .is_terminated = false};
            }
            text += *escaped_char;
        } else if (current() == '$' && peek_next() == '{') {
            return scan_interpolation(std::move(text));
        } else if (current() == '\n') {
            emit_syntax_error(std::string{k_unterminated_string}, current_location(),
                              k_newline_in_string_hint, DiagnosticCode::UnterminatedString);
            return {.text = std::move(text), .has_interpolation = false, .is_terminated = false};
        } else {
            text += advance();
        }
    }

    if (is_at_end()) {
        emit_syntax_error(std::string{k_unterminated_string}, current_location(),
                          k_close_quote_hint, DiagnosticCode::UnterminatedString);
        return {.text = std::move(text), .has_interpolation = false, .is_terminated = false};
    }

    advance(); // closing "

    return {.text = std::move(text), .has_interpolation = false, .is_terminated = true};
}

void Lexer::add_terminal_string_token(bool is_continuation, const std::string& text) {
    if (is_continuation) {
        add_token(TokenType::StringEnd, text);
    } else {
        add_token(TokenType::StringLiteral, text, TokenLiteral{text});
    }
    // Record the true source start; the lexeme is processed (quotes stripped,
    // escapes resolved, triple-quoted body dedented) so it cannot be recovered
    // from the lexeme width.
    tokens_.back().start_location = string_segment_start_;
}

TokenType Lexer::triple_quoted_segment_open_type(bool is_continuation) const {
    // A continuation segment always resumes as StringMiddle.  A fresh segment
    // opens a new string (StringStart) unless it directly follows a closed
    // interpolation, in which case it is a middle segment.
    if (is_continuation) {
        return TokenType::StringMiddle;
    }

    if (tokens_.empty() || tokens_.back().type != TokenType::InterpolationEnd) {
        return TokenType::StringStart;
    }

    return TokenType::StringMiddle;
}

void Lexer::emit_string_tokens(bool is_continuation) {
    auto result = scan_string_content();

    if (result.has_interpolation) {
        const auto token_type = is_continuation ? TokenType::StringMiddle : TokenType::StringStart;
        add_token(token_type, result.text);
        tokens_.back().start_location = string_segment_start_;
        begin_interpolation(false);
    } else {
        // Both terminated and unterminated (error already emitted) paths produce
        // the same token structure — only simple vs interpolated strings differ.
        add_terminal_string_token(is_continuation, result.text);
    }
}

void Lexer::scan_string() {
    emit_string_tokens(/*is_continuation=*/false);
}

void Lexer::scan_string_continuation() {
    emit_string_tokens(/*is_continuation=*/true);
}

// ──────────── Triple-quoted strings ────────────

void Lexer::scan_triple_quoted_string() {
    scan_triple_quoted_string_body(/*is_continuation=*/false);
}

void Lexer::scan_triple_quoted_string_continuation() {
    scan_triple_quoted_string_body(/*is_continuation=*/true);
}

bool Lexer::try_consume_triple_quote(std::string& raw) {
    // We've already matched '"' followed by peek_next() == '"'.
    // Advance past the first '"' and check for the full closing '"""'.
    advance();

    if (is_at_end() || current() != '"') {
        raw += '"';
        return false;
    }

    advance();

    if (is_at_end() || current() != '"') {
        raw += "\"\"";
        return false;
    }

    advance();
    return true;
}

void Lexer::scan_triple_quoted_string_body(bool is_continuation) {
    std::string raw{};

    while (!is_at_end()) {
        if (current() == '"' && peek_next() == '"') {
            if (try_consume_triple_quote(raw)) {
                add_terminal_string_token(is_continuation, dedent(raw));

                return;
            }
        } else if (current() == '$' && peek_next() == '{') {
            add_token(triple_quoted_segment_open_type(is_continuation), dedent(raw));
            tokens_.back().start_location = string_segment_start_;

            raw.clear();

            begin_interpolation(true);

            return;
        } else {
            raw += advance();
        }
    }

    emit_syntax_error(std::string{k_unterminated_string}, current_location(),
                      k_close_triple_quote_hint, DiagnosticCode::UnterminatedString);

    // Add partial token for error recovery.
    add_terminal_string_token(is_continuation, dedent(raw));
}

// ──────────── Escape sequences ────────────

char Lexer::scan_escape_char() {
    const char character = advance();

    switch (character) {
        case 'n':
            return '\n';
        case 't':
            return '\t';
        case 'r':
            return '\r';
        case '0':
            return '\0';
        case '\\':
            return '\\';
        case '"':
            return '"';
        case '$':
            return '$';
        default:
            emit_syntax_error(std::string{"unknown escape sequence '\\"} + character + "'",
                              current_location(),
                              R"(valid escape sequences are \n, \t, \r, \0, \\, \" and \$)",
                              DiagnosticCode::UnexpectedToken);
            return '?'; // Recovery: treat the unknown escape as '?'.
    }
}

} // namespace luma
