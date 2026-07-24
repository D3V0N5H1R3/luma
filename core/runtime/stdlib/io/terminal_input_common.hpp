#ifndef LUMA_STDLIB_TERMINAL_INPUT_COMMON_HPP
#define LUMA_STDLIB_TERMINAL_INPUT_COMMON_HPP

/// @file terminal_input_common.hpp
/// @brief Mouse event formatting (and inverse decoding) helpers shared by the
///        POSIX and Win32 terminal input backends and Terminal.parse_mouse_event.

#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace luma::terminal_detail {

// Mouse button identifiers used by SGR mouse protocol and Win32 mouse events.
inline constexpr int k_mouse_left = 0;
inline constexpr int k_mouse_middle = 1;
inline constexpr int k_mouse_right = 2;
inline constexpr int k_mouse_wheel_up = 64;
inline constexpr int k_mouse_wheel_down = 65;
inline constexpr int k_mouse_wheel_left = 66;
inline constexpr int k_mouse_wheel_right = 67;

// Drag events have bit 5 set (button codes 32-63 in SGR protocol).
inline constexpr int k_mouse_drag_flag = 32;
inline constexpr int k_mouse_drag_limit = 64;

[[nodiscard]] inline std::string format_mouse_button(int button) {
    switch (button & 0x03) {
        case k_mouse_left:
            return "left";
        case k_mouse_middle:
            return "middle";
        case k_mouse_right:
            return "right";
        default:
            return "unknown";
    }
}

[[nodiscard]] inline std::string format_mouse_event(int button, int col, int row, bool is_release,
                                                    bool is_drag = false) {
    if (button >= k_mouse_wheel_up && button <= k_mouse_wheel_right) {
        std::string_view dir;

        switch (button) {
            case k_mouse_wheel_up:
                dir = "wheel_up";
                break;
            case k_mouse_wheel_down:
                dir = "wheel_down";
                break;
            case k_mouse_wheel_left:
                dir = "wheel_left";
                break;
            case k_mouse_wheel_right:
                dir = "wheel_right";
                break;
            default:
                dir = "wheel_unknown";
                break;
        }

        return std::format("mouse:{}:{}:{}", dir, row, col);
    }

    const std::string btn{format_mouse_button(button)};

    std::string_view action = "press";
    if (is_release) {
        action = "release";
    } else if (is_drag) {
        action = "drag";
    }

    return std::format("mouse:{}_{}:{}:{}", btn, action, row, col);
}

// Inverse of the `<kind>` token that format_mouse_event emits: maps a mouse
// event kind (e.g. "left_press", "wheel_up") to its Terminal.MouseEventKind
// variant name (e.g. "LeftPress", "WheelUp"), or nullopt for an unrecognised
// token.  Keeping this beside format_mouse_event keeps the two in lockstep; the
// returned names must match the Terminal.MouseEventKind ChoiceDeclaration in
// core/analysis/types/stdlib_type_arities.cpp exactly.  The degenerate tokens
// format_mouse_button/format_mouse_event can emit for malformed input
// ("unknown_*", "wheel_unknown") are intentionally absent, so they decode to
// none rather than a bogus variant.
[[nodiscard]] inline std::optional<std::string_view> mouse_event_kind_variant(
    std::string_view kind) {
    if (kind == "left_press") {
        return "LeftPress";
    }
    if (kind == "left_release") {
        return "LeftRelease";
    }
    if (kind == "left_drag") {
        return "LeftDrag";
    }
    if (kind == "middle_press") {
        return "MiddlePress";
    }
    if (kind == "middle_release") {
        return "MiddleRelease";
    }
    if (kind == "middle_drag") {
        return "MiddleDrag";
    }
    if (kind == "right_press") {
        return "RightPress";
    }
    if (kind == "right_release") {
        return "RightRelease";
    }
    if (kind == "right_drag") {
        return "RightDrag";
    }
    if (kind == "wheel_up") {
        return "WheelUp";
    }
    if (kind == "wheel_down") {
        return "WheelDown";
    }
    if (kind == "wheel_left") {
        return "WheelLeft";
    }
    if (kind == "wheel_right") {
        return "WheelRight";
    }

    return std::nullopt;
}

} // namespace luma::terminal_detail

#endif // LUMA_STDLIB_TERMINAL_INPUT_COMMON_HPP
