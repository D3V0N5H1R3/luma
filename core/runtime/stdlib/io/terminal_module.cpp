#include "runtime/stdlib/io/terminal_module.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "analysis/source/source_location.hpp"
#include "common/platform_utils.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/io/platform_terminal.hpp"
#include "runtime/stdlib/io/terminal_input.hpp"
#include "runtime/stdlib/io/terminal_input_common.hpp"
#include "runtime/stdlib/io/terminal_module_internal.hpp"

namespace luma {

// ============================================================================
// terminal_internal — shared state and helpers (defined in
// terminal_module_internal.hpp, visible to terminal_module_ansi.cpp)
// ============================================================================

namespace terminal_internal {

std::mutex terminal_mutex;
std::atomic<bool> raw_mode_active{false};
std::atomic<bool> mouse_mode_active{false};

bool stdout_is_terminal() {
    if (headless_input_active.load()) {
        return true;
    }

    return platform_terminal::stdout_is_terminal();
}

void emit_unlocked(std::string_view sequence) {
    if (capture_active.load(std::memory_order_relaxed)) {
        capture_buffer.append(sequence);

        return;
    }

    std::cout << sequence << std::flush;
}

void emit(std::string_view sequence) {
    const std::scoped_lock lock{terminal_mutex};
    emit_unlocked(sequence);
}

void prepare() {
    platform_terminal::enable_vt_processing();
}

// Use static maps for O(1) lookup instead of O(n) linear scan.
struct ColorCodes {
    std::string_view fg_code;
    std::string_view bg_code;
};

static const std::unordered_map<std::string_view, ColorCodes>& color_map() {
    static const std::unordered_map<std::string_view, ColorCodes> table = {
        {"black", {.fg_code = "\033[30m", .bg_code = "\033[40m"}},
        {"blue", {.fg_code = "\033[34m", .bg_code = "\033[44m"}},
        {"bright_black", {.fg_code = "\033[90m", .bg_code = "\033[100m"}},
        {"bright_blue", {.fg_code = "\033[94m", .bg_code = "\033[104m"}},
        {"bright_cyan", {.fg_code = "\033[96m", .bg_code = "\033[106m"}},
        {"bright_green", {.fg_code = "\033[92m", .bg_code = "\033[102m"}},
        {"bright_magenta", {.fg_code = "\033[95m", .bg_code = "\033[105m"}},
        {"bright_red", {.fg_code = "\033[91m", .bg_code = "\033[101m"}},
        {"bright_white", {.fg_code = "\033[97m", .bg_code = "\033[107m"}},
        {"bright_yellow", {.fg_code = "\033[93m", .bg_code = "\033[103m"}},
        {"cyan", {.fg_code = "\033[36m", .bg_code = "\033[46m"}},
        {"default", {.fg_code = "\033[39m", .bg_code = "\033[49m"}},
        {"green", {.fg_code = "\033[32m", .bg_code = "\033[42m"}},
        {"magenta", {.fg_code = "\033[35m", .bg_code = "\033[45m"}},
        {"red", {.fg_code = "\033[31m", .bg_code = "\033[41m"}},
        {"white", {.fg_code = "\033[37m", .bg_code = "\033[47m"}},
        {"yellow", {.fg_code = "\033[33m", .bg_code = "\033[43m"}},
    };

    return table;
}

std::string valid_color_names() {
    return "black, blue, bright_black, bright_blue, bright_cyan, bright_green, "
           "bright_magenta, bright_red, bright_white, bright_yellow, cyan, "
           "default, green, magenta, red, white, yellow";
}

std::string_view fg_code_for(std::string_view name) {
    const auto& map = color_map();
    const auto it = map.find(name);
    return it != map.end() ? it->second.fg_code : std::string_view{};
}

std::string_view bg_code_for(std::string_view name) {
    const auto& map = color_map();
    const auto it = map.find(name);
    return it != map.end() ? it->second.bg_code : std::string_view{};
}

// Maps a Terminal.Color variant name (PascalCase) to the lowercase colour name
// used by color_map().  The variant list must stay in step with the Terminal.Color
// choice in core/analysis/types/stdlib_type_arities.cpp.
std::optional<std::string_view> color_name_from_variant(std::string_view variant) {
    static const std::unordered_map<std::string_view, std::string_view> table = {
        {"Black", "black"},
        {"Red", "red"},
        {"Green", "green"},
        {"Yellow", "yellow"},
        {"Blue", "blue"},
        {"Magenta", "magenta"},
        {"Cyan", "cyan"},
        {"White", "white"},
        {"BrightBlack", "bright_black"},
        {"BrightRed", "bright_red"},
        {"BrightGreen", "bright_green"},
        {"BrightYellow", "bright_yellow"},
        {"BrightBlue", "bright_blue"},
        {"BrightMagenta", "bright_magenta"},
        {"BrightCyan", "bright_cyan"},
        {"BrightWhite", "bright_white"},
        {"Default", "default"},
    };

    const auto it = table.find(variant);
    return it != table.end() ? std::optional<std::string_view>{it->second} : std::nullopt;
}

TerminalDimensions query_terminal_size() {
    TerminalDimensions dims;

    // In a headless test session there is no real terminal; report a fixed,
    // deterministic size so columns()/rows()-driven layout is reproducible.
    if (headless_input_active.load()) {
        return dims;
    }

    platform_terminal::query_terminal_size(dims.cols, dims.rows);

    return dims;
}

} // namespace terminal_internal

// ============================================================================
// Raw mode (file-local — only used in this translation unit)
// ============================================================================

namespace {

using namespace terminal_internal;

void enter_raw_mode(const SourceLocation& loc) {
    const std::scoped_lock lock{terminal_mutex};

    if (raw_mode_active) {
        return;
    }

    // In a headless test session there is no real console to reconfigure;
    // simply mark raw mode active so the program under test proceeds.
    if (headless_input_active.load()) {
        raw_mode_active = true;

        return;
    }

    platform_terminal::enter_raw_mode(loc);

    raw_mode_active = true;
}

void leave_raw_mode() {
    const std::scoped_lock lock{terminal_mutex};

    if (!raw_mode_active) {
        return;
    }

    // In a headless test session no real console mode was changed (enter_raw_mode
    // no-opped), so just clear the flags without touching the console.
    if (headless_input_active.load()) {
        mouse_mode_active = false;
        raw_mode_active = false;

        return;
    }

    if (mouse_mode_active) {
        if (const auto seq = platform_terminal::disable_mouse(); !seq.empty()) {
            emit_unlocked(seq);
        }

        mouse_mode_active = false;
    }

    platform_terminal::leave_raw_mode();

    raw_mode_active = false;
}

// Build a Terminal.InputEvent record from a decoded key string, splitting any
// leading "shift+" / "ctrl+" / "alt+" modifier prefixes into boolean fields.
// Shared by the live get_input path and the headless test harness.
[[nodiscard]] Value build_input_event(const std::string& raw_key) {
    bool shift{false};
    bool ctrl{false};
    bool alt{false};

    std::string_view remaining{raw_key};

    while (true) {
        if (remaining.starts_with("shift+")) {
            shift = true;
            remaining.remove_prefix(6);
        } else if (remaining.starts_with("ctrl+")) {
            ctrl = true;
            remaining.remove_prefix(5);
        } else if (remaining.starts_with("alt+")) {
            alt = true;
            remaining.remove_prefix(4);
        } else {
            break;
        }
    }

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "InputEvent";
    rec->fields.emplace_back("key", Value{std::string{remaining}});
    rec->fields.emplace_back("shift", Value{shift});
    rec->fields.emplace_back("control", Value{ctrl});
    rec->fields.emplace_back("alt", Value{alt});

    return Value{std::move(rec)};
}

// Parse a non-negative base-10 integer that occupies the whole view, or nullopt.
[[nodiscard]] std::optional<std::int64_t> parse_coordinate(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }

    // std::from_chars parses a leading '-' for a signed integer, which would let
    // a negative slip through (a leading '+' is already rejected).  Coordinates
    // and F-key numbers are non-negative, so reject the sign up front.
    if (text.front() == '-') {
        return std::nullopt;
    }

    std::int64_t value{0};
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);

    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }

    return value;
}

// Decode a "mouse:<kind>:<row>:<col>" event string (as produced by
// Terminal.get_input / read_key in mouse mode) into an optional<Terminal.MouseEvent>.
// Returns none (NullValue) for any string that is not a well-formed, recognised
// mouse event — a non-"mouse:" prefix, a wrong field count, an unknown kind, or
// non-integer coordinates.  The kind token never contains ':', so the body
// splits cleanly on its two remaining colons.
[[nodiscard]] Value parse_mouse_event_value(std::string_view key) {
    constexpr std::string_view k_prefix{"mouse:"};

    if (!key.starts_with(k_prefix)) {
        return Value{NullValue{}};
    }

    const std::string_view body = key.substr(k_prefix.size());

    const std::size_t first = body.find(':');
    if (first == std::string_view::npos) {
        return Value{NullValue{}};
    }

    const std::size_t second = body.find(':', first + 1);
    if (second == std::string_view::npos) {
        return Value{NullValue{}};
    }

    // A well-formed event has exactly three fields (kind, row, col); reject a
    // stray trailing colon so malformed input decodes to none, not a partial event.
    if (body.find(':', second + 1) != std::string_view::npos) {
        return Value{NullValue{}};
    }

    const std::string_view kind = body.substr(0, first);
    const std::optional<std::string_view> variant = terminal_detail::mouse_event_kind_variant(kind);
    if (!variant) {
        return Value{NullValue{}};
    }

    const std::optional<std::int64_t> row =
        parse_coordinate(body.substr(first + 1, second - (first + 1)));
    const std::optional<std::int64_t> column = parse_coordinate(body.substr(second + 1));
    if (!row || !column) {
        return Value{NullValue{}};
    }

    auto kind_choice = std::make_shared<ChoiceValue>();
    kind_choice->type_name = "MouseEventKind";
    kind_choice->variant = std::string{*variant};

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "MouseEvent";
    rec->fields.emplace_back("kind", Value{std::move(kind_choice)});
    rec->fields.emplace_back("row", Value{*row});
    rec->fields.emplace_back("column", Value{*column});

    return Value{std::move(rec)};
}

