#ifndef LUMA_STDLIB_PLATFORM_TIME_HPP
#define LUMA_STDLIB_PLATFORM_TIME_HPP

// ═══════════════════════════════════════════════════════════
// Platform-safe time functions
// ═══════════════════════════════════════════════════════════
//
// Thread-safe wrappers around gmtime and localtime that hide
// the Windows (gmtime_s) vs POSIX (gmtime_r) difference.
// Used by DateTime and Log modules.

#include <cstdint>
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

// Convert a std::tm interpreted as UTC into a Unix timestamp.  Returns
// std::nullopt only when the broken-down time cannot be represented.
//
// The conversion is computed directly with Howard Hinnant's days_from_civil
// algorithm rather than the C library's timegm/_mkgmtime, because those
// disagree at the calendar extremes: Windows _mkgmtime rejects negative time_t
// outright, and the BSD-derived timegm on macOS rejects far-past years such as
// 0001 that glibc accepts.  Doing the arithmetic here makes the DateTime codec
// produce identical, calendar-exact results across the whole supported
// 0001-9999 range on every platform.  The caller supplies already validated,
// in-range fields (the DateTime module validates or normalises every component
// before calling this), so out-of-range fields are not normalised here; the
// range of the result is enforced by the DateTime layer.
[[nodiscard]] inline std::optional<std::time_t> safe_timegm(std::tm& tm) noexcept {
    const std::int64_t year = static_cast<std::int64_t>(tm.tm_year) + 1900;
    const std::int64_t month = static_cast<std::int64_t>(tm.tm_mon) + 1;
    const std::int64_t day = static_cast<std::int64_t>(tm.tm_mday);

    // days_from_civil: number of days since 1970-01-01 for a proleptic
    // Gregorian date.  Shifts the year so that March is the first month, which
    // makes the leap-day fall at the end of the era.
    const std::int64_t y = year - (month <= 2 ? 1 : 0);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t year_of_era = y - (era * 400); // [0, 399]
    const std::int64_t day_of_year =
        ((153 * (month + (month > 2 ? -3 : 9))) + 2) / 5 + (day - 1); // [0, 365]
    const std::int64_t day_of_era =
        (year_of_era * 365) + (year_of_era / 4) - (year_of_era / 100) + day_of_year; // [0, 146096]
    const std::int64_t days = (era * 146097) + day_of_era - 719468;

    const std::int64_t seconds = (days * 86400) + (static_cast<std::int64_t>(tm.tm_hour) * 3600) +
                                 (static_cast<std::int64_t>(tm.tm_min) * 60) +
                                 static_cast<std::int64_t>(tm.tm_sec);

    return static_cast<std::time_t>(seconds);
}

} // namespace luma::platform

#endif // LUMA_STDLIB_PLATFORM_TIME_HPP
