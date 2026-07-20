#include "resource_limits.hpp"

#include <cctype>
#include <charconv>
#include <concepts>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>

#include "platform_utils.hpp"

namespace luma {
namespace {

// Unified numeric parser for all integral limit types.
// Uses std::from_chars for locale-independent, type-safe parsing.
// Unsigned types additionally reject negative input (a leading '-').
template <std::integral T> [[nodiscard]] bool parse_value(const char* str, T& target) {
    if constexpr (std::is_unsigned_v<T>) {
        // Reject negative values — from_chars for unsigned types
        // would silently succeed on some implementations.
        if (str[0] == '-') {
            return false;
        }
    }
    const auto len = std::strlen(str);
    T value{};
    const auto [ptr, ec] = std::from_chars(str, str + len, value);
    if (ec != std::errc{} || ptr != str + len) {
        return false;
    }
    if constexpr (std::is_signed_v<T>) {
        if (value < 0) {
            return false;
        }
    }
    target = value;
    return true;
}

template <typename T> void read_env(const char* name, T& target) {
    const auto val = safe_getenv(name);

    if (val.has_value() && !val->empty()) {
        if (!parse_value(val->c_str(), target)) {
            std::cerr << "luma: ignoring invalid value for " << name << ": '" << *val << "'\n";
        }
    }
}

// Convert a field name like "max_array_size" to "LUMA_LIMIT_MAX_ARRAY_SIZE".
std::string make_env_name(const char* field_name) {
    std::string env_name = "LUMA_LIMIT_";
    for (const char* p = field_name; (*p) != 0; ++p) {
        env_name += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    }
    return env_name;
}

} // namespace

void ResourceLimits::init_from_env() {
#define READ_LIMIT(type, name) read_env(make_env_name(#name).c_str(), name);
    LUMA_RESOURCE_LIMITS_X(READ_LIMIT)
#undef READ_LIMIT
}

} // namespace luma
