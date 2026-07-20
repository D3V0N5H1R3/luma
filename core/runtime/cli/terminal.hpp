#ifndef LUMA_RUNTIME_CLI_TERMINAL_HPP
#define LUMA_RUNTIME_CLI_TERMINAL_HPP

#include <cstdio>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace luma::term {

namespace detail {

[[nodiscard]] inline int portable_fileno(std::FILE* stream) noexcept {
#ifdef _WIN32
    return _fileno(stream);
#else
    return fileno(stream);
#endif
}

[[nodiscard]] inline bool portable_isatty(int fd) noexcept {
#ifdef _WIN32
    return _isatty(fd) != 0;
#else
    return isatty(fd) != 0;
#endif
}

} // namespace detail

// Returns true when stderr is connected to an interactive terminal.
[[nodiscard]] inline bool stderr_is_tty() noexcept {
    return detail::portable_isatty(detail::portable_fileno(stderr));
}

// Returns true when stdout is connected to an interactive terminal.
[[nodiscard]] inline bool stdout_is_tty() noexcept {
    return detail::portable_isatty(detail::portable_fileno(stdout));
}

// Named ANSI escape-code constants.
namespace ansi {

inline constexpr const char* red = "\033[31m";
inline constexpr const char* green = "\033[32m";
inline constexpr const char* yellow = "\033[33m";
inline constexpr const char* cyan = "\033[36m";
inline constexpr const char* dim = "\033[2m";
inline constexpr const char* bold = "\033[1m";
inline constexpr const char* bold_red = "\033[1;31m";
inline constexpr const char* bold_yellow = "\033[1;33m";
inline constexpr const char* bold_cyan = "\033[1;36m";
inline constexpr const char* reset = "\033[0m";

// Cursor and screen control sequences (used by the REPL line editor).
inline constexpr const char* csi = "\033[";
inline constexpr const char* clear_line = "\r\033[2K";
inline constexpr const char* clear_screen_home = "\033[2J\033[H";

} // namespace ansi

// ANSI escape sequences — return empty strings when colour is disabled.
// Each function checks a cached flag so the TTY query runs at most once.

namespace detail {

[[nodiscard]] inline bool colour_enabled_stderr() noexcept {
    static const bool enabled = stderr_is_tty();

    return enabled;
}

[[nodiscard]] inline bool colour_enabled_stdout() noexcept {
    static const bool enabled = stdout_is_tty();

    return enabled;
}

// Returns `code` when colour is enabled for the given stream, otherwise "".
[[nodiscard]] inline std::string_view ansi(const char* code, bool enabled) noexcept {
    return enabled ? code : "";
}

[[nodiscard]] inline std::string_view err(const char* code) noexcept {
    return ansi(code, colour_enabled_stderr());
}

[[nodiscard]] inline std::string_view out(const char* code) noexcept {
    return ansi(code, colour_enabled_stdout());
}

} // namespace detail

// ─── Colours for stderr (error diagnostics) ───

[[nodiscard]] inline std::string_view red() noexcept {
    return detail::err(ansi::red);
}

[[nodiscard]] inline std::string_view yellow() noexcept {
    return detail::err(ansi::yellow);
}

[[nodiscard]] inline std::string_view green() noexcept {
    return detail::err(ansi::green);
}

[[nodiscard]] inline std::string_view cyan() noexcept {
    return detail::err(ansi::cyan);
}

[[nodiscard]] inline std::string_view dim() noexcept {
    return detail::err(ansi::dim);
}

[[nodiscard]] inline std::string_view bold() noexcept {
    return detail::err(ansi::bold);
}

[[nodiscard]] inline std::string_view bold_red() noexcept {
    return detail::err(ansi::bold_red);
}

[[nodiscard]] inline std::string_view bold_yellow() noexcept {
    return detail::err(ansi::bold_yellow);
}

[[nodiscard]] inline std::string_view bold_cyan() noexcept {
    return detail::err(ansi::bold_cyan);
}

[[nodiscard]] inline std::string_view reset() noexcept {
    return detail::err(ansi::reset);
}

// ─── Colours for stdout (test results, REPL) ───

[[nodiscard]] inline std::string_view out_green() noexcept {
    return detail::out(ansi::green);
}

[[nodiscard]] inline std::string_view out_red() noexcept {
    return detail::out(ansi::red);
}

[[nodiscard]] inline std::string_view out_bold() noexcept {
    return detail::out(ansi::bold);
}

[[nodiscard]] inline std::string_view out_cyan() noexcept {
    return detail::out(ansi::cyan);
}

[[nodiscard]] inline std::string_view out_reset() noexcept {
    return detail::out(ansi::reset);
}

// Enable ANSI escape processing on Windows 10+.
// Call once from main() before any coloured output.
// Defined in terminal.cpp (uses Windows API, so <windows.h> is kept
// out of this header).
#ifdef _WIN32
void enable_ansi_escapes() noexcept;
#else
inline void enable_ansi_escapes() noexcept {}
#endif

} // namespace luma::term

#endif // LUMA_RUNTIME_CLI_TERMINAL_HPP
