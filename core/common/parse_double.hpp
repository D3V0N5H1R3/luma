#pragma once

// Locale-independent conversion of a decimal floating-point token to double.
//
// std::from_chars is the right tool, but floating-point from_chars is missing
// from some standard libraries the project builds with (Apple's libc++ in
// Xcode 16 deletes it). Where the library does not advertise it through
// __cpp_lib_to_chars, the conversion falls back to std::strtod: Luma never
// changes the C locale, so strtod's decimal separator is always '.'.

#include <charconv>
#include <string_view>
#include <system_error>

#if !(defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L)
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <string>
#endif

namespace luma {

// Parses a double from the front of `text` with std::from_chars semantics:
// `ec` is std::errc::invalid_argument when no number starts at the front,
// std::errc::result_out_of_range when the value does not fit, and `ptr` points
// one past the last character consumed. `value` is written only on success.
[[nodiscard]] inline std::from_chars_result parse_double(std::string_view text, double& value) {
    const char* const first = text.data();

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    return std::from_chars(first, first + text.size(), value);
#else
    // strtod skips leading whitespace and accepts a '+' sign, both of which
    // from_chars rejects.
    constexpr std::string_view k_rejected_leads{"+ \t\n\v\f\r"};
    if (text.empty() || k_rejected_leads.find(text.front()) != std::string_view::npos) {
        return {first, std::errc::invalid_argument};
    }

    const std::string buffer{text};
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(buffer.c_str(), &end);
    const auto consumed = static_cast<std::size_t>(end - buffer.c_str());

    if (consumed == 0) {
        return {first, std::errc::invalid_argument};
    }

    if (errno == ERANGE) {
        return {first + consumed, std::errc::result_out_of_range};
    }

    value = parsed;
    return {first + consumed, std::errc{}};
#endif
}

} // namespace luma
