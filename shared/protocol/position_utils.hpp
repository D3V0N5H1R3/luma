#ifndef LUMA_PROTOCOL_POSITION_UTILS_HPP
#define LUMA_PROTOCOL_POSITION_UTILS_HPP

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "common/utf8.hpp"

namespace luma::protocol {

// ═══════════════════════════════════════════════════════════
// UTF-8 ↔ UTF-16 position conversion utilities
//
// Many editor protocols (LSP, DAP) use UTF-16 code unit offsets
// for character positions while source files are UTF-8.
// These helpers convert between byte offsets within a line
// and UTF-16 column indices.
//
// All functions operate on 0-based values.
// ═══════════════════════════════════════════════════════════

// Returns true if a UTF-8 sequence of the given length encodes a
// supplementary codepoint (U+10000 and above) that requires a surrogate
// pair in UTF-16.  In practice, all 4-byte UTF-8 sequences are supplementary.
[[nodiscard]] constexpr bool is_supplementary_sequence(std::size_t sequence_length) noexcept {
    return sequence_length == 4;
}

// Delegate to the canonical implementation in common/utf8.hpp.
using luma::is_valid_utf8_run;

// Convert a 0-based byte column offset within a line to a 0-based UTF-16
// code unit offset.
// `line` is the content of the line (without the trailing newline).
[[nodiscard]] inline int byte_offset_to_utf16_column(std::string_view line,
                                                     std::size_t byte_offset) {
    int utf16{0};
    std::size_t pos{0};
    const auto limit = std::min(byte_offset, line.size());

    while (pos < limit) {
        const auto byte = static_cast<unsigned char>(line[pos]);
        auto seq_len = static_cast<std::size_t>(luma::utf8_codepoint_len(byte));

        if (pos + seq_len > limit) {
            break;
        }

        if (!is_valid_utf8_run(line, pos, seq_len)) {
            ++utf16;
            ++pos;
            continue;
        }

        // A 4-byte UTF-8 sequence maps to a surrogate pair (2 UTF-16 units).
        utf16 += is_supplementary_sequence(seq_len) ? 2 : 1;
        pos += seq_len;
    }

    // Any remaining bytes (partial sequence) count as 1 UTF-16 unit each.
    utf16 += static_cast<int>(limit - pos);

    return utf16;
}

// Convert a 0-based CODEPOINT column within a line to a byte offset. The Luma
// lexer advances columns once per Unicode codepoint, so a token column is a
// codepoint index and must be walked through the UTF-8 line to find the
// matching byte position before any byte→UTF-16 conversion.
// `line` is the content of the line (without the trailing newline).
[[nodiscard]] inline std::size_t codepoint_column_to_byte_offset(std::string_view line,
                                                                 int codepoint_col) {
    std::size_t pos{0};
    int cols{0};
    while (pos < line.size() && cols < codepoint_col) {
        pos += static_cast<std::size_t>(
            luma::utf8_codepoint_len(static_cast<unsigned char>(line[pos])));
        ++cols;
    }
    return std::min(pos, line.size());
}

// Convert a 0-based byte offset within a line to a 0-based CODEPOINT column
// (the inverse of codepoint_column_to_byte_offset). Counts one column per
// UTF-8 lead byte up to `byte_offset`.
// `line` is the content of the line (without the trailing newline).
[[nodiscard]] inline int byte_offset_to_codepoint_column(std::string_view line,
                                                         std::size_t byte_offset) {
    int cols{0};
    std::size_t pos{0};
    const auto limit = std::min(byte_offset, line.size());
    while (pos < limit) {
        auto seq_len = static_cast<std::size_t>(
            luma::utf8_codepoint_len(static_cast<unsigned char>(line[pos])));
        if (pos + seq_len > limit) {
            // Partial trailing sequence: count each remaining byte as one column.
            break;
        }
        ++cols;
        pos += seq_len;
    }
    cols += static_cast<int>(limit - pos);
    return cols;
}

// Convert a 0-based UTF-16 column index to a byte offset within a line.
// `line` is the content of the line (without the trailing newline).
[[nodiscard]] inline std::size_t utf16_column_to_byte_offset(std::string_view line, int utf16_col) {
    std::size_t pos{0};
    int utf16_units{0};

    while (pos < line.size() && utf16_units < utf16_col) {
        const auto byte = static_cast<unsigned char>(line[pos]);
        auto seq_len = static_cast<std::size_t>(luma::utf8_codepoint_len(byte));

        // Don't walk past end-of-line.
        if (pos + seq_len > line.size()) {
            seq_len = line.size() - pos;
        }

        if (!is_valid_utf8_run(line, pos, seq_len)) {
            ++utf16_units;
            ++pos;
            continue;
        }

        // A 4-byte UTF-8 sequence maps to a surrogate pair (2 UTF-16 units).
        const int units = is_supplementary_sequence(seq_len) ? 2 : 1;
        utf16_units += units;
        pos += seq_len;
    }

    return pos;
}

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_POSITION_UTILS_HPP
