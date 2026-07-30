// GraphicalUi module C++ unit tests: app lifecycle, commands, subscriptions, and renderer coverage.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "analysis/errors/error.hpp"
#include "common/platform_utils.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/io/graphicalui_css.hpp"
#include "stdlib_test_helpers.hpp"

// build_mouse_event_record lives behind LUMA_HAS_WEBVIEW in graphicalui_events.cpp
// (compiled into luma_core whenever a webview backend is available — the only
// case this test is built).  Forward-declare it unconditionally so the test TU,
// which does not define LUMA_HAS_WEBVIEW, can still link against it.
namespace luma::gui_detail {
[[nodiscard]] Value build_mouse_event_record(const DictionaryValue& payload);
[[nodiscard]] Value build_scroll_position_record(const DictionaryValue& payload);
[[nodiscard]] Value build_key_event_record(const std::string& key, const DictionaryValue* mods);
[[nodiscard]] Value build_window_size_record(std::int64_t width, std::int64_t height);
[[nodiscard]] Value build_drag_event_record(const DictionaryValue& payload);
[[nodiscard]] Value build_drop_event_record(const DictionaryValue& payload);
[[nodiscard]] Value build_storage_event_record(const DictionaryValue& payload);
[[nodiscard]] Value build_wheel_delta_record(const DictionaryValue& payload);
[[nodiscard]] Value
build_http_response_record_gui(int status, std::string body,
                               const std::vector<std::pair<std::string, std::string>>& headers);
} // namespace luma::gui_detail

// ═══════════════════════════════════════════════════════════
// App config validation
// ═══════════════════════════════════════════════════════════

LUMA_TEST(app_requires_dict) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.app("not a dict")
    )"));
}

LUMA_TEST(app_requires_view) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.app({"title": "Test", "model": 0})
    )"));
}

// ═══════════════════════════════════════════════════════════
// App lifecycle — headless execution (regression guard)
// ═══════════════════════════════════════════════════════════

// Regression guard: the "init" function must receive the configured initial
// model as its argument (documented contract: func(model) -> model|pair). A
// prior bug invoked init with zero arguments, so the model arrived as `none`
// and any model access inside init threw "cannot index into 'none'". Headless
// mode runs the real init → view lifecycle without creating a window.
LUMA_TEST(app_init_receives_model) {
    const ScopedEnv headless{"LUMA_GUI_HEADLESS", "1"};

    // init indexes model["count"]; under the old bug it received `none` and
    // threw. Suppress the headless progress output via CapturedStream.
    const CapturedStream captured{std::cout};

    const auto result = eval(R"(
        GraphicalUi.app({
            "title": "InitModelTest",
            "model": {"count": 41},
            "init": (dictionary<integer> model) -> {
                return Dictionary.set(model, "count", model["count"] + 1)
            },
            "view": (dictionary<integer> model) -> GraphicalUi.label("ready")
        })
    )");

    ASSERT_TRUE(result.is_null());
    ASSERT_TRUE(captured.str().find("initial render OK") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// One-line state persistence ("persist" config key)
// ═══════════════════════════════════════════════════════════

// The "persist" key names a file that the model is serialised to when the app
// exits. Under headless mode the full lifecycle runs (init → update → save), so
// after app() returns the file holds the *final* model, not the initial one.
LUMA_TEST(app_persist_saves_final_model_on_exit) {
    const ScopedEnv headless{"LUMA_GUI_HEADLESS", "1"};
    const ScopedEnv messages{"LUMA_GUI_MESSAGES", "inc"};
    const CapturedStream captured{std::cout};

    std::error_code ec;
    const auto path =
        (std::filesystem::temp_directory_path(ec) / "luma_gui_persist_save.json").generic_string();
    std::filesystem::remove(path, ec);

    // model {count:1}; the "inc" message drives update to {count:2}; on exit the
    // final model is written to the persist file.
    const auto src = std::string(R"(
        GraphicalUi.app({
            "title": "PersistSave",
            "persist": ")") +
                     path + R"(",
            "model": {"count": 1},
            "update": (dictionary<integer> model, string msg) -> match msg {
                case == "inc" { Dictionary.set(model, "count", model["count"] + 1) }
                else { model }
            },
            "view": (dictionary<integer> _model) -> GraphicalUi.label("n")
        })
    )";

    const auto result = eval(src);
    ASSERT_TRUE(result.is_null());

    std::ifstream in{path};
    ASSERT_TRUE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const auto contents = ss.str();
    ASSERT_TRUE(contents.find("count") != std::string::npos);
    ASSERT_TRUE(contents.find('2') != std::string::npos);

    std::filesystem::remove(path, ec);
}

// A pre-existing persist file is restored as the initial model, overriding the
// configured "model". Seeding {count:999} and driving one "inc" must yield 1000
// (proving restore ran before update), which the exit save then writes back.
LUMA_TEST(app_persist_restores_saved_model) {
    const ScopedEnv headless{"LUMA_GUI_HEADLESS", "1"};
    const ScopedEnv messages{"LUMA_GUI_MESSAGES", "inc"};
    const CapturedStream captured{std::cout};

    std::error_code ec;
    const auto path = (std::filesystem::temp_directory_path(ec) / "luma_gui_persist_restore.json")
                          .generic_string();

    {
        std::ofstream seed{path, std::ios::binary | std::ios::trunc};
        seed << R"({"count": 999})";
    }

    const auto src = std::string(R"(
        GraphicalUi.app({
            "title": "PersistRestore",
            "persist": ")") +
                     path + R"(",
            "model": {"count": 1},
            "update": (dictionary<integer> model, string msg) -> match msg {
                case == "inc" { Dictionary.set(model, "count", model["count"] + 1) }
                else { model }
            },
            "view": (dictionary<integer> _model) -> GraphicalUi.label("n")
        })
    )";

    const auto result = eval(src);
    ASSERT_TRUE(result.is_null());

    std::ifstream in{path};
    ASSERT_TRUE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const auto contents = ss.str();
    ASSERT_TRUE(contents.find("1000") != std::string::npos);
    ASSERT_TRUE(contents.find("999") == std::string::npos);

    std::filesystem::remove(path, ec);
}

// ═══════════════════════════════════════════════════════════
// Theme contrast validation ("devtools" WCAG AA developer aid)
// ═══════════════════════════════════════════════════════════

// With devtools enabled the runtime measures the theme's text/accent colours
// against the background at startup and warns (to stderr) about any pair below
// the WCAG AA 4.5:1 minimum for normal text. Light-grey text on a white
// background (~1.2:1) must trigger the warning. Headless mode runs the init
// lifecycle where the check lives, so no window is created.
LUMA_TEST(contrast_warns_on_low_contrast_theme) {
    const ScopedEnv headless{"LUMA_GUI_HEADLESS", "1"};
    const CapturedStreams captured{std::cout, std::cerr};

    const auto result = eval(R"(
        GraphicalUi.app({
            "title": "ContrastLow",
            "devtools": true,
            "theme": {
                "background": "#ffffff",
                "text_color": "#eeeeee",
                "accent": "#dddddd"
            },
            "model": {"count": 0},
            "view": (dictionary<integer> _model) -> GraphicalUi.label("hi")
        })
    )");

    ASSERT_TRUE(result.is_null());
    ASSERT_TRUE(captured.str().find("low colour contrast") != std::string::npos);
    ASSERT_TRUE(captured.str().find("text_color on background") != std::string::npos);
}

// A theme that clears AA — near-black text and the default indigo accent on a
// white background (both well above 4.5:1) — must emit no contrast warning.
LUMA_TEST(contrast_silent_on_accessible_theme) {
    const ScopedEnv headless{"LUMA_GUI_HEADLESS", "1"};
    const CapturedStreams captured{std::cout, std::cerr};

    const auto result = eval(R"(
        GraphicalUi.app({
            "title": "ContrastOk",
            "devtools": true,
            "theme": {
                "background": "#ffffff",
                "text_color": "#111827",
                "accent": "#4f46e5"
            },
            "model": {"count": 0},
            "view": (dictionary<integer> _model) -> GraphicalUi.label("hi")
        })
    )");

    ASSERT_TRUE(result.is_null());
    ASSERT_TRUE(captured.str().find("low colour contrast") == std::string::npos);
}

// The contrast audit is a devtools-only developer aid: with devtools disabled
// (the default) even an unreadable theme must stay silent, so shipping apps pay
// no diagnostic cost and see no stray console output.
LUMA_TEST(contrast_check_gated_on_devtools) {
    const ScopedEnv headless{"LUMA_GUI_HEADLESS", "1"};
    const CapturedStreams captured{std::cout, std::cerr};

    const auto result = eval(R"(
        GraphicalUi.app({
            "title": "ContrastGated",
            "devtools": false,
            "theme": {
                "background": "#ffffff",
                "text_color": "#eeeeee",
                "accent": "#dddddd"
            },
            "model": {"count": 0},
            "view": (dictionary<integer> _model) -> GraphicalUi.label("hi")
        })
    )");

    ASSERT_TRUE(result.is_null());
    ASSERT_TRUE(captured.str().find("low colour contrast") == std::string::npos);
}

