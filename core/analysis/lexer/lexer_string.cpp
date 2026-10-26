#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/diagnostics/display_text.hpp"
#include "analysis/lexer/lexer.hpp"
#include "common/resource_limits.hpp"

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

// ── Triple-quoted dedentation ──
//
// Pure functions with no Lexer state dependency.  A triple-quoted literal is
// the sequence of raw text pieces between its `"""`, `${…}` and `"""`
// delimiters.  Dedentation treats the pieces as ONE text in which every
// interpolation is opaque, non-blank content, so:
//   * the common indentation is computed once, across all lines of all pieces;
//   * a line that starts with (or contains) an interpolation is never blank;
//   * whitespace between interpolations on the same line is content, not
//     indentation, and is preserved;
//   * only the literal's first line (right after the opening quotes) and its
//     last line (right before the closing quotes) are dropped, and only when
//     they are blank.

[[nodiscard]] constexpr bool is_space_or_tab(char character) {
    return character == ' ' || character == '\t';
}

// One line of the combined text: [begin, end) excludes the '\n' (and a '\r'
// directly before it).  `indent_end` bounds the indentation scan — it stops at
// the first interpolation on the line.
struct DedentLine {
    std::size_t begin{0};
    std::size_t end{0};
    std::size_t indent{0};
    bool is_blank{true};
};

