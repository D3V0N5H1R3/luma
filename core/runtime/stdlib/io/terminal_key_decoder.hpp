#ifndef LUMA_STDLIB_TERMINAL_KEY_DECODER_HPP
#define LUMA_STDLIB_TERMINAL_KEY_DECODER_HPP

/// @file terminal_key_decoder.hpp
/// @brief Pure, platform-independent decoding of terminal key presses and
///        escape/mouse sequences.
///
/// The byte-stream parsing logic is shared by the POSIX input backend and is
/// exercised directly by unit and fuzz tests.  The decoder is decoupled from
/// the underlying I/O: callers supply a @ref byte_reader callback that yields
/// the next input byte, or -1 when no further byte is available within the
/// escape-sequence timeout window.  This keeps the parser free of any blocking
/// syscalls so it can be driven deterministically from in-memory buffers.

#include <exception>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "runtime/stdlib/io/terminal_input_common.hpp"

namespace luma::terminal_detail {

/// Reads the next byte of an input sequence, returning -1 when no further byte
/// is available (timeout or end of input).
using byte_reader = std::function<int()>;

// ═══════════════════════════════════════════════════════════
// Escape sequence parsing
// ═══════════════════════════════════════════════════════════

/// Parse an SGR mouse event after the CSI '<' prefix has been consumed.
/// Returns the formatted mouse event string, or empty if the sequence is malformed.
[[nodiscard]] inline std::string parse_sgr_mouse_event(const byte_reader& read_more,
                                                       bool mouse_mode_active) {
    if (!mouse_mode_active) {
        return "";
    }

    std::string params{};
    int ch{0};

    while ((ch = read_more()) != -1 && ch != 'M' && ch != 'm') {
        params += static_cast<char>(ch);
    }

    if (ch == 'M' || ch == 'm') {
        const bool is_release = (ch == 'm');

        int button{0};
        int col{1};
        int row{1};
        std::size_t pos{0};

        const std::size_t semi1 = params.find(';', pos);

        if (semi1 != std::string::npos) {
            try {
                button = std::stoi(params.substr(pos, semi1 - pos));

                const std::size_t semi2 = params.find(';', semi1 + 1);

                if (semi2 != std::string::npos) {
                    col = std::stoi(params.substr(semi1 + 1, semi2 - semi1 - 1));
                    row = std::stoi(params.substr(semi2 + 1));
                }
            } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
                // Malformed SGR mouse sequence — ignore.
            }
        }

        // SGR reports motion with bit 5 set (button 32-34).
        const bool is_drag = (button >= k_mouse_drag_flag && button < k_mouse_drag_limit);

        if (is_drag) {
            button -= k_mouse_drag_flag;
        }

        return format_mouse_event(button, col, row, is_release, is_drag);
    }

    return "unknown";
}

// Map a CSI numeric parameter (terminated by '~') to a key name, or "" when
// the parameter does not correspond to a known key.
[[nodiscard]] inline std::string csi_tilde_keyname(const std::string& num) {
    static const std::unordered_map<std::string_view, std::string_view> table = {
        {"1", "home"},      {"2", "insert"}, {"3", "delete"}, {"4", "end"},  {"5", "page_up"},
        {"6", "page_down"}, {"7", "home"},   {"8", "end"},    {"11", "f1"},  {"12", "f2"},
        {"13", "f3"},       {"14", "f4"},    {"15", "f5"},    {"17", "f6"},  {"18", "f7"},
        {"19", "f8"},       {"20", "f9"},    {"21", "f10"},   {"23", "f11"}, {"24", "f12"},
    };

    const auto it = table.find(num);

    return it != table.end() ? std::string{it->second} : std::string{};
}

// Map a CSI modifier digit (the "mod" in CSI 1;mod X) to its key-name prefix.
[[nodiscard]] inline std::string csi_modifier_prefix(int mod) {
    switch (mod) {
        case '2':
            return "shift+";
        case '3':
            return "alt+";
        case '4':
            return "alt+shift+";
        case '5':
            return "ctrl+";
        case '6':
            return "ctrl+shift+";
        case '7':
            return "ctrl+alt+";
        default:
            return "";
    }
}

// Map a CSI final byte to its cursor/navigation key name, or "" when the byte
// is not one of A/B/C/D/H/F.  Shared by the plain and modified CSI forms so the
// mapping lives in a single place.
[[nodiscard]] inline std::string_view csi_cursor_key_name(int code) {
    switch (code) {
        case 'A':
            return "up";
        case 'B':
            return "down";
        case 'C':
            return "right";
        case 'D':
            return "left";
        case 'H':
            return "home";
        case 'F':
            return "end";
        default:
            return "";
    }
}

