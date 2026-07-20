#ifndef LUMA_PROTOCOL_MESSAGE_FRAME_HPP
#define LUMA_PROTOCOL_MESSAGE_FRAME_HPP

#include <charconv>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "common/string_utils.hpp"
#include "protocol/constants.hpp"

namespace luma::protocol {

// ─── Header parsing free functions ───

// Check whether a header line starts with the given header name
// (case-insensitive prefix match, including the trailing ": ").
// Example: is_header_name("Content-Length: 42", "Content-Length: ") → true
[[nodiscard]] inline bool is_header_name(std::string_view line, std::string_view name) {
    if (line.size() < name.size()) {
        return false;
    }

    return luma::case_insensitive_equal(line.substr(0, name.size()), name);
}

// The canonical Content-Length header prefix used for matching.
inline constexpr std::string_view k_content_length_prefix = "Content-Length: ";

// Check whether a header line is a Content-Length header (case-insensitive
// prefix match).  Does not validate the value portion.
[[nodiscard]] inline bool is_content_length_header(std::string_view header_line) {
    return is_header_name(header_line, k_content_length_prefix);
}

// Parse a Content-Length value from a header line (case-insensitive).
// Returns the parsed length, or std::nullopt if the line is not a
// Content-Length header or the value is malformed.
[[nodiscard]] inline std::optional<std::size_t>
try_parse_content_length(std::string_view header_line) {
    if (!is_content_length_header(header_line)) {
        return std::nullopt;
    }

    const auto value_sv = header_line.substr(k_content_length_prefix.size());

    if (value_sv.empty()) {
        return std::nullopt;
    }

    std::size_t length{0};
    const auto* first = value_sv.data();
    const auto* last = first + value_sv.size();
    auto [ptr, ec] = std::from_chars(first, last, length);

    if (ec != std::errc{} || ptr != last) {
        return std::nullopt;
    }

    return length;
}

// Build the Content-Length framing header (name, value and the blank-line
// separator) for a body of the given size.  The body is transmitted
// separately so it never needs to be copied into the same buffer.
[[nodiscard]] inline std::string content_length_header(std::size_t body_size) {
    return std::format("Content-Length: {}\r\n\r\n", body_size);
}

// Serialise a body string into a Content-Length framed protocol message.
// Returns the complete wire-format string (headers + separator + body).
[[nodiscard]] inline std::string write_framed_message(std::string_view body) {
    std::string framed = content_length_header(body.size());
    framed.reserve(framed.size() + body.size());
    framed += body;
    return framed;
}

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_MESSAGE_FRAME_HPP
