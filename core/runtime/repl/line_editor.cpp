#include "runtime/repl/line_editor.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "common/string_utils.hpp"
#include "runtime/cli/terminal.hpp"
#include "runtime/repl/repl_detail.hpp"

namespace luma {

// ANSI cursor/clear sequences are defined once in runtime/cli/terminal.hpp
// (luma::term::ansi) and referenced here to keep a single source of truth.

// ─── compute_brace_depth_delta ───────────────────────────────────────────────

namespace repl_detail {

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

    // Move cursor to correct position.
    if (pos < buf.size()) {
        const auto move_back = buf.size() - pos;
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
    out << '\n';

    for (std::size_t i = 0; i < matches.size(); ++i) {
        out << "  ";
        out << matches[i];

        if ((i + 1) % num_cols == 0 || i + 1 == matches.size()) {
            out << '\n';
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

            case 'C': // Right arrow
                if (pos < buf.size()) {
                    ++pos;
                    write_out(std::string{term::ansi::csi} + "C");
                }

                break;

            case 'D': // Left arrow
                if (pos > 0) {
                    --pos;
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
                read_char(tilde);

                if (tilde == '~' && pos < buf.size()) {
                    buf.erase(pos, 1);
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
            write_out("\n");
            return false;
        }

        switch (c) {
            case '\r': // Enter
            case '\n':
                write_out("\n");
                result = buf;

                return true;

            case 4: // Ctrl+D (EOF)
                if (buf.empty()) {
                    write_out("\n");

                    return false;
                }

                // Delete character at cursor (standard terminal behaviour).
                if (pos < buf.size()) {
                    buf.erase(pos, 1);
                    refresh_line(prompt, buf, pos);
                }

                break;

            case 3: // Ctrl+C
                write_out("^C\n");
                interrupted_ = true;
                result.clear();

                return true;

            case 127: // Backspace (Unix)
            case 8:   // Backspace (Windows)
                if (pos > 0) {
                    buf.erase(pos - 1, 1);
                    --pos;
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
                    buf.insert(pos, 1, c);
                    ++pos;

                    if (pos == buf.size()) {
                        write_out(c);
                    } else {
                        refresh_line(prompt, buf, pos);
                    }
                }

                break;
        }
    }
}

} // namespace luma