// GraphicalUi contract, e.g. the guide's Minimal Counter button () -> "inc").
// A callback that returns a structured value (the new model) is applied
// directly, without consulting update. Exercised through the headless
// interaction API, so no window is created.
LUMA_TEST(callback_string_routes_through_update) {
    // The "Up" button returns the message "inc"; update turns it into model + 1.
    const auto routed = eval(R"(
        GraphicalUi.test_click({
            "_": "gui_config",
            "model": 0,
            "view": (integer _count) -> GraphicalUi.column([
                GraphicalUi.button("Up", () -> "inc")
            ]),
            "update": (integer model, string msg) -> match msg {
                case == "inc" { model + 1 }
                else { model }
            }
        }, 7, "Up")
    )");

    ASSERT_TRUE(routed.is_integer());
    ASSERT_EQ(routed.as_integer(), 8);

    // A numeric (model) return is applied as-is. Here update would force 0 if it
    // were (incorrectly) consulted, so a result of 8 proves direct application.
    const auto direct = eval(R"(
        GraphicalUi.test_click({
            "_": "gui_config",
            "model": 0,
            "view": (integer count) -> GraphicalUi.column([
                GraphicalUi.button("Up", () -> count + 1)
            ]),
            "update": (integer _model, string _msg) -> 0
        }, 7, "Up")
    )");

    ASSERT_TRUE(direct.is_integer());
    ASSERT_EQ(direct.as_integer(), 8);
}

// A typed `choice` message (Solaris style — e.g. a button returning Msg.Inc) is
// routed through update(model, msg) exactly like a string message.  This is
// what lets typed messages flow through clicks and keyboard events without the
// view having to compute update() itself.  Regression guard for the choice
// branch of apply_event_result.
LUMA_TEST(callback_choice_routes_through_update) {
    // The "Up" button returns the choice message Msg.Inc; update maps it to
    // model + 1, so a starting model of 7 becomes 8.
    const auto routed = eval(R"(
        choice Msg { Inc }
        GraphicalUi.test_click({
            "_": "gui_config",
            "model": 0,
            "view": (integer _count) -> GraphicalUi.column([
                GraphicalUi.button("Up", () -> Msg.Inc)
            ]),
            "update": (integer model, Msg msg) -> match msg {
                case Msg.Inc { model + 1 }
            }
        }, 7, "Up")
    )");

    ASSERT_TRUE(routed.is_integer());
    ASSERT_EQ(routed.as_integer(), 8);
}

// ── Headless interaction API: duplicate locators, secondary pointer events,
//    arbitrary arity, end-to-end keyboard, and widget-state assertions ──

// Two buttons share the label "Step"; test_count reports the duplicate, an
// index selects which one fires, and a style id disambiguates by identity.
LUMA_TEST(interaction_count_and_indexed_click) {
    const std::string cfg = R"({
        "_": "gui_config", "model": 0,
        "view": (integer _c) -> GraphicalUi.column([
            GraphicalUi.button("Step", () -> "inc", {"id": "step-a"}),
            GraphicalUi.button("Step", () -> "dec", {"id": "step-b"})
        ]),
        "update": (integer m, string msg) -> match msg {
            case == "inc" { m + 1 }
            case == "dec" { m - 1 }
            else { m }
        }
    })";

    const auto count = eval("GraphicalUi.test_count(" + cfg + ", 0, \"Step\")");
    ASSERT_TRUE(count.is_integer());
    ASSERT_EQ(count.as_integer(), 2);

    const auto first = eval("GraphicalUi.test_click(" + cfg + ", 10, \"Step\", 0)");
    ASSERT_TRUE(first.is_integer());
    ASSERT_EQ(first.as_integer(), 11);

    const auto second = eval("GraphicalUi.test_click(" + cfg + ", 10, \"Step\", 1)");
    ASSERT_TRUE(second.is_integer());
    ASSERT_EQ(second.as_integer(), 9);

    // A style id is surfaced as a locator, disambiguating without an index.
    const auto by_id = eval("GraphicalUi.test_click(" + cfg + ", 10, \"step-b\")");
    ASSERT_TRUE(by_id.is_integer());
    ASSERT_EQ(by_id.as_integer(), 9);
}

LUMA_TEST(interaction_click_index_out_of_range_throws) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.test_click({
            "_": "gui_config", "model": 0,
            "view": (integer _c) -> GraphicalUi.button("Only", () -> "x")
        }, 0, "Only", 5)
    )"));
}

// Secondary pointer handlers (style on_double_click / on_right_click) are
// registered as their own callback ids and fired by name through test_event.
LUMA_TEST(interaction_event_secondary_pointer) {
    const std::string cfg = R"({
        "_": "gui_config", "model": 0,
        "view": (integer count) -> GraphicalUi.column([
            GraphicalUi.button("Special", () -> "noop", {"on_double_click": () -> count + 100}),
            GraphicalUi.button("Menu", () -> "noop", {"on_right_click": () -> count + 1000})
        ]),
        "update": (integer m, string _msg) -> m
    })";

    const auto dbl = eval("GraphicalUi.test_event(" + cfg + ", 5, \"Special\", \"double_click\")");
    ASSERT_TRUE(dbl.is_integer());
    ASSERT_EQ(dbl.as_integer(), 105);

    const auto rc = eval("GraphicalUi.test_event(" + cfg + ", 5, \"Menu\", \"right_click\")");
    ASSERT_TRUE(rc.is_integer());
    ASSERT_EQ(rc.as_integer(), 1005);
}

// test_event forwards an explicit args array, so handlers of any arity work.
LUMA_TEST(interaction_event_arbitrary_arity) {
    const std::string cfg = R"({
        "_": "gui_config", "model": 0,
        "view": (integer count) -> GraphicalUi.column([
            GraphicalUi.button("Pad", () -> "noop",
                {"on_mouse_move": (integer x, integer y) -> count + x + y})
        ]),
        "update": (integer m, string _msg) -> m
    })";

    const auto moved =
        eval("GraphicalUi.test_event(" + cfg + ", 5, \"Pad\", \"mouse_move\", [3, 4])");
    ASSERT_TRUE(moved.is_integer());
    ASSERT_EQ(moved.as_integer(), 12);
}

LUMA_TEST(interaction_event_unknown_throws) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.test_event({
            "_": "gui_config", "model": 0,
            "view": (integer _c) -> GraphicalUi.button("X", () -> "noop")
        }, 0, "X", "teleport")
    )"));
}

// test_key drives the keyboard subscription end-to-end: subscribe -> on_key
// callback -> update -> new model, mirroring the live dispatch.
LUMA_TEST(interaction_key_drives_subscription) {
    const std::string cfg = R"({
        "_": "gui_config", "model": 0,
        "view": (integer _c) -> GraphicalUi.label("k"),
        "update": (integer m, string msg) -> match msg {
            case == "inc" { m + 1 }
            case == "dec" { m - 1 }
            else { m }
        },
        "subscribe": (integer _c) -> [
            GraphicalUi.on_key("keys", "*", (string key) -> match key {
                case == "ArrowUp" { "inc" }
                case == "ArrowDown" { "dec" }
                else { "" }
            })
        ]
    })";

    const auto up = eval("GraphicalUi.test_key(" + cfg + ", 0, \"ArrowUp\")");
    ASSERT_TRUE(up.is_integer());
    ASSERT_EQ(up.as_integer(), 1);

    const auto down = eval("GraphicalUi.test_key(" + cfg + ", 5, \"ArrowDown\")");
    ASSERT_TRUE(down.is_integer());
    ASSERT_EQ(down.as_integer(), 4);

    // A filtered subscription fires only for its own key.
    const auto saved = eval(R"(
        GraphicalUi.test_key({
            "_": "gui_config", "model": 0,
            "view": (integer _c) -> GraphicalUi.label("k"),
            "update": (integer m, string msg) -> match msg {
                case == "save" { m + 7 }
                else { m }
            },
            "subscribe": (integer _c) -> [
                GraphicalUi.on_key("save", "s", (string _k) -> "save")
            ]
        }, 0, "s")
    )");
    ASSERT_TRUE(saved.is_integer());
    ASSERT_EQ(saved.as_integer(), 7);
}

LUMA_TEST(interaction_key_no_match_throws) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.test_key({
            "_": "gui_config", "model": 0,
            "view": (integer _c) -> GraphicalUi.label("k"),
            "subscribe": (integer _c) -> [
                GraphicalUi.on_key("save", "s", (string _k) -> "save")
            ]
        }, 0, "x")
    )"));
}

