// GraphicalUi module C++ unit tests: app lifecycle, commands, subscriptions, and renderer coverage.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "analysis/errors/error.hpp"
#include "common/platform_utils.hpp"
#include "runtime/stdlib/io/graphicalui_css.hpp"
#include "stdlib_test_helpers.hpp"

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
