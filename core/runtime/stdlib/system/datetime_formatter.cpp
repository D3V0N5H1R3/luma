#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "runtime/stdlib/system/datetime_codec.hpp"
#include "runtime/stdlib/system/datetime_internal.hpp"

namespace luma::datetime {

using namespace datetime_detail;

std::string format_iso8601_with_suffix(const std::tm& tm, std::string_view suffix) {
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}{}", tm.tm_year + 1900, tm.tm_mon + 1,
                       tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, suffix);
}

std::optional<std::string> format_iso8601(double unix_seconds) {
    const auto tm = to_tm(unix_seconds);

    if (!tm) {
        return std::nullopt;
    }

    return format_iso8601_with_suffix(*tm, "Z");
}

} // namespace luma::datetime