// test_find returns a rendered widget dictionary so tests can assert on its
// serialized state without firing an interaction.
LUMA_TEST(interaction_find_returns_widget) {
    const std::string cfg = R"({
        "_": "gui_config", "model": 0,
        "view": (integer count) -> GraphicalUi.column([
            GraphicalUi.label("Count: ${count}"),
            GraphicalUi.button("Step", () -> "inc", {"id": "step-a"}),
            GraphicalUi.button("Step", () -> "dec", {"id": "step-b"})
        ])
    })";

    const auto text = eval("Dictionary.get_or(GraphicalUi.test_find(" + cfg +
                           ", 7, \"Count: 7\"), \"text\", \"\")");
    ASSERT_TRUE(text.is_string());
    ASSERT_EQ(text.as_string(), "Count: 7");

    const auto id = eval("Dictionary.get_or(GraphicalUi.test_find(" + cfg +
                         ", 0, \"Step\", 1), \"_element_id\", \"\")");
    ASSERT_TRUE(id.is_string());
    ASSERT_EQ(id.as_string(), "step-b");

    const auto type = eval("Dictionary.get_or(GraphicalUi.test_find(" + cfg +
                           ", 0, \"Step\", 0), \"type\", \"\")");
    ASSERT_TRUE(type.is_string());
    ASSERT_EQ(type.as_string(), "button");
}

LUMA_TEST(interaction_find_index_out_of_range_throws) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.test_find({
            "_": "gui_config", "model": 0,
            "view": (integer _c) -> GraphicalUi.button("Only", () -> "x")
        }, 0, "Only", 5)
    )"));
}

// ═══════════════════════════════════════════════════════════
// Commands
// ═══════════════════════════════════════════════════════════

LUMA_TEST(none_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.none()
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "none");
}

LUMA_TEST(batch_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.batch([GraphicalUi.none(), GraphicalUi.none()])
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "batch");
}

LUMA_TEST(write_clipboard_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.write_clipboard("hello")
        Dictionary.get_or(cmd, "text", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "hello");
}

LUMA_TEST(navigate_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.navigate("Settings")
        Dictionary.get_or(cmd, "route", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Settings");
}

LUMA_TEST(navigate_back_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.navigate_back()
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "navigate_back");
}

LUMA_TEST(focus_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.focus("my-input")
        Dictionary.get_or(cmd, "widget_id", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "my-input");
}

LUMA_TEST(announce_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.announce("Item deleted")
        Dictionary.get_or(cmd, "text", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Item deleted");
}

LUMA_TEST(with_command) {
    const auto v = eval(R"(
        dictionary<string> pair = GraphicalUi.with_command(42, GraphicalUi.none())
        boolean has_model = Dictionary.has(pair, "_gui_model")
        boolean has_cmd = Dictionary.has(pair, "_gui_command")
        has_model == true
    )");
    ASSERT_TRUE(v.is_bool());
    ASSERT_EQ(v.as_bool(), true);
}

// ═══════════════════════════════════════════════════════════
// Subscriptions
// ═══════════════════════════════════════════════════════════

LUMA_TEST(constant_subscribe) {
    const auto v = eval("GraphicalUi.SUBSCRIBE");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "subscribe");
}

LUMA_TEST(on_tick_subscription) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_tick("timer1", 1000, () -> 0)
        Dictionary.get_or(sub, "_sub_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "timer");
}

LUMA_TEST(on_key_subscription) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_key("keys", "Ctrl+S", (string key) -> 0)
        Dictionary.get_or(sub, "_sub_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "keyboard");
}

LUMA_TEST(on_resize_subscription) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_resize("resize", (integer w, integer h) -> 0)
        Dictionary.get_or(sub, "_sub_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "resize");
}

LUMA_TEST(on_focus_subscription) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_focus("focus", (boolean focused) -> 0)
        Dictionary.get_or(sub, "_sub_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "focus");
}

// ═══════════════════════════════════════════════════════════
// Components, routing, accessibility
// ═══════════════════════════════════════════════════════════

LUMA_TEST(component_calls_render_fn) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.component("my-comp", 42, (any slice) -> GraphicalUi.label("val"))
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "label");
}

LUMA_TEST(component_memoization) {
    // Without a running app, memoization is skipped, but component still works.
    const auto v = eval(R"(
        dictionary<string> w1 = GraphicalUi.component("memo-test", 42, (any s) -> GraphicalUi.label("a"))
        dictionary<string> w2 = GraphicalUi.component("memo-test", 42, (any s) -> GraphicalUi.label("b"))
        # Without active_app both calls invoke render_fn independently.
        Dictionary.get_or(w2, "text", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "b");
}

LUMA_TEST(router_selects_route) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.router("home", {
            "home": () -> GraphicalUi.label("Home page"),
            "about": () -> GraphicalUi.label("About page")
        })
        Dictionary.get_or(w, "text", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Home page");
}

LUMA_TEST(router_with_widget_values) {
    // Router should accept pre-built widget values, not just callables.
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.router("home", {
            "home": GraphicalUi.label("Home"),
            "about": GraphicalUi.label("About")
        })
        Dictionary.get_or(w, "text", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Home");
}

LUMA_TEST(router_parameterised) {
    // Router should match parameterised routes.
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.router("/user/42", {
            "/user/{id}": (any params) -> GraphicalUi.label(Dictionary.get_or(params, "id", ""))
        })
        Dictionary.get_or(w, "text", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "42");
}

LUMA_TEST(router_unknown_route) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.router("unknown", {"home": () -> GraphicalUi.label("Home")})
        Dictionary.get_or(w, "text", "")
    )");
    ASSERT_TRUE(v.is_string());
    // Should contain "not found" fallback text.
    ASSERT_TRUE(v.as_string().find("not found") != std::string::npos);
}

LUMA_TEST(navigation_link_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.navigation_link("Go Home", "home")
    )"));
}

LUMA_TEST(accessible_widget) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.accessible(
            GraphicalUi.label("Click here"),
            {"role": "button", "label": "Action button"}
        )
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "accessible");
}

LUMA_TEST(keyed_widget) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.keyed("item-1", GraphicalUi.label("Hello"))
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "keyed");
}

LUMA_TEST(keyed_widget_key) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.keyed("item-1", GraphicalUi.label("Hello"))
        Dictionary.get_or(w, "_key", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "item-1");
}

LUMA_TEST(on_mouse_subscription) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_mouse("m1", "move", (any evt) -> 0)
        Dictionary.get_or(sub, "_sub_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "mouse");
}

LUMA_TEST(on_mouse_event_type) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_mouse("m1", "scroll", (any evt) -> 0)
        Dictionary.get_or(sub, "event", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "scroll");
}

// GraphicalUi.on_mouse_typed reuses the mouse subscription wiring but flags the
// subscription so the runtime delivers a typed GraphicalUi.MouseEvent record.
LUMA_TEST(on_mouse_typed_marks_typed_mouse_subscription) {
    const auto v = eval(R"(
        GraphicalUi.on_mouse_typed("m1", "move", (GraphicalUi.MouseEvent e) -> 0)
    )");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();

    const auto* sub_type = d.find("_sub_type");
    ASSERT_TRUE(sub_type != nullptr && sub_type->is_string());
    ASSERT_EQ(sub_type->as_string(), "mouse");

    const auto* typed = d.find("_typed");
    ASSERT_TRUE(typed != nullptr && typed->is_bool());
    ASSERT_TRUE(typed->as_bool());
}

// build_mouse_event_record turns the browser payload dictionary into a typed
// GraphicalUi.MouseEvent record: number coordinates, a MouseButton choice, and
// boolean modifiers.
LUMA_TEST(build_mouse_event_record_maps_payload) {
    auto payload = std::make_shared<DictionaryValue>();
    payload->set("x", Value{12.0});
    payload->set("y", Value{34.0});
    payload->set("button", Value{std::string{"right"}});
    payload->set("ctrl", Value{true});
    payload->set("shift", Value{false});
    payload->set("alt", Value{true});

    const auto rec_val = gui_detail::build_mouse_event_record(*payload);
    ASSERT_TRUE(rec_val.is_record());
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.type_name, "MouseEvent");

    const auto* x = rec.find_field("x");
    ASSERT_TRUE(x != nullptr && x->is_number());
    ASSERT_NEAR(x->as_number(), 12.0, 1e-9);

    const auto* y = rec.find_field("y");
    ASSERT_TRUE(y != nullptr && y->is_number());
    ASSERT_NEAR(y->as_number(), 34.0, 1e-9);

    const auto* button = rec.find_field("button");
    ASSERT_TRUE(button != nullptr && button->is_choice());
    ASSERT_EQ(button->as_choice()->type_name, "MouseButton");
    ASSERT_EQ(button->as_choice()->variant, "Right");

    const auto* ctrl = rec.find_field("ctrl");
    ASSERT_TRUE(ctrl != nullptr && ctrl->is_bool());
    ASSERT_TRUE(ctrl->as_bool());

    const auto* shift = rec.find_field("shift");
    ASSERT_TRUE(shift != nullptr && shift->is_bool());
    ASSERT_FALSE(shift->as_bool());

    const auto* alt = rec.find_field("alt");
    ASSERT_TRUE(alt != nullptr && alt->is_bool());
    ASSERT_TRUE(alt->as_bool());
}

