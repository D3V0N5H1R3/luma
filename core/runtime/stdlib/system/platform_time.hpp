#ifndef LUMA_STDLIB_PLATFORM_TIME_HPP
#define LUMA_STDLIB_PLATFORM_TIME_HPP

// ═══════════════════════════════════════════════════════════
// Platform-safe time functions
// ═══════════════════════════════════════════════════════════
//
// Thread-safe wrappers around gmtime and localtime that hide
// the Windows (gmtime_s) vs POSIX (gmtime_r) difference.
// Used by DateTime and Log modules.

#include <ctime>
#include <optional>

namespace luma::platform {

// Thread-safe gmtime wrapper.  Returns std::nullopt on failure.
[[nodiscard]] inline std::optional<std::tm> safe_gmtime(std::time_t t) noexcept {
    std::tm result{};
#if defined(_MSC_VER) || defined(_WIN32)
    if (gmtime_s(&result, &t) != 0) {
        return std::nullopt;
    }
#else
    if (gmtime_r(&t, &result) == nullptr) {
        return std::nullopt;
    }
#endif
    return result;
}

// Thread-safe localtime wrapper.  Returns std::nullopt on failure.
[[nodiscard]] inline std::optional<std::tm> safe_localtime(std::time_t t) noexcept {
    std::tm result{};
#if defined(_MSC_VER) || defined(_WIN32)
    if (localtime_s(&result, &t) != 0) {
        return std::nullopt;
    }
#else
    if (localtime_r(&t, &result) == nullptr) {
        return std::nullopt;
    }
#endif
    return result;
}

// Convert a std::tm interpreted as UTC into a Unix timestamp, hiding the
// Windows (_mkgmtime) vs POSIX (timegm) difference.  Returns std::nullopt when
// the broken-down time cannot be represented.  Used by the DateTime module.
[[nodiscard]] inline std::optional<std::time_t> safe_timegm(std::tm& tm) noexcept {
#if defined(_MSC_VER) || defined(_WIN32)
    const std::time_t t = _mkgmtime(&tm);
#else
    const std::time_t t = timegm(&tm);
#endif
    if (t == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }
    return t;
}

} // namespace luma::platform

#endif // LUMA_STDLIB_PLATFORM_TIME_HPP
