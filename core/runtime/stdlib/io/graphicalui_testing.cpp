#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/native_function.hpp"

// ═══════════════════════════════════════════════════════════
// Headless interaction-testing API (GraphicalUi.test_*)
// ═══════════════════════════════════════════════════════════
//
// These functions let Luma code drive a GraphicalUi application without
// opening a window, so GUI examples can be tested by simulating real user
// input and asserting on the resulting state.  Each call is stateless: it
// builds a transient, windowless AppState from the same config dictionary that
// GraphicalUi.app() consumes, renders the view (which registers the interactive
// widgets' callbacks exactly as the live app does), fires the requested
// interaction through the real callback/update cycle, and returns the new
// model.  Because no webview exists, command-producing callbacks are safely
// no-ops (execute_command guards on a null webview).

namespace luma::gui_detail {

namespace {

// Widget dictionary fields that identify a widget for locating it in a rendered
// tree.  A locator string is matched against each of these, in order.  Style
// `id` is surfaced as `_element_id`, giving every widget a stable, unique handle
// that disambiguates otherwise-identical labels.  `selected` is the current
// choice of a radio_group (the equivalent of `value` for a dropdown), so a radio
// group can be located by its current selection.
constexpr const char* k_locator_keys[] = {
    "label", "text", "placeholder", "value", "selected", "name", "title", "_element_id",
};

// Candidate callback-id fields per interaction.  A widget's primary action lives
// in `_callback_id`; secondary pointer events are registered by
// apply_widget_events as `_on_<event>_id`; the dialog dismiss and search-input
// clear handlers are bound (by apply_widget_events) as `_close_id` / `_clear_id`.
// Listing several candidates lets one resolver serve clicks, inputs, pointer
// events, and the dismiss/clear handlers alike (first non-empty wins, in order).
constexpr const char* k_click_fields[] = {"_callback_id", "_on_click_id"};
constexpr const char* k_primary_fields[] = {"_callback_id"};
constexpr const char* k_double_click_fields[] = {"_on_double_click_id"};
constexpr const char* k_right_click_fields[] = {"_on_right_click_id"};
constexpr const char* k_mouse_enter_fields[] = {"_on_mouse_enter_id"};
constexpr const char* k_mouse_leave_fields[] = {"_on_mouse_leave_id"};
constexpr const char* k_mouse_move_fields[] = {"_on_mouse_move_id"};
constexpr const char* k_close_fields[] = {"_close_id"};
constexpr const char* k_clear_fields[] = {"_clear_id"};
constexpr const char* k_action_fields[] = {"_action_id"};

// text_input / text_area commit their value (on blur or Enter) through a
// dedicated `_commit_id`, distinct from the per-keystroke on_change
// (`_callback_id`).  The "commit" event fires it.
constexpr const char* k_commit_fields[] = {"_commit_id"};

// The combobox binds its commit handler (on_select) as `_select_id`; the menu
// commits through its primary `_callback_id`.  Listing both lets one "select"
// event drive either widget (first non-empty wins, in order).
constexpr const char* k_select_fields[] = {"_select_id", "_callback_id"};

// True when `dict` is a widget node (carries a string "type").
[[nodiscard]] bool is_widget(const DictionaryValue& dict) {
    const auto* type = dict.find("type");
    return type != nullptr && type->is_string();
}

// True when any locator field of `dict` equals `locator`.
[[nodiscard]] bool identity_matches(const DictionaryValue& dict, const std::string& locator) {
    for (const auto* match_key : k_locator_keys) {
        const auto* field = dict.find(match_key);

        if (field != nullptr && field->is_string() && field->as_string() == locator) {
            return true;
        }
    }

    return false;
}

// Depth-first (pre-order) collection of every widget node whose identity equals
// `locator`.  Descent is structure-agnostic: it recurses through every nested
// dictionary and array, so container key names (children / content / ...) and
// wrappers (accessible, tooltip, ...) are handled uniformly.  The returned
// values share the underlying widget dictionaries (cheap shared-pointer copies),
// and document order is preserved so callers can address duplicates by index.
void collect_matching_widgets(const Value& node, const std::string& locator,
                              std::vector<Value>& out) {
    if (!node.is_dictionary()) {
        return;
    }

    const auto& dict = *node.as_dictionary();

    if (is_widget(dict) && identity_matches(dict, locator)) {
        out.push_back(node);
    }

    for (const auto& [entry_key, entry_value] : dict.entries) {
        if (entry_value.is_dictionary()) {
            collect_matching_widgets(entry_value, locator, out);
        } else if (entry_value.is_array()) {
            for (const auto& element : *entry_value.as_array()->elements) {
                collect_matching_widgets(element, locator, out);
            }
        }
    }
}

// Convenience wrapper returning the widgets matching `locator` as a vector.
[[nodiscard]] std::vector<Value> matching_widgets(const Value& tree, const std::string& locator) {
    std::vector<Value> out;
    collect_matching_widgets(tree, locator, out);
    return out;
}

// From `widgets`, collect the callback id of each widget carrying a non-empty
// string in one of `fields` (first matching field wins).  Preserves document
// order, so callers can address duplicates by index.
[[nodiscard]] std::vector<std::string> callback_ids_for(const std::vector<Value>& widgets,
                                                        std::span<const char* const> fields) {
    std::vector<std::string> ids;

    for (const auto& widget : widgets) {
        const auto& dict = *widget.as_dictionary();

        for (const auto* field : fields) {
            auto id = dict_string(dict, field);

            if (!id.empty()) {
                ids.push_back(std::move(id));
                break;
            }
        }
    }

    return ids;
}

// A transient, windowless application built from a GraphicalUi.app config dict.
// Installs itself as the thread-local active app so interactive-widget
// construction (which registers callbacks) succeeds during rendering.
//
// Member order matters: `scope` is declared after `state` so that it is
// destroyed first, clearing the active_app pointer before `state` is torn down.
struct HeadlessApp {
    AppState state;
    ActiveAppScope scope;