[[nodiscard]] std::vector<std::string> dedent_pieces(const std::vector<std::string_view>& pieces) {
    // Concatenate the pieces; an interpolation sits at each interior boundary.
    std::string combined;
    std::vector<std::size_t> piece_starts;
    piece_starts.reserve(pieces.size() + 1);

    for (const auto piece : pieces) {
        piece_starts.push_back(combined.size());
        combined.append(piece);
    }

    piece_starts.push_back(combined.size());

    // Interpolation positions: the starts of every piece after the first.
    const std::vector<std::size_t> interpolations(piece_starts.begin() + 1, piece_starts.end() - 1);

    // First interpolation at or after `position`, or npos.
    const auto next_interpolation = [&interpolations](std::size_t position) {
        const auto it = std::ranges::lower_bound(interpolations, position);
        return it == interpolations.end() ? std::string::npos : *it;
    };

    std::vector<bool> keep(combined.size(), true);
    std::vector<DedentLine> lines;

    for (std::size_t begin{0};;) {
        const std::size_t newline = combined.find('\n', begin);
        const std::size_t line_end = (newline == std::string::npos) ? combined.size() : newline;

        DedentLine line{.begin = begin, .end = line_end};

        // Normalise CRLF: the '\r' before a newline is never content.
        if (newline != std::string::npos && line.end > line.begin &&
            combined[line.end - 1] == '\r') {
            keep[line.end - 1] = false;
            --line.end;
        }

        const std::size_t interpolation = next_interpolation(line.begin);
        const bool has_interpolation =
            interpolation != std::string::npos && interpolation <= line_end;
        const std::size_t indent_end = has_interpolation ? interpolation : line.end;

        while (line.begin + line.indent < indent_end &&
               is_space_or_tab(combined[line.begin + line.indent])) {
            ++line.indent;
        }

        line.is_blank = !has_interpolation && line.begin + line.indent == line.end;
        lines.push_back(line);

        if (newline == std::string::npos) {
            break;
        }

        begin = newline + 1;
    }

    // Drop a blank first line (with its newline) and a blank last line (with
    // the newline before it).
    std::size_t first_kept{0};
    std::size_t last_kept{lines.size()};

    if (lines.front().is_blank) {
        const std::size_t drop_end = std::min(lines.front().end + 1, combined.size());
        std::fill(keep.begin(), keep.begin() + static_cast<std::ptrdiff_t>(drop_end), false);
        first_kept = 1;
    }

    if (lines.size() > 1 && lines.back().is_blank) {
        const std::size_t drop_begin = lines.back().begin - 1; // the preceding '\n'
        std::fill(keep.begin() + static_cast<std::ptrdiff_t>(drop_begin), keep.end(), false);
        last_kept = lines.size() - 1;
    }

    std::size_t common_indent{std::numeric_limits<std::size_t>::max()};

    for (std::size_t index{first_kept}; index < last_kept; ++index) {
        if (!lines[index].is_blank) {
            common_indent = std::min(common_indent, lines[index].indent);
        }
    }

    if (common_indent == std::numeric_limits<std::size_t>::max()) {
        common_indent = 0;
    }

    // Strip the common indentation from every kept line; blank lines lose up
    // to that much of their whitespace.
    for (std::size_t index{first_kept}; index < last_kept; ++index) {
        const auto& line = lines[index];
        const std::size_t strip = std::min(common_indent, line.indent);

        std::fill(keep.begin() + static_cast<std::ptrdiff_t>(line.begin),
                  keep.begin() + static_cast<std::ptrdiff_t>(line.begin + strip), false);
    }

    std::vector<std::string> result(pieces.size());

    for (std::size_t piece{0}; piece < pieces.size(); ++piece) {
        for (std::size_t position{piece_starts[piece]}; position < piece_starts[piece + 1];
             ++position) {
            if (keep[position]) {
                result[piece] += combined[position];
            }
        }
    }

    return result;
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

bool Lexer::begin_interpolation(bool is_triple_quoted) {
    // Enforce the nesting limit before emitting InterpolationStart or consuming
    // "${". If the token were added first, an over-limit level would leave a start
    // token with no matching state on interpolation_state_; a later '}' would then
    // close an outer level prematurely, desynchronising the token stream from the
    // state stack. Returning here leaves the cursor on '$' so the main loop
    // re-scans "${" as ordinary operators — the '{' still increments the enclosing
    // level's brace_depth, which the matching '}' decrements, so outer levels stay
    // balanced during error recovery.
    if (!check_interpolation_depth()) {
        return false;
    }

    token_start_ = current_location();

    advance(); // $
    advance(); // {
    add_token(TokenType::InterpolationStart, "${");

    // Push an interpolation level; scan_token() pops it when the matching '}' arrives.
    interpolation_state_.push_back({.brace_depth = 1, .is_triple_quoted = is_triple_quoted});

    return true;
}

// ──────────── String scanning ────────────

std::optional<char> Lexer::scan_escape_sequence() {
    advance(); // consume '\'

    if (is_at_end()) {
        emit_syntax_error(std::string{k_unterminated_string}, string_segment_start_,
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
            emit_syntax_error(std::string{k_unterminated_string}, string_segment_start_,
                              k_newline_in_string_hint, DiagnosticCode::UnterminatedString);
            return {.text = std::move(text), .has_interpolation = false, .is_terminated = false};
        } else {
            text += advance();
        }
    }

    if (is_at_end()) {
        emit_syntax_error(std::string{k_unterminated_string}, string_segment_start_,
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
        (void)begin_interpolation(false);
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

void Lexer::record_triple_quoted_piece(std::string raw) {
    if (open_triple_quoted_.empty()) {
        open_triple_quoted_.emplace_back();
    }

    open_triple_quoted_.back().push_back(
        {.token_index = tokens_.size() - 1, .raw = std::move(raw)});
}

void Lexer::finish_triple_quoted_string() {
    const auto pieces = std::move(open_triple_quoted_.back());
    open_triple_quoted_.pop_back();

    if (pieces.empty()) {
        return;
    }

    std::vector<std::string_view> raw_pieces;
    raw_pieces.reserve(pieces.size());

    for (const auto& piece : pieces) {
        raw_pieces.emplace_back(piece.raw);
    }

    const auto dedented = dedent_pieces(raw_pieces);

    for (std::size_t index{0}; index < pieces.size(); ++index) {
        // Escapes are resolved only after dedenting, so an escaped tab or
        // newline (`\t`, `\n`) is content, never indentation or a line break.
        // Unknown escapes were already diagnosed while scanning.
        std::string text;
        const std::string_view raw{dedented[index]};

        for (std::size_t position{0}; position < raw.size(); ++position) {
            if (raw[position] == '\\' && position + 1 < raw.size()) {
                ++position;
                text += escape_value(raw[position]).value_or('?');
            } else if (raw[position] != '\\') {
                text += raw[position];
            }
        }

        auto& token = tokens_[pieces[index].token_index];

        if (token.type == TokenType::StringLiteral) {
            token.literal = TokenLiteral{text};
        }

        token.lexeme = std::move(text);
    }
}

void Lexer::scan_triple_quoted_string_body(bool is_continuation) {
    if (!is_continuation) {
        open_triple_quoted_.emplace_back();
    }

    // The piece's raw source text; escape sequences stay unresolved until the
    // whole literal has been dedented (see finish_triple_quoted_string).
    std::string raw{};

    while (!is_at_end()) {
        if (current() == '"' && peek_next() == '"') {
            if (try_consume_triple_quote(raw)) {
                add_terminal_string_token(is_continuation, {});
                record_triple_quoted_piece(std::move(raw));
                finish_triple_quoted_string();

                return;
            }
        } else if (current() == '\\') {
            // Escapes work as in ordinary strings: `\$` suppresses
            // interpolation and `\"` cannot close the literal.  Validate the
            // escape now (so errors point at it) but keep the raw text.
            raw += advance();

            if (is_at_end()) {
                break;
            }

            raw += current();
            (void)scan_escape_char();
        } else if (current() == '$' && peek_next() == '{') {
            add_token(triple_quoted_segment_open_type(is_continuation), {});
            tokens_.back().start_location = string_segment_start_;
            record_triple_quoted_piece(std::move(raw));

            if (!begin_interpolation(true)) {
                // Nesting limit hit: the literal cannot resume after this point.
                finish_triple_quoted_string();
            }

            return;
        } else {
            raw += advance();
        }
    }

    emit_syntax_error(std::string{k_unterminated_string}, string_segment_start_,
                      k_close_triple_quote_hint, DiagnosticCode::UnterminatedString);

    // Add partial token for error recovery.
    add_terminal_string_token(is_continuation, {});
    record_triple_quoted_piece(std::move(raw));
    finish_triple_quoted_string();
}

// ──────────── Escape sequences ────────────

std::optional<char> Lexer::escape_value(char character) noexcept {
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
            return std::nullopt;
    }
}

char Lexer::scan_escape_char() {
    // The escape's backslash sits one column before the cursor.
    auto backslash_location = current_location();
    backslash_location.column = std::max(1, backslash_location.column - 1);

    const char character = advance();

    if (const auto value = escape_value(character)) {
        return *value;
    }

    emit_syntax_error(std::string{"unknown escape sequence '\\"} +
                          sanitize_for_terminal(std::string_view{&character, 1}) + "'",
                      backslash_location,
                      R"(valid escape sequences are \n, \t, \r, \0, \\, \" and \$)",
                      DiagnosticCode::UnexpectedToken);

    return '?'; // Recovery: treat the unknown escape as '?'.
}

} // namespace luma
