#ifndef LUMA_REPL_LINE_EDITOR_HPP
#define LUMA_REPL_LINE_EDITOR_HPP

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace luma {

// Provides line editing with history, cursor movement, and tab completion.
// Extracted to its own header for reuse by other components (e.g. debugger console).
class LineEditor {
public:
    // Read a line of input with editing support.
    // Returns false on EOF (Ctrl+D) or if input was interrupted.
    [[nodiscard]] bool read_line(const std::string& prompt, std::string& result);

    // Add a line to the history.
    void add_history(const std::string& line);

    // Set the list of completions for tab completion.
    void set_completions(std::vector<std::string> completions);

    // Was the last read interrupted by Ctrl+C?
    [[nodiscard]] bool was_interrupted() const noexcept {
        return interrupted_;
    }

    // Read-only view of the input history (oldest first, newest last).
    // Exposed for unit testing of the history-management logic.
    [[nodiscard]] const std::vector<std::string>& history() const noexcept {
        return history_;
    }

private:
    // Write a string to stdout without buffering.
    static void write_out(std::string_view s);
    static void write_out(char c);

    // Refresh the displayed line.
    void refresh_line(const std::string& prompt, const std::string& buf, std::size_t pos);

    // Find completions matching the current prefix.
    [[nodiscard]] std::vector<std::string> find_completions(const std::string& prefix) const;

    // Handle escape/arrow key sequences (cursor movement, history navigation).
    void handle_escape_sequence(const std::string& prompt, std::string& buf, std::size_t& pos,
                                int& history_index, std::string& saved_line);

    // Handle tab key for completion.
    void handle_tab_completion(const std::string& prompt, std::string& buf, std::size_t& pos);

    // ─── Platform abstraction ───

    // Read a single character from stdin in raw mode.
    // Returns false on EOF or read error.
    static bool read_char(char& c);

    // Read up to two bytes of an escape sequence into seq (after the leading
    // ESC).  Returns false when the sequence is incomplete (e.g. a bare Escape),
    // signalling the caller to stop.  Platform-specific.
    static bool read_escape_sequence_bytes(std::array<char, 3>& seq);

    // True when stdin is connected to an interactive terminal.
    [[nodiscard]] static bool stdin_is_terminal();

    // Format completions into columns for display.
    static std::string format_completions_columns(const std::vector<std::string>& matches);

    std::vector<std::string> history_;
    std::vector<std::string> completions_;
    bool interrupted_{false};
};

// RAII guard to save and restore terminal mode on any exit path.
class TerminalGuard {
public:
    TerminalGuard();
    ~TerminalGuard() noexcept;

    TerminalGuard(const TerminalGuard&) = delete;
    TerminalGuard& operator=(const TerminalGuard&) = delete;

private:
    // Platform-specific saved terminal state.  Defined in line_editor_posix.cpp
    // and line_editor_win32.cpp so <termios.h> / <windows.h> stay out of this
    // widely-included header, mirroring how cli/terminal.hpp keeps <windows.h>
    // confined to its .cpp.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace luma

#endif // LUMA_REPL_LINE_EDITOR_HPP