    HeadlessApp(const DictionaryValue& config, SourceLocation loc) {
        auto cfg = extract_app_config(config, loc);
        init_app_state(state, cfg, nullptr, loc);
        scope.set(&state);
    }
};

// Render the view for `model`, returning the resulting widget tree.  Side
// effect: registers the model's interactive-widget callbacks on `state`.
[[nodiscard]] Value render_tree(AppState& state, const Value& model, SourceLocation loc) {
    state.model = model;

    if (state.view_fn.is_null() || !state.view_fn.is_callable()) {
        throw RuntimeError{
            error_msg("GraphicalUi", "test", "config has no 'view' function to render"), loc,
            "add a \"view\" entry to the config dictionary"};
    }

    std::vector<Value> view_args{model};
    return invoke_callable(state.view_fn, view_args, loc);
}

// Map an event name to the widget id fields it can fire.  Returns nullopt for an
// unknown event so callers can report the supported set.
[[nodiscard]] std::optional<std::span<const char* const>>
event_id_fields(const std::string& event) {
    if (event == "click") {
        return std::span<const char* const>{k_click_fields};
    }
    if (event == "change" || event == "input") {
        return std::span<const char* const>{k_primary_fields};
    }
    if (event == "double_click") {
        return std::span<const char* const>{k_double_click_fields};
    }
    if (event == "right_click") {
        return std::span<const char* const>{k_right_click_fields};
    }
    if (event == "mouse_enter") {
        return std::span<const char* const>{k_mouse_enter_fields};
    }
    if (event == "mouse_leave") {
        return std::span<const char* const>{k_mouse_leave_fields};
    }
    if (event == "mouse_move") {
        return std::span<const char* const>{k_mouse_move_fields};
    }
    if (event == "close") {
        return std::span<const char* const>{k_close_fields};
    }
    if (event == "clear") {
        return std::span<const char* const>{k_clear_fields};
    }
    if (event == "select") {
        return std::span<const char* const>{k_select_fields};
    }
    if (event == "action") {
        return std::span<const char* const>{k_action_fields};
    }
    if (event == "commit") {
        return std::span<const char* const>{k_commit_fields};
    }

    return std::nullopt;
}

// Render `model`, locate the `index`-th widget matching `locator` that exposes a
// callback in one of `fields`, fire it with `call_args`, run the result through
// the standard update cycle, and return the resulting model.
[[nodiscard]] Value fire_event(AppState& state, const Value& model, const std::string& locator,
                               std::span<const char* const> fields, std::vector<Value> call_args,
                               std::size_t index, std::string_view fn_name, std::string_view what,
                               SourceLocation loc) {
    const auto tree = render_tree(state, model, loc);
    const auto widgets = matching_widgets(tree, locator);
    const auto ids = callback_ids_for(widgets, fields);

    if (ids.empty()) {
        throw RuntimeError{
            error_msg("GraphicalUi", fn_name,
                      "no " + std::string{what} + " widget found matching '" + locator + "'"),
            loc, "check the locator matches the widget's label, placeholder, value, or style id"};
    }

    if (index >= ids.size()) {
        throw RuntimeError{
            error_msg("GraphicalUi", fn_name,
                      "index " + std::to_string(index) + " is out of range: only " +
                          std::to_string(ids.size()) + " " + std::string{what} +
                          " widget(s) match '" + locator + "'"),
            loc, "use a smaller index, or GraphicalUi.test_count() to count matches first"};
    }

    auto callback = state.find_callback(ids[index]);
    auto result = invoke_callable(callback, call_args, loc);
    apply_event_result(state, std::move(result));
    return state.model;
}

// Drive the application's keyboard subscriptions for `key_name`: call subscribe
// to obtain the current subscriptions, then fire every keyboard subscription
// whose filter is "*" or equals `key_name`, threading the model through each.
// Mirrors the live dispatch (handle_keyboard -> dispatch_event) without needing
// a window.  Throws when no keyboard subscription matches.
[[nodiscard]] Value drive_key(AppState& state, const Value& model, const std::string& key_name,
                              SourceLocation loc) {
    state.model = model;

    if (state.subscribe_fn.is_null() || !state.subscribe_fn.is_callable()) {
        throw RuntimeError{
            error_msg("GraphicalUi", "test_key",
                      "config has no 'subscribe' function to receive keyboard events"),
            loc, "add a \"subscribe\" entry returning GraphicalUi.on_key(...)"};
    }

    std::vector<Value> sub_args{state.model};
    const auto subs_value = invoke_callable(state.subscribe_fn, sub_args, loc);
    const auto subscriptions = unwrap_array_arg(subs_value);

    bool fired = false;

    if (subscriptions.is_array()) {
        for (const auto& sub : *subscriptions.as_array()->elements) {
            if (!sub.is_dictionary()) {
                continue;
            }

            const auto& sub_dict = *sub.as_dictionary();

            if (dict_string(sub_dict, key::sub_type) != sub::keyboard) {
                continue;
            }

            const auto filter = dict_string(sub_dict, "filter", "*");

            if (filter != "*" && filter != key_name) {
                continue;
            }

            const auto* callback = sub_dict.find(key::callback);

            if (callback == nullptr || !callback->is_callable()) {
                continue;
            }

            // A GraphicalUi.on_key_typed subscription (flagged typed) receives a
            // GraphicalUi.KeyEvent record; plain on_key gets the bare key string.
            // The headless test path supplies no modifiers, so they default to
            // false — mirroring handle_keyboard's live dispatch.
            std::vector<Value> callback_args;

            if (dict_bool(sub_dict, key::typed)) {
                callback_args.push_back(build_key_event_record(key_name, nullptr));
            } else {
                callback_args.push_back(Value{key_name});
            }

            auto result = invoke_callable(*callback, callback_args, loc);
            apply_event_result(state, std::move(result));
            fired = true;
        }
    }

    if (!fired) {
        throw RuntimeError{error_msg("GraphicalUi", "test_key",
                                     "no keyboard subscription matches key '" + key_name + "'"),
                           loc,
                           "ensure subscribe returns GraphicalUi.on_key with filter \"*\" or \"" +
                               key_name + "\""};
    }

    return state.model;
}

// Drive the application's drag subscriptions for a drag `phase` ("start" /
// "move" / "end" / "enter" / "leave" / "drop") carrying `data`: call subscribe,
// then fire every drag subscription whose filter is "*" or equals `phase`,
// threading the model through each.  Mirrors the live dispatch (handle_drag ->
// dispatch_event) without a window: a typed (on_drag_typed) subscription receives
// a GraphicalUi.DragEvent record, a plain on_drag one gets the raw dictionary.
// Throws when no drag subscription matches.
[[nodiscard]] Value drive_drag(AppState& state, const Value& model, const std::string& phase,
                               const std::string& data, SourceLocation loc) {
    state.model = model;

    if (state.subscribe_fn.is_null() || !state.subscribe_fn.is_callable()) {
        throw RuntimeError{
            error_msg("GraphicalUi", "test_drag",
                      "config has no 'subscribe' function to receive drag events"),
            loc,
            "add a \"subscribe\" entry returning GraphicalUi.on_drag(...) or on_drag_typed(...)"};
    }

    std::vector<Value> sub_args{state.model};
    const auto subs_value = invoke_callable(state.subscribe_fn, sub_args, loc);
    const auto subscriptions = unwrap_array_arg(subs_value);

    bool fired = false;

    if (subscriptions.is_array()) {
        for (const auto& sub : *subscriptions.as_array()->elements) {
            if (!sub.is_dictionary()) {
                continue;
            }

            const auto& sub_dict = *sub.as_dictionary();

            if (dict_string(sub_dict, key::sub_type) != sub::drag) {
                continue;
            }

            const auto filter = dict_string(sub_dict, "event", "*");

            if (filter != "*" && filter != phase) {
                continue;
            }

            const auto* callback = sub_dict.find(key::callback);

            if (callback == nullptr || !callback->is_callable()) {
                continue;
            }

            // Reconstruct the payload the browser drag listener emits, then
            // deliver a typed GraphicalUi.DragEvent record for on_drag_typed or
            // the raw dictionary for on_drag — mirroring handle_drag.
            auto payload = make_dict();
            payload->set("event", Value{phase});
            payload->set("x", Value{0.0});
            payload->set("y", Value{0.0});
            payload->set("data", Value{data});

            std::vector<Value> callback_args;

            if (dict_bool(sub_dict, key::typed)) {
                callback_args.push_back(build_drag_event_record(*payload));
            } else {
                callback_args.push_back(Value{std::move(payload)});
            }

            auto result = invoke_callable(*callback, callback_args, loc);
            apply_event_result(state, std::move(result));
            fired = true;
        }
    }

    if (!fired) {
        throw RuntimeError{
            error_msg("GraphicalUi", "test_drag",
                      "no drag subscription matches phase '" + phase + "'"),
            loc,
            "ensure subscribe returns GraphicalUi.on_drag/on_drag_typed with filter \"*\" or \"" +
                phase + "\""};
    }

    return state.model;
}

// Drive the application's storage subscriptions for a localStorage key change:
// call subscribe, then fire every storage subscription whose key filter is empty
// or equals `key`, threading the model through each.  Mirrors the live dispatch
// (handle_storage -> dispatch_event) without a window: a typed
// (on_storage_change_typed) subscription receives a GraphicalUi.StorageEvent
// record; a plain on_storage_change one gets the bare new-value string.  The
// `old_value` / `new_value` optionals are passed as Value (a null Value means
// none — key added or cleared).  Throws when no storage subscription matches.
[[nodiscard]] Value drive_storage(AppState& state, const Value& model, const std::string& key,
                                  const Value& old_value, const Value& new_value,
                                  SourceLocation loc) {
    state.model = model;

    if (state.subscribe_fn.is_null() || !state.subscribe_fn.is_callable()) {
        throw RuntimeError{
            error_msg("GraphicalUi", "test_storage",
                      "config has no 'subscribe' function to receive storage events"),
            loc,
            "add a \"subscribe\" entry returning GraphicalUi.on_storage_change(...) or "
            "on_storage_change_typed(...)"};
    }

    std::vector<Value> sub_args{state.model};
    const auto subs_value = invoke_callable(state.subscribe_fn, sub_args, loc);
    const auto subscriptions = unwrap_array_arg(subs_value);

    bool fired = false;

    if (subscriptions.is_array()) {
        for (const auto& sub : *subscriptions.as_array()->elements) {
            if (!sub.is_dictionary()) {
                continue;
            }

            const auto& sub_dict = *sub.as_dictionary();

            if (dict_string(sub_dict, key::sub_type) != sub::storage) {
                continue;
            }

            const auto filter = dict_string(sub_dict, "key", "");

            if (!filter.empty() && filter != key) {
                continue;
            }

            const auto* callback = sub_dict.find(key::callback);

            if (callback == nullptr || !callback->is_callable()) {
                continue;
            }

            // Reconstruct the payload the browser storage listener emits (omitting
            // an absent old/new value so a typed StorageEvent maps it to none),
            // then deliver a typed record for on_storage_change_typed or the raw
            // new-value string for on_storage_change — mirroring handle_storage.
            auto payload = make_dict();
            payload->set("key", Value{key});

            if (old_value.is_string()) {
                payload->set("oldValue", old_value);
            }

            if (new_value.is_string()) {
                payload->set("newValue", new_value);
            }

            std::vector<Value> callback_args;

            if (dict_bool(sub_dict, key::typed)) {
                callback_args.push_back(build_storage_event_record(*payload));
            } else {
                callback_args.push_back(Value{dict_string(*payload, "newValue", "")});
            }

            auto result = invoke_callable(*callback, callback_args, loc);
            apply_event_result(state, std::move(result));
            fired = true;
        }
    }

    if (!fired) {
        throw RuntimeError{
            error_msg("GraphicalUi", "test_storage",
                      "no storage subscription matches key '" + key + "'"),
            loc,
            "ensure subscribe returns GraphicalUi.on_storage_change/on_storage_change_typed "
            "with an empty key filter or \"" +
                key + "\""};
    }

    return state.model;
}

// Drive the application's wheel subscriptions (on_wheel_typed): call subscribe,
// then fire every wheel subscription with a GraphicalUi.WheelDelta record built
// from `delta_x` / `delta_y`, threading the model through each.  Mirrors the live
// dispatch (handle_wheel -> dispatch_event) without a window.  Throws when no
// wheel subscription matches.
[[nodiscard]] Value drive_wheel(AppState& state, const Value& model, double delta_x, double delta_y,
                                SourceLocation loc) {
    state.model = model;

    if (state.subscribe_fn.is_null() || !state.subscribe_fn.is_callable()) {
        throw RuntimeError{error_msg("GraphicalUi", "test_wheel",
                                     "config has no 'subscribe' function to receive wheel events"),
                           loc,
                           "add a \"subscribe\" entry returning GraphicalUi.on_wheel_typed(...)"};
    }

    std::vector<Value> sub_args{state.model};
    const auto subs_value = invoke_callable(state.subscribe_fn, sub_args, loc);
    const auto subscriptions = unwrap_array_arg(subs_value);

    bool fired = false;

    if (subscriptions.is_array()) {
        for (const auto& sub : *subscriptions.as_array()->elements) {
            if (!sub.is_dictionary()) {
                continue;
            }

            const auto& sub_dict = *sub.as_dictionary();

            if (dict_string(sub_dict, key::sub_type) != sub::wheel) {
                continue;
            }

            const auto* callback = sub_dict.find(key::callback);

            if (callback == nullptr || !callback->is_callable()) {
                continue;
            }

            auto payload = make_dict();
            payload->set("deltaX", Value{delta_x});
            payload->set("deltaY", Value{delta_y});

            std::vector<Value> callback_args{build_wheel_delta_record(*payload)};
            auto result = invoke_callable(*callback, callback_args, loc);
            apply_event_result(state, std::move(result));
            fired = true;
        }
    }

    if (!fired) {
        throw RuntimeError{error_msg("GraphicalUi", "test_wheel", "no wheel subscription found"),
                           loc, "ensure subscribe returns GraphicalUi.on_wheel_typed(...)"};
    }

    return state.model;
}

// Drive the application's visibility subscriptions for a `visible` state: call
// subscribe, then fire every visibility subscription, threading the model through
// each.  Mirrors the live dispatch (handle_visibility_change -> dispatch_event)
// without a window: a typed (on_visibility_change_typed) subscription receives a
// GraphicalUi.VisibilityState choice; a plain on_visibility_change one gets the
// bare boolean.  Throws when no visibility subscription matches.
[[nodiscard]] Value drive_visibility(AppState& state, const Value& model, bool visible,
                                     SourceLocation loc) {
    state.model = model;

    if (state.subscribe_fn.is_null() || !state.subscribe_fn.is_callable()) {
        throw RuntimeError{
            error_msg("GraphicalUi", "test_visibility",
                      "config has no 'subscribe' function to receive visibility events"),
            loc,
            "add a \"subscribe\" entry returning GraphicalUi.on_visibility_change(...) or "
            "on_visibility_change_typed(...)"};
    }

    std::vector<Value> sub_args{state.model};
    const auto subs_value = invoke_callable(state.subscribe_fn, sub_args, loc);
    const auto subscriptions = unwrap_array_arg(subs_value);

    bool fired = false;

    if (subscriptions.is_array()) {
        for (const auto& sub : *subscriptions.as_array()->elements) {
            if (!sub.is_dictionary()) {
                continue;
            }

            const auto& sub_dict = *sub.as_dictionary();

            if (dict_string(sub_dict, key::sub_type) != sub::visibility) {
                continue;
            }

            const auto* callback = sub_dict.find(key::callback);

            if (callback == nullptr || !callback->is_callable()) {
                continue;
            }

            std::vector<Value> callback_args;

            if (dict_bool(sub_dict, key::typed)) {
                auto choice = std::make_shared<ChoiceValue>();
                choice->type_name = "VisibilityState";
                choice->variant = visibility_state_from_visible(visible);
                callback_args.push_back(Value{std::move(choice)});
            } else {
                callback_args.push_back(Value{visible});
            }

            auto result = invoke_callable(*callback, callback_args, loc);
            apply_event_result(state, std::move(result));
            fired = true;
        }
    }

    if (!fired) {
        throw RuntimeError{
            error_msg("GraphicalUi", "test_visibility", "no visibility subscription found"), loc,
            "ensure subscribe returns "
            "GraphicalUi.on_visibility_change/on_visibility_change_typed"};
    }

    return state.model;
}

} // namespace

// Register the GraphicalUi.test_* interaction-testing functions.
void register_graphicalui_testing(const EnvPtr& env) {
    // GraphicalUi.test_init(config) -> any
    // Run the application's init lifecycle (or fall back to the configured
    // model) and return the resulting initial model.
    define_native(env, "GraphicalUi.test_init",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.test_init", args, 1, loc);
                      const auto& config = *expect_dict(args[0], "GraphicalUi.test_init", loc);
                      HeadlessApp app{config, loc};
                      return app.state.model;
                  });