// Middle-button strings map to the Middle variant.
LUMA_TEST(build_mouse_event_record_middle_button) {
    auto payload = std::make_shared<DictionaryValue>();
    payload->set("button", Value{std::string{"middle"}});

    const auto rec_val = gui_detail::build_mouse_event_record(*payload);
    const auto* button = rec_val.as_record()->find_field("button");
    ASSERT_TRUE(button != nullptr && button->is_choice());
    ASSERT_EQ(button->as_choice()->variant, "Middle");
}

// A missing payload stays total: coordinates default to 0, modifiers to false,
// and an absent/unknown button falls back to Left.
LUMA_TEST(build_mouse_event_record_defaults_to_left) {
    auto payload = std::make_shared<DictionaryValue>();

    const auto rec_val = gui_detail::build_mouse_event_record(*payload);
    const auto& rec = *rec_val.as_record();

    ASSERT_NEAR(rec.find_field("x")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(rec.find_field("y")->as_number(), 0.0, 1e-9);
    ASSERT_EQ(rec.find_field("button")->as_choice()->variant, "Left");
    ASSERT_FALSE(rec.find_field("ctrl")->as_bool());
    ASSERT_FALSE(rec.find_field("shift")->as_bool());
    ASSERT_FALSE(rec.find_field("alt")->as_bool());
}

// GraphicalUi.on_scroll_typed flags the scroll subscription so the runtime
// delivers a typed GraphicalUi.ScrollPosition record.
LUMA_TEST(on_scroll_typed_marks_typed_scroll_subscription) {
    const auto v = eval(R"(
        GraphicalUi.on_scroll_typed("s1", (GraphicalUi.ScrollPosition p) -> p.y)
    )");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();

    const auto* sub_type = d.find("_sub_type");
    ASSERT_TRUE(sub_type != nullptr && sub_type->is_string());
    ASSERT_EQ(sub_type->as_string(), "scroll");

    const auto* typed = d.find("_typed");
    ASSERT_TRUE(typed != nullptr && typed->is_bool());
    ASSERT_TRUE(typed->as_bool());
}

// build_scroll_position_record turns the browser scroll payload into a typed
// GraphicalUi.ScrollPosition record with number coordinates.
LUMA_TEST(build_scroll_position_record_maps_payload) {
    auto payload = std::make_shared<DictionaryValue>();
    payload->set("x", Value{5.0});
    payload->set("y", Value{120.0});

    const auto rec_val = gui_detail::build_scroll_position_record(*payload);
    ASSERT_TRUE(rec_val.is_record());
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.type_name, "ScrollPosition");

    const auto* x = rec.find_field("x");
    ASSERT_TRUE(x != nullptr && x->is_number());
    ASSERT_NEAR(x->as_number(), 5.0, 1e-9);

    const auto* y = rec.find_field("y");
    ASSERT_TRUE(y != nullptr && y->is_number());
    ASSERT_NEAR(y->as_number(), 120.0, 1e-9);
}

// A missing scroll payload defaults both coordinates to 0.
LUMA_TEST(build_scroll_position_record_defaults_zero) {
    auto payload = std::make_shared<DictionaryValue>();

    const auto rec_val = gui_detail::build_scroll_position_record(*payload);
    const auto& rec = *rec_val.as_record();
    ASSERT_NEAR(rec.find_field("x")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(rec.find_field("y")->as_number(), 0.0, 1e-9);
}

// GraphicalUi.on_key_typed reuses the keyboard subscription wiring but flags the
// subscription so the runtime delivers a typed GraphicalUi.KeyEvent record.
LUMA_TEST(on_key_typed_marks_typed_keyboard_subscription) {
    const auto v = eval(R"(
        GraphicalUi.on_key_typed("k1", "s", (GraphicalUi.KeyEvent e) -> e.key)
    )");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();

    const auto* sub_type = d.find("_sub_type");
    ASSERT_TRUE(sub_type != nullptr && sub_type->is_string());
    ASSERT_EQ(sub_type->as_string(), "keyboard");

    const auto* filter = d.find("filter");
    ASSERT_TRUE(filter != nullptr && filter->is_string());
    ASSERT_EQ(filter->as_string(), "s");

    const auto* typed = d.find("_typed");
    ASSERT_TRUE(typed != nullptr && typed->is_bool());
    ASSERT_TRUE(typed->as_bool());
}

// build_key_event_record turns the key name plus a {ctrl, shift, alt, meta}
// modifier payload into a typed GraphicalUi.KeyEvent record.
LUMA_TEST(build_key_event_record_maps_payload) {
    auto mods = std::make_shared<DictionaryValue>();
    mods->set("ctrl", Value{true});
    mods->set("shift", Value{false});
    mods->set("alt", Value{false});
    mods->set("meta", Value{true});

    const auto rec_val = gui_detail::build_key_event_record("s", mods.get());
    ASSERT_TRUE(rec_val.is_record());
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.type_name, "KeyEvent");

    const auto* key = rec.find_field("key");
    ASSERT_TRUE(key != nullptr && key->is_string());
    ASSERT_EQ(key->as_string(), "s");

    const auto* ctrl = rec.find_field("ctrl");
    ASSERT_TRUE(ctrl != nullptr && ctrl->is_bool());
    ASSERT_TRUE(ctrl->as_bool());

    const auto* shift = rec.find_field("shift");
    ASSERT_TRUE(shift != nullptr && shift->is_bool());
    ASSERT_FALSE(shift->as_bool());

    const auto* alt = rec.find_field("alt");
    ASSERT_TRUE(alt != nullptr && alt->is_bool());
    ASSERT_FALSE(alt->as_bool());

    const auto* meta = rec.find_field("meta");
    ASSERT_TRUE(meta != nullptr && meta->is_bool());
    ASSERT_TRUE(meta->as_bool());
}

// A null modifier payload (the headless test path) keeps the record total:
// every modifier defaults to false while the key name is preserved.
LUMA_TEST(build_key_event_record_null_mods_defaults_false) {
    const auto rec_val = gui_detail::build_key_event_record("Enter", nullptr);
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.find_field("key")->as_string(), "Enter");
    ASSERT_FALSE(rec.find_field("ctrl")->as_bool());
    ASSERT_FALSE(rec.find_field("shift")->as_bool());
    ASSERT_FALSE(rec.find_field("alt")->as_bool());
    ASSERT_FALSE(rec.find_field("meta")->as_bool());
}

// ── N05: GraphicalUi.WindowSize / on_resize_typed ──────────

// GraphicalUi.on_resize_typed flags the resize subscription so the runtime
// delivers a single GraphicalUi.WindowSize record.
LUMA_TEST(on_resize_typed_marks_typed_resize_subscription) {
    const auto v = eval(R"(
        GraphicalUi.on_resize_typed("r1", (GraphicalUi.WindowSize s) -> s.width)
    )");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();

    const auto* sub_type = d.find("_sub_type");
    ASSERT_TRUE(sub_type != nullptr && sub_type->is_string());
    ASSERT_EQ(sub_type->as_string(), "resize");

    const auto* typed = d.find("_typed");
    ASSERT_TRUE(typed != nullptr && typed->is_bool());
    ASSERT_TRUE(typed->as_bool());
}

// build_window_size_record maps a width/height pair to integer record fields.
LUMA_TEST(build_window_size_record_maps_dimensions) {
    const auto rec_val = gui_detail::build_window_size_record(1280, 720);
    ASSERT_TRUE(rec_val.is_record());
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.type_name, "WindowSize");
    ASSERT_TRUE(rec.find_field("width")->is_integer());
    ASSERT_EQ(rec.find_field("width")->as_integer(), 1280);
    ASSERT_EQ(rec.find_field("height")->as_integer(), 720);
}

// ── N03: GraphicalUi.DragEvent / DragPhase / on_drag_typed ─

// GraphicalUi.on_drag_typed flags the drag subscription so the runtime delivers
// a typed GraphicalUi.DragEvent record.
LUMA_TEST(on_drag_typed_marks_typed_drag_subscription) {
    const auto v = eval(R"(
        GraphicalUi.on_drag_typed("d1", (GraphicalUi.DragEvent e) -> e.data)
    )");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();

    const auto* sub_type = d.find("_sub_type");
    ASSERT_TRUE(sub_type != nullptr && sub_type->is_string());
    ASSERT_EQ(sub_type->as_string(), "drag");

    const auto* typed = d.find("_typed");
    ASSERT_TRUE(typed != nullptr && typed->is_bool());
    ASSERT_TRUE(typed->as_bool());
}

