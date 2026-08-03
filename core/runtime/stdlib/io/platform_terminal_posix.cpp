// POSIX implementations of the platform_terminal primitives.
// Compiled only on non-Windows platforms (see core/runtime/CMakeLists.txt).

#include <cstdlib>
#include <string_view>

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/stdlib/io/platform_terminal.hpp"

namespace luma::platform_terminal {

namespace {

// Terminal attributes saved by enter_raw_mode() and restored by leave_raw_mode().
termios original_termios{};

} // namespace

bool stdout_is_terminal() {
    return isatty(STDOUT_FILENO) != 0;
}

bool stdin_is_terminal() {
    return isatty(STDIN_FILENO) != 0;
}

void enable_vt_processing() {
    // POSIX terminals process ANSI sequences natively; nothing to enable.
}

void query_terminal_size(int& cols, int& rows) {
    struct winsize ws {};

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        cols = ws.ws_col;
        rows = ws.ws_row;
    }
}

void enter_raw_mode(const SourceLocation& loc) {
    if (!isatty(STDIN_FILENO)) {
        throw RuntimeError{"Terminal.enable_raw_mode: stdin is not a terminal", loc,
                           "stdin must be connected to a terminal device"};
    }

    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
        throw RuntimeError{"Terminal.enable_raw_mode: tcgetattr failed", loc,
                           "failed to read terminal attributes"};
    }

    struct termios raw {
        original_termios
    };

    raw.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        throw RuntimeError{"Terminal.enable_raw_mode: tcsetattr failed", loc,
                           "failed to apply terminal settings"};
    }
}

void leave_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

std::string_view enable_mouse() {
    // Enable SGR mouse reporting with button-event tracking
    // (press, release, and drag) plus SGR extended format.
    return "\033[?1000h\033[?1002h\033[?1006h";
}

std::string_view disable_mouse() {
    return "\033[?1006l\033[?1002l\033[?1000l";
}

bool supports_color() {
    // Check TERM and COLORTERM env vars.  std::getenv has no thread-safe std
    // alternative; these read at startup with no concurrent setenv, so it is safe.
    // NOLINTBEGIN(concurrency-mt-unsafe)
    const char* term = std::getenv("TERM");
    const char* colorterm = std::getenv("COLORTERM");
    // NOLINTEND(concurrency-mt-unsafe)

    if (colorterm != nullptr) {
        return true;
    }

    if (term != nullptr) {
        const std::string_view t{term};

        return t != "dumb";
    }

    return false;
}

bool supports_true_color() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): no thread-safe std alternative; read at startup only.
    const char* colorterm = std::getenv("COLORTERM");

    if (colorterm != nullptr) {
        const std::string_view ct{colorterm};

        return ct == "truecolor" || ct == "24bit";
    }

    return false;
}

} // namespace luma::platform_terminal