// Parse a CSI ('[') escape sequence: SGR mouse, numeric "~"/modifier sequences,
// and single-letter cursor/navigation codes.
[[nodiscard]] inline std::string parse_csi_sequence(const byte_reader& read_more,
                                                    bool mouse_mode_active) {
    const int third = read_more();

    if (third == -1) {
        return "escape";
    }

    // SGR mouse event: CSI < button ; col ; row M/m
    if (third == '<') {
        const std::string mouse_result = parse_sgr_mouse_event(read_more, mouse_mode_active);

        if (!mouse_result.empty()) {
            return mouse_result;
        }
    }

    // CSI sequences with numeric parameters.
    if (third >= '0' && third <= '9') {
        std::string num{};
        num += static_cast<char>(third);

        int next = read_more();

        while (next >= '0' && next <= '9') {
            num += static_cast<char>(next);

            next = read_more();
        }

        if (next == '~') {
            const std::string key = csi_tilde_keyname(num);

            if (!key.empty()) {
                return key;
            }
        }

        // Modifier sequences: CSI 1;mod X
        if (next == ';') {
            const int mod{read_more()};
            const int code{read_more()};
            const std::string prefix = csi_modifier_prefix(mod);

            if (const std::string_view name = csi_cursor_key_name(code); !name.empty()) {
                return prefix + std::string{name};
            }
        }

        return "unknown";
    }

    if (const std::string_view name = csi_cursor_key_name(third); !name.empty()) {
        return std::string{name};
    }

    if (third == 'Z') {
        return "shift+tab";
    }

    return "unknown";
}

// Parse an SS3 ('O') escape sequence: function keys F1-F4 and Home/End.
[[nodiscard]] inline std::string parse_ss3_sequence(const byte_reader& read_more) {
    const int third = read_more();

    switch (third) {
        case 'P':
            return "f1";
        case 'Q':
            return "f2";
        case 'R':
            return "f3";
        case 'S':
            return "f4";
        case 'H':
            return "home";
        case 'F':
            return "end";
        default:
            if (third < 0) {
                return "unknown";
            }

            return std::format("alt+{}", static_cast<char>(third));
    }
}

[[nodiscard]] inline std::string parse_escape_sequence(const byte_reader& read_more,
                                                       bool mouse_mode_active) {
    const int second = read_more();

    if (second == -1) {
        return "escape";
    }

    if (second == '[') {
        return parse_csi_sequence(read_more, mouse_mode_active);
    }

    if (second == 'O') {
        return parse_ss3_sequence(read_more);
    }

    // Alt + character
    if (second >= 32 && second <= 126) {
        return std::format("alt+{}", static_cast<char>(second));
    }

    return "unknown";
}

// ═══════════════════════════════════════════════════════════
// Key decoding
// ═══════════════════════════════════════════════════════════

/// Decode a single key press from an already-read first byte plus a callback
/// that yields subsequent bytes (returning -1 on timeout/end of input).
///
/// @param first_byte         The first input byte, a value in [0, 255].
/// @param read_more          Reads the next byte, or -1 when none is available.
/// @param mouse_mode_active  Whether SGR mouse reporting should be decoded.
[[nodiscard]] inline std::string decode_key(int first_byte, const byte_reader& read_more,
                                            bool mouse_mode_active) {
    const int c = first_byte;

    if (c == 27) {
        return parse_escape_sequence(read_more, mouse_mode_active);
    }

    if (c == 13 || c == 10) {
        return "enter";
    }

    if (c == 9) {
        return "tab";
    }

    if (c == 127 || c == 8) {
        return "backspace";
    }

    if (c == 0) {
        return "ctrl+space";
    }

    if (c == 32) {
        return "space";
    }

    // Ctrl+A through Ctrl+Z
    if (c >= 1 && c <= 26) {
        return std::format("ctrl+{}", static_cast<char>('a' + c - 1));
    }

    // UTF-8 multi-byte sequences.
    if (c >= 0x80) {
        std::string utf8;
        utf8 += static_cast<char>(c);

        int remaining{0};

        if ((c & 0xE0) == 0xC0) {
            remaining = 1;
        } else if ((c & 0xF0) == 0xE0) {
            remaining = 2;
        } else if ((c & 0xF8) == 0xF0) {
            remaining = 3;
        } else {
            // Invalid leading byte — return replacement character.
            return "\xEF\xBF\xBD";
        }

        for (int i{0}; i < remaining; ++i) {
            const int b = read_more();

            if (b == -1 || (b & 0xC0) != 0x80) {
                // Incomplete or malformed sequence — return replacement character.
                return "\xEF\xBF\xBD";
            }

            utf8 += static_cast<char>(b);
        }

        return utf8;
    }

    return std::format("{}", static_cast<char>(c));
}

} // namespace luma::terminal_detail

#endif // LUMA_STDLIB_TERMINAL_KEY_DECODER_HPP