// build_drag_event_record maps the payload into number coordinates, a data
// string, and a DragPhase choice mapped from the `event` key.
LUMA_TEST(build_drag_event_record_maps_payload) {
    auto payload = std::make_shared<DictionaryValue>();
    payload->set("x", Value{40.0});
    payload->set("y", Value{55.0});
    payload->set("data", Value{std::string{"card-7"}});
    payload->set("event", Value{std::string{"drop"}});

    const auto rec_val = gui_detail::build_drag_event_record(*payload);
    ASSERT_TRUE(rec_val.is_record());
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.type_name, "DragEvent");
    ASSERT_NEAR(rec.find_field("x")->as_number(), 40.0, 1e-9);
    ASSERT_NEAR(rec.find_field("y")->as_number(), 55.0, 1e-9);
    ASSERT_EQ(rec.find_field("data")->as_string(), "card-7");

    const auto* phase = rec.find_field("phase");
    ASSERT_TRUE(phase != nullptr && phase->is_choice());
    ASSERT_EQ(phase->as_choice()->type_name, "DragPhase");
    ASSERT_EQ(phase->as_choice()->variant, "Drop");
}

// A missing/unknown drag phase falls back to Start; missing fields stay total.
LUMA_TEST(build_drag_event_record_defaults_to_start) {
    auto payload = std::make_shared<DictionaryValue>();

    const auto rec_val = gui_detail::build_drag_event_record(*payload);
    const auto& rec = *rec_val.as_record();
    ASSERT_NEAR(rec.find_field("x")->as_number(), 0.0, 1e-9);
    ASSERT_EQ(rec.find_field("data")->as_string(), "");
    ASSERT_EQ(rec.find_field("phase")->as_choice()->variant, "Start");
}

// ── G04: GraphicalUi.DropEvent / drop_target_typed ─────────

// build_drop_event_record maps the payload into a data string plus number drop
// coordinates.
LUMA_TEST(build_drop_event_record_maps_payload) {
    auto payload = std::make_shared<DictionaryValue>();
    payload->set("data", Value{std::string{"card-3"}});
    payload->set("x", Value{120.0});
    payload->set("y", Value{64.0});

    const auto rec_val = gui_detail::build_drop_event_record(*payload);
    ASSERT_TRUE(rec_val.is_record());
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.type_name, "DropEvent");
    ASSERT_EQ(rec.find_field("data")->as_string(), "card-3");
    ASSERT_NEAR(rec.find_field("x")->as_number(), 120.0, 1e-9);
    ASSERT_NEAR(rec.find_field("y")->as_number(), 64.0, 1e-9);
}

// Missing fields stay total: data defaults to "" and coordinates to 0.
LUMA_TEST(build_drop_event_record_defaults) {
    auto payload = std::make_shared<DictionaryValue>();

    const auto rec_val = gui_detail::build_drop_event_record(*payload);
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.find_field("data")->as_string(), "");
    ASSERT_NEAR(rec.find_field("x")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(rec.find_field("y")->as_number(), 0.0, 1e-9);
}

// GraphicalUi.drop_target_typed flags the widget so the renderer forwards drop
// coordinates for a typed GraphicalUi.DropEvent.  Interactive widgets need app
// context, so render through the headless harness.
LUMA_TEST(drop_target_typed_flags_widget) {
    const std::string cfg = R"({
        "_": "gui_config", "model": 0,
        "view": (integer _c) -> GraphicalUi.drop_target_typed(GraphicalUi.label("zone"),
            (GraphicalUi.DropEvent e) -> e.data)
    })";

    const auto v = eval("GraphicalUi.test_render(" + cfg + ", 0)");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();
    ASSERT_EQ(d.find("type")->as_string(), "drop_target");
    ASSERT_TRUE(d.find("_drop_typed") != nullptr && d.find("_drop_typed")->as_bool());
}

// ── G01: GraphicalUi.StorageEvent / on_storage_change_typed ─

// GraphicalUi.on_storage_change_typed flags the storage subscription so the
// runtime delivers a typed GraphicalUi.StorageEvent record, and threads the key
// filter through unchanged.
LUMA_TEST(on_storage_change_typed_marks_typed_storage_subscription) {
    const auto v = eval(R"(
        GraphicalUi.on_storage_change_typed("st1", "theme",
            (GraphicalUi.StorageEvent e) -> e.key)
    )");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();

    const auto* sub_type = d.find("_sub_type");
    ASSERT_TRUE(sub_type != nullptr && sub_type->is_string());
    ASSERT_EQ(sub_type->as_string(), "storage");

    const auto* skey = d.find("key");
    ASSERT_TRUE(skey != nullptr && skey->is_string());
    ASSERT_EQ(skey->as_string(), "theme");

    const auto* typed = d.find("_typed");
    ASSERT_TRUE(typed != nullptr && typed->is_bool());
    ASSERT_TRUE(typed->as_bool());
}

// build_storage_event_record maps the payload into a key string plus optional
// old/new values.  A present value becomes some(string); an absent one becomes
// none (a null Value).
LUMA_TEST(build_storage_event_record_maps_payload) {
    auto payload = std::make_shared<DictionaryValue>();
    payload->set("key", Value{std::string{"theme"}});
    payload->set("oldValue", Value{std::string{"light"}});
    payload->set("newValue", Value{std::string{"dark"}});

    const auto rec_val = gui_detail::build_storage_event_record(*payload);
    ASSERT_TRUE(rec_val.is_record());
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.type_name, "StorageEvent");
    ASSERT_EQ(rec.find_field("key")->as_string(), "theme");

    const auto* old_value = rec.find_field("old_value");
    ASSERT_TRUE(old_value != nullptr && old_value->is_string());
    ASSERT_EQ(old_value->as_string(), "light");

    const auto* new_value = rec.find_field("new_value");
    ASSERT_TRUE(new_value != nullptr && new_value->is_string());
    ASSERT_EQ(new_value->as_string(), "dark");
}

// A key that was added has no old value, and a cleared key has no new value —
// the absent field maps to none (a null Value).
LUMA_TEST(build_storage_event_record_optional_absence) {
    auto added = std::make_shared<DictionaryValue>();
    added->set("key", Value{std::string{"token"}});
    added->set("newValue", Value{std::string{"abc"}});

    const auto rec_val = gui_detail::build_storage_event_record(*added);
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.find_field("key")->as_string(), "token");
    ASSERT_TRUE(rec.find_field("old_value")->is_null());
    ASSERT_EQ(rec.find_field("new_value")->as_string(), "abc");

    // A wholly-empty payload keeps the record total: key "" and both values none.
    auto empty = std::make_shared<DictionaryValue>();
    const auto empty_val = gui_detail::build_storage_event_record(*empty);
    const auto& empty_rec = *empty_val.as_record();
    ASSERT_EQ(empty_rec.find_field("key")->as_string(), "");
    ASSERT_TRUE(empty_rec.find_field("old_value")->is_null());
    ASSERT_TRUE(empty_rec.find_field("new_value")->is_null());
}

// ── G07: GraphicalUi.WheelDelta / on_wheel_typed ───────────

// GraphicalUi.on_wheel_typed flags the wheel subscription so the runtime delivers
// a typed GraphicalUi.WheelDelta record.
LUMA_TEST(on_wheel_typed_marks_typed_wheel_subscription) {
    const auto v = eval(R"(
        GraphicalUi.on_wheel_typed("wh1", (GraphicalUi.WheelDelta d) -> d.delta_y)
    )");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();

    const auto* sub_type = d.find("_sub_type");
    ASSERT_TRUE(sub_type != nullptr && sub_type->is_string());
    ASSERT_EQ(sub_type->as_string(), "wheel");

    const auto* typed = d.find("_typed");
    ASSERT_TRUE(typed != nullptr && typed->is_bool());
    ASSERT_TRUE(typed->as_bool());
}

// build_wheel_delta_record maps the payload into number deltas.
LUMA_TEST(build_wheel_delta_record_maps_payload) {
    auto payload = std::make_shared<DictionaryValue>();
    payload->set("deltaX", Value{-12.0});
    payload->set("deltaY", Value{48.5});

    const auto rec_val = gui_detail::build_wheel_delta_record(*payload);
    ASSERT_TRUE(rec_val.is_record());
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.type_name, "WheelDelta");
    ASSERT_NEAR(rec.find_field("delta_x")->as_number(), -12.0, 1e-9);
    ASSERT_NEAR(rec.find_field("delta_y")->as_number(), 48.5, 1e-9);
}

