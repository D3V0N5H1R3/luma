#ifndef LUMA_DIAGNOSTICS_DISPLAY_TEXT_HPP
#define LUMA_DIAGNOSTICS_DISPLAY_TEXT_HPP

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include "analysis/common/utf8_validation.hpp"

namespace luma {

// Make untrusted text (source lines, lexemes quoted in messages) safe to print
// to a terminal.  C0 control bytes other than tab and newline, DEL, C1 control
// code points and malformed UTF-8 bytes are shown as `\xNN` (one escape per
// byte), so a hostile .luma file cannot inject terminal escape sequences (e.g.
// ESC [ …) through a diagnostic.  Tabs, newlines (multi-line messages) and
// well-formed printable UTF-8 pass through unchanged.
[[nodiscard]] inline std::string sanitize_for_terminal(std::string_view text) {
    std::string out;
    out.reserve(text.size());

    const auto append_escaped = [&out](std::string_view bytes) {
        for (const char byte : bytes) {
            out += std::format("\\x{:02X}", static_cast<std::uint8_t>(byte));
        }
    };

    std::size_t pos{0};

    while (pos < text.size()) {
        const std::size_t length = well_formed_utf8_length(text, pos);

        if (length == 0) {
            append_escaped(text.substr(pos, 1));
            ++pos;
            continue;
        }

        const char32_t code_point = decode_well_formed_utf8(text, pos, length);
        const bool is_control = code_point != U'\t' && code_point != U'\n' &&
                                (code_point < 0x20 || (code_point >= 0x7F && code_point < 0xA0));

        if (is_control) {
            append_escaped(text.substr(pos, length));
        } else {
            out.append(text.substr(pos, length));
        }

        pos += length;
    }

    return out;
}

} // namespace luma

#endif // LUMA_DIAGNOSTICS_DISPLAY_TEXT_HPP
