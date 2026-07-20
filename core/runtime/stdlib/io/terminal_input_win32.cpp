#include <array>
#include <chrono>
#include <format>
#include <optional>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/stdlib/io/terminal_input.hpp"
#include "runtime/stdlib/io/terminal_input_common.hpp"

namespace luma::terminal_detail {

namespace {

/// Dispatch a Win32 mouse event record into a formatted mouse event string.
/// Returns std::nullopt if the event should be skipped (e.g. plain mouse movement without a button).
[[nodiscard]] std::optional<std::string> dispatch_mouse_event(const MOUSE_EVENT_RECORD& me, int col,
                                                              int row) {
    if (me.dwEventFlags == 0) {
        // Button press or release.
        if ((me.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0u) {
            return format_mouse_event(0, col, row, false);
        }

        if ((me.dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) != 0u) {
            return format_mouse_event(1, col, row, false);
        }

        if ((me.dwButtonState & RIGHTMOST_BUTTON_PRESSED) != 0u) {
            return format_mouse_event(2, col, row, false);
        }

        return format_mouse_event(0, col, row, true);
    }

    if (me.dwEventFlags == MOUSE_WHEELED) {
        const int direction = (static_cast<int>(me.dwButtonState) >> 16) > 0 ? 64 : 65;

        return format_mouse_event(direction, col, row, false);
    }

    // Drag: mouse moved with a button held down.
    if (me.dwEventFlags == MOUSE_MOVED) {
        if ((me.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0u) {
            return format_mouse_event(0, col, row, false, true);
        }

        if ((me.dwButtonState & RIGHTMOST_BUTTON_PRESSED) != 0u) {
            return format_mouse_event(2, col, row, false, true);
        }

        if ((me.dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) != 0u) {
            return format_mouse_event(1, col, row, false, true);
        }
    }

    return std::nullopt;
}

/// Map a Win32 virtual-key code for a known special key to its Luma key name.
/// Returns an empty string for keys that require character-level decoding.
[[nodiscard]] std::string map_virtual_key_name(WORD vk) {
    switch (vk) {
        case VK_RETURN:
            return "enter";
        case VK_ESCAPE:
            return "escape";
        case VK_TAB:
            return "tab";
        case VK_BACK:
            return "backspace";
        case VK_DELETE:
            return "delete";
        case VK_INSERT:
            return "insert";
        case VK_UP:
            return "up";
        case VK_DOWN:
            return "down";
        case VK_LEFT:
            return "left";
        case VK_RIGHT:
            return "right";
        case VK_HOME:
            return "home";
        case VK_END:
            return "end";
        case VK_PRIOR:
            return "page_up";
        case VK_NEXT:
            return "page_down";
        case VK_SPACE:
            return "space";
        case VK_F1:
            return "f1";
        case VK_F2:
            return "f2";
        case VK_F3:
            return "f3";
        case VK_F4:
            return "f4";
        case VK_F5:
            return "f5";
        case VK_F6:
            return "f6";
        case VK_F7:
            return "f7";
        case VK_F8:
            return "f8";
        case VK_F9:
            return "f9";
        case VK_F10:
            return "f10";
        case VK_F11:
            return "f11";
        case VK_F12:
            return "f12";
        default:
            return "";
    }
}

/// Decode a Win32 key-down event into an InputResult. Returns std::nullopt when
/// the event carries no usable key (the caller should keep reading).
[[nodiscard]] std::optional<InputResult> decode_key_event(const KEY_EVENT_RECORD& ke) {
    const DWORD ctrl = ke.dwControlKeyState;
    const bool has_ctrl = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const bool has_alt = (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
    const bool has_shift = (ctrl & SHIFT_PRESSED) != 0;

    std::string name = map_virtual_key_name(ke.wVirtualKeyCode);

    if (name.empty()) {
        const wchar_t wc = ke.uChar.UnicodeChar;

        if (wc == 0) {
            return std::nullopt;
        }

        // With ENABLE_VIRTUAL_TERMINAL_INPUT the Escape key may arrive with
        // wVirtualKeyCode == 0 and UnicodeChar == 0x1B instead of VK_ESCAPE.
        if (wc == 0x1B) {
            name = "escape";
        } else if (has_ctrl && !has_alt && wc >= 1 && wc <= 26) {
            return InputResult{.key = std::format("ctrl+{}", static_cast<char>('a' + wc - 1)),
                               .timed_out = false};
        } else {
            // Convert wide char to UTF-8.
            std::array<char, 4> utf8{};

            const int len = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8.data(),
                                                static_cast<int>(utf8.size()), nullptr, nullptr);

            if (len > 0) {
                name.assign(utf8.data(), static_cast<std::size_t>(len));
            } else {
                return std::nullopt;
            }
        }
    }

    if (name.empty()) {
        return std::nullopt;
    }

    std::string prefix{};

    if (has_ctrl && name.size() > 1) {
        prefix += "ctrl+";
    }

    if (has_alt) {
        prefix += "alt+";
    }

    if (has_shift && name.size() > 1) {
        prefix += "shift+";
    }

    return InputResult{.key = prefix + name, .timed_out = false};
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Key reading — Windows
// ═══════════════════════════════════════════════════════════

[[nodiscard]] InputResult read_input_windows(std::int64_t timeout_ms, const SourceLocation& loc,
                                             bool mouse_mode_active) {
    const HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);

    INPUT_RECORD record{};
    DWORD events_read{0};

    const auto start = std::chrono::steady_clock::now();

    while (true) {
        if (timeout_ms >= 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            const auto remaining = timeout_ms - elapsed;

            if (remaining <= 0) {
                return {.key = "", .timed_out = true};
            }

            const DWORD wait = WaitForSingleObject(hin, static_cast<DWORD>(remaining));

            if (wait == WAIT_TIMEOUT) {
                return {.key = "", .timed_out = true};
            }
        }

        if ((ReadConsoleInputW(hin, &record, 1, &events_read) == 0) || events_read == 0) {
            throw RuntimeError{"Terminal.read_key: read failed", loc,
                               "console input read returned an error"};
        }

        // Handle mouse events when mouse mode is active.
        if (mouse_mode_active && record.EventType == MOUSE_EVENT) {
            const auto& me = record.Event.MouseEvent;
            const int col = me.dwMousePosition.X + 1;
            const int row = me.dwMousePosition.Y + 1;

            const auto result = dispatch_mouse_event(me, col, row);

            if (result.has_value()) {
                return {.key = *result, .timed_out = false};
            }

            continue;
        }

        if (record.EventType != KEY_EVENT || (record.Event.KeyEvent.bKeyDown == 0)) {
            continue;
        }

        const auto decoded = decode_key_event(record.Event.KeyEvent);

        if (decoded.has_value()) {
            return *decoded;
        }
    }
}

InputResult read_input(std::int64_t timeout_ms, const SourceLocation& loc, bool mouse_mode_active) {
    return read_input_windows(timeout_ms, loc, mouse_mode_active);
}

} // namespace luma::terminal_detail
