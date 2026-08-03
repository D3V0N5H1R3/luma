#ifndef LUMA_LSP_LEXICAL_CONTEXT_HPP
#define LUMA_LSP_LEXICAL_CONTEXT_HPP

#include <cstddef>
#include <string>

namespace luma::lsp::lexical {

// ═══════════════════════════════════════════════════════════════════
// Lexical context tracking for raw (unparsed) Luma text.
//
// Several LSP features must reason about whether a character position is
// inside a string literal, a ${...} interpolation, or a line comment —
// while the buffer is still being typed and may not parse. Two access
// patterns are needed:
//
//   • A FORWARD line scanner (LineContext) used by the formatter, which
//     walks a single line left-to-right.
//   • CURSOR-RELATIVE backward primitives used by signature help, which
//     walk the buffer right-to-left from the cursor.
//
// They scan in opposite directions and have intentionally different edge
// handling (e.g. the forward scanner treats a single preceding backslash as
// an escape, while the backward scanner counts backslashes for parity), so
// they are not a single routine. They are co-located here so the
// string/comment/interpolation grammar lives in one place and any change to
// that grammar is made for both directions together.
// ═══════════════════════════════════════════════════════════════════

// ───────────────────────────────────────────────────────────
// Forward, single-line context tracking.
// Determines whether the current position is inside a string
// literal, interpolation expression, or comment — so that
// operator normalization only applies to actual code.
// ───────────────────────────────────────────────────────────

struct LineContext {
    bool in_string{false};
    int interpolation_depth{0}; // > 0 means inside ${...}

    // Returns true if the character at the current position was consumed
    // (string, comment, or interpolation boundary). When `append_rest` is
    // set on return, the caller should append the remainder of the line
    // and stop processing further characters.
    [[nodiscard]] bool update(const std::string& line, std::size_t i, std::string& out,
                              bool& append_rest) {
        const char c = line[i];
        append_rest = false;

        if (check_comment(c, i, line, out, append_rest)) {
            return true;
        }
        if (check_string(c, i, line, out)) {
            return true;
        }
        if (check_interpolation(c, i, line, out)) {
            return true;
        }
        return false;
    }

    // True when the current position is plain code (not inside a string
    // literal with interpolation_depth == 0).
    [[nodiscard]] bool is_code() const {
        return !in_string || interpolation_depth > 0;
    }

private:
    // If `c` opens a line comment outside a string, appends the rest of the
    // line to `out`, sets `append_rest`, and returns true.
    [[nodiscard]] bool check_comment(char c, std::size_t i, const std::string& line,
                                     std::string& out, bool& append_rest) const {
        if (!in_string && c == '#') {
            out.append(line, i, std::string::npos);
            append_rest = true;
            return true;
        }
        return false;
    }

    // If `c` is an unescaped string-delimiter outside an interpolation
    // expression, toggles `in_string`, appends `c`, and returns true.
    [[nodiscard]] bool check_string(char c, std::size_t i, const std::string& line,
                                    std::string& out) {
        if (c == '"' && (i == 0 || line[i - 1] != '\\') && interpolation_depth == 0) {
            in_string = !in_string;
            out += c;
            return true;
        }
        return false;
    }

    // Manages interpolation depth inside string literals (`${...}`).
    // Returns true if `c` was consumed as part of an interpolation boundary
    // or as plain string content outside an active interpolation expression.
    [[nodiscard]] bool check_interpolation(char c, std::size_t i, const std::string& line,
                                           std::string& out) {
        if (!in_string) {
            return false;
        }

        if (c == '$' && i + 1 < line.size() && line[i + 1] == '{') {
            ++interpolation_depth;
            out += c;
            return true;
        }

        if (interpolation_depth > 0) {
            if (c == '{') {
                ++interpolation_depth;
            } else if (c == '}') {
                --interpolation_depth;
                if (interpolation_depth == 0) {
                    out += c;
                    return true;
                }
            }
        }

        if (interpolation_depth == 0) {
            out += c;
            return true;
        }

        return false;
    }
};

// ───────────────────────────────────────────────────────────
// Cursor-relative (backward) scanning primitives.
// ───────────────────────────────────────────────────────────

// Count consecutive backslashes immediately before `pos` (down to `limit`).
// Used to determine whether a quote character is escaped.
[[nodiscard]] inline int count_preceding_backslashes(const std::string& text, std::size_t pos,
                                                     std::size_t limit) {
    int count = 0;
    std::size_t bp = pos;

    while (bp > limit && text[bp - 1] == '\\') {
        ++count;
        --bp;
    }

    return count;
}

// Skip forward past a triple-quoted string ("""), starting with `pos` pointing
// at the leftmost '"' of the opening triple-quote. Returns the index just past
// the closing triple-quote's last '"' (or `line_end` if unterminated on this
// line — triple-quoted strings spanning multiple lines are handled line by
// line by the caller's outer loop, which re-invokes this scanner per line).
[[nodiscard]] inline std::size_t
skip_triple_quoted_string_forward(const std::string& text, std::size_t pos, std::size_t line_end) {
    pos += 3; // consume the opening """

    while (pos + 2 < line_end) {
        if (text[pos] == '"' && text[pos + 1] == '"' && text[pos + 2] == '"') {
            return pos + 3; // consume the closing """
        }
        ++pos;
    }

    return line_end;
}

// Scan forward on a single line (from `line_begin` to `line_end`) to find
// the position of the first '#' that is not inside a string literal.
// Returns the index of '#', or `line_end` if none found.
[[nodiscard]] inline std::size_t
find_comment_start_on_line(const std::string& text, std::size_t line_begin, std::size_t line_end) {
    bool in_str = false;

    for (std::size_t i = line_begin; i < line_end; ++i) {
        if (text[i] == '"') {
            // A triple-quote sequence (""") delimits a triple-quoted string,
            // whose interior '"' characters must not toggle `in_str` one at a
            // time — that would misidentify code inside the string as
            // outside of it (and a '#' inside it as a comment start). Only
            // recognise this outside an already-open regular string.
            if (!in_str && i + 2 < line_end && text[i + 1] == '"' && text[i + 2] == '"') {
                i = skip_triple_quoted_string_forward(text, i, line_end) - 1;
                continue;
            }
            if (count_preceding_backslashes(text, i, line_begin) % 2 == 0) {
                in_str = !in_str;
            }
        } else if (text[i] == '#' && !in_str) {
            return i;
        }
    }

    return line_end;
}

// Skip backward past a triple-quoted string (""").
// `pos` must point to the leftmost '"' of the closing triple-quote.
// Returns the position of the opening triple-quote's first '"'.
[[nodiscard]] inline std::size_t skip_triple_quoted_string_backward(const std::string& text,
                                                                    std::size_t pos,
                                                                    std::size_t scan_start) {
    while (pos > scan_start) {
        --pos;

        if (text[pos] == '"' && pos + 2 < text.size() && text[pos + 1] == '"' &&
            text[pos + 2] == '"') {
            break; // opening triple-quote found
        }
    }

    return pos;
}

// Tracks interpolation depth changes for a character when scanning backward.
// Returns the new depth. Does not handle the '$' prefix skip — that side
// effect remains the caller's responsibility (check if new_depth == 0 and
// the prior character is '$').
[[nodiscard]] inline int track_interpolation_depth_backward(const std::string& line,
                                                            std::size_t pos, int current_depth) {
    if (line[pos] == '}' && current_depth == 0) {
        return 1;
    }
    if (current_depth > 0) {
        if (line[pos] == '}') {
            return current_depth + 1;
        }
        if (line[pos] == '{') {
            return current_depth - 1;
        }
    }
    return current_depth;
}

// Skip backward past a regular (single-delimited) string literal,
// handling string interpolation (${expr}) and escaped quotes.
// `pos` must point to the closing '"'.
// Returns the opening '"' position (or the position where scanning
// stopped, e.g. a newline).
[[nodiscard]] inline std::size_t
skip_regular_string_backward(const std::string& text, std::size_t pos, std::size_t scan_start) {
    int interp_depth{0};

    while (pos > scan_start) {
        --pos;

        if (text[pos] == '\n') {
            break; // strings do not span lines
        }

        const int new_depth = track_interpolation_depth_backward(text, pos, interp_depth);

        if (new_depth != interp_depth || interp_depth > 0) {
            // If we've closed the interpolation, skip the '$' prefix.
            if (new_depth == 0 && text[pos] == '{' && pos > scan_start && text[pos - 1] == '$') {
                --pos;
            }
            interp_depth = new_depth;
            continue;
        }

        if (text[pos] == '"') {
            if (count_preceding_backslashes(text, pos, scan_start) % 2 == 0) {
                break; // unescaped '"' — opening quote found
            }
        }
    }

    return pos;
}

// Skip backward past any string literal (triple-quoted or regular).
// `pos` must point to a closing '"' character.
// Returns the updated position at the opening quote (or where scanning stopped).
[[nodiscard]] inline std::size_t skip_string_backward(const std::string& text, std::size_t pos,
                                                      std::size_t scan_start) {
    if (pos >= 2 && text[pos - 1] == '"' && text[pos - 2] == '"') {
        // Triple-quoted string — consume the other two closing quotes.
        if (pos > scan_start) {
            --pos;
        }
        if (pos > scan_start) {
            --pos;
        }

        return skip_triple_quoted_string_backward(text, pos, scan_start);
    }

    return skip_regular_string_backward(text, pos, scan_start);
}

} // namespace luma::lsp::lexical

#endif // LUMA_LSP_LEXICAL_CONTEXT_HPP