// A missing wheel payload defaults both deltas to 0.
LUMA_TEST(build_wheel_delta_record_defaults_zero) {
    auto payload = std::make_shared<DictionaryValue>();

    const auto rec_val = gui_detail::build_wheel_delta_record(*payload);
    const auto& rec = *rec_val.as_record();
    ASSERT_NEAR(rec.find_field("delta_x")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(rec.find_field("delta_y")->as_number(), 0.0, 1e-9);
}

// ── G05: GraphicalUi.VisibilityState / visibility_state_to_string ─

// GraphicalUi.on_visibility_change_typed flags the visibility subscription so the
// runtime delivers a typed GraphicalUi.VisibilityState choice.
LUMA_TEST(on_visibility_change_typed_marks_typed_visibility_subscription) {
    const auto v = eval(R"(
        GraphicalUi.on_visibility_change_typed("vi1",
            (GraphicalUi.VisibilityState s) -> GraphicalUi.visibility_state_to_string(s))
    )");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();

    const auto* sub_type = d.find("_sub_type");
    ASSERT_TRUE(sub_type != nullptr && sub_type->is_string());
    ASSERT_EQ(sub_type->as_string(), "visibility");

    const auto* typed = d.find("_typed");
    ASSERT_TRUE(typed != nullptr && typed->is_bool());
    ASSERT_TRUE(typed->as_bool());
}

// GraphicalUi.visibility_state_to_string bridges each variant to its lowercase
// string.
LUMA_TEST(visibility_state_to_string_bridges_variants) {
    ASSERT_EQ(eval(R"(GraphicalUi.visibility_state_to_string(GraphicalUi.VisibilityState.Visible))")
                  .as_string(),
              "visible");
    ASSERT_EQ(eval(R"(GraphicalUi.visibility_state_to_string(GraphicalUi.VisibilityState.Hidden))")
                  .as_string(),
              "hidden");
}

// ── G05: GraphicalUi.ScrollBehavior / scroll_to_of ─────────

// GraphicalUi.scroll_to_of lowers the ScrollBehavior choice to the same behavior
// string scroll_to uses.
LUMA_TEST(scroll_to_of_lowers_behavior_choice) {
    const auto v = eval(R"(GraphicalUi.scroll_to_of("box", GraphicalUi.ScrollBehavior.Smooth))");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();
    ASSERT_EQ(d.find("_command_type")->as_string(), "scroll_to");
    ASSERT_EQ(d.find("behavior")->as_string(), "smooth");
    ASSERT_EQ(d.find("widget_id")->as_string(), "box");
}

// ── G01: GraphicalUi.ThemeMode / set_theme_mode_of ─────────

// GraphicalUi.set_theme_mode_of lowers the ThemeMode choice to the same mode
// string set_theme_mode uses; theme_mode_to_string bridges each variant.
LUMA_TEST(set_theme_mode_of_and_bridge) {
    const auto v = eval(R"(GraphicalUi.set_theme_mode_of(GraphicalUi.ThemeMode.Dark))");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();
    ASSERT_EQ(d.find("_command_type")->as_string(), "set_theme_mode");
    ASSERT_EQ(d.find("mode")->as_string(), "dark");

    ASSERT_EQ(eval(R"(GraphicalUi.theme_mode_to_string(GraphicalUi.ThemeMode.Light))").as_string(),
              "light");
    ASSERT_EQ(eval(R"(GraphicalUi.theme_mode_to_string(GraphicalUi.ThemeMode.Auto))").as_string(),
              "auto");
}

// ── G06: GraphicalUi.SortDirection ─────────────────────────

// GraphicalUi.sort_direction_to_string bridges each variant, and the table
// `sort_direction` option accepts the typed choice (lowered to asc/desc).
LUMA_TEST(sort_direction_to_string_and_table_option) {
    ASSERT_EQ(eval(R"(GraphicalUi.sort_direction_to_string(GraphicalUi.SortDirection.Ascending))")
                  .as_string(),
              "asc");
    ASSERT_EQ(eval(R"(GraphicalUi.sort_direction_to_string(GraphicalUi.SortDirection.Descending))")
                  .as_string(),
              "desc");

    const auto v = eval(R"(
        GraphicalUi.table(["A"], [["1"]],
            {"sort_direction": GraphicalUi.SortDirection.Descending})
    )");
    ASSERT_TRUE(v.is_dictionary());
    ASSERT_EQ(v.as_dictionary()->find("sort_direction")->as_string(), "desc");
}

// ── N02: GraphicalUi.MouseEventType / on_mouse_of ──────────

// GraphicalUi.on_mouse_of lowers the MouseEventType choice to the same "event"
// key on_mouse uses, and stays untyped (raw dictionary payload).
LUMA_TEST(on_mouse_of_lowers_event_type_choice) {
    const auto v = eval(R"(
        GraphicalUi.on_mouse_of("m1", GraphicalUi.MouseEventType.Down, (dictionary d) -> d)
    )");
    ASSERT_TRUE(v.is_dictionary());
    const auto& d = *v.as_dictionary();
    ASSERT_EQ(d.find("_sub_type")->as_string(), "mouse");
    ASSERT_EQ(d.find("event")->as_string(), "down");
    // Not a typed subscription — the raw dictionary is still delivered.
    ASSERT_TRUE(d.find("_typed") == nullptr);
}

// GraphicalUi.mouse_event_type_to_string bridges each variant to its string.
LUMA_TEST(mouse_event_type_to_string_bridges_variants) {
    ASSERT_EQ(eval(R"(GraphicalUi.mouse_event_type_to_string(GraphicalUi.MouseEventType.Click))")
                  .as_string(),
              "click");
    ASSERT_EQ(eval(R"(GraphicalUi.mouse_event_type_to_string(GraphicalUi.MouseEventType.Scroll))")
                  .as_string(),
              "scroll");
}

// ── N04: GraphicalUi.HttpResponse / http_get_full ──────────

// GraphicalUi.http_get_full builds the typed command variant.
LUMA_TEST(http_get_full_builds_typed_command) {
    const auto v = eval(R"(
        GraphicalUi.http_get_full("http://example.test", (result<GraphicalUi.HttpResponse> r) -> r)
    )");
    ASSERT_TRUE(v.is_dictionary());
    ASSERT_EQ(v.as_dictionary()->find("_command_type")->as_string(), "http_get_full");
}

// build_http_response_record_gui maps status/headers/body into the record.
LUMA_TEST(build_http_response_record_gui_maps_fields) {
    std::vector<std::pair<std::string, std::string>> headers{{"Content-Type", "application/json"}};
    const auto rec_val = gui_detail::build_http_response_record_gui(404, "not found", headers);
    ASSERT_TRUE(rec_val.is_record());
    const auto& rec = *rec_val.as_record();
    ASSERT_EQ(rec.type_name, "HttpResponse");
    ASSERT_TRUE(rec.find_field("status")->is_integer());
    ASSERT_EQ(rec.find_field("status")->as_integer(), 404);
    ASSERT_EQ(rec.find_field("body")->as_string(), "not found");

    const auto* hdrs = rec.find_field("headers");
    ASSERT_TRUE(hdrs != nullptr && hdrs->is_dictionary());
    ASSERT_EQ(hdrs->as_dictionary()->find("Content-Type")->as_string(), "application/json");
}

// GraphicalUi.classify_device_typed returns a DeviceInfo record with typed
// DeviceClass / Orientation choices, reusing the classify_device thresholds.
LUMA_TEST(classify_device_typed_returns_record) {
    const auto v = eval(R"(
        GraphicalUi.classify_device_typed(390, 844).width
    )");
    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 390);
}

// severity_to_string and button_variant_to_string bridge the typed choices back
// to the string keys of the untyped API.
LUMA_TEST(severity_to_string_bridge) {
    const auto v = eval(R"(
        [GraphicalUi.severity_to_string(GraphicalUi.Severity.Info),
         GraphicalUi.severity_to_string(GraphicalUi.Severity.Warning),
         GraphicalUi.severity_to_string(GraphicalUi.Severity.Error),
         GraphicalUi.severity_to_string(GraphicalUi.Severity.Success)]
    )");
    ASSERT_TRUE(v.is_array());
    const auto& e = *v.as_array()->elements;
    ASSERT_EQ(e[0].as_string(), "info");
    ASSERT_EQ(e[1].as_string(), "warning");
    ASSERT_EQ(e[2].as_string(), "error");
    ASSERT_EQ(e[3].as_string(), "success");
}

LUMA_TEST(button_variant_to_string_bridge) {
    const auto v = eval(R"(
        [GraphicalUi.button_variant_to_string(GraphicalUi.ButtonVariant.Primary),
         GraphicalUi.button_variant_to_string(GraphicalUi.ButtonVariant.Secondary),
         GraphicalUi.button_variant_to_string(GraphicalUi.ButtonVariant.Ghost),
         GraphicalUi.button_variant_to_string(GraphicalUi.ButtonVariant.Danger)]
    )");
    ASSERT_TRUE(v.is_array());
    const auto& e = *v.as_array()->elements;
    ASSERT_EQ(e[0].as_string(), "primary");
    ASSERT_EQ(e[1].as_string(), "secondary");
    ASSERT_EQ(e[2].as_string(), "ghost");
    ASSERT_EQ(e[3].as_string(), "danger");
}

