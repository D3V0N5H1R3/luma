// Terminal module — ANSI escape code generation for cursor, screen,
// styling, colors, scroll control, and hyperlinks.
// Split from terminal_module.cpp for readability.  Registered by
// register_terminal_ansi() called from register_terminal_ns().

#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/io/terminal_input.hpp"
#include "runtime/stdlib/io/terminal_module.hpp"
#include "runtime/stdlib/io/terminal_module_internal.hpp"

namespace luma {

using namespace terminal_internal;

namespace {

// Named ANSI escape sequences for terminal control.
constexpr std::string_view k_ansi_clear_screen = "\033[2J\033[H";
constexpr std::string_view k_ansi_clear_line = "\033[2K\r";
constexpr std::string_view k_ansi_clear_to_eol = "\033[K";
constexpr std::string_view k_ansi_clear_to_eos = "\033[J";

// Build a raw_body lambda for cursor movement commands that differ only
// in the ANSI escape code suffix character (A=up, B=down, C=right, D=left).
auto movement_body(char code) {
    return [code](std::span<const Value> args, SourceLocation) -> Value {
        prepare();

        const auto n = args[0].as_integer();

        if (n > 0) {
            emit(std::format("\033[{}{}", n, code));
        }

        return NullValue{};
    };
}

// Standard "unknown color" failure.  Lists the valid names from the single
// source of truth (valid_color_names()) so color() and background_color() can
// never drift out of sync with color_map().
[[nodiscard]] Value unknown_color_failure(std::string_view function, std::string_view color_name) {
    return make_failure_value(error_msg(
        "Terminal", function,
        std::format("unknown color '{}'. Valid colors: {}", color_name, valid_color_names())));
}

// Resolves a colour argument that may be either a Terminal.Color choice variant
// or a colour-name string to the lowercase colour name used by color_map(),
// mirroring the dual choice/string acceptance of Decimal.round.  A choice maps
// its variant (BrightBlack → "bright_black"); a string passes through unchanged
// so an unknown name still yields the domain "unknown color" failure result.  A
// value of any other type is a programmer error and throws — the choice path is
// total, so the typed form never fails.
[[nodiscard]] std::string resolve_color_name(const Value& arg, std::string_view fn,
                                             const SourceLocation& loc) {
    if (arg.is_choice()) {
        const auto& variant = arg.as_choice()->variant;

        if (auto name = color_name_from_variant(variant)) {
            return std::string{*name};
        }

        throw RuntimeError{std::format("{}: unknown colour 'Terminal.Color.{}'", fn, variant), loc,
                           "use a Terminal.Color variant, e.g. Terminal.Color.Red"};
    }

    if (arg.is_string()) {
        return arg.as_string();
    }

    throw RuntimeError{
        std::format("{}: colour must be a Terminal.Color or a colour-name string", fn), loc,
        "pass a Terminal.Color variant (e.g. Terminal.Color.Red) or a string (e.g. \"red\")"};
}

// True when every RGB component is within the representable 0-255 range.
[[nodiscard]] bool rgb_components_valid(std::int64_t r, std::int64_t g, std::int64_t b) {
    const auto in_range = [](std::int64_t v) {
        return v >= 0 && v <= 255;
    };

    return in_range(r) && in_range(g) && in_range(b);
}

// ─── Terminal.Style support ────────────────────────────────────────────────

// Builds a Terminal.Color choice value (runtime short name "Color", matching the
// postamble registration in stdlib_registry.hpp) for the given variant.
[[nodiscard]] Value make_color_choice(std::string_view variant) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "Color";
    cv->variant = std::string{variant};
    return Value{std::move(cv)};
}

// Builds the default Terminal.Style record: both colours Default (leave
// unchanged) and every attribute off.  Callers override individual fields with a
// record-update (`with`).  The eight field names must match the RecordDeclaration
// in core/analysis/types/stdlib_type_arities.cpp exactly.
[[nodiscard]] Value make_plain_style_record() {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Style";
    rec->fields.emplace_back("foreground", make_color_choice("Default"));
    rec->fields.emplace_back("background", make_color_choice("Default"));
    rec->fields.emplace_back("bold", Value{false});
    rec->fields.emplace_back("dim", Value{false});
    rec->fields.emplace_back("italic", Value{false});
    rec->fields.emplace_back("underline", Value{false});
    rec->fields.emplace_back("inverse", Value{false});
    rec->fields.emplace_back("strikethrough", Value{false});
    return Value{std::move(rec)};
}

// Reads a boolean attribute field from a Terminal.Style record, defaulting to
// false when the field is absent or not a boolean (defensive — the type checker
// guarantees the shape).
[[nodiscard]] bool style_flag(const RecordValue& rec, std::string_view field) {
    const auto* value = rec.find_field(field);
    return value != nullptr && value->is_bool() && value->as_bool();
}

// Resolves a Terminal.Style colour field to its full ANSI set-code, or an empty
// string when the colour is Default (leave unchanged) or unrecognised.  A choice
// maps its variant (BrightBlack → "bright_black"); a string passes through.
[[nodiscard]] std::string style_color_code(const RecordValue& rec, std::string_view field,
                                           bool background) {
    const auto* value = rec.find_field(field);
    if (value == nullptr) {
        return {};
    }

    std::string name;
    if (value->is_choice()) {
        const auto& variant = value->as_choice()->variant;
        if (variant == "Default") {
            return {};
        }
        if (auto mapped = color_name_from_variant(variant)) {
            name = std::string{*mapped};
        } else {
            return {};
        }
    } else if (value->is_string()) {
        name = value->as_string();
    } else {
        return {};
    }

    const auto code = background ? bg_code_for(name) : fg_code_for(name);
    return std::string{code};
}

} // namespace

