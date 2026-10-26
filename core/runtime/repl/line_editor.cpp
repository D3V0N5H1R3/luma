#include "runtime/repl/line_editor.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "common/string_utils.hpp"
#include "common/terminal.hpp"
#include "common/utf8.hpp"
#include "runtime/repl/repl_detail.hpp"

namespace luma {

// ANSI cursor/clear sequences are defined once in common/terminal.hpp
// (luma::term::ansi) and referenced here to keep a single source of truth.

namespace {

// Line terminator written while the terminal is in raw mode.  POSIX raw mode
// clears OPOST, so a bare "\n" moves down without returning to column 0 and
// successive lines "staircase"; the explicit "\r" is harmless on Windows.
constexpr std::string_view k_raw_newline = "\r\n";

} // namespace

// ─── compute_brace_depth_delta ───────────────────────────────────────────────

namespace repl_detail {

std::size_t next_char_boundary(std::string_view buf, std::size_t pos) noexcept {
    if (pos >= buf.size()) {
        return buf.size();
    }

    return pos + utf8_decode(buf, pos).length;
}

std::size_t prev_char_boundary(std::string_view buf, std::size_t pos) noexcept {
    if (pos == 0) {
        return 0;
    }

    pos = std::min(pos, buf.size());

    // Walk back over at most three continuation bytes to the lead byte, then
    // accept it only if it decodes to a character ending exactly at `pos`;
    // otherwise the previous byte is an ill-formed byte of its own.
    constexpr std::size_t k_max_continuation_bytes = 3;
    std::size_t start = pos - 1;

    while (start > 0 && pos - start <= k_max_continuation_bytes &&
           is_utf8_continuation(static_cast<unsigned char>(buf[start]))) {
        --start;
    }

    if (start + utf8_decode(buf, start).length == pos) {
        return start;
    }

    return pos - 1;
}

std::size_t display_width(std::string_view text) noexcept {
    return static_cast<std::size_t>(utf8_codepoint_count(text));
}

int compute_brace_depth_delta(std::string_view line) noexcept {
    try {
        DiagnosticCollector ignored;
        Lexer lexer{std::string{line}, ignored};
        const auto tokens = lexer.tokenize();

        int delta{0};

        for (const auto& token : tokens) {
            if (token.type == TokenType::LeftBrace) {
                ++delta;
            } else if (token.type == TokenType::RightBrace) {
                --delta;
            }
        }

        return delta;
    } catch (...) {
        return 0;
    }
}

std::vector<std::string> match_completions(const std::vector<std::string>& completions,
                                           std::string_view prefix) {
    std::vector<std::string> matches;

    if (prefix.empty()) {
        return matches;
    }

    // First pass: exact prefix match (highest priority).
    for (const auto& c : completions) {
        if (c.starts_with(prefix)) {
            matches.push_back(c);
        }
    }

    // Second pass: case-insensitive substring match (fuzzy).
    if (matches.empty()) {
        for (const auto& c : completions) {
            if (case_insensitive_contains(c, prefix)) {
                matches.push_back(c);
            }
        }
    }

    std::ranges::sort(matches);

    return matches;
}

} // namespace repl_detail

// ─── Line Editor Implementation ─────────────────────────────────────────────

void LineEditor::write_out(std::string_view s) {
    std::cout << s;
    std::cout.flush();
}

void LineEditor::write_out(char c) {
    std::cout << c;
    std::cout.flush();
}

void LineEditor::refresh_line(const std::string& prompt, const std::string& buf, std::size_t pos) {
    // Build the entire refresh sequence in a buffer to reduce flush calls.
    std::string output;
    output.reserve(prompt.size() + buf.size() + 32);

    output += term::ansi::clear_line;
    output += prompt;
    output += buf;

    // Move the cursor back over the text after `pos`.  The terminal moves one
    // column per character, so count codepoints rather than bytes — a
    // multi-byte character such as 'é' still occupies a single column.
    const auto move_back = repl_detail::display_width(std::string_view{buf}.substr(pos));

    if (move_back > 0) {
        output += term::ansi::csi;
        output += std::to_string(move_back);
        output += 'D';
    }

    write_out(output);
}

std::vector<std::string> LineEditor::find_completions(const std::string& prefix) const {
    return repl_detail::match_completions(completions_, prefix);
}

std::string LineEditor::format_completions_columns(const std::vector<std::string>& matches) {
    if (matches.empty()) {
        return {};
    }

    // Find the widest completion for column sizing.
    std::size_t max_width = 0;
    for (const auto& m : matches) {
        max_width = std::max(max_width, m.size());
    }

    constexpr std::size_t k_terminal_width = 80;
    constexpr std::size_t k_column_padding = 2;
    const auto col_width = max_width + k_column_padding;
    const auto num_cols = std::max<std::size_t>(1, k_terminal_width / col_width);

    std::ostringstream out;
    out << k_raw_newline;

    for (std::size_t i = 0; i < matches.size(); ++i) {
        out << "  ";
        out << matches[i];

        if ((i + 1) % num_cols == 0 || i + 1 == matches.size()) {
            out << k_raw_newline;
        } else {
            // Pad to column width.
            const auto padding = col_width - matches[i].size();
            out << std::string(padding, ' ');
        }
    }

    return out.str();
}

void LineEditor::handle_tab_completion(const std::string& prompt, std::string& buf,
                                       std::size_t& pos) {
    // Find the word being completed.
    if (pos == 0) {
        return; // Nothing to complete at the start of the line.
    }

    auto word_start = buf.rfind(' ', pos - 1);
    word_start = (word_start == std::string::npos) ? 0 : word_start + 1;

    if (word_start > pos) {
        return; // Guard against unsigned underflow.
    }

    const auto prefix = buf.substr(word_start, pos - word_start);
    const auto matches = find_completions(prefix);

    if (matches.size() == 1) {
        buf.replace(word_start, pos - word_start, matches[0]);
        pos = word_start + matches[0].size();
        refresh_line(prompt, buf, pos);
    } else if (matches.size() > 1) {
        write_out(format_completions_columns(matches));
        refresh_line(prompt, buf, pos);
    }
}

void LineEditor::handle_escape_sequence(const std::string& prompt, std::string& buf,
                                        std::size_t& pos, int& history_index,
                                        std::string& saved_line) {
    std::array<char, 3> seq{};

    if (!read_escape_sequence_bytes(seq)) {
        return;
    }

    if (seq[0] == '[') {
        switch (seq[1]) {
            case 'A': // Up arrow — previous history
                if (!history_.empty() && history_index > 0) {
                    if (history_index == static_cast<int>(history_.size())) {
                        saved_line = buf;
                    }

                    --history_index;
                    buf = history_[static_cast<std::size_t>(history_index)];
                    pos = buf.size();
                    refresh_line(prompt, buf, pos);
                }

                break;

            case 'B': // Down arrow — next history
                if (history_index < static_cast<int>(history_.size())) {
                    ++history_index;

                    if (history_index == static_cast<int>(history_.size())) {
                        buf = saved_line;
                    } else {
                        buf = history_[static_cast<std::size_t>(history_index)];
                    }

                    pos = buf.size();
                    refresh_line(prompt, buf, pos);
                }

                break;

            case 'C': // Right arrow — one character (codepoint), one column
                if (pos < buf.size()) {
                    pos = repl_detail::next_char_boundary(buf, pos);
                    write_out(std::string{term::ansi::csi} + "C");
                }

                break;

            case 'D': // Left arrow
                if (pos > 0) {
                    pos = repl_detail::prev_char_boundary(buf, pos);
                    write_out(std::string{term::ansi::csi} + "D");
                }

                break;

            case 'H': // Home
                pos = 0;
                refresh_line(prompt, buf, pos);
                break;

            case 'F': // End
                pos = buf.size();
                refresh_line(prompt, buf, pos);
                break;

            case '3': { // Delete key (ESC [ 3 ~)
                char tilde{};

                if (read_char(tilde) && tilde == '~' && pos < buf.size()) {
                    buf.erase(pos, repl_detail::next_char_boundary(buf, pos) - pos);
                    refresh_line(prompt, buf, pos);
                }

                break;
            }

            default:
                break;
        }
    }
}

void LineEditor::add_history(const std::string& line) {
    if (line.empty()) {
        return;
    }

    // Don't add duplicates of the last entry.
    if (!history_.empty() && history_.back() == line) {
        return;
    }

    history_.push_back(line);

    // Keep a reasonable history size.
    constexpr std::size_t k_max_history_size = 1000;
    if (history_.size() > k_max_history_size) {
        history_.erase(history_.begin());
    }
}

void LineEditor::set_completions(std::vector<std::string> completions) {
    completions_ = std::move(completions);
}

bool LineEditor::read_line(const std::string& prompt, std::string& result) {
    interrupted_ = false;

    // If stdin is not a terminal, fall back to std::getline.
    if (!stdin_is_terminal()) {
        write_out(prompt);

        return !!std::getline(std::cin, result);
    }

    // Enable raw mode via RAII — automatically restored on return or exception.
    const TerminalGuard guard{};

    std::string buf;
    std::size_t pos{0};
    int history_index = static_cast<int>(history_.size());
    std::string saved_line; // line being edited before history navigation

    write_out(prompt);

    while (true) {
        char c{};

        if (!read_char(c)) {
            write_out(k_raw_newline);
            return false;
        }

        switch (c) {
            case '\r': // Enter
            case '\n':
                write_out(k_raw_newline);
                result = buf;

                return true;

            case 4: // Ctrl+D (EOF)
                if (buf.empty()) {
                    write_out(k_raw_newline);

                    return false;
                }

                // Delete character at cursor (standard terminal behaviour).
                if (pos < buf.size()) {
                    buf.erase(pos, repl_detail::next_char_boundary(buf, pos) - pos);
                    refresh_line(prompt, buf, pos);
                }

                break;

            case 3: // Ctrl+C
                write_out("^C");
                write_out(k_raw_newline);
                interrupted_ = true;
                result.clear();

                return true;

            case 127: // Backspace (Unix)
            case 8:   // Backspace (Windows)
                if (pos > 0) {
                    // Remove the whole previous character, not just its last
                    // byte, so the buffer never holds a partial UTF-8 sequence.
                    const auto start = repl_detail::prev_char_boundary(buf, pos);
                    buf.erase(start, pos - start);
                    pos = start;
                    refresh_line(prompt, buf, pos);
                }

                break;

            case 1: // Ctrl+A — move to beginning
                pos = 0;
                refresh_line(prompt, buf, pos);
                break;

            case 5: // Ctrl+E — move to end
                pos = buf.size();
                refresh_line(prompt, buf, pos);
                break;

            case 11: // Ctrl+K — kill to end of line
                buf.erase(pos);
                refresh_line(prompt, buf, pos);
                break;

            case 21: // Ctrl+U — kill to beginning of line
                buf.erase(0, pos);
                pos = 0;
                refresh_line(prompt, buf, pos);
                break;

            case 12: // Ctrl+L — clear screen
                write_out(term::ansi::clear_screen_home);
                refresh_line(prompt, buf, pos);
                break;

            case '\t': // Tab completion
                handle_tab_completion(prompt, buf, pos);
                break;

            case 27: // Escape sequence
                handle_escape_sequence(prompt, buf, pos, history_index, saved_line);
                break;

            default:
                // Regular printable character.  `c` is a (possibly signed)
                // char; cast to unsigned so UTF-8 lead/continuation bytes
                // (>= 0x80, negative when char is signed) are inserted rather
                // than silently dropped by the `>= 32` test.
                if (static_cast<unsigned char>(c) >= 32) {
                    // A multi-byte character arrives as consecutive bytes;
                    // read the rest of it now so the buffer (and the cursor
                    // position) only ever moves by whole characters.
                    std::string ch(1, c);
                    const auto expected =
                        static_cast<std::size_t>(utf8_codepoint_len(static_cast<std::uint8_t>(c)));
                    char next{};

                    while (ch.size() < expected && read_char(next)) {
                        ch += next;
                    }

                    buf.insert(pos, ch);
                    pos += ch.size();

                    if (pos == buf.size()) {
                        write_out(ch);
                    } else {
                        refresh_line(prompt, buf, pos);
                    }
                }

                break;
        }
    }
}

} // namespace luma
