#ifndef LUMA_STDLIB_GRAPHICALUI_HELPERS_HPP
#define LUMA_STDLIB_GRAPHICALUI_HELPERS_HPP

// Inline helper functions for the GraphicalUi module.
// Provides dictionary helpers, config extraction, and utility functions
// used across the graphicalui_*.cpp translation units.

#include "runtime/stdlib/io/graphicalui_types.hpp"

#ifdef LUMA_HAS_WEBVIEW

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "analysis/errors/error.hpp"
#include "common/string_utils.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/io/graphicalui_constants.hpp"

namespace luma::gui_detail {

// ═══════════════════════════════════════════════════════════
// Forward declarations — functions implemented in .cpp files
// ═══════════════════════════════════════════════════════════

// graphicalui_serialization.cpp
// Headless execution mode (set via the LUMA_GUI_HEADLESS environment variable).
// When enabled, GraphicalUi.app runs the application lifecycle (init, view,
// subscribe, optional scripted updates) without creating a webview window, so
// GUI examples can be executed and tested unattended.
[[nodiscard]] bool gui_headless_enabled();
// Scripted update messages for headless mode, parsed from the comma-separated
// LUMA_GUI_MESSAGES environment variable. Each entry is delivered to the
// application's update(model, msg) function in order.
[[nodiscard]] std::vector<std::string> gui_headless_messages();
void bind_deferred_callback(DictionaryValue& widget, std::string_view deferred_key,
                            std::string_view id_key, std::string_view widget_name);
void apply_widget_events(DictionaryValue& widget);
[[nodiscard]] Value finalize_widget(std::shared_ptr<DictionaryValue> w);
[[nodiscard]] std::string build_gui_framework_css(const std::filesystem::path& cached_dir = {});
[[nodiscard]] std::string build_gui_framework_js(const std::filesystem::path& cached_dir = {});
[[nodiscard]] std::filesystem::path dev_asset_dir();
[[nodiscard]] std::string value_to_json(const Value& v, int depth = 0);

// graphicalui_commands.cpp
void execute_command(AppState& state, const Value& cmd);
void process_callback_result(AppState& state, Value result);
// Apply the result of an event callback (click, input, key press, tick, command
// result). A string return is treated as an Elm-style message and routed through
// update(model, msg) when an update function is defined; structured returns (the
// new model) and (model, command) pairs are applied directly.
void apply_event_result(AppState& state, Value result);
void drain_command_queue(AppState& state);
[[nodiscard]] bool is_command_pair(const Value& v);

// graphicalui_events.cpp
void render_view(AppState& state);
void manage_subscriptions(AppState& state);
void check_dev_asset_reload(AppState& state);
void render_error(AppState& state, const std::string& error_message);
void on_gui_event(const char* id, const char* req, void* arg);

// Convert a mouse-event payload dictionary (x, y, button, ctrl, shift, alt) into
// a typed GraphicalUi.MouseEvent record — the payload GraphicalUi.on_mouse_typed
// delivers to its callback.  Exposed for unit testing.
[[nodiscard]] Value build_mouse_event_record(const DictionaryValue& payload);

// Convert a scroll-event payload dictionary (x, y) into a typed
// GraphicalUi.ScrollPosition record — the payload GraphicalUi.on_scroll_typed
// delivers to its callback.  Exposed for unit testing.
[[nodiscard]] Value build_scroll_position_record(const DictionaryValue& payload);

// Convert a keyboard-event key string plus its modifier payload dictionary
// (ctrl, shift, alt, meta) into a typed GraphicalUi.KeyEvent record — the
// payload GraphicalUi.on_key_typed delivers to its callback.  `mods` may be
// null (e.g. from the headless test path), in which case every modifier
// defaults to false.  Exposed for unit testing.
[[nodiscard]] Value build_key_event_record(const std::string& key, const DictionaryValue* mods);

// Convert a window-resize width/height pair into a typed GraphicalUi.WindowSize
// record — the payload GraphicalUi.on_resize_typed delivers to its callback.
// Exposed for unit testing.
[[nodiscard]] Value build_window_size_record(std::int64_t width, std::int64_t height);

// Convert a drag-event payload dictionary (x, y, data, event) into a typed
// GraphicalUi.DragEvent record — the payload GraphicalUi.on_drag_typed delivers
// to its callback.  A missing coordinate defaults to 0, missing data to "", and
// an unrecognised/missing phase to Start.  Exposed for unit testing.
[[nodiscard]] Value build_drag_event_record(const DictionaryValue& payload);

// Convert a drop-event payload dictionary (data, x, y) into a typed
// GraphicalUi.DropEvent record — the payload GraphicalUi.drop_target_typed
// delivers to its on_drop callback.  Missing data defaults to "" and a missing
// coordinate to 0.  Exposed for unit testing.
[[nodiscard]] Value build_drop_event_record(const DictionaryValue& payload);

// Build a GraphicalUi.HttpResponse record {status, headers, body} — the payload
// GraphicalUi.http_get_full / http_post_full deliver inside a result<...>.
// Exposed for unit testing.
[[nodiscard]] Value
build_http_response_record_gui(int status, std::string body,
                               const std::vector<std::pair<std::string, std::string>>& headers);

// graphicalui_widgets*.cpp
void register_graphicalui_widgets(const EnvPtr& env);
void register_basic_widgets(const EnvPtr& env);
void register_layout_widgets(const EnvPtr& env);
void register_chart_widgets(const EnvPtr& env);
void register_commands_and_subscriptions(const EnvPtr& env);
void register_advanced_widgets(const EnvPtr& env);
void register_or_defer_command_callback(std::shared_ptr<DictionaryValue>& w, const Value& cb_arg);

// graphicalui_testing.cpp
// Headless interaction-testing API (GraphicalUi.test_*) — drives an app without
// a window so GUI examples can be tested by simulating user input.
void register_graphicalui_testing(const EnvPtr& env);

// ═══════════════════════════════════════════════════════════
// Dictionary helpers
// ═══════════════════════════════════════════════════════════

[[nodiscard]] inline std::shared_ptr<DictionaryValue> make_dict() {
    return std::make_shared<DictionaryValue>();
}

[[nodiscard]] inline std::shared_ptr<DictionaryValue> make_widget(std::string_view type) {
    auto d = make_dict();
    d->set("type", Value{std::string{type}});
    return d;
}

// Create a command widget dictionary with the given command type.
[[nodiscard]] inline std::shared_ptr<DictionaryValue>
make_command_dict(std::string_view command_type) {
    auto w = make_dict();
    w->set(key::command_type, Value{std::string{command_type}});
    return w;
}

// Require an active app context. Throws a RuntimeError if no app is running.
// Centralises the null check for interactive widgets that need an active app.
[[nodiscard]] inline AppState& require_active_app(std::string_view widget_name) {
    if (!active_app) {
        throw RuntimeError{
            error_msg("GraphicalUi", widget_name,
                      "interactive widgets must be created inside a view function passed to "
                      "GraphicalUi.app()"),
            SourceLocation{},
            "move this widget into a view function, e.g. "
            "function widget my_view(model) {{ ... }}"};
    }

    return *active_app;
}

[[nodiscard]] inline std::string dict_string(const DictionaryValue& dict, std::string_view key,
                                             std::string_view fallback = "") {
    const std::string key_str{key};
    const auto* v = dict.find(key_str);

    if (v != nullptr && v->is_string()) {
        return v->as_string();
    }

    return std::string{fallback};
}

[[nodiscard]] inline std::int64_t dict_int(const DictionaryValue& dict, std::string_view key,
                                           std::int64_t fallback = 0) {
    const std::string key_str{key};
    const auto* v = dict.find(key_str);

    if (v != nullptr && v->is_integer()) {
        return v->as_integer();
    }

    return fallback;
}

[[nodiscard]] inline bool dict_bool(const DictionaryValue& dict, std::string_view key,
                                    bool fallback = false) {
    const std::string key_str{key};
    const auto* v = dict.find(key_str);

    if (v != nullptr && v->is_bool()) {
        return v->as_bool();
    }

    return fallback;
}

[[nodiscard]] inline Value get_style_arg(std::span<const Value> args, std::size_t index) {
    if (index < args.size() && args[index].is_dictionary()) {
        return args[index];
    }

    return Value{make_dict()};
}

// Map a GraphicalUi.Severity choice value to its lowercase string key
// ("info" / "warning" / "error" / "success") — the representation the alert /
// toast widgets and the GraphicalUi.INFO/WARNING/ERROR/SUCCESS constants use.
// Anything that is not a recognised Severity variant defaults to "info", keeping
// the bridge total.
[[nodiscard]] inline std::string severity_to_lower(const Value& v) {
    if (v.is_choice()) {
        const auto& variant = v.as_choice()->variant;

        if (variant == "Warning") {
            return "warning";
        }

        if (variant == "Error") {
            return "error";
        }

        if (variant == "Success") {
            return "success";
        }
    }

    return "info";
}

// Map a GraphicalUi.ButtonVariant choice value to its lowercase style key
// ("primary" / "secondary" / "ghost" / "danger") — the value the button widget's
// `variant` key carries and the GraphicalUi.PRIMARY/SECONDARY/GHOST/DANGER
// constants hold.  Anything that is not a recognised variant defaults to
// "primary", keeping the bridge total.
[[nodiscard]] inline std::string button_variant_to_lower(const Value& v) {
    if (v.is_choice()) {
        const auto& variant = v.as_choice()->variant;

        if (variant == "Secondary") {
            return "secondary";
        }

        if (variant == "Ghost") {
            return "ghost";
        }

        if (variant == "Danger") {
            return "danger";
        }
    }

    return "primary";
}

// Map a GraphicalUi.MouseEventType choice value to its lowercase event-type
// string ("click" / "move" / "down" / "up" / "scroll") — the value the on_mouse
// subscription's `event` key carries and the JS evtMap keys on.  Anything that is
// not a recognised variant defaults to "move" (on_mouse's own default), keeping
// the bridge total.
[[nodiscard]] inline std::string mouse_event_type_to_lower(const Value& v) {
    if (v.is_choice()) {
        const auto& variant = v.as_choice()->variant;

        if (variant == "Click") {
            return "click";
        }

        if (variant == "Down") {
            return "down";
        }

        if (variant == "Up") {
            return "up";
        }

        if (variant == "Scroll") {
            return "scroll";
        }
    }

    return "move";
}

// Map a drag-event phase string (the value on_drag's `event` key carries — e.g.
// "start" / "move" / "end" / "enter" / "leave" / "drop") to its
// GraphicalUi.DragPhase choice variant name.  An unrecognised/missing phase
// defaults to "Start", keeping build_drag_event_record total.
[[nodiscard]] inline std::string drag_phase_from_string(std::string_view phase) {
    if (phase == "move") {
        return "Move";
    }

    if (phase == "end") {
        return "End";
    }

    if (phase == "enter") {
        return "Enter";
    }

    if (phase == "leave") {
        return "Leave";
    }

    if (phase == "drop") {
        return "Drop";
    }

    return "Start";
}

// Map a GraphicalUi.ThemeMode choice value to its lowercase string key
// ("light" / "dark" / "auto") — the representation the GraphicalUi.set_theme_mode
// command accepts.  Anything that is not a recognised variant defaults to "auto"
// (the neutral system-follows default), keeping the bridge total.
[[nodiscard]] inline std::string theme_mode_to_lower(const Value& v) {
    if (v.is_choice()) {
        const auto& variant = v.as_choice()->variant;

        if (variant == "Light") {
            return "light";
        }

        if (variant == "Dark") {
            return "dark";
        }
    }

    return "auto";
}

// Map a GraphicalUi.ScrollBehavior choice value to its lowercase behaviour key
// ("smooth" / "instant" / "auto") — the value GraphicalUi.scroll_to's `behavior`
// argument carries and the web scrollIntoView({behavior}) accepts.  Anything that
// is not a recognised variant defaults to "auto", keeping the bridge total.
[[nodiscard]] inline std::string scroll_behavior_to_lower(const Value& v) {
    if (v.is_choice()) {
        const auto& variant = v.as_choice()->variant;

        if (variant == "Smooth") {
            return "smooth";
        }

        if (variant == "Instant") {
            return "instant";
        }
    }

    return "auto";
}

// Map a GraphicalUi.SortDirection choice value to its lowercase direction key
// ("asc" / "desc") — the value the GraphicalUi.table `sort_direction` option
// carries and the JS renderer keys on.  Anything that is not a recognised variant
// defaults to "asc", keeping the bridge total.
[[nodiscard]] inline std::string sort_direction_to_lower(const Value& v) {
    if (v.is_choice() && v.as_choice()->variant == "Descending") {
        return "desc";
    }

    return "asc";
}

// Partition a trailing style/options dictionary into recognised widget-option
// keys and the remaining CSS style.  For each entry of `source` whose key is in
// `option_keys`, the value is stored as a top-level key on the widget `w`
// (matching the convention used by severity, center_label, x_label, etc.);
// every other entry is copied into a fresh style dictionary that is returned.
// The caller's dictionary is never mutated, so user-supplied style dictionaries
// stay intact across renders.  Callable-valued options are stored verbatim so
// the caller can re-bind them as deferred callbacks before finalisation.
[[nodiscard]] inline std::shared_ptr<DictionaryValue>
split_widget_options(DictionaryValue& w, const Value& source,
                     std::initializer_list<std::string_view> option_keys) {
    auto style = make_dict();
    // Build the (empty) hash index up front so each style->set() below takes the
    // O(1) hashed path instead of a linear scan when splitting a large source
    // dictionary.
    style->rebuild_index();

    if (!source.is_dictionary()) {
        return style;
    }

    for (const auto& [k, v] : source.as_dictionary()->entries) {
        const bool is_option = std::any_of(option_keys.begin(), option_keys.end(),
                                           [&k](std::string_view name) { return k == name; });

        if (is_option) {
            w.set(k, v);
        } else {
            style->set(k, v);
        }
    }

    return style;
}

// ═══════════════════════════════════════════════════════════
// Table-driven simple widget registration helper
// ═══════════════════════════════════════════════════════════

// Property descriptor for register_simple_widget.
struct WidgetProp {
    const char* name;
    bool is_callback{false};
};

// Register a widget whose lambda follows a uniform pattern:
//   1. expect_min_args(name, args, required_count, loc)
//   2. make_widget(type)
//   3. For each property, set args[i] (callbacks via key::deferred_callback)
//   4. set style from the first trailing dictionary arg
//   5. finalize_widget
inline void register_simple_widget(const EnvPtr& env, const char* name, const char* type,
                                   std::initializer_list<WidgetProp> properties) {
    auto props = std::vector<WidgetProp>(properties);
    auto required = props.size();

    define_native(env, name,
                  [name, type, props = std::move(props), required](std::span<const Value> args,
                                                                   SourceLocation loc) -> Value {
                      expect_min_args(name, args, required, loc);

                      auto w = make_widget(type);

                      for (std::size_t i = 0; i < props.size(); ++i) {
                          if (props[i].is_callback) {
                              w->set(key::deferred_callback, args[i]);
                          } else {
                              w->set(props[i].name, args[i]);
                          }
                      }

                      w->set("style", get_style_arg(args, props.size()));
                      return finalize_widget(std::move(w));
                  });
}

// Unwrap a Value that is either an array or a result<array> (as returned by
// Array.map / Array.filter).  Returns the inner array Value on success, or a
// null Value if unwrapping fails.
[[nodiscard]] inline Value unwrap_array_arg(const Value& v) {
    if (v.is_array()) {
        return v;
    }

    if (v.is_result()) {
        const auto& res = v.as_result();

        if (res->is_success && res->owned_inner && res->owned_inner->is_array()) {
            return *res->owned_inner;
        }
    }

    return Value{NullValue{}};
}

// Read a file into a string; returns empty string on failure.
[[nodiscard]] std::string read_file_to_string(const std::filesystem::path& path);

// ═══════════════════════════════════════════════════════════
// App configuration helpers
// ═══════════════════════════════════════════════════════════

// Extract AppConfig from the user-supplied dictionary.
[[nodiscard]] AppConfig extract_app_config(const DictionaryValue& config, SourceLocation loc);

// Initialise AppState fields from a parsed AppConfig.
void init_app_state(AppState& state, AppConfig& cfg, webview_t w, SourceLocation loc);

// Update the model and trigger a re-render (unless render is suppressed).
void update_model_and_render(AppState& state, Value new_model);

// ═══════════════════════════════════════════════════════════
// One-line model persistence (the "persist" config key)
// ═══════════════════════════════════════════════════════════

// Load a previously persisted model from `path`. Returns the parsed value, or
// std::nullopt when the file is missing, empty, or unparseable (a warning is
// logged in the last case). JSON objects restore as dictionaries.
[[nodiscard]] std::optional<Value> load_persisted_model(const std::filesystem::path& path);

// Serialize `model` to `path` as pretty-printed JSON. Failures are logged and
// swallowed — persistence is best-effort and never aborts app shutdown.
void save_persisted_model(const std::filesystem::path& path, const Value& model);

// ═══════════════════════════════════════════════════════════
// Theme accessibility validation (dev-only)
// ═══════════════════════════════════════════════════════════

// Warn (to stderr) when theme foreground/accent colours fail the WCAG AA
// contrast minimum (4.5:1) against the theme background. Only hex colours are
// checked; unparseable colours are skipped silently. Intended to be called
// only when devtools are enabled.
void validate_theme_contrast(const Value& theme_value);

// Split a URL path into segments by '/'.
[[nodiscard]] inline std::vector<std::string> split_path(const std::string& s) {
    return luma::split_string(s, '/');
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW

#endif // LUMA_STDLIB_GRAPHICALUI_HELPERS_HPP