// Decode a Terminal.InputEvent.key string into a Terminal.Key choice.  Total:
// every input maps to exactly one variant.  Recognised special-key names and
// function keys ("f1".."f12", or any "f" + positive integer) take priority; the
// decoder's "unknown" fallback maps to Unknown; every other string (a printable
// character, a UTF-8 grapheme, or arbitrary text) becomes Character(<text>).
// Modifier prefixes are already stripped by build_input_event, so they never
// reach here.
[[nodiscard]] Value parse_key_value(std::string_view key) {
    static const std::unordered_map<std::string_view, std::string_view> named = {
        {"enter", "Enter"},         {"escape", "Escape"}, {"tab", "Tab"},
        {"backspace", "Backspace"}, {"space", "Space"},   {"up", "Up"},
        {"down", "Down"},           {"left", "Left"},     {"right", "Right"},
        {"home", "Home"},           {"end", "End"},       {"page_up", "PageUp"},
        {"page_down", "PageDown"},  {"insert", "Insert"}, {"delete", "Delete"},
        {"unknown", "Unknown"},
    };

    const auto make_key = [](std::string_view variant,
                             std::optional<Value> payload = std::nullopt) {
        auto cv = std::make_shared<ChoiceValue>();
        cv->type_name = "Key";
        cv->variant = std::string{variant};

        if (payload) {
            cv->fields.emplace_back(std::move(*payload));
        }

        return Value{std::move(cv)};
    };

    if (const auto it = named.find(key); it != named.end()) {
        return make_key(it->second);
    }

    // Function keys: 'f' followed by a positive integer (F1 -> Function(1)).
    if (key.size() >= 2 && key.front() == 'f') {
        if (const std::optional<std::int64_t> n = parse_coordinate(key.substr(1)); n && *n >= 1) {
            return make_key("Function", Value{*n});
        }
    }

    return make_key("Character", Value{std::string{key}});
}

// Maps a Terminal.CursorStyle variant (PascalCase) to its DECSCUSR parameter
// (`\x1b[<n> q`).  The variant list must stay in step with the
// Terminal.CursorStyle choice in core/analysis/types/stdlib_type_arities.cpp.
[[nodiscard]] std::optional<int> cursor_style_code(std::string_view variant) {
    static const std::unordered_map<std::string_view, int> table = {
        {"BlinkingBlock", 1},   {"SteadyBlock", 2}, {"BlinkingUnderline", 3},
        {"SteadyUnderline", 4}, {"BlinkingBar", 5}, {"SteadyBar", 6},
    };

    const auto it = table.find(variant);
    return it != table.end() ? std::optional<int>{it->second} : std::nullopt;
}