void register_terminal_ansi(const EnvPtr& env) {
    // === Screen control ===

    ModuleBuilder{"Terminal", env}
        .func("clear_screen", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit(k_ansi_clear_screen);

            return NullValue{};
        })
        .func("clear_line", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit(k_ansi_clear_line);

            return NullValue{};
        })
        .func("clear_to_end_of_line", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit(k_ansi_clear_to_eol);

            return NullValue{};
        })
        .func("clear_to_end_of_screen", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit(k_ansi_clear_to_eos);

            return NullValue{};
        });

    // === Cursor movement ===

    ModuleBuilder{"Terminal", env}
        .func("move_to", 2)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            const auto row = args[0].as_integer();
            const auto col = args[1].as_integer();

            if (row < 1 || col < 1) {
                return make_failure_value(
                    error_msg("Terminal", "move_to", "row and column must be >= 1"));
            }

            emit(std::format("\033[{};{}H", row, col));

            return make_success_value(Value{NullValue{}});
        })
        .func("move_up", 1)
        .raw_body(movement_body('A'))
        .func("move_down", 1)
        .raw_body(movement_body('B'))
        .func("move_right", 1)
        .raw_body(movement_body('C'))
        .func("move_left", 1)
        .raw_body(movement_body('D'))
        .func("move_to_column", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            const auto col = args[0].as_integer();

            if (col < 1) {
                return make_failure_value(
                    error_msg("Terminal", "move_to_column", "column must be >= 1"));
            }

            emit(std::format("\033[{}G", col));

            return make_success_value(Value{NullValue{}});
        })
        .func("move_to_row", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            const auto row = args[0].as_integer();

            if (row < 1) {
                return make_failure_value(error_msg("Terminal", "move_to_row", "row must be >= 1"));
            }

            emit(std::format("\033[{}d", row));

            return make_success_value(Value{NullValue{}});
        });

    // === Cursor visibility & save/restore ===

    ModuleBuilder{"Terminal", env}
        .func("hide_cursor", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit("\033[?25l");

            return NullValue{};
        })
        .func("show_cursor", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit("\033[?25h");

            return NullValue{};
        })
        .func("save_cursor", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit("\033[s");

            return NullValue{};
        })
        .func("restore_cursor", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit("\033[u");

            return NullValue{};
        })
        .func("get_cursor_position", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            if (headless_input_is_active()) {
                // No real terminal to query (CPR needs a live TTY round-trip);
                // report failure so callers exercise their failure(_) branch.
                return make_failure_value(
                    error_msg("Terminal", "get_cursor_position",
                              "cursor position is unavailable in a headless test session"));
            }

            if (!raw_mode_active) {
                return make_failure_value(
                    error_msg("Terminal", "get_cursor_position", k_raw_mode_required));
            }

            int row{1};
            int col{1};

#ifdef _WIN32
            const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

            CONSOLE_SCREEN_BUFFER_INFO csbi;

            if (!GetConsoleScreenBufferInfo(h, &csbi)) {
                return make_failure_value(
                    error_msg("Terminal", "get_cursor_position", "failed to read cursor position"));
            }

            row = csbi.dwCursorPosition.Y + 1;
            col = csbi.dwCursorPosition.X + 1;
#else
            emit("\033[6n");

            if (!terminal_detail::read_cpr_response(row, col)) {
                return make_failure_value(error_msg("Terminal", "get_cursor_position",
                                                    "failed to read cursor position response"));
            }
#endif

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "CursorPosition";
            rec->fields.emplace_back("row", Value{static_cast<std::int64_t>(row)});
            rec->fields.emplace_back("column", Value{static_cast<std::int64_t>(col)});

            return make_success_value(Value{std::move(rec)});
        });

    // === Text styling ===

    ModuleBuilder{"Terminal", env}
        .func("reset_style", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit("\033[0m");

            return NullValue{};
        })
        .func("bold", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            return Value{std::format("\033[1m{}\033[22m", args[0].to_string())};
        })
        .func("dim", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            return Value{std::format("\033[2m{}\033[22m", args[0].to_string())};
        })
        .func("italic", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            return Value{std::format("\033[3m{}\033[23m", args[0].to_string())};
        })
        .func("underline", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            return Value{std::format("\033[4m{}\033[24m", args[0].to_string())};
        })
        .func("strikethrough", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            return Value{std::format("\033[9m{}\033[29m", args[0].to_string())};
        })
        .func("inverse", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            return Value{std::format("\033[7m{}\033[27m", args[0].to_string())};
        })
        .func("plain_style", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            return make_plain_style_record();
        })
        .func("styled", 2)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            const auto& style = *args[1].as_record();

            // Compose one combined ANSI sequence — attributes first, then the
            // foreground and background set-codes — followed by the text and a
            // single reset, replacing the order-sensitive nesting of
            // bold(color(...)).  Like the per-attribute helpers above, this emits
            // ANSI unconditionally; a caller that wants to suppress codes when
            // output is not a terminal can guard on Terminal.supports_color().
            std::string sequence;

            if (style_flag(style, "bold")) {
                sequence += "\033[1m";
            }
            if (style_flag(style, "dim")) {
                sequence += "\033[2m";
            }
            if (style_flag(style, "italic")) {
                sequence += "\033[3m";
            }
            if (style_flag(style, "underline")) {
                sequence += "\033[4m";
            }
            if (style_flag(style, "inverse")) {
                sequence += "\033[7m";
            }
            if (style_flag(style, "strikethrough")) {
                sequence += "\033[9m";
            }

            sequence += style_color_code(style, "foreground", false);
            sequence += style_color_code(style, "background", true);

            const auto text = args[0].to_string();

            // A fully-default style adds nothing, so return the text unchanged
            // rather than emitting a bare reset.
            if (sequence.empty()) {
                return Value{text};
            }

            return Value{std::format("{}{}\033[0m", sequence, text)};
        });

    // === Foreground / background color ===

    ModuleBuilder{"Terminal", env}
        .func("color", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            prepare();

            const auto color_name = resolve_color_name(args[0], "Terminal.color", loc);
            const auto fg = fg_code_for(color_name);

            if (fg.empty()) {
                return unknown_color_failure("color", color_name);
            }

            return make_success_value(Value{std::format("{}{}\033[39m", fg, args[1].to_string())});
        })
        .func("background_color", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            prepare();

            const auto color_name = resolve_color_name(args[0], "Terminal.background_color", loc);
            const auto bg = bg_code_for(color_name);

            if (bg.empty()) {
                return unknown_color_failure("background_color", color_name);
            }

            return make_success_value(Value{std::format("{}{}\033[49m", bg, args[1].to_string())});
        })
        .func("rgb_color", 4)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            const auto r = args[0].as_integer();
            const auto g = args[1].as_integer();
            const auto b = args[2].as_integer();

            if (!rgb_components_valid(r, g, b)) {
                return make_failure_value(
                    error_msg("Terminal", "rgb_color", "RGB values must be 0-255"));
            }

            return make_success_value(
                Value{std::format("\033[38;2;{};{};{}m{}\033[39m", r, g, b, args[3].to_string())});
        })
        .func("rgb_background_color", 4)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            const auto r = args[0].as_integer();
            const auto g = args[1].as_integer();
            const auto b = args[2].as_integer();

            if (!rgb_components_valid(r, g, b)) {
                return make_failure_value(
                    error_msg("Terminal", "rgb_background_color", "RGB values must be 0-255"));
            }

            return make_success_value(
                Value{std::format("\033[48;2;{};{};{}m{}\033[49m", r, g, b, args[3].to_string())});
        });

    // === Output helpers ===

    // Terminal.write is variadic (min 1 arg); use native() to skip the exact-arity check.
    ModuleBuilder{"Terminal", env}
        .native("write",
                [](std::span<const Value> args, SourceLocation loc) -> Value {
                    expect_min_args("Terminal.write", args, 1, loc);

                    std::string out;

                    for (std::size_t i{0}; i < args.size(); ++i) {
                        if (i > 0) {
                            out += ' ';
                        }

                        out += args[i].to_string();
                    }

                    emit(out);

                    return NullValue{};
                })
        .func("overwrite_line", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            emit("\r\033[2K" + args[0].to_string());

            return NullValue{};
        });

    // === Alternate screen buffer ===

    ModuleBuilder{"Terminal", env}
        .func("enter_alternate_screen", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit("\033[?1049h");

            return NullValue{};
        })
        .func("leave_alternate_screen", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit("\033[?1049l");

            return NullValue{};
        });

    // === Scroll control ===

    ModuleBuilder{"Terminal", env}
        .func("scroll_up", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            const auto n = args[0].as_integer();

            if (n > 0) {
                emit(std::format("\033[{}S", n));
            }

            return NullValue{};
        })
        .func("scroll_down", 1)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            const auto n = args[0].as_integer();

            if (n > 0) {
                emit(std::format("\033[{}T", n));
            }

            return NullValue{};
        })
        .func("set_scroll_region", 2)
        .raw_body([](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            const auto top = args[0].as_integer();
            const auto bottom = args[1].as_integer();

            if (top < 1 || bottom < 1 || top >= bottom) {
                return make_failure_value("Terminal.set_scroll_region: top and bottom must be "
                                          ">= 1 and top must be less than bottom");
            }

            emit(std::format("\033[{};{}r", top, bottom));

            return make_success_value(Value{NullValue{}});
        })
        .func("reset_scroll_region", 0)
        .raw_body([](std::span<const Value>, SourceLocation) -> Value {
            prepare();

            emit("\033[r");

            return NullValue{};
        });

    // === Bell / notification ===

    ModuleBuilder{"Terminal", env}.func("bell", 0).raw_body(
        [](std::span<const Value>, SourceLocation) -> Value {
            emit("\a");

            return NullValue{};
        });

    // === Link (OSC 8 hyperlink) ===

    ModuleBuilder{"Terminal", env}.func("link", 2).raw_body(
        [](std::span<const Value> args, SourceLocation) -> Value {
            prepare();

            const auto& url = args[0].as_string();
            const auto& text = args[1].as_string();

            return Value{std::format("\033]8;;{}\033\\{}\033]8;;\033\\", url, text)};
        });
}

} // namespace luma
