#ifndef LUMA_STDLIB_DATETIME_INTERNAL_HPP
#define LUMA_STDLIB_DATETIME_INTERNAL_HPP

// Shared, dependency-free calendar helpers for the DateTime module.  They are
// used by the registration/arithmetic code (datetime_module.cpp), the ISO-8601
// parser (datetime_parser.cpp), and the ISO-8601 formatter
// (datetime_formatter.cpp), so they live here as inline/constexpr functions
// with a single definition shared across those translation units.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string_view>

#include "runtime/stdlib/system/platform_time.hpp"

namespace luma::datetime_detail {

// Error message for timestamps outside the supported 0001-9999 calendar range.
// Shared by the field accessors and to_parts/to_iso paths (datetime_module.cpp),
// the difference_*/add_* handlers (datetime_arithmetic.cpp), and the ISO parsing
// paths, so it lives here as a single definition.
inline constexpr std::string_view k_timestamp_range_error =
    "timestamp out of supported range (year 0001-9999)";

namespace datetime_limits {
// Unix timestamp boundaries for valid date range: year 0001-01-01 to 9999-12-31 UTC.
// Outside this range gmtime_r/gmtime_s produces undefined behaviour.
constexpr double k_min_unix_timestamp = -62135596800.0; // 0001-01-01T00:00:00Z
constexpr double k_max_unix_timestamp = 253402300799.0; // 9999-12-31T23:59:59Z

// UTC offset range based on IANA timezone database extremes.
constexpr double k_min_offset_minutes = -12.0 * 60.0; // UTC-12:00
constexpr double k_max_offset_minutes = 14.0 * 60.0;  // UTC+14:00
} // namespace datetime_limits

[[nodiscard]] inline std::optional<std::tm> to_tm(double ts) {
    // Reject NaN and infinities before the range test: NaN compares false against
    // both bounds, so without this guard it would slip through to the cast below,
    // where static_cast<std::time_t>(NaN) is undefined behaviour.
    if (!std::isfinite(ts) || ts < datetime_limits::k_min_unix_timestamp ||
        ts > datetime_limits::k_max_unix_timestamp) {
        return std::nullopt;
    }

    return luma::platform::safe_gmtime(static_cast<std::time_t>(ts));
}

[[nodiscard]] constexpr bool is_leap_year(std::int64_t year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// 1-indexed month (1 = January). Returns 0 for out-of-range month.
[[nodiscard]] constexpr int days_in_month_for(int month, std::int64_t year) noexcept {
    constexpr std::array<int, 13> days{{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};

    if (month < 1 || month > 12) {
        return 0;
    }

    int count{days[static_cast<std::size_t>(month)]};

    if (month == 2 && is_leap_year(year)) {
        count = 29;
    }

    return count;
}

// Converts a std::tm (in UTC) to a Unix timestamp via mkgmtime/timegm.
// Returns std::nullopt if the conversion fails.
[[nodiscard]] inline std::optional<double> tm_to_unix(std::tm& tm) {
    const auto t = luma::platform::safe_timegm(tm);

    if (!t) {
        return std::nullopt;
    }

    return static_cast<double>(*t);
}

} // namespace luma::datetime_detail

#endif // LUMA_STDLIB_DATETIME_INTERNAL_HPP