    // GraphicalUi.test_render(config, model) -> dictionary
    // Render the view for `model` and return the widget tree for assertions.
    define_native(env, "GraphicalUi.test_render",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.test_render", args, 2, loc);
                      const auto& config = *expect_dict(args[0], "GraphicalUi.test_render", loc);
                      HeadlessApp app{config, loc};
                      return render_tree(app.state, args[1], loc);
                  });

    // GraphicalUi.test_click(config, model, locator, index?) -> any
    // Simulate a click on the widget identified by `locator` and return the new
    // model.  When several widgets share the locator, `index` (0-based, default
    // 0) selects which one; use GraphicalUi.test_count() to count them.
    define_native(env, "GraphicalUi.test_click",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.test_click", args, 3, loc);
                      const auto& config = *expect_dict(args[0], "GraphicalUi.test_click", loc);
                      const auto& locator = expect_string(args[2], "GraphicalUi.test_click", loc);
                      std::size_t index = 0;
                      if (args.size() >= 4 && args[3].is_integer()) {
                          index = static_cast<std::size_t>(args[3].as_integer());
                      }
                      HeadlessApp app{config, loc};
                      return fire_event(app.state, args[1], locator,
                                        std::span<const char* const>{k_click_fields}, {}, index,
                                        "test_click", "clickable", loc);
                  });

    // GraphicalUi.test_input(config, model, locator, value, index?) -> any
    // Simulate entering `value` into the widget identified by `locator`
    // (text input change, toggle, slider, dropdown select, ...) and return the
    // new model.  The value's type should match the widget callback's parameter
    // (string for text/dropdown, boolean for toggle/checkbox, number for
    // slider).  `index` (0-based, default 0) disambiguates duplicate locators.
    define_native(env, "GraphicalUi.test_input",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.test_input", args, 4, loc);
                      const auto& config = *expect_dict(args[0], "GraphicalUi.test_input", loc);
                      const auto& locator = expect_string(args[2], "GraphicalUi.test_input", loc);
                      std::size_t index = 0;
                      if (args.size() >= 5 && args[4].is_integer()) {
                          index = static_cast<std::size_t>(args[4].as_integer());
                      }
                      HeadlessApp app{config, loc};
                      return fire_event(app.state, args[1], locator,
                                        std::span<const char* const>{k_primary_fields}, {args[3]},
                                        index, "test_input", "input", loc);
                  });

    // GraphicalUi.test_event(config, model, locator, event, args?, index?) -> any
    // Fire a named widget event on the widget identified by `locator` and return
    // the new model.  `event` is one of: click, change, double_click,
    // right_click, mouse_enter, mouse_leave, mouse_move, close (dialog dismiss),
    // clear (search-input clear), select (menu / combobox commit).  `args` is an
    // optional array forwarded to the callback (length and contents are up to the
    // handler, so arbitrary arity is supported).  `index` (0-based, default 0)
    // disambiguates duplicate locators.
    define_native(
        env, "GraphicalUi.test_event",
        [](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_min_args("GraphicalUi.test_event", args, 4, loc);
            const auto& config = *expect_dict(args[0], "GraphicalUi.test_event", loc);
            const auto& locator = expect_string(args[2], "GraphicalUi.test_event", loc);
            const auto& event = expect_string(args[3], "GraphicalUi.test_event", loc);

            const auto fields = event_id_fields(event);
            if (!fields) {
                throw RuntimeError{
                    error_msg("GraphicalUi", "test_event", "unknown event '" + event + "'"), loc,
                    "valid events: click, change, double_click, right_click, "
                    "mouse_enter, mouse_leave, mouse_move, close, clear, select, action"};
            }

            std::vector<Value> call_args;
            if (args.size() >= 5 && args[4].is_array()) {
                const auto& elements = *args[4].as_array()->elements;
                call_args.assign(elements.begin(), elements.end());
            }

            std::size_t index = 0;
            if (args.size() >= 6 && args[5].is_integer()) {
                index = static_cast<std::size_t>(args[5].as_integer());
            }

            HeadlessApp app{config, loc};
            return fire_event(app.state, args[1], locator, *fields, std::move(call_args), index,
                              "test_event", "matching", loc);
        });

    // GraphicalUi.test_count(config, model, locator) -> integer
    // Count the widgets in the rendered view that match `locator` (the same
    // matching test_find uses, so the two agree).  Useful for asserting how many
    // duplicates exist before addressing one by `index` with test_click /
    // test_event / test_find.  When matches mix interactive and non-interactive
    // widgets, give the target a unique style `id` so it can be addressed by
    // identity rather than position.
    define_native(env, "GraphicalUi.test_count",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.test_count", args, 3, loc);
                      const auto& config = *expect_dict(args[0], "GraphicalUi.test_count", loc);
                      const auto& locator = expect_string(args[2], "GraphicalUi.test_count", loc);
                      HeadlessApp app{config, loc};
                      const auto tree = render_tree(app.state, args[1], loc);
                      const auto widgets = matching_widgets(tree, locator);
                      return Value{static_cast<std::int64_t>(widgets.size())};
                  });

    // GraphicalUi.test_find(config, model, locator, index?) -> dictionary
    // Return the rendered widget dictionary identified by `locator` so tests can
    // assert on its serialized state (value, disabled, style, children, ...)
    // without firing an interaction.  `index` (0-based, default 0) selects among
    // duplicate locators.
    define_native(
        env, "GraphicalUi.test_find", [](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_min_args("GraphicalUi.test_find", args, 3, loc);
            const auto& config = *expect_dict(args[0], "GraphicalUi.test_find", loc);
            const auto& locator = expect_string(args[2], "GraphicalUi.test_find", loc);
            std::size_t index = 0;
            if (args.size() >= 4 && args[3].is_integer()) {
                index = static_cast<std::size_t>(args[3].as_integer());
            }
            HeadlessApp app{config, loc};
            const auto tree = render_tree(app.state, args[1], loc);
            const auto widgets = matching_widgets(tree, locator);

            if (widgets.empty()) {
                throw RuntimeError{
                    error_msg("GraphicalUi", "test_find",
                              "no widget found matching '" + locator + "'"),
                    loc,
                    "check the locator matches the widget's label, placeholder, value, "
                    "or style id"};
            }
            if (index >= widgets.size()) {
                throw RuntimeError{
                    error_msg("GraphicalUi", "test_find",
                              "index " + std::to_string(index) + " is out of range: only " +
                                  std::to_string(widgets.size()) + " widget(s) match '" + locator +
                                  "'"),
                    loc, "use a smaller index, or GraphicalUi.test_count() to count"};
            }

            return widgets[index];
        });

    // GraphicalUi.test_key(config, model, key) -> any
    // Deliver a keyboard event to the application's subscriptions (the path
    // driven by GraphicalUi.on_key) and return the new model.  Every keyboard
    // subscription whose filter is "*" or equals `key` fires, exactly as the live
    // runtime dispatches keys.
    define_native(env, "GraphicalUi.test_key",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.test_key", args, 3, loc);
                      const auto& config = *expect_dict(args[0], "GraphicalUi.test_key", loc);
                      const auto& key_name = expect_string(args[2], "GraphicalUi.test_key", loc);
                      HeadlessApp app{config, loc};
                      return drive_key(app.state, args[1], key_name, loc);
                  });

    // GraphicalUi.test_drag(config, model, phase, data?) -> any
    // Deliver a drag event of the given `phase` ("start"/"move"/"end"/"enter"/
    // "leave"/"drop") carrying optional `data` to the application's drag
    // subscriptions (on_drag / on_drag_typed) and return the new model — the
    // headless mirror of the live drag dispatch.
    define_native(env, "GraphicalUi.test_drag",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.test_drag", args, 3, loc);
                      const auto& config = *expect_dict(args[0], "GraphicalUi.test_drag", loc);
                      const auto& phase = expect_string(args[2], "GraphicalUi.test_drag", loc);
                      const std::string data =
                          args.size() > 3 && args[3].is_string() ? args[3].as_string() : "";
                      HeadlessApp app{config, loc};
                      return drive_drag(app.state, args[1], phase, data, loc);
                  });

    // GraphicalUi.test_storage(config, model, key, old_value?, new_value?) -> any
    // Deliver a cross-tab storage change for `key` (with optional old/new values;
    // omit or pass none for an added/cleared key) to the application's storage
    // subscriptions (on_storage_change / on_storage_change_typed) and return the
    // new model — the headless mirror of the live storage dispatch.
    define_native(
        env, "GraphicalUi.test_storage",
        [](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_min_args("GraphicalUi.test_storage", args, 3, loc);
            const auto& config = *expect_dict(args[0], "GraphicalUi.test_storage", loc);
            const auto& storage_key = expect_string(args[2], "GraphicalUi.test_storage", loc);
            const Value old_value = args.size() > 3 ? args[3] : Value{};
            const Value new_value = args.size() > 4 ? args[4] : Value{};
            HeadlessApp app{config, loc};
            return drive_storage(app.state, args[1], storage_key, old_value, new_value, loc);
        });

    // GraphicalUi.test_wheel(config, model, delta_x, delta_y) -> any
    // Deliver a scroll-wheel delta to the application's wheel subscriptions
    // (on_wheel_typed) and return the new model — the headless mirror of the live
    // wheel dispatch.
    define_native(env, "GraphicalUi.test_wheel",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.test_wheel", args, 4, loc);
                      const auto& config = *expect_dict(args[0], "GraphicalUi.test_wheel", loc);
                      const double delta_x = expect_numeric(args[2], "GraphicalUi.test_wheel", loc);
                      const double delta_y = expect_numeric(args[3], "GraphicalUi.test_wheel", loc);
                      HeadlessApp app{config, loc};
                      return drive_wheel(app.state, args[1], delta_x, delta_y, loc);
                  });

    // GraphicalUi.test_visibility(config, model, visible) -> any
    // Deliver a document visibility change (visible=true / hidden=false) to the
    // application's visibility subscriptions (on_visibility_change /
    // on_visibility_change_typed) and return the new model — the headless mirror
    // of the live visibility dispatch.
    define_native(env, "GraphicalUi.test_visibility",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.test_visibility", args, 3, loc);
                      const auto& config =
                          *expect_dict(args[0], "GraphicalUi.test_visibility", loc);
                      const bool visible =
                          expect_boolean(args[2], "GraphicalUi.test_visibility", loc);
                      HeadlessApp app{config, loc};
                      return drive_visibility(app.state, args[1], visible, loc);
                  });

    // GraphicalUi.test_message(config, model, message) -> any
    // Deliver `message` to the application's update(model, message) function
    // (the Elm-style message path used for keyboard/navigation events) and
    // return the new model.
    define_native(env, "GraphicalUi.test_message",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.test_message", args, 3, loc);
                      const auto& config = *expect_dict(args[0], "GraphicalUi.test_message", loc);
                      HeadlessApp app{config, loc};
                      app.state.model = args[1];

                      if (app.state.update_fn.is_null() || !app.state.update_fn.is_callable()) {
                          throw RuntimeError{
                              error_msg("GraphicalUi", "test_message",
                                        "config has no 'update' function to receive messages"),
                              loc, "add an \"update\" entry to the config dictionary"};
                      }

                      std::vector<Value> update_args{args[1], args[2]};
                      auto result = invoke_callable(app.state.update_fn, update_args, loc);
                      process_callback_result(app.state, std::move(result));
                      return app.state.model;
                  });
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW
