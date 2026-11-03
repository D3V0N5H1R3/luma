#ifndef LUMA_LSP_LEXICAL_CONTEXT_HPP
#define LUMA_LSP_LEXICAL_CONTEXT_HPP

#include <cstddef>
#include <string>
#include <vector>

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
// They scan in opposite directions, so they are not a single routine. Both
// decide whether a quote is escaped by backslash parity
// (count_preceding_backslashes). They are co-located here so the
// string/comment/interpolation grammar lives in one place and any change to
// that grammar is made for both directions together.
// ═══════════════════════════════════════════════════════════════════

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

// True when the character at `pos` is escaped by an odd run of backslashes.
[[nodiscard]] inline bool is_escaped(const std::string& text, std::size_t pos) {
    return count_preceding_backslashes(text, pos, 0) % 2 != 0;
}

// ───────────────────────────────────────────────────────────
// Forward, single-line context tracking.
// Determines whether the current position is inside a string
// literal, interpolation expression, or comment — so that
// operator normalization only applies to actual code.
// ───────────────────────────────────────────────────────────

struct LineContext {
    // Feeds the character at `line[i]` to the scanner. Returns true when the
    // character belongs to a string literal or a ${...} interpolation (or opens
    // a comment) and has been appended to `out` verbatim; returns false for a
    // plain code character, which the caller handles itself. When
    // `append_rest` is set on return, a line comment started at `i`: the rest
    // of the line has been appended and the caller should stop.
    [[nodiscard]] bool update(const std::string& line, std::size_t i, std::string& out,
                              bool& append_rest) {
        const char c = line[i];
        append_rest = false;

        if (modes_.empty()) {
            if (c == '#') {
                out.append(line, i, std::string::npos);
                append_rest = true;
                return true;
            }
            if (c == '"') {
                modes_.push_back(k_string_mode);
                out += c;
                return true;
            }
            return false;
        }

        out += c;

        if (modes_.back() == k_string_mode) {
            if (c == '"' && !is_escaped(line, i)) {
                modes_.pop_back();
            } else if (c == '$' && i + 1 < line.size() && line[i + 1] == '{' &&
                       !is_escaped(line, i)) {
                // The `{` of `${` is consumed by the next call, so it must not
                // also count as a nested brace inside the interpolation.
                modes_.push_back(k_interpolation_opening);
            }
            return true;
        }

        // Inside ${...}: track nested braces and nested string literals.
        int& depth = modes_.back();
        if (depth == k_interpolation_opening) {
            depth = 0;
        } else if (c == '"') {
            modes_.push_back(k_string_mode);
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (depth == 0) {
                modes_.pop_back();
            } else {
                --depth;
            }
        }
        return true;
    }

    // True inside a string literal or a ${...} interpolation within one.
    [[nodiscard]] bool in_string() const {
        return !modes_.empty();
    }

    // True when the current position is code: outside any string literal, or
    // inside a ${...} interpolation expression.
    [[nodiscard]] bool is_code() const {
        return modes_.empty() || modes_.back() != k_string_mode;
    }

private:
    // Stack of open lexical modes: k_string_mode for a string literal, and a
    // non-negative nested-brace depth (or k_interpolation_opening right after
    // `$`) for a ${...} interpolation.
    static constexpr int k_string_mode = -1;
    static constexpr int k_interpolation_opening = -2;
    std::vector<int> modes_;
};

// ───────────────────────────────────────────────────────────
// Cursor-relative (backward) scanning primitives.
// ───────────────────────────────────────────────────────────

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
