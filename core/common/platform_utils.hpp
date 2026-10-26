#ifndef LUMA_COMMON_PLATFORM_UTILS_HPP
#define LUMA_COMMON_PLATFORM_UTILS_HPP

// Shared Win32/POSIX helpers for UTF-8 environment access, wide-string
// conversion, and terminal (TTY) detection.  Header-only; all functions
// are inline.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace luma {

#ifdef _WIN32
// Convert a wide string to UTF-8 using the Win32 API.
[[nodiscard]] inline std::string wstring_to_utf8(const std::wstring& ws) {
    if (ws.empty()) {
        return {};
    }

    const int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr,
                                        0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), result.data(), len,
                        nullptr, nullptr);
    return result;
}
#endif

// Safe wrapper around environment variable lookup, returning the value as
// UTF-8 on every platform.  On MSVC it reads the wide environment with
// _wdupenv_s (which also satisfies the Security Development Lifecycle /sdl
// checks) and converts to UTF-8 — the narrow _dupenv_s would return text in
// the ANSI code page instead.  Elsewhere it uses std::getenv, whose bytes are
// already UTF-8 on the platforms Luma supports.
[[nodiscard]] inline std::optional<std::string> safe_getenv(const char* name) {
#ifdef _MSC_VER
    // Environment variable names are ASCII in practice; widen byte-by-byte.
    std::wstring wide_name;

    for (const char* p = name; *p != '\0'; ++p) {
        wide_name += static_cast<wchar_t>(static_cast<unsigned char>(*p));
    }

    wchar_t* raw_buf{nullptr};
    std::size_t len{0};

    if (_wdupenv_s(&raw_buf, &len, wide_name.c_str()) == 0 && raw_buf != nullptr) {
        const auto buf = std::unique_ptr<wchar_t, decltype(&free)>{raw_buf, free};

        return wstring_to_utf8(std::wstring{buf.get()});
    }
    return std::nullopt;
#else
    // NOLINTNEXTLINE(concurrency-mt-unsafe): no thread-safe std alternative; read at startup only.
    const char* val = std::getenv(name);

    if (val) {
        return std::string{val};
    }

    return std::nullopt;
#endif
}

// True when the given standard stream is connected to an interactive
// terminal (TTY) rather than a pipe, file redirect, or non-interactive
// process host.  Consolidates the isatty()/_isatty() check that used to be
// duplicated across the CLI colour helpers, the Terminal stdlib module, and
// the REPL line editor.
[[nodiscard]] inline bool is_stdin_terminal() noexcept {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

[[nodiscard]] inline bool is_stdout_terminal() noexcept {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

[[nodiscard]] inline bool is_stderr_terminal() noexcept {
#ifdef _WIN32
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(STDERR_FILENO) != 0;
#endif
}

} // namespace luma

#endif // LUMA_COMMON_PLATFORM_UTILS_HPP
