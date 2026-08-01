// Standard library tests: Terminal.

#include <string>
#include <utility>

#include "runtime/stdlib/io/terminal_input_common.hpp"
#include "runtime/stdlib/io/terminal_key_decoder.hpp"
#include "stdlib_test_helpers.hpp"

static void test_terminal_bold() {
    const auto v = eval("Terminal.bold(\"hello\")");

    ASSERT_TRUE(v.is_string());
    ASSERT_TRUE(v.as_string().find("hello") != std::string::npos);
}

static void test_terminal_color() {
    const auto v = eval("Terminal.color(\"red\", \"warning\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_string().find("warning") != std::string::npos);
}

static void test_terminal_color_invalid() {
    ASSERT_EVAL_FAILURE("Terminal.color(\"not_a_color\", \"text\")");
}

// ─── Terminal.Color choice (type-safe colour, choice-or-string) ───────────────

static void test_terminal_color_choice() {
    // A Terminal.Color variant selects the same ANSI code as its string name.
    const auto v = eval("Terminal.color(Terminal.Color.Red, \"warning\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& out = v.as_result()->owned_inner->as_string();

    ASSERT_TRUE(out.find("warning") != std::string::npos);
    ASSERT_TRUE(out.find("\033[31m") != std::string::npos);
}

static void test_terminal_color_choice_bright() {
    // A bright variant maps PascalCase → snake_case (BrightBlack → bright_black).
    const auto v = eval("Terminal.color(Terminal.Color.BrightBlack, \"dim\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_string().find("\033[90m") != std::string::npos);
}

static void test_terminal_color_choice_matches_string() {
    // The choice form and the equivalent string produce identical output.
    const auto by_choice = eval("Terminal.color(Terminal.Color.Green, \"ok\")");
    const auto by_string = eval("Terminal.color(\"green\", \"ok\")");

    ASSERT_RESULT_SUCCESS(by_choice);
    ASSERT_RESULT_SUCCESS(by_string);
    ASSERT_EQ(by_choice.as_result()->owned_inner->as_string(),
              by_string.as_result()->owned_inner->as_string());
}

// ─── Terminal.Style record + Terminal.styled ─────────────────────────────────

static void test_terminal_plain_style_defaults() {
    const auto v = eval("Terminal.plain_style()");

    ASSERT_TRUE(v.is_record());

    const auto& rec = *v.as_record();

    // Both colours default and every attribute off.
    ASSERT_EQ(rec.find_field("foreground")->as_choice()->variant, "Default");
    ASSERT_EQ(rec.find_field("background")->as_choice()->variant, "Default");
    ASSERT_FALSE(rec.find_field("bold")->as_bool());
    ASSERT_FALSE(rec.find_field("strikethrough")->as_bool());
}

static void test_terminal_styled_plain_is_unchanged() {
    // A fully-default style adds no codes, so the text is returned verbatim.
    const auto v = eval("Terminal.styled(\"hi\", Terminal.plain_style())");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hi");
}

static void test_terminal_styled_bold() {
    const auto v = eval("Terminal.styled(\"hi\", Terminal.plain_style() with { bold = true })");

    ASSERT_TRUE(v.is_string());

    const auto& out = v.as_string();

    ASSERT_TRUE(out.find("\033[1m") != std::string::npos);
    ASSERT_TRUE(out.find("hi") != std::string::npos);
    // A single trailing reset closes the whole sequence.
    ASSERT_TRUE(out.rfind("\033[0m") == out.size() - 4);
}

static void test_terminal_styled_composes_color_and_attributes() {
    // "bold red on white" in one call — the nest-free replacement for
    // Terminal.bold(color(background_color(...))).
    const auto v =
        eval("Terminal.styled(\"x\", Terminal.plain_style() with { bold = true, foreground = "
             "Terminal.Color.Red, background = Terminal.Color.White })");

    ASSERT_TRUE(v.is_string());

    const auto& out = v.as_string();

    ASSERT_TRUE(out.find("\033[1m") != std::string::npos);  // bold
    ASSERT_TRUE(out.find("\033[31m") != std::string::npos); // red foreground
    ASSERT_TRUE(out.find("\033[47m") != std::string::npos); // white background
    ASSERT_TRUE(out.find("x") != std::string::npos);
    ASSERT_TRUE(out.rfind("\033[0m") == out.size() - 4);
}

static void test_terminal_color_rejects_non_color_arg() {
    // A non-string, non-choice colour argument is a programmer error and throws.
    ASSERT_TRUE(luma::test::eval_throws("Terminal.color(123, \"text\")"));
}

static void test_terminal_columns_rows() {
    auto v = eval("Terminal.columns()");

    ASSERT_TRUE(v.is_integer());
    ASSERT_TRUE(v.as_integer() > 0);

    v = eval("Terminal.rows()");

    ASSERT_TRUE(v.is_integer());
    ASSERT_TRUE(v.as_integer() > 0);
}

static void test_terminal_is_mouse() {
    const auto v = eval("Terminal.is_mouse_enabled()");

    ASSERT_TRUE(v.is_bool());
    ASSERT_FALSE(v.as_bool());
}

static void test_terminal_is_raw_mode() {
    // Raw mode should be off by default.
    const auto v = eval("Terminal.is_in_raw_mode()");

    ASSERT_TRUE(v.is_bool());
    ASSERT_FALSE(v.as_bool());
}

static void test_terminal_is_terminal() {
    const auto v = eval("Terminal.is_terminal()");

    ASSERT_TRUE(v.is_bool());
}

static void test_terminal_link() {
    const auto v = eval("Terminal.link(\"https://example.com\", \"click\")");

    ASSERT_TRUE(v.is_string());
    ASSERT_TRUE(v.as_string().find("click") != std::string::npos);
}

static void test_terminal_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Terminal.is_terminal"));
    ASSERT_TRUE(env->has("Terminal.size"));
    ASSERT_TRUE(env->has("Terminal.columns"));
    ASSERT_TRUE(env->has("Terminal.rows"));
    ASSERT_TRUE(env->has("Terminal.clear_screen"));
    ASSERT_TRUE(env->has("Terminal.clear_line"));
    ASSERT_TRUE(env->has("Terminal.move_to"));
    ASSERT_TRUE(env->has("Terminal.move_up"));
    ASSERT_TRUE(env->has("Terminal.move_down"));
    ASSERT_TRUE(env->has("Terminal.move_right"));
    ASSERT_TRUE(env->has("Terminal.move_left"));
    ASSERT_TRUE(env->has("Terminal.move_to_column"));
    ASSERT_TRUE(env->has("Terminal.move_to_row"));
    ASSERT_TRUE(env->has("Terminal.hide_cursor"));
    ASSERT_TRUE(env->has("Terminal.show_cursor"));
    ASSERT_TRUE(env->has("Terminal.save_cursor"));
    ASSERT_TRUE(env->has("Terminal.restore_cursor"));
    ASSERT_TRUE(env->has("Terminal.reset_style"));
    ASSERT_TRUE(env->has("Terminal.bold"));
    ASSERT_TRUE(env->has("Terminal.dim"));
    ASSERT_TRUE(env->has("Terminal.italic"));
    ASSERT_TRUE(env->has("Terminal.underline"));
    ASSERT_TRUE(env->has("Terminal.strikethrough"));
    ASSERT_TRUE(env->has("Terminal.inverse"));
    ASSERT_TRUE(env->has("Terminal.color"));
    ASSERT_TRUE(env->has("Terminal.background_color"));
    ASSERT_TRUE(env->has("Terminal.rgb_color"));
    ASSERT_TRUE(env->has("Terminal.rgb_background_color"));
    ASSERT_TRUE(env->has("Terminal.write"));
    ASSERT_TRUE(env->has("Terminal.overwrite_line"));
    ASSERT_TRUE(env->has("Terminal.enter_alternate_screen"));
    ASSERT_TRUE(env->has("Terminal.leave_alternate_screen"));
    ASSERT_TRUE(env->has("Terminal.scroll_up"));
    ASSERT_TRUE(env->has("Terminal.scroll_down"));
    ASSERT_TRUE(env->has("Terminal.bell"));
    ASSERT_TRUE(env->has("Terminal.link"));
    ASSERT_TRUE(env->has("Terminal.enable_raw_mode"));
    ASSERT_TRUE(env->has("Terminal.disable_raw_mode"));
    ASSERT_TRUE(env->has("Terminal.is_in_raw_mode"));
    ASSERT_TRUE(env->has("Terminal.read_key"));
    ASSERT_TRUE(env->has("Terminal.read_key_timeout"));
    ASSERT_TRUE(env->has("Terminal.get_input"));
    ASSERT_TRUE(env->has("Terminal.get_cursor_position"));
    ASSERT_TRUE(env->has("Terminal.set_scroll_region"));
    ASSERT_TRUE(env->has("Terminal.reset_scroll_region"));
    ASSERT_TRUE(env->has("Terminal.enable_mouse"));
    ASSERT_TRUE(env->has("Terminal.disable_mouse"));
    ASSERT_TRUE(env->has("Terminal.is_mouse_enabled"));
    ASSERT_TRUE(env->has("Terminal.set_title"));
    ASSERT_TRUE(env->has("Terminal.supports_color"));
    ASSERT_TRUE(env->has("Terminal.supports_true_color"));
    ASSERT_TRUE(env->has("Terminal.set_escape_timeout"));
    ASSERT_TRUE(env->has("Terminal.get_escape_timeout"));
    ASSERT_TRUE(env->has("Terminal.flush"));
    ASSERT_TRUE(env->has("Terminal.read_line"));
    ASSERT_TRUE(env->has("Terminal.set_cursor_style"));
    ASSERT_TRUE(env->has("Terminal.blink"));
    ASSERT_TRUE(env->has("Terminal.insert_line"));
    ASSERT_TRUE(env->has("Terminal.delete_line"));
    ASSERT_TRUE(env->has("Terminal.clear_to_start_of_line"));
    ASSERT_TRUE(env->has("Terminal.supports_unicode"));
}

static void test_terminal_move_to_row() {
    // Valid row should return result<null> success.
    const auto v = eval("Terminal.move_to_row(5)");

    ASSERT_RESULT_SUCCESS(v);
}

static void test_terminal_move_to_row_invalid() {
    ASSERT_EVAL_FAILURE("Terminal.move_to_row(0)");
}

static void test_terminal_rgb_color() {
    const auto v = eval("Terminal.rgb_color(255, 0, 128, \"pink\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_string().find("pink") != std::string::npos);
}

static void test_terminal_rgb_color_invalid() {
    ASSERT_EVAL_FAILURE("Terminal.rgb_color(256, 0, 0, \"text\")");
}

static void test_terminal_scroll_region() {
    // set_scroll_region now returns result<null>.
    const auto v = eval("Terminal.set_scroll_region(1, 10)");

    ASSERT_RESULT_SUCCESS(v);

    eval("Terminal.reset_scroll_region()");
}

static void test_terminal_scroll_region_invalid() {
    ASSERT_EVAL_FAILURE("Terminal.set_scroll_region(10, 1)");
}

static void test_terminal_text_styles() {
    // dim / italic / underline / strikethrough / inverse each wrap the text in
    // SGR codes: the result preserves the input and adds an escape sequence.
    for (const char* style : {"dim", "italic", "underline", "strikethrough", "inverse"}) {
        const auto v = eval(std::string{"Terminal."} + style + "(\"data\")");

        ASSERT_TRUE(v.is_string());
        ASSERT_TRUE(v.as_string().find("data") != std::string::npos);
        ASSERT_TRUE(v.as_string().find("\x1b[") != std::string::npos);
    }
}

static void test_terminal_background_color() {
    const auto v = eval("Terminal.background_color(\"yellow\", \"warn\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_string().find("warn") != std::string::npos);
}

static void test_terminal_background_color_invalid() {
    ASSERT_EVAL_FAILURE("Terminal.background_color(\"not_a_color\", \"text\")");
}

static void test_terminal_background_color_choice() {
    // Terminal.background_color accepts a Terminal.Color variant too.
    const auto v = eval("Terminal.background_color(Terminal.Color.Yellow, \"warn\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& out = v.as_result()->owned_inner->as_string();

    ASSERT_TRUE(out.find("warn") != std::string::npos);
    ASSERT_TRUE(out.find("\033[43m") != std::string::npos);
}

static void test_terminal_rgb_background_color() {
    const auto v = eval("Terminal.rgb_background_color(0, 0, 128, \"navy\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_string().find("navy") != std::string::npos);
}

static void test_terminal_rgb_background_color_invalid() {
    ASSERT_EVAL_FAILURE("Terminal.rgb_background_color(256, 0, 0, \"text\")");
    ASSERT_EVAL_FAILURE("Terminal.rgb_background_color(0, -1, 0, \"text\")");
}

static void test_terminal_rgb_color_negative() {
    ASSERT_EVAL_FAILURE("Terminal.rgb_color(-1, 0, 0, \"text\")");
}

static void test_terminal_move_to() {
    const auto v = eval("Terminal.move_to(1, 1)");

    ASSERT_RESULT_SUCCESS(v);
}

static void test_terminal_move_to_invalid() {
    ASSERT_EVAL_FAILURE("Terminal.move_to(0, 1)");
}

static void test_terminal_move_to_column() {
    const auto v = eval("Terminal.move_to_column(3)");

    ASSERT_RESULT_SUCCESS(v);
}

static void test_terminal_move_to_column_invalid() {
    ASSERT_EVAL_FAILURE("Terminal.move_to_column(0)");
}

static void test_terminal_escape_timeout_roundtrip() {
    eval("Terminal.set_escape_timeout(100)");

    const auto v = eval("Terminal.get_escape_timeout()");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 100);

    // Restore the default so later tests observing the timeout are unaffected.
    eval("Terminal.set_escape_timeout(50)");
}

static void test_terminal_set_escape_timeout_out_of_range() {
    // Out-of-range timeouts throw rather than returning a result.
    ASSERT_THROWS(eval("Terminal.set_escape_timeout(0)"));
    ASSERT_THROWS(eval("Terminal.set_escape_timeout(5001)"));
}

static void test_terminal_supports_color_returns_bool() {
    ASSERT_TRUE(eval("Terminal.supports_color()").is_bool());
    ASSERT_TRUE(eval("Terminal.supports_true_color()").is_bool());
}

// The input functions require raw mode; with raw mode disabled (the default in
// the test harness) they must fail fast with a result failure rather than block
// on stdin.

static void test_terminal_read_key_requires_raw_mode() {
    ASSERT_EVAL_FAILURE("Terminal.read_key()");
}

static void test_terminal_read_key_timeout_requires_raw_mode() {
    ASSERT_EVAL_FAILURE("Terminal.read_key_timeout(100)");
}

static void test_terminal_get_input_requires_raw_mode() {
    ASSERT_EVAL_FAILURE("Terminal.get_input()");
}

static void test_terminal_get_cursor_position_requires_raw_mode() {
    ASSERT_EVAL_FAILURE("Terminal.get_cursor_position()");
}

static void test_terminal_enable_mouse_requires_raw_mode() {
    ASSERT_EVAL_FAILURE("Terminal.enable_mouse()");
}

static void test_terminal_write_requires_argument() {
    // write is variadic with a one-argument minimum.
    ASSERT_THROWS(eval("Terminal.write()"));
}

static void test_terminal_mouse_button_names() {
    ASSERT_EQ(terminal_detail::format_mouse_button(0), "left");
    ASSERT_EQ(terminal_detail::format_mouse_button(1), "middle");
    ASSERT_EQ(terminal_detail::format_mouse_button(2), "right");
    ASSERT_EQ(terminal_detail::format_mouse_button(3), "unknown");
}

static void test_terminal_mouse_event_format() {
    // Format is "mouse:<button>_<action>:<row>:<col>".
    ASSERT_EQ(terminal_detail::format_mouse_event(0, 5, 10, false), "mouse:left_press:10:5");
    ASSERT_EQ(terminal_detail::format_mouse_event(2, 5, 10, true), "mouse:right_release:10:5");
    ASSERT_EQ(terminal_detail::format_mouse_event(1, 3, 4, false, true), "mouse:middle_drag:4:3");
}

static void test_terminal_mouse_wheel_format() {
    ASSERT_EQ(terminal_detail::format_mouse_event(64, 1, 2, false), "mouse:wheel_up:2:1");
    ASSERT_EQ(terminal_detail::format_mouse_event(65, 1, 2, false), "mouse:wheel_down:2:1");
}

// ── Terminal.MouseEvent (typed decode of "mouse:<kind>:ROW:COL") ──────────────

static void test_terminal_mouse_event_kind_variant() {
    using luma::terminal_detail::mouse_event_kind_variant;

    // Every kind format_mouse_event can emit maps to a Terminal.MouseEventKind
    // variant — the two must stay in lockstep.
    ASSERT_EQ(mouse_event_kind_variant("left_press").value(), "LeftPress");
    ASSERT_EQ(mouse_event_kind_variant("left_release").value(), "LeftRelease");
    ASSERT_EQ(mouse_event_kind_variant("left_drag").value(), "LeftDrag");
    ASSERT_EQ(mouse_event_kind_variant("middle_press").value(), "MiddlePress");
    ASSERT_EQ(mouse_event_kind_variant("middle_release").value(), "MiddleRelease");
    ASSERT_EQ(mouse_event_kind_variant("middle_drag").value(), "MiddleDrag");
    ASSERT_EQ(mouse_event_kind_variant("right_press").value(), "RightPress");
    ASSERT_EQ(mouse_event_kind_variant("right_release").value(), "RightRelease");
    ASSERT_EQ(mouse_event_kind_variant("right_drag").value(), "RightDrag");
    ASSERT_EQ(mouse_event_kind_variant("wheel_up").value(), "WheelUp");
    ASSERT_EQ(mouse_event_kind_variant("wheel_down").value(), "WheelDown");
    ASSERT_EQ(mouse_event_kind_variant("wheel_left").value(), "WheelLeft");
    ASSERT_EQ(mouse_event_kind_variant("wheel_right").value(), "WheelRight");

    // The degenerate tokens format_mouse_button/format_mouse_event emit for
    // malformed input have no variant, so they decode to none, not a bogus kind.
    ASSERT_FALSE(mouse_event_kind_variant("unknown_press").has_value());
    ASSERT_FALSE(mouse_event_kind_variant("wheel_unknown").has_value());
    ASSERT_FALSE(mouse_event_kind_variant("").has_value());
}

static void test_terminal_parse_mouse_event() {
    // A well-formed event decodes into a Terminal.MouseEvent record whose kind is
    // the matching Terminal.MouseEventKind choice and whose row/column are the
    // integer coordinates (row first, per "mouse:<kind>:ROW:COL").
    const auto v = eval("Terminal.parse_mouse_event(\"mouse:left_press:10:5\")");

    ASSERT_TRUE(v.is_record());

    const auto& rec = *v.as_record();

    ASSERT_EQ(rec.type_name, "MouseEvent");
    ASSERT_TRUE(rec.find_field("kind")->is_choice());
    ASSERT_EQ(rec.find_field("kind")->as_choice()->type_name, "MouseEventKind");
    ASSERT_EQ(rec.find_field("kind")->as_choice()->variant, "LeftPress");
    ASSERT_EQ(rec.find_field("row")->as_integer(), 10);
    ASSERT_EQ(rec.find_field("column")->as_integer(), 5);
}

static void test_terminal_parse_mouse_event_kinds() {
    // Release, drag, and wheel kinds all decode to their variants.
    const auto release = eval("Terminal.parse_mouse_event(\"mouse:right_release:2:7\")");
    ASSERT_TRUE(release.is_record());
    ASSERT_EQ(release.as_record()->find_field("kind")->as_choice()->variant, "RightRelease");

    const auto drag = eval("Terminal.parse_mouse_event(\"mouse:middle_drag:4:3\")");
    ASSERT_TRUE(drag.is_record());
    ASSERT_EQ(drag.as_record()->find_field("kind")->as_choice()->variant, "MiddleDrag");
    ASSERT_EQ(drag.as_record()->find_field("row")->as_integer(), 4);
    ASSERT_EQ(drag.as_record()->find_field("column")->as_integer(), 3);

    const auto wheel = eval("Terminal.parse_mouse_event(\"mouse:wheel_up:1:1\")");
    ASSERT_TRUE(wheel.is_record());
    ASSERT_EQ(wheel.as_record()->find_field("kind")->as_choice()->variant, "WheelUp");
}

static void test_terminal_parse_mouse_event_roundtrip() {
    // Feeding format_mouse_event's own output back through the parser recovers the
    // event — the decoder is the exact inverse of the encoder the backends use.
    const std::string raw = terminal_detail::format_mouse_event(2, 5, 10, true);
    const auto v = eval("Terminal.parse_mouse_event(\"" + raw + "\")");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->find_field("kind")->as_choice()->variant, "RightRelease");
    ASSERT_EQ(v.as_record()->find_field("row")->as_integer(), 10);
    ASSERT_EQ(v.as_record()->find_field("column")->as_integer(), 5);
}

static void test_terminal_parse_mouse_event_none() {
    // Anything that is not a well-formed, recognised mouse event decodes to none.
    ASSERT_TRUE(eval("Terminal.parse_mouse_event(\"enter\")").is_null()); // not a mouse string
    ASSERT_TRUE(
        eval("Terminal.parse_mouse_event(\"mouse:left_press:10\")").is_null()); // missing column
    ASSERT_TRUE(
        eval("Terminal.parse_mouse_event(\"mouse:left_press:10:5:0\")").is_null()); // extra field
    ASSERT_TRUE(eval("Terminal.parse_mouse_event(\"mouse:zoom:1:2\")").is_null());  // unknown kind
    ASSERT_TRUE(
        eval("Terminal.parse_mouse_event(\"mouse:left_press:x:5\")").is_null()); // non-integer row
    ASSERT_TRUE(eval("Terminal.parse_mouse_event(\"mouse:left_press::5\")").is_null()); // empty row
    ASSERT_TRUE(
        eval("Terminal.parse_mouse_event(\"mouse:left_press:-1:5\")").is_null()); // negative row
    ASSERT_TRUE(
        eval("Terminal.parse_mouse_event(\"mouse:left_press:1:-5\")").is_null()); // negative column
    ASSERT_TRUE(eval("Terminal.parse_mouse_event(\"\")").is_null());              // empty string
}

static void test_terminal_parse_key_named() {
    // Every named key the decoder emits maps to its unit Terminal.Key variant.
    const auto expect_variant = [](const std::string& name, const std::string& variant) {
        const auto v = eval("Terminal.parse_key(\"" + name + "\")");
        ASSERT_TRUE(v.is_choice());
        ASSERT_EQ(v.as_choice()->type_name, "Key");
        ASSERT_EQ(v.as_choice()->variant, variant);
        ASSERT_TRUE(v.as_choice()->fields.empty()); // unit variants carry no payload
    };

    expect_variant("enter", "Enter");
    expect_variant("escape", "Escape");
    expect_variant("tab", "Tab");
    expect_variant("backspace", "Backspace");
    expect_variant("space", "Space");
    expect_variant("up", "Up");
    expect_variant("down", "Down");
    expect_variant("left", "Left");
    expect_variant("right", "Right");
    expect_variant("home", "Home");
    expect_variant("end", "End");
    expect_variant("page_up", "PageUp");
    expect_variant("page_down", "PageDown");
    expect_variant("insert", "Insert");
    expect_variant("delete", "Delete");
    expect_variant("unknown", "Unknown");
}

static void test_terminal_parse_key_function() {
    // 'f' + positive integer decodes to Function(n), carrying the number.
    const auto expect_function = [](const std::string& name, std::int64_t n) {
        const auto v = eval("Terminal.parse_key(\"" + name + "\")");
        ASSERT_TRUE(v.is_choice());
        ASSERT_EQ(v.as_choice()->variant, "Function");
        ASSERT_EQ(v.as_choice()->fields.size(), 1U);
        ASSERT_EQ(v.as_choice()->fields[0].as_integer(), n);
    };

    expect_function("f1", 1);
    expect_function("f5", 5);
    expect_function("f12", 12);
}

static void test_terminal_parse_key_character() {
    // parse_key is total: any unrecognised key becomes Character(text), so a
    // program can always match without an optional. This also covers the strings
    // that merely look like function keys but are not (f0, f, fx).
    const auto expect_character = [](const std::string& name, const std::string& text) {
        const auto v = eval("Terminal.parse_key(\"" + name + "\")");
        ASSERT_TRUE(v.is_choice());
        ASSERT_EQ(v.as_choice()->variant, "Character");
        ASSERT_EQ(v.as_choice()->fields.size(), 1U);
        ASSERT_EQ(v.as_choice()->fields[0].as_string(), text);
    };

    expect_character("a", "a");
    expect_character("Z", "Z");
    expect_character("5", "5");
    expect_character("f", "f");   // too short to be a function key
    expect_character("f0", "f0"); // 0 is not a positive function-key index
    expect_character("fx", "fx"); // 'x' is not an integer
}

// ── terminal key decoder (platform-independent escape/key parsing) ──

// Build a byte_reader that yields each byte of `rest` (as an unsigned value),
// then -1 once exhausted — mirroring read_byte_timeout() at end of input.
static terminal_detail::byte_reader make_byte_reader(std::string rest) {
    std::size_t idx{0};

    return [rest = std::move(rest), idx]() mutable -> int {
        if (idx >= rest.size()) {
            return -1;
        }

        return static_cast<unsigned char>(rest[idx++]);
    };
}

// Decode a full key press: `first` is the initial byte, `rest` the subsequent
// bytes available before the (simulated) escape timeout.
static std::string decode_seq(int first, const std::string& rest = "", bool mouse_mode = false) {
    return terminal_detail::decode_key(first, make_byte_reader(rest), mouse_mode);
}

static void test_terminal_decode_basic_keys() {
    ASSERT_EQ(decode_seq(13), "enter");
    ASSERT_EQ(decode_seq(10), "enter");
    ASSERT_EQ(decode_seq(9), "tab");
    ASSERT_EQ(decode_seq(127), "backspace");
    ASSERT_EQ(decode_seq(8), "backspace");
    ASSERT_EQ(decode_seq(0), "ctrl+space");
    ASSERT_EQ(decode_seq(32), "space");
    ASSERT_EQ(decode_seq(1), "ctrl+a");
    ASSERT_EQ(decode_seq(26), "ctrl+z");
    // Plain printable ASCII is returned verbatim.
    ASSERT_EQ(decode_seq('A'), "A");
    ASSERT_EQ(decode_seq('z'), "z");
}

static void test_terminal_decode_escape_arrows() {
    // A bare ESC with no continuation decodes as "escape".
    ASSERT_EQ(decode_seq(27, ""), "escape");
    // A truncated CSI introducer also yields "escape".
    ASSERT_EQ(decode_seq(27, "["), "escape");
    ASSERT_EQ(decode_seq(27, "[A"), "up");
    ASSERT_EQ(decode_seq(27, "[B"), "down");
    ASSERT_EQ(decode_seq(27, "[C"), "right");
    ASSERT_EQ(decode_seq(27, "[D"), "left");
    ASSERT_EQ(decode_seq(27, "[H"), "home");
    ASSERT_EQ(decode_seq(27, "[F"), "end");
    ASSERT_EQ(decode_seq(27, "[Z"), "shift+tab");
    // Unknown final byte in a CSI sequence.
    ASSERT_EQ(decode_seq(27, "[!"), "unknown");
}

static void test_terminal_decode_ss3_and_alt() {
    // SS3 function keys (ESC O ...).
    ASSERT_EQ(decode_seq(27, "OP"), "f1");
    ASSERT_EQ(decode_seq(27, "OQ"), "f2");
    ASSERT_EQ(decode_seq(27, "OR"), "f3");
    ASSERT_EQ(decode_seq(27, "OS"), "f4");
    ASSERT_EQ(decode_seq(27, "OH"), "home");
    ASSERT_EQ(decode_seq(27, "OF"), "end");
    // SS3 with an unrecognised printable falls back to Alt+char.
    ASSERT_EQ(decode_seq(27, "OX"), "alt+X");
    // ESC followed by a printable byte is Alt+char.
    ASSERT_EQ(decode_seq(27, "a"), "alt+a");
    ASSERT_EQ(decode_seq(27, "Z"), "alt+Z");
}

static void test_terminal_decode_csi_tilde() {
    ASSERT_EQ(decode_seq(27, "[1~"), "home");
    ASSERT_EQ(decode_seq(27, "[2~"), "insert");
    ASSERT_EQ(decode_seq(27, "[3~"), "delete");
    ASSERT_EQ(decode_seq(27, "[4~"), "end");
    ASSERT_EQ(decode_seq(27, "[5~"), "page_up");
    ASSERT_EQ(decode_seq(27, "[6~"), "page_down");
    ASSERT_EQ(decode_seq(27, "[11~"), "f1");
    ASSERT_EQ(decode_seq(27, "[15~"), "f5");
    ASSERT_EQ(decode_seq(27, "[24~"), "f12");
    // A numeric parameter that maps to no key decodes as "unknown".
    ASSERT_EQ(decode_seq(27, "[9~"), "unknown");
    // A numeric parameter with no terminator decodes as "unknown".
    ASSERT_EQ(decode_seq(27, "[99"), "unknown");
}

static void test_terminal_decode_csi_modifiers() {
    ASSERT_EQ(decode_seq(27, "[1;2A"), "shift+up");
    ASSERT_EQ(decode_seq(27, "[1;3B"), "alt+down");
    ASSERT_EQ(decode_seq(27, "[1;4C"), "alt+shift+right");
    ASSERT_EQ(decode_seq(27, "[1;5D"), "ctrl+left");
    ASSERT_EQ(decode_seq(27, "[1;6H"), "ctrl+shift+home");
    ASSERT_EQ(decode_seq(27, "[1;7F"), "ctrl+alt+end");
}

static void test_terminal_decode_mouse() {
    // SGR mouse: ESC [ < button ; col ; row M/m. Output is "mouse:<x>:<row>:<col>".
    ASSERT_EQ(decode_seq(27, "[<0;5;10M", true), "mouse:left_press:10:5");
    ASSERT_EQ(decode_seq(27, "[<0;5;10m", true), "mouse:left_release:10:5");
    ASSERT_EQ(decode_seq(27, "[<2;3;4M", true), "mouse:right_press:4:3");
    // Motion events set bit 5 (button += 32) and decode as a drag.
    ASSERT_EQ(decode_seq(27, "[<32;1;1M", true), "mouse:left_drag:1:1");
    // Wheel events use button codes 64/65.
    ASSERT_EQ(decode_seq(27, "[<64;1;1M", true), "mouse:wheel_up:1:1");
    ASSERT_EQ(decode_seq(27, "[<65;2;2M", true), "mouse:wheel_down:2:2");
    // With mouse mode disabled the same sequence is not decoded as a mouse event.
    ASSERT_EQ(decode_seq(27, "[<0;5;10M", false), "unknown");
    // A truncated SGR sequence (no terminator) decodes gracefully.
    ASSERT_EQ(decode_seq(27, "[<0;5;10", true), "unknown");
}

static void test_terminal_decode_utf8() {
    // Valid 2-, 3-, and 4-byte UTF-8 code points pass through intact
    // (U+00E9, U+20AC, U+1F600).
    ASSERT_EQ(decode_seq(0xC3, "\xA9"), "\xC3\xA9");
    ASSERT_EQ(decode_seq(0xE2, "\x82\xAC"), "\xE2\x82\xAC");
    ASSERT_EQ(decode_seq(0xF0, "\x9F\x98\x80"), "\xF0\x9F\x98\x80");
    // Invalid leading byte yields the Unicode replacement character.
    ASSERT_EQ(decode_seq(0x80), "\xEF\xBF\xBD");
    // Truncated multi-byte sequence yields the replacement character.
    ASSERT_EQ(decode_seq(0xC3, ""), "\xEF\xBF\xBD");
    // A non-continuation trailing byte yields the replacement character.
    ASSERT_EQ(decode_seq(0xC3, "A"), "\xEF\xBF\xBD");
}

static void test_terminal_size() {
    const auto v = eval("Terminal.size()");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, "Size");
    ASSERT_TRUE(v.as_record()->find_field("columns") != nullptr);
    ASSERT_TRUE(v.as_record()->find_field("rows") != nullptr);
}

// ── Terminal headless interaction-testing harness (Terminal.test_*) ──
// Each program is self-contained: it calls test_start(...) and test_stop() so
// the process-global harness state is reset before the next test runs.

static void test_terminal_test_harness_reads_scripted_keys() {
    const auto v = eval(R"(
        Terminal.test_start(["a", "b", "enter"])
        mutable string out = ""
        mutable boolean running = true
        while running {
            match Terminal.read_key() {
                success(k) { out = out + k }
                failure(_) { running = false }
            }
        }
        Terminal.test_stop()
        out
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "abenter");
}

static void test_terminal_test_harness_drains_to_failure() {
    // An empty scripted queue makes read_key report end-of-input immediately.
    const auto v = eval(R"(
        Terminal.test_start([])
        boolean failed = match Terminal.read_key() {
            success(_) { false }
            failure(_) { true }
        }
        Terminal.test_stop()
        failed
    )");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

static void test_terminal_test_harness_captures_output() {
    // write / overwrite_line / bell are all routed into the capture buffer.
    const auto v = eval(R"(
        Terminal.test_start([])
        Terminal.write("hello")
        Terminal.bell()
        Terminal.overwrite_line("X")
        Terminal.test_stop()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_TRUE(v.as_string().find("hello") != std::string::npos);
    ASSERT_TRUE(v.as_string().find('\a') != std::string::npos);
    ASSERT_TRUE(v.as_string().find('X') != std::string::npos);
}

static void test_terminal_test_output_reads_mid_session() {
    const auto v = eval(R"(
        Terminal.test_start([])
        Terminal.write("AB")
        string mid = Terminal.test_output()
        Terminal.write("CD")
        Terminal.test_stop()
        mid
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "AB");
}

static void test_terminal_test_get_input_parses_modifiers() {
    const auto v = eval(R"(
        Terminal.test_start(["ctrl+c"])
        string r = match Terminal.get_input() {
            success(ev) { "${ev.key}:${ev.control}:${ev.shift}:${ev.alt}" }
            failure(_) { "fail" }
        }
        Terminal.test_stop()
        r
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "c:true:false:false");
}

static void test_terminal_test_remaining_and_feed() {
    // start with 2 keys, consume 1 (remaining 1), feed 1 more (remaining 2).
    const auto v = eval(R"(
        Terminal.test_start(["a", "b"])
        Terminal.read_key()
        integer after_one = Terminal.test_remaining()
        Terminal.test_feed(["c"])
        integer after_feed = Terminal.test_remaining()
        Terminal.test_stop()
        after_one * 10 + after_feed
    )");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 12);
}

static void test_terminal_test_session_reports_as_terminal() {
    // Inside a session is_terminal() and is_in_raw_mode() both report true so the
    // program under test runs its real loop; after test_stop() they reset.
    const auto v = eval(R"(
        Terminal.test_start(["x"])
        boolean term = Terminal.is_terminal()
        boolean raw = Terminal.is_in_raw_mode()
        Terminal.test_stop()
        boolean term_after = Terminal.is_in_raw_mode()
        term && raw && (!term_after)
    )");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

static void test_terminal_test_read_key_timeout_drains_to_timeout() {
    const auto v = eval(R"(
        Terminal.test_start(["a"])
        string first = match Terminal.read_key_timeout(50) {
            success(k) { k }
            failure(_) { "timeout" }
        }
        string second = match Terminal.read_key_timeout(50) {
            success(k) { k }
            failure(_) { "timeout" }
        }
        Terminal.test_stop()
        first + "/" + second
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "a/timeout");
}

// ── New functions: flush / blink / line insert-delete / cursor style ──────────

static void test_terminal_blink_wraps_text() {
    // blink wraps the text in the SGR blink attribute (ESC[5m … ESC[0m),
    // mirroring bold/italic; the result is a pure string.
    const auto v = eval("Terminal.blink(\"hi\")");

    ASSERT_TRUE(v.is_string());

    const auto& out = v.as_string();

    ASSERT_TRUE(out.find("\033[5m") != std::string::npos);
    ASSERT_TRUE(out.find("hi") != std::string::npos);
    ASSERT_TRUE(out.rfind("\033[0m") == out.size() - 4);
}

static void test_terminal_flush_returns_result() {
    // flush reports success; there is nothing to observe headlessly.
    ASSERT_RESULT_SUCCESS(eval("Terminal.flush()"));
}

static void test_terminal_insert_delete_line() {
    ASSERT_RESULT_SUCCESS(eval("Terminal.insert_line(2)"));
    ASSERT_RESULT_SUCCESS(eval("Terminal.delete_line(1)"));
}

static void test_terminal_insert_delete_line_invalid() {
    ASSERT_EVAL_FAILURE("Terminal.insert_line(0)");
    ASSERT_EVAL_FAILURE("Terminal.delete_line(0)");
}

static void test_terminal_clear_to_start_of_line() {
    ASSERT_RESULT_SUCCESS(eval("Terminal.clear_to_start_of_line()"));
}

static void test_terminal_set_cursor_style() {
    // A Terminal.CursorStyle variant emits its DECSCUSR escape and returns success.
    ASSERT_RESULT_SUCCESS(eval("Terminal.set_cursor_style(Terminal.CursorStyle.SteadyBar)"));
    ASSERT_RESULT_SUCCESS(eval("Terminal.set_cursor_style(Terminal.CursorStyle.BlinkingBlock)"));
}

static void test_terminal_set_cursor_style_emits_decscusr() {
    // SteadyBar is DECSCUSR parameter 6 ("\x1b[6 q").
    const auto v = eval(R"(
        Terminal.test_start([])
        Terminal.set_cursor_style(Terminal.CursorStyle.SteadyBar)
        Terminal.test_stop()
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_TRUE(v.as_string().find("\033[6 q") != std::string::npos);
}

static void test_terminal_cursor_style_variants_exist() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Terminal.CursorStyle.BlinkingBlock"));
    ASSERT_TRUE(env->has("Terminal.CursorStyle.SteadyBlock"));
    ASSERT_TRUE(env->has("Terminal.CursorStyle.BlinkingUnderline"));
    ASSERT_TRUE(env->has("Terminal.CursorStyle.SteadyUnderline"));
    ASSERT_TRUE(env->has("Terminal.CursorStyle.BlinkingBar"));
    ASSERT_TRUE(env->has("Terminal.CursorStyle.SteadyBar"));
}

static void test_terminal_supports_unicode_returns_bool() {
    ASSERT_TRUE(eval("Terminal.supports_unicode()").is_bool());
}

static void test_terminal_read_line_reads_scripted_input() {
    // read_line echoes printable keys, handles backspace, and submits on Enter.
    // Scripted keys: h i x <backspace> <enter> → "hi".
    const auto v = eval(R"(
        Terminal.test_start(["h", "i", "x", "backspace", "enter"])
        string line = match Terminal.read_line("> ") {
            success(text) { text }
            failure(_) { "fail" }
        }
        Terminal.test_stop()
        line
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hi");
}

static void test_terminal_read_line_drains_to_failure() {
    // An exhausted scripted queue makes read_line fail (EOF).
    const auto v = eval(R"(
        Terminal.test_start([])
        boolean failed = match Terminal.read_line("> ") {
            success(_) { false }
            failure(_) { true }
        }
        Terminal.test_stop()
        failed
    )");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

int main() {
    RUN(test_terminal_bold);
    RUN(test_terminal_color);
    RUN(test_terminal_color_invalid);
    RUN(test_terminal_color_choice);
    RUN(test_terminal_color_choice_bright);
    RUN(test_terminal_color_choice_matches_string);
    RUN(test_terminal_plain_style_defaults);
    RUN(test_terminal_styled_plain_is_unchanged);
    RUN(test_terminal_styled_bold);
    RUN(test_terminal_styled_composes_color_and_attributes);
    RUN(test_terminal_color_rejects_non_color_arg);
    RUN(test_terminal_columns_rows);
    RUN(test_terminal_is_mouse);
    RUN(test_terminal_is_raw_mode);
    RUN(test_terminal_is_terminal);
    RUN(test_terminal_link);
    RUN(test_terminal_module);
    RUN(test_terminal_move_to_row);
    RUN(test_terminal_move_to_row_invalid);
    RUN(test_terminal_rgb_color);
    RUN(test_terminal_rgb_color_invalid);
    RUN(test_terminal_scroll_region);
    RUN(test_terminal_scroll_region_invalid);
    RUN(test_terminal_text_styles);
    RUN(test_terminal_background_color);
    RUN(test_terminal_background_color_invalid);
    RUN(test_terminal_background_color_choice);
    RUN(test_terminal_rgb_background_color);
    RUN(test_terminal_rgb_background_color_invalid);
    RUN(test_terminal_rgb_color_negative);
    RUN(test_terminal_move_to);
    RUN(test_terminal_move_to_invalid);
    RUN(test_terminal_move_to_column);
    RUN(test_terminal_move_to_column_invalid);
    RUN(test_terminal_escape_timeout_roundtrip);
    RUN(test_terminal_set_escape_timeout_out_of_range);
    RUN(test_terminal_supports_color_returns_bool);
    RUN(test_terminal_read_key_requires_raw_mode);
    RUN(test_terminal_read_key_timeout_requires_raw_mode);
    RUN(test_terminal_get_input_requires_raw_mode);
    RUN(test_terminal_get_cursor_position_requires_raw_mode);
    RUN(test_terminal_enable_mouse_requires_raw_mode);
    RUN(test_terminal_write_requires_argument);
    RUN(test_terminal_mouse_button_names);
    RUN(test_terminal_mouse_event_format);
    RUN(test_terminal_mouse_wheel_format);
    RUN(test_terminal_mouse_event_kind_variant);
    RUN(test_terminal_parse_mouse_event);
    RUN(test_terminal_parse_mouse_event_kinds);
    RUN(test_terminal_parse_mouse_event_roundtrip);
    RUN(test_terminal_parse_mouse_event_none);
    RUN(test_terminal_parse_key_named);
    RUN(test_terminal_parse_key_function);
    RUN(test_terminal_parse_key_character);
    RUN(test_terminal_decode_basic_keys);
    RUN(test_terminal_decode_escape_arrows);
    RUN(test_terminal_decode_ss3_and_alt);
    RUN(test_terminal_decode_csi_tilde);
    RUN(test_terminal_decode_csi_modifiers);
    RUN(test_terminal_decode_mouse);
    RUN(test_terminal_decode_utf8);
    RUN(test_terminal_size);
    RUN(test_terminal_test_harness_reads_scripted_keys);
    RUN(test_terminal_test_harness_drains_to_failure);
    RUN(test_terminal_test_harness_captures_output);
    RUN(test_terminal_test_output_reads_mid_session);
    RUN(test_terminal_test_get_input_parses_modifiers);
    RUN(test_terminal_test_remaining_and_feed);
    RUN(test_terminal_test_session_reports_as_terminal);
    RUN(test_terminal_test_read_key_timeout_drains_to_timeout);

    RUN(test_terminal_blink_wraps_text);
    RUN(test_terminal_flush_returns_result);
    RUN(test_terminal_insert_delete_line);
    RUN(test_terminal_insert_delete_line_invalid);
    RUN(test_terminal_clear_to_start_of_line);
    RUN(test_terminal_set_cursor_style);
    RUN(test_terminal_set_cursor_style_emits_decscusr);
    RUN(test_terminal_cursor_style_variants_exist);
    RUN(test_terminal_supports_unicode_returns_bool);
    RUN(test_terminal_read_line_reads_scripted_input);
    RUN(test_terminal_read_line_drains_to_failure);

    return SUMMARY();
}