// alert_of lowers a typed Severity onto the same widget the string form builds.
LUMA_TEST(alert_of_lowers_severity) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.alert_of("hi", GraphicalUi.Severity.Warning)
        Dictionary.get_or(w, "severity", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "warning");
}

// toast_of lowers a typed Severity and defaults the duration.
LUMA_TEST(toast_of_lowers_severity) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.toast_of("saved", GraphicalUi.Severity.Success)
        Dictionary.get_or(w, "severity", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "success");
}

LUMA_TEST(error_boundary_success) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.error_boundary(
            (string err) -> GraphicalUi.label("Error: " + err),
            () -> GraphicalUi.label("OK")
        )
        Dictionary.get_or(w, "text", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "OK");
}

LUMA_TEST(error_boundary_fallback) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.error_boundary(
            (string err) -> GraphicalUi.label("caught"),
            () -> {
                array<integer> a = []
                integer _x = a[99]
                GraphicalUi.label("never")
            }
        )
        Dictionary.get_or(w, "text", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "caught");
}

LUMA_TEST(constant_init) {
    const auto v = eval("GraphicalUi.INIT");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "init");
}

LUMA_TEST(http_get_with_headers) {
    // http_get should accept an optional headers dictionary.
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.http_get(
            "https://example.com",
            (any r) -> 0,
            {"Authorization": "Bearer token123"}
        )
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "http_get");
}

// ═══════════════════════════════════════════════════════════
// Elm loop infrastructure tests (§K)
// ═══════════════════════════════════════════════════════════

// Test that widgets throw a context-violation error with a hint when called
// outside a GraphicalUi.app() view function.
// (Refactored: widgets now use deferred callback binding instead of throwing.)
// Test that button throws when created outside app context (no active_app).
LUMA_TEST(deferred_callback_button) {
    ASSERT_THROWS(eval(R"(GraphicalUi.button("Click", () -> {}))"));
}

// Test that text_input throws when created outside app context (no active_app).
LUMA_TEST(deferred_callback_text_input) {
    ASSERT_THROWS(eval(R"(GraphicalUi.text_input("val", (string _v) -> {}))"));
}

// ═══════════════════════════════════════════════════════════
// New widget types (require app context — deferred callbacks)
// ═══════════════════════════════════════════════════════════

LUMA_TEST(file_input_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.file_input(() -> "selected")
    )"));
}

LUMA_TEST(file_input_with_accept_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.file_input(() -> "selected", ".png,.jpg")
    )"));
}

LUMA_TEST(date_picker_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.date_picker("2025-01-01", (string _d) -> _d)
    )"));
}

LUMA_TEST(time_picker_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.time_picker("14:30", (string _t) -> _t)
    )"));
}

LUMA_TEST(color_picker_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.color_picker("#ff0000", (string _c) -> _c)
    )"));
}

LUMA_TEST(tabs_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.tabs(["Tab1", "Tab2"], 0, (integer _i) -> _i, [GraphicalUi.label("C1"), GraphicalUi.label("C2")])
    )"));
}

// ═══════════════════════════════════════════════════════════
// New commands
// ═══════════════════════════════════════════════════════════

LUMA_TEST(read_clipboard_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.read_clipboard((string _t) -> 0)
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "read_clipboard");
}

LUMA_TEST(get_local_storage_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.get_local_storage("key", (string _v) -> 0)
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "get_local_storage");
}

LUMA_TEST(get_local_storage_key) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.get_local_storage("mykey", (string _v) -> 0)
        Dictionary.get_or(cmd, "key", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "mykey");
}

LUMA_TEST(set_local_storage_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.set_local_storage("key", "value")
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "set_local_storage");
}

LUMA_TEST(set_local_storage_key) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.set_local_storage("mykey", "myvalue")
        Dictionary.get_or(cmd, "key", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "mykey");
}

LUMA_TEST(set_local_storage_value) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.set_local_storage("mykey", "myvalue")
        Dictionary.get_or(cmd, "value", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "myvalue");
}

LUMA_TEST(download_file_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.download_file("https://example.com/file.txt", "file.txt")
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "download_file");
}

LUMA_TEST(download_file_url) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.download_file("https://example.com/file.txt", "file.txt")
        Dictionary.get_or(cmd, "url", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "https://example.com/file.txt");
}

LUMA_TEST(download_file_filename) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.download_file("https://example.com/file.txt", "file.txt")
        Dictionary.get_or(cmd, "filename", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "file.txt");
}

LUMA_TEST(notify_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.notify("Hello")
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "notify");
}

LUMA_TEST(notify_title) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.notify("Hello")
        Dictionary.get_or(cmd, "title", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Hello");
}

LUMA_TEST(notify_with_body) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.notify("Title", "Body text")
        Dictionary.get_or(cmd, "body", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Body text");
}

LUMA_TEST(notify_with_icon) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.notify("Title", "Body text", "/icon.png")
        Dictionary.get_or(cmd, "icon", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "/icon.png");
}

// ═══════════════════════════════════════════════════════════
// New subscriptions
// ═══════════════════════════════════════════════════════════

LUMA_TEST(on_visibility_change_subscription) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_visibility_change("vis", (boolean _v) -> 0)
        Dictionary.get_or(sub, "_sub_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "visibility");
}

LUMA_TEST(on_visibility_change_id) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_visibility_change("vis", (boolean _v) -> 0)
        Dictionary.get_or(sub, "id", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "vis");
}

LUMA_TEST(on_online_subscription) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_online("net", (boolean _v) -> 0)
        Dictionary.get_or(sub, "_sub_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "online");
}

LUMA_TEST(on_online_id) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_online("net", (boolean _v) -> 0)
        Dictionary.get_or(sub, "id", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "net");
}

LUMA_TEST(on_offline_subscription) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_offline("net", (boolean _v) -> 0)
        Dictionary.get_or(sub, "_sub_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "offline");
}

LUMA_TEST(on_offline_id) {
    const auto v = eval(R"(
        dictionary<string> sub = GraphicalUi.on_offline("net", (boolean _v) -> 0)
        Dictionary.get_or(sub, "id", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "net");
}

LUMA_TEST(on_media_query_subscription) {
    const auto v = eval(R"LUMA(
        dictionary<string> sub = GraphicalUi.on_media_query("dark", "(prefers-color-scheme: dark)", (boolean _m) -> 0)
        Dictionary.get_or(sub, "_sub_type", "")
    )LUMA");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "media_query");
}

LUMA_TEST(on_media_query_id) {
    const auto v = eval(R"LUMA(
        dictionary<string> sub = GraphicalUi.on_media_query("dark", "(prefers-color-scheme: dark)", (boolean _m) -> 0)
        Dictionary.get_or(sub, "id", "")
    )LUMA");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "dark");
}

LUMA_TEST(on_media_query_query) {
    const auto v = eval(R"LUMA(
        dictionary<string> sub = GraphicalUi.on_media_query("dark", "(prefers-color-scheme: dark)", (boolean _m) -> 0)
        Dictionary.get_or(sub, "query", "")
    )LUMA");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "(prefers-color-scheme: dark)");
}

// ═══════════════════════════════════════════════════════════
// Accessibility helpers
// ═══════════════════════════════════════════════════════════

LUMA_TEST(aria_live_widget) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.aria_live("polite", GraphicalUi.label("status"))
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "aria_live");
}

LUMA_TEST(aria_live_level) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.aria_live("polite", GraphicalUi.label("status"))
        Dictionary.get_or(w, "level", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "polite");
}

LUMA_TEST(aria_describedby_widget) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.aria_describedby("desc1", GraphicalUi.label("input"))
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "aria_describedby");
}

LUMA_TEST(aria_describedby_desc_id) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.aria_describedby("desc1", GraphicalUi.label("input"))
        Dictionary.get_or(w, "desc_id", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "desc1");
}

// ═══════════════════════════════════════════════════════════
// Renderer coverage
// ═══════════════════════════════════════════════════════════
//
// The browser-side dispatch in gui-renderer.js is a pure
// WIDGET_RENDERERS[type] table lookup, so a catalog widget type with no
// renderer entry silently renders as "[unknown widget: <type>]" in the live
// webview. The headless --test harness cannot catch this: it inspects the C++
// widget dictionary, not the JS renderer. This test closes that gap by parsing
// both sources and asserting every catalog widget type has a renderer.

