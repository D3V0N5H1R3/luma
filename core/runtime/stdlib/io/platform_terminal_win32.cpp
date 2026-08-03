// Windows implementations of the platform_terminal primitives.
// Compiled only on Windows (see core/runtime/CMakeLists.txt).

#include <cstdio>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/stdlib/io/platform_terminal.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <windows.h>

namespace luma::platform_terminal {

namespace {

// Console modes saved by enter_raw_mode() and restored by leave_raw_mode().
DWORD original_stdin_mode{0};
DWORD original_stdout_mode{0};

} // namespace

bool stdout_is_terminal() {
    return _isatty(_fileno(stdout)) != 0;
}

bool stdin_is_terminal() {
    return _isatty(_fileno(stdin)) != 0;
}

void enable_vt_processing() {
    // Called once on first use; subsequent calls are no-ops.
    static bool done{false};

    if (done) {
        return;
    }

    done = true;

    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD mode{0};

    if (GetConsoleMode(h, &mode) == 0) {
        return;
    }

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

    SetConsoleMode(h, mode);
}

void query_terminal_size(int& cols, int& rows) {
    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (GetConsoleScreenBufferInfo(h, &csbi) != 0) {
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
}

void enter_raw_mode(const SourceLocation& loc) {
    const HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (hin == INVALID_HANDLE_VALUE || hout == INVALID_HANDLE_VALUE) {
        throw RuntimeError{"Terminal.enable_raw_mode: cannot get console handles", loc,
                           "failed to access console handles on Windows"};
    }

    if (GetConsoleMode(hin, &original_stdin_mode) == 0) {
        throw RuntimeError{"Terminal.enable_raw_mode: cannot get console mode", loc,
                           "failed to read current console mode"};
    }

    GetConsoleMode(hout, &original_stdout_mode);

    DWORD in_mode = original_stdin_mode;
    in_mode &= ~static_cast<DWORD>(ENABLE_LINE_INPUT);
    in_mode &= ~static_cast<DWORD>(ENABLE_ECHO_INPUT);
    in_mode &= ~static_cast<DWORD>(ENABLE_PROCESSED_INPUT);
    in_mode &= ~static_cast<DWORD>(ENABLE_VIRTUAL_TERMINAL_INPUT);
    in_mode |= ENABLE_WINDOW_INPUT;

    if (SetConsoleMode(hin, in_mode) == 0) {
        throw RuntimeError{"Terminal.enable_raw_mode: cannot set console mode", loc,
                           "failed to apply raw mode to console"};
    }

    DWORD out_mode = original_stdout_mode;
    out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    out_mode |= DISABLE_NEWLINE_AUTO_RETURN;

    if (SetConsoleMode(hout, out_mode) == 0) {
        // VT processing is not critical -- fall back silently.
        // ANSI sequences may not render, but the terminal remains usable.
    }
}

void leave_raw_mode() {
    const HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);

    SetConsoleMode(hin, original_stdin_mode);
    SetConsoleMode(hout, original_stdout_mode);
}

std::string_view enable_mouse() {
    const HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);

    DWORD mode{0};

    GetConsoleMode(hin, &mode);

    mode |= ENABLE_MOUSE_INPUT;
    mode &= ~static_cast<DWORD>(ENABLE_QUICK_EDIT_MODE);

    SetConsoleMode(hin, mode);

    return {};
}

std::string_view disable_mouse() {
    const HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);

    DWORD mode{0};

    GetConsoleMode(hin, &mode);

    mode &= ~static_cast<DWORD>(ENABLE_MOUSE_INPUT);

    SetConsoleMode(hin, mode);

    return {};
}

bool supports_color() {
    // Windows Terminal and modern consoles support color
    // when VT processing is available.
    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD mode{0};

    if (GetConsoleMode(h, &mode)) {
        return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
    }

    return false;
}

bool supports_true_color() {
    // Modern Windows Terminal supports true color.
    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD mode{0};

    if (GetConsoleMode(h, &mode)) {
        return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
    }

    return false;
}

} // namespace luma::platform_terminal
