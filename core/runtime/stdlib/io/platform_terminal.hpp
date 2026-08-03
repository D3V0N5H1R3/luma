#ifndef LUMA_STDLIB_PLATFORM_TERMINAL_HPP
#define LUMA_STDLIB_PLATFORM_TERMINAL_HPP

// Platform-agnostic terminal control primitives.
//
// Abstracts the Windows console API (SetConsoleMode / GetConsoleScreenBufferInfo)
// and the POSIX termios / ioctl differences behind a uniform interface so the
// Terminal module stays free of #ifdef branches.  The bodies live in
// platform_terminal_win32.cpp and platform_terminal_posix.cpp, compiled
// conditionally by CMake (mirroring the terminal_input split).

#include <string_view>

#include "analysis/source/source_location.hpp"

namespace luma::platform_terminal {

// True when stdout is attached to an interactive terminal device.
[[nodiscard]] bool stdout_is_terminal();

// True when stdin is attached to an interactive terminal device.
[[nodiscard]] bool stdin_is_terminal();

// Enable virtual-terminal (ANSI) processing on the Windows console.  Runs at
// most once per process; a no-op on other platforms.
void enable_vt_processing();

// Fill cols/rows with the current terminal dimensions when they can be queried,
// leaving the caller-provided values unchanged otherwise.
void query_terminal_size(int& cols, int& rows);

// Save the current console/terminal state and switch to raw mode.  Throws
// RuntimeError (using loc) if the terminal cannot be reconfigured.
void enter_raw_mode(const SourceLocation& loc);

// Restore the console/terminal state saved by enter_raw_mode().
void leave_raw_mode();

// Enable mouse reporting.  On Windows this reconfigures the console and returns
// an empty view; on POSIX it returns the ANSI escape sequence the caller must
// emit to the terminal.
[[nodiscard]] std::string_view enable_mouse();

// Disable mouse reporting.  See enable_mouse() for the return-value contract.
[[nodiscard]] std::string_view disable_mouse();

// True when the terminal supports colour / 24-bit true-colour output.
[[nodiscard]] bool supports_color();
[[nodiscard]] bool supports_true_color();

} // namespace luma::platform_terminal

#endif // LUMA_STDLIB_PLATFORM_TERMINAL_HPP
