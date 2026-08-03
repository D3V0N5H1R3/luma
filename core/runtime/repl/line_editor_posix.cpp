// POSIX terminal-control primitives for LineEditor.  Compiled only on
// non-Windows platforms (see core/runtime/CMakeLists.txt); the Windows
// counterpart lives in line_editor_win32.cpp.  Behaviour mirrors the inline
// #else branches that previously lived in line_editor.cpp.

#include <array>
#include <memory>

#include <termios.h>
#include <unistd.h>

#include "runtime/repl/line_editor.hpp"

namespace luma {

// Platform-specific saved terminal state for POSIX (termios), kept out of the
// header so <termios.h> does not leak to every LineEditor consumer.
struct TerminalGuard::Impl {
    struct termios saved_termios {};
};

TerminalGuard::TerminalGuard() : impl_{std::make_unique<Impl>()} {
    struct termios raw {};

    tcgetattr(STDIN_FILENO, &impl_->saved_termios);
    raw = impl_->saved_termios;
    raw.c_iflag &= ~static_cast<unsigned>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~static_cast<unsigned>(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~static_cast<unsigned>(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

TerminalGuard::~TerminalGuard() noexcept {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &impl_->saved_termios);
}

bool LineEditor::read_char(char& c) {
    return read(STDIN_FILENO, &c, 1) > 0;
}

namespace {

// Set stdin to timeout mode (VMIN=0, VTIME=1) so a lone Escape can be
// distinguished from the start of an escape sequence.  These are POSIX-only
// termios details (VMIN/VTIME), used solely by read_escape_sequence_bytes
// below, so they live here as file-local helpers rather than on the
// cross-platform LineEditor interface.
void set_timeout_mode() {
    struct termios mode {};

    tcgetattr(STDIN_FILENO, &mode);
    mode.c_cc[VMIN] = 0;
    mode.c_cc[VTIME] = 1; // 100 ms
    tcsetattr(STDIN_FILENO, TCSANOW, &mode);
}

// Restore stdin to blocking mode (VMIN=1, VTIME=0).
void set_blocking_mode() {
    struct termios mode {};

    tcgetattr(STDIN_FILENO, &mode);
    mode.c_cc[VMIN] = 1;
    mode.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &mode);
}

} // namespace

bool LineEditor::read_escape_sequence_bytes(std::array<char, 3>& seq) {
    // Use a short timeout to distinguish bare Escape from escape sequences
    // (e.g. ESC [ A for arrow keys).
    set_timeout_mode();

    if (read(STDIN_FILENO, &seq[0], 1) <= 0) {
        // Bare Escape — no follow-up byte within timeout.
        set_blocking_mode();
        return false;
    }

    if (seq[0] == '[') {
        if (read(STDIN_FILENO, &seq[1], 1) <= 0) {
            set_blocking_mode();
            return false;
        }
    }

    // Restore blocking mode for normal input.
    set_blocking_mode();
    return true;
}

bool LineEditor::stdin_is_terminal() {
    return isatty(STDIN_FILENO) != 0;
}

} // namespace luma
