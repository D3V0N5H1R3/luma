#ifndef LUMA_COMMON_FORMAT_NUMBER_HPP
#define LUMA_COMMON_FORMAT_NUMBER_HPP

#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <system_error>

namespace luma {

// Format a double as a string, trimming trailing zeros while keeping at
// least one digit after the decimal point (e.g. 3.0, 1.5, -0.25).
//
// Uses std::to_chars with chars_format::fixed for locale-independent output
// (always uses '.' as the decimal separator regardless of the active locale).
[[nodiscard]] inline std::string format_number(double value) {
    if (std::isnan(value)) {
        return "NaN";
    }

    if (std::isinf(value)) {
        return value > 0 ? "Infinity" : "-Infinity";
    }

    // std::to_chars with chars_format::fixed emits the shortest round-tripping
    // fixed-notation string, whose length is bounded by the exponent rather than
    // by max_digits10: up to ~309 integer digits near DBL_MAX and ~325 fractional
    // digits for the smallest subnormals. 512 bytes covers every finite double,
    // but should a value ever need more we grow onto the heap instead of falling
    // back to std::to_string — which is locale-dependent (violating this
    // function's locale-independence guarantee) and would also bypass the
    // trailing-zero trimming below.
    std::array<char, 512> stack_buf{};
    char* begin = stack_buf.data();
    char* end = stack_buf.data() + stack_buf.size();

    std::string heap_buf;
    auto result = std::to_chars(begin, end, value, std::chars_format::fixed);

    while (result.ec == std::errc::value_too_large) {
        heap_buf.resize(heap_buf.empty() ? stack_buf.size() * 2 : heap_buf.size() * 2);
        begin = heap_buf.data();
        end = heap_buf.data() + heap_buf.size();
        result = std::to_chars(begin, end, value, std::chars_format::fixed);
    }

    std::string s(begin, result.ptr);
    const auto dot = s.find('.');

    if (dot == std::string::npos) {
        // Whole number — append ".0" to satisfy the guarantee.
        s += ".0";
    } else {
        // Trim trailing zeros, but keep at least one digit after the dot.
        auto last = s.find_last_not_of('0');

        if (last == dot) {
            ++last; // keep "x.0"
        }

        s.erase(last + 1);
    }

    return s;
}

} // namespace luma

#endif // LUMA_COMMON_FORMAT_NUMBER_HPP