static std::string coverage_read_file(const std::string& rel_path) {
    const std::optional<std::string> root = luma::safe_getenv("LUMA_TEST_ROOT");
    const std::string base = (root.has_value() && !root->empty()) ? *root : std::string(".");
    const std::string full = base + "/" + rel_path;
    auto contents = snapshot::read_file(full);
    if (!contents.has_value()) {
        luma::test::throw_assertion_error("renderer coverage: cannot open '" + full +
                                          "' (LUMA_TEST_ROOT must point at the repo root)");
    }
    return *contents;
}

static std::string coverage_lstrip(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    return s.substr(i);
}

// The widget type string literals declared in `namespace wtype` — the
// authoritative set of types the C++ builders emit.
static std::vector<std::string> coverage_widget_types(const std::string& hpp) {
    std::vector<std::string> types;
    const std::size_t begin = hpp.find("namespace wtype {");
    const std::size_t end = hpp.find("} // namespace wtype", begin);
    ASSERT_TRUE(begin != std::string::npos);
    ASSERT_TRUE(end != std::string::npos);
    std::size_t pos = begin;
    while (pos < end) {
        std::size_t eol = hpp.find('\n', pos);
        if (eol == std::string::npos || eol > end) {
            eol = end;
        }
        const std::string line = hpp.substr(pos, eol - pos);
        if (line.find("const char*") != std::string::npos) {
            const std::size_t eq = line.find("= \"");
            if (eq != std::string::npos) {
                const std::size_t vstart = eq + 3;
                const std::size_t vend = line.find('"', vstart);
                if (vend != std::string::npos) {
                    types.push_back(line.substr(vstart, vend - vstart));
                }
            }
        }
        pos = eol + 1;
    }
    return types;
}

// The renderer keys: WIDGET_RENDERERS object properties (`name: (args) => …`)
// plus the chart types registered through the CHART_TYPES loop.
static std::unordered_set<std::string> coverage_renderer_keys(const std::string& js) {
    std::unordered_set<std::string> keys;

    const std::size_t table = js.find("WIDGET_RENDERERS = {");
    std::size_t pos = (table == std::string::npos) ? std::size_t{0} : table;
    while (pos < js.size()) {
        std::size_t eol = js.find('\n', pos);
        if (eol == std::string::npos) {
            eol = js.size();
        }
        const std::string line = coverage_lstrip(js.substr(pos, eol - pos));
        std::size_t i = 0;
        while (i < line.size() && ((line[i] >= 'a' && line[i] <= 'z') || line[i] == '_')) {
            ++i;
        }
        // Renderer entries take one of two forms after the `name: ` key:
        //   name: (w, …) => …                 — an inline arrow renderer
        //   name: createContainerRenderer(…)  — the shared layout-container factory
        // Both register a WIDGET_RENDERERS[name] entry, so accept either.
        if (i > 0 && i + 2 < line.size() && line[i] == ':' && line[i + 1] == ' ') {
            const std::string value = line.substr(i + 2);
            if (value[0] == '(' || value.starts_with("createContainerRenderer(")) {
                keys.insert(line.substr(0, i));
            }
        }
        pos = eol + 1;
    }

    const std::string chart_marker = "CHART_TYPES = [";
    const std::size_t cstart = js.find(chart_marker);
    if (cstart != std::string::npos) {
        const std::size_t cend = js.find(']', cstart);
        std::size_t p = cstart + chart_marker.size();
        while (p < cend) {
            const std::size_t q1 = js.find('"', p);
            if (q1 == std::string::npos || q1 >= cend) {
                break;
            }
            const std::size_t q2 = js.find('"', q1 + 1);
            if (q2 == std::string::npos || q2 >= cend) {
                break;
            }
            keys.insert(js.substr(q1 + 1, q2 - q1 - 1));
            p = q2 + 1;
        }
    }

    return keys;
}

LUMA_TEST(renderer_covers_all_widget_types) {
    const std::string constants =
        coverage_read_file("core/runtime/stdlib/io/graphicalui_constants.hpp");
    const std::string renderer = coverage_read_file("external/gui-framework/gui-renderer.js");

    const std::vector<std::string> widget_types = coverage_widget_types(constants);

    // The WIDGET_RENDERERS table is split across per-category fragment files
    // (renderers/*.js), concatenated into gui-renderer.js at build time by
    // scripts/generate_gui_assets.mjs. Parse the shell (for the CHART_TYPES loop)
    // and every fragment (for the renderer keys), then union the keys.
    std::unordered_set<std::string> renderer_keys = coverage_renderer_keys(renderer);

    for (const char* frag : {"basic", "layout", "advanced", "interaction"}) {
        const std::string frag_js =
            coverage_read_file(std::string("external/gui-framework/renderers/") + frag + ".js");
        const auto frag_keys = coverage_renderer_keys(frag_js);
        renderer_keys.insert(frag_keys.begin(), frag_keys.end());
    }

    // Guard against a parser that silently matched nothing.
    ASSERT_GT(widget_types.size(), static_cast<std::size_t>(50));
    ASSERT_GT(renderer_keys.size(), static_cast<std::size_t>(50));

    std::vector<std::string> missing;
    for (const std::string& type : widget_types) {
        if (renderer_keys.find(type) == renderer_keys.end()) {
            missing.push_back(type);
        }
    }

    if (!missing.empty()) {
        std::ostringstream oss;
        oss << "GraphicalUi widget type(s) with no JS renderer (would render as "
               "\"[unknown widget]\"): ";
        for (std::size_t i = 0; i < missing.size(); ++i) {
            oss << (i == 0 ? "" : ", ") << missing[i];
        }
        oss << ". Add a renderer entry in the matching external/gui-framework/renderers/"
               "*.js fragment, then regenerate assets with: node scripts/generate_gui_assets.mjs";
        luma::test::throw_assertion_error(oss.str());
    }
}

// ═══════════════════════════════════════════════════════════
// Coverage additions — commands, drag source, and interactive
// widgets that previously lacked a dedicated unit test.
// ═══════════════════════════════════════════════════════════

// ─── Commands (constructible outside an app; callbacks deferred) ───

LUMA_TEST(delay_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.delay(500, (any _r) -> 0)
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "delay");
}

LUMA_TEST(delay_milliseconds) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.delay(500, (any _r) -> 0)
        Dictionary.get_or(cmd, "milliseconds", 0)
    )");
    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 500);
}

LUMA_TEST(random_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.random(1, 10, (number _n) -> 0)
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "random");
}

LUMA_TEST(random_max) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.random(1, 10, (number _n) -> 0)
        Dictionary.get_or(cmd, "max", 0)
    )");
    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 10);
}

LUMA_TEST(debounce_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.debounce("search", 300, (any _r) -> 0)
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "debounce");
}

LUMA_TEST(debounce_id) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.debounce("search", 300, (any _r) -> 0)
        Dictionary.get_or(cmd, "debounce_id", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "search");
}

LUMA_TEST(http_post_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.http_post("https://example.com", "payload", (any _r) -> 0)
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "http_post");
}

LUMA_TEST(http_post_body) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.http_post("https://example.com", "payload", (any _r) -> 0)
        Dictionary.get_or(cmd, "body", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "payload");
}

// ─── Drag source (no callback, constructible) ───

LUMA_TEST(draggable_widget) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.draggable(GraphicalUi.label("Item"), "payload")
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "draggable");
}

LUMA_TEST(draggable_data) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.draggable(GraphicalUi.label("Item"), "payload")
        Dictionary.get_or(w, "data", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "payload");
}

// ─── Interactive widgets that require an active app context ───
// These bind a deferred callback (or, for responsive, query the live
// window size), so they must be constructed inside a view function.

LUMA_TEST(slider_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.slider(0.5, 0.0, 1.0, (number _v) -> {})
    )"));
}

LUMA_TEST(switch_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.switch("On", true, (boolean _v) -> {})
    )"));
}

LUMA_TEST(text_area_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.text_area("body", (string _v) -> {})
    )"));
}

LUMA_TEST(number_input_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.number_input(5, 0, 10, (number _v) -> {})
    )"));
}

LUMA_TEST(form_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.form([GraphicalUi.label("F")], () -> {})
    )"));
}

LUMA_TEST(drop_target_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.drop_target(GraphicalUi.label("Drop"), (string _v) -> {})
    )"));
}

LUMA_TEST(paginator_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.paginator(1, 5, (integer _p) -> {})
    )"));
}

LUMA_TEST(infinite_scroll_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.infinite_scroll(["a", "b"], 40, () -> {})
    )"));
}

LUMA_TEST(wizard_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.wizard([GraphicalUi.label("Step 1")], 0, (integer _s) -> {})
    )"));
}

LUMA_TEST(responsive_requires_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.responsive({"mobile": "stack"})
    )"));
}

// ═══════════════════════════════════════════════════════════
// Entry point
// ═══════════════════════════════════════════════════════════

int main() {
    LUMA_RUN_ALL();
}