// True when a decoded key string carries printable text that read_line should
// append to the edited line.  Named special keys (navigation, editing, function
// keys) and modifier combinations ("ctrl+…") carry no printable text.
[[nodiscard]] bool is_printable_key(std::string_view key) {
    static const std::unordered_set<std::string_view> specials = {
        "enter", "escape", "tab", "backspace", "space",     "up",     "down",   "left",
        "right", "home",   "end", "page_up",   "page_down", "insert", "delete", "unknown",
    };

    if (key.empty() || specials.contains(key)) {
        return false;
    }

    // Modifier combinations (e.g. "ctrl+a", "alt+x") carry no printable glyph.
    if (key.find('+') != std::string_view::npos) {
        return false;
    }

    // Function keys: 'f' followed by digits (f1..f12) are not printable text.
    if (key.size() >= 2 && key.front() == 'f' &&
        std::all_of(key.begin() + 1, key.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return false;
    }

    return true;
}

// Best-effort, heuristic detection of whether the terminal can render Unicode.
// On Windows the active output code page 65001 (CP_UTF8) is treated as capable;
// on every platform an LC_ALL / LC_CTYPE / LANG locale advertising "UTF-8" /
// "utf8" is treated as capable.  This is a hint, not a guarantee — fonts and
// remote terminals may still lack glyph coverage.
[[nodiscard]] bool detect_unicode_support() {
#ifdef _WIN32
    if (GetConsoleOutputCP() == 65001) {
        return true;
    }
#endif

    for (const char* const var : {"LC_ALL", "LC_CTYPE", "LANG"}) {
        if (const auto value = luma::safe_getenv(var); value.has_value()) {
            std::string lowered{*value};
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (lowered.find("utf-8") != std::string::npos ||
                lowered.find("utf8") != std::string::npos) {
                return true;
            }
        }
    }

    return false;
}

} // namespace

// ============================================================================
// Registration
// ============================================================================

