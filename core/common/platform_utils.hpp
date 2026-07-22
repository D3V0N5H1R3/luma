#ifndef LUMA_COMMON_PLATFORM_UTILS_HPP
#define LUMA_COMMON_PLATFORM_UTILS_HPP

// Shared Win32/POSIX helpers for UTF-8 environment access and
// wide-string conversion.  Header-only; all functions are inline.

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
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

// Safe wrapper around environment variable lookup.
// Uses _dupenv_s on MSVC to satisfy the Security Development
// Lifecycle (/sdl) checks; falls back to std::getenv elsewhere.
[[nodiscard]] inline std::optional<std::string> safe_getenv(const char* name) {
#ifdef _MSC_VER
    char* raw_buf{nullptr};
    std::size_t len{0};

    if (_dupenv_s(&raw_buf, &len, name) == 0 && raw_buf != nullptr) {
        const auto buf = std::unique_ptr<char, decltype(&free)>{raw_buf, free};

        return std::string{buf.get()};
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

} // namespace luma

#endif // LUMA_COMMON_PLATFORM_UTILS_HPP
