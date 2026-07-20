#ifndef LUMA_STDLIB_TERMINAL_INPUT_COMMON_HPP
#define LUMA_STDLIB_TERMINAL_INPUT_COMMON_HPP

/// @file terminal_input_common.hpp
/// @brief Mouse event formatting helpers shared by the POSIX and Win32
///        terminal input backends.

#include <format>
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

} // namespace luma::terminal_detail

#endif // LUMA_STDLIB_TERMINAL_INPUT_COMMON_HPP