void register_terminal_ns(const EnvPtr& env) {
    using namespace terminal_internal;

    // === Terminal information ===

    ModuleBuilder{"Terminal", env}
        .func("is_terminal", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            return Value{stdout_is_terminal()};
        })
        .func("size", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            const auto [cols, rows] = query_terminal_size();

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "Size";
            rec->fields.emplace_back("columns", Value{static_cast<std::int64_t>(cols)});
            rec->fields.emplace_back("rows", Value{static_cast<std::int64_t>(rows)});

            return Value{std::move(rec)};
        })
        .func("columns", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            return Value{static_cast<std::int64_t>(query_terminal_size().cols)};
        })
        .func("rows", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            return Value{static_cast<std::int64_t>(query_terminal_size().rows)};
        });

    // === Raw mode & key input ===

    ModuleBuilder{"Terminal", env}
        .func("enable_raw_mode", 0)
        .raw_body([](std::span<const Value>, SourceLocation loc) -> Value {
            return apply_with_error_handling([&]() -> Value {
                enter_raw_mode(loc);

                return Value{NullValue{}};
            });
        })
        .func("disable_raw_mode", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            leave_raw_mode();

            return NullValue{};
        })
        .func("is_in_raw_mode", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            return Value{raw_mode_active.load()};
        })
        .func("read_key", 0)
        .raw_body([](std::span<const Value>, SourceLocation loc) -> Value {
            if (headless_input_is_active()) {
                const auto key = next_scripted_key();

                if (!key) {
                    return make_failure_value("Terminal.read_key: end of scripted input");
                }

                return make_success_value(Value{*key});
            }

            if (!raw_mode_active) {
                return make_failure_value(error_msg("Terminal", "read_key", k_raw_mode_required));
            }

            try {
                return make_success_value(
                    Value{terminal_detail::read_input(-1, loc, mouse_mode_active).key});
            } catch (const RuntimeError& e) {
                return failure_from_exception(e);
            }
        })
        .func("read_key_timeout", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto ms = args[0].as_integer();

            if (ms < 0) {
                throw RuntimeError{"Terminal.read_key_timeout: timeout must be >= 0", loc,
                                   "timeout cannot be negative"};
            }

            if (headless_input_is_active()) {
                const auto key = next_scripted_key();

                if (!key) {
                    // A drained scripted queue behaves like an idle terminal.
                    return make_failure_value("timeout");
                }

                return make_success_value(Value{*key});
            }

            if (!raw_mode_active) {
                return make_failure_value(
                    error_msg("Terminal", "read_key_timeout", k_raw_mode_required));
            }

            auto [key, timed_out] = terminal_detail::read_input(ms, loc, mouse_mode_active);

            if (timed_out) {
                return make_failure_value("timeout");
            }

            return make_success_value(Value{key});
        })
        .func("get_input", 0)
        .raw_body([](std::span<const Value>, SourceLocation loc) -> Value {
            if (headless_input_is_active()) {
                const auto key = next_scripted_key();

                if (!key) {
                    return make_failure_value("Terminal.get_input: end of scripted input");
                }

                return make_success_value(build_input_event(*key));
            }

            if (!raw_mode_active) {
                return make_failure_value(error_msg("Terminal", "get_input", k_raw_mode_required));
            }

            try {
                auto raw_key = terminal_detail::read_input(-1, loc, mouse_mode_active).key;

                return make_success_value(build_input_event(raw_key));
            } catch (const RuntimeError& e) {
                return failure_from_exception(e);
            }
        });

    // === Mouse input ===

    ModuleBuilder{"Terminal", env}
        .func("enable_mouse", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            if (headless_input_is_active()) {
                // No real console to reconfigure in a headless test session;
                // record the mode so scripted mouse events are interpreted.
                mouse_mode_active = true;

                return make_success_value(Value{NullValue{}});
            }

            prepare();

            if (!raw_mode_active) {
                return make_failure_value(
                    error_msg("Terminal", "enable_mouse", k_raw_mode_required));
            }

            if (const auto seq = platform_terminal::enable_mouse(); !seq.empty()) {
                emit(seq);
            }

            mouse_mode_active = true;

            return make_success_value(Value{NullValue{}});
        })
        .func("disable_mouse", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            if (headless_input_is_active()) {
                mouse_mode_active = false;

                return NullValue{};
            }

            prepare();

            if (const auto seq = platform_terminal::disable_mouse(); !seq.empty()) {
                emit(seq);
            }

            mouse_mode_active = false;

            return NullValue{};
        })
        .func("is_mouse_enabled", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            return Value{mouse_mode_active.load()};
        })
        .func("parse_mouse_event", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& key = expect_string(args[0], "Terminal.parse_mouse_event", loc);

            return parse_mouse_event_value(key);
        })
        .func("parse_key", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& key = expect_string(args[0], "Terminal.parse_key", loc);

            return parse_key_value(key);
        });

    // === Window title ===

    ModuleBuilder{"Terminal", env}
        .func("set_title", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            prepare();

            (void)expect_string(args[0], "Terminal.set_title", loc);

            emit(std::format("\033]2;{}\033\\", args[0].as_string()));

            return NullValue{};
        });

    // === Capability detection ===

    ModuleBuilder{"Terminal", env}
        .func("supports_color", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            if (!stdout_is_terminal()) {
                return Value{false};
            }

            return Value{platform_terminal::supports_color()};
        })
        .func("supports_true_color", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            if (!stdout_is_terminal()) {
                return Value{false};
            }

            return Value{platform_terminal::supports_true_color()};
        });

    // === Escape timeout ===

    ModuleBuilder{"Terminal", env}
        .func("set_escape_timeout", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto ms = expect_integer(args[0], "Terminal.set_escape_timeout", loc);

            if (ms < 1 || ms > 5000) {
                throw RuntimeError{
                    "Terminal.set_escape_timeout: timeout must be between 1 and 5000 ms", loc,
                    "pass a timeout between 1 and 5000 milliseconds"};
            }

            terminal_detail::escape_timeout_ms.store(ms, std::memory_order_relaxed);

            return NullValue{};
        })
        .func("get_escape_timeout", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            return Value{terminal_detail::escape_timeout_ms.load(std::memory_order_relaxed)};
        });

    // === Buffered output control ===

    ModuleBuilder{"Terminal", env}
        .func("flush", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            // Flush any output buffered by prior writes / cursor moves so it
            // appears immediately.  Under output capture (headless tests) there
            // is no real stream to flush; report success either way.
            std::cout.flush();

            return make_success_value(Value{true});
        });

    // === Line editing input ===

    ModuleBuilder{"Terminal", env}
        .func("read_line", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const std::string& prompt = expect_string(args[0], "Terminal.read_line", loc);

            const bool headless = headless_input_is_active();

            if (!headless && !raw_mode_active) {
                return make_failure_value(error_msg("Terminal", "read_line", k_raw_mode_required));
            }

            emit(prompt);

            std::string line;

            while (true) {
                std::string key;

                if (headless) {
                    const auto scripted = next_scripted_key();

                    if (!scripted) {
                        return make_failure_value(
                            error_msg("Terminal", "read_line", "end of scripted input"));
                    }

                    key = *scripted;
                } else {
                    try {
                        key = terminal_detail::read_input(-1, loc, mouse_mode_active).key;
                    } catch (const RuntimeError& e) {
                        return failure_from_exception(e);
                    }
                }

                if (key == "enter") {
                    emit("\r\n");

                    return make_success_value(Value{line});
                }

                if (key == "ctrl+c" || key == "ctrl+d") {
                    return make_failure_value(
                        error_msg("Terminal", "read_line", "input interrupted"));
                }

                if (key == "backspace") {
                    if (!line.empty()) {
                        line.pop_back();
                        emit("\b \b");
                    }

                    continue;
                }

                if (key == "space") {
                    line += ' ';
                    emit(" ");

                    continue;
                }

                if (is_printable_key(key)) {
                    line += key;
                    emit(key);
                }
            }
        });

    // === Cursor shape ===

    ModuleBuilder{"Terminal", env}
        .func("set_cursor_style", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            prepare();

            const Value& arg = args[0];

            if (!arg.is_choice()) {
                throw RuntimeError{
                    "Terminal.set_cursor_style: style must be a Terminal.CursorStyle", loc,
                    "pass a Terminal.CursorStyle variant, e.g. Terminal.CursorStyle.SteadyBar"};
            }

            const auto& variant = arg.as_choice()->variant;
            const auto code = cursor_style_code(variant);

            if (!code) {
                throw RuntimeError{
                    std::format("Terminal.set_cursor_style: unknown style "
                                "'Terminal.CursorStyle.{}'",
                                variant),
                    loc, "use a Terminal.CursorStyle variant, e.g. Terminal.CursorStyle.SteadyBar"};
            }

            emit(std::format("\033[{} q", *code));

            return make_success_value(Value{true});
        });

    // === Blink styling ===

    ModuleBuilder{"Terminal", env}
        .func("blink", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            return Value{std::format("\033[5m{}\033[0m", args[0].to_string())};
        });

    // === Line insert / delete ===

    ModuleBuilder{"Terminal", env}
        .func("insert_line", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            prepare();

            const auto n = expect_integer(args[0], "Terminal.insert_line", loc);

            if (n < 1) {
                return make_failure_value(
                    error_msg("Terminal", "insert_line", "count must be >= 1"));
            }

            emit(std::format("\033[{}L", n));

            return make_success_value(Value{true});
        })
        .func("delete_line", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            prepare();

            const auto n = expect_integer(args[0], "Terminal.delete_line", loc);

            if (n < 1) {
                return make_failure_value(
                    error_msg("Terminal", "delete_line", "count must be >= 1"));
            }

            emit(std::format("\033[{}M", n));

            return make_success_value(Value{true});
        })
        .func("clear_to_start_of_line", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit("\033[1K");

            return make_success_value(Value{true});
        });

    // === Unicode capability detection ===

    ModuleBuilder{"Terminal", env}
        .func("supports_unicode", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            return Value{detect_unicode_support()};
        });

    // === ANSI escape code registrations (split into terminal_module_ansi.cpp) ===

    register_terminal_ansi(env);

    // === Headless interaction-testing API (split into terminal_testing.cpp) ===

    register_terminal_testing(env);
}

} // namespace luma
