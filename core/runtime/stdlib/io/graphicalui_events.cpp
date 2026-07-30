#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

#include <array>
#include <cstdint>
#include <format>
#include <iostream>
#include <variant>

#include "common/resource_limits.hpp"
#include "json/json.hpp"

namespace luma::gui_detail {

// ═══════════════════════════════════════════════════════════
// JSON event parsing — uses shared/json library
// ═══════════════════════════════════════════════════════════

namespace {

// Event payload discriminated by std::variant — replaces manual has_string/has_number/has_bool
// flags for type-safe access via std::get_if.
using EventPayload = std::variant<std::monostate, std::string, double, bool>;

struct EventMessage {
    std::string type;
    std::string id;
    EventPayload payload;

    // Parsed dict payload for mouse_event / widget_event — populated
    // during the single parse pass so we never re-parse the JSON.
    std::shared_ptr<DictionaryValue> dict_payload;

    // Convenience accessors for the payload.
    [[nodiscard]] bool has_string() const {
        return std::holds_alternative<std::string>(payload);
    }

    [[nodiscard]] bool has_number() const {
        return std::holds_alternative<double>(payload);
    }

    [[nodiscard]] bool has_bool() const {
        return std::holds_alternative<bool>(payload);
    }

    [[nodiscard]] const std::string& string_value() const {
        return std::get<std::string>(payload);
    }

    [[nodiscard]] double number_value() const {
        return std::get<double>(payload);
    }

    [[nodiscard]] bool bool_value() const {
        return std::get<bool>(payload);
    }
};

[[nodiscard]] EventMessage parse_event(const luma::json::JsonValue& root) {
    EventMessage msg;

    if (!root.is_object()) {
        return msg;
    }

    if (root.has("type") && root["type"].is_string()) {
        msg.type = root["type"].as_string();
    }

    if (root.has("id") && root["id"].is_string()) {
        msg.id = root["id"].as_string();
    }

    if (root.has("value")) {
        const auto& val = root["value"];

        if (val.is_string()) {
            msg.payload = val.as_string();
        } else if (val.is_bool()) {
            msg.payload = val.as_bool();
        } else if (val.is_number()) {
            msg.payload = val.as_number();
        } else if (val.is_integer()) {
            msg.payload = static_cast<double>(val.as_integer());
        }
    }

    // For dict events (mouse_event, widget_event, scroll_event, keyboard,
    // drag_event), extract the payload fields in the same pass instead of
    // re-parsing later.  The keyboard case carries its key name in `value` (a
    // string) and the modifier flags at the top level, so on_key_typed can build
    // a KeyEvent; the drag case carries `event` (phase), x, y, and `data`.
    if (msg.type == "mouse_event" || msg.type == "widget_event" || msg.type == "scroll_event" ||
        msg.type == "keyboard" || msg.type == "drag_event" || msg.type == "drop_event" ||
        msg.type == "storage_change" || msg.type == "wheel_event") {
        auto dict = make_dict();

        auto set_string = [&](const char* key) {
            if (root.has(key) && root[key].is_string()) {
                dict->set(key, Value{root[key].as_string()});
            }
        };

        auto set_number = [&](const char* key) {
            if (root.has(key)) {
                const auto& v = root[key];

                if (v.is_number()) {
                    dict->set(key, Value{v.as_number()});
                } else if (v.is_integer()) {
                    dict->set(key, Value{static_cast<double>(v.as_integer())});
                }
            }
        };

        auto set_bool = [&](const char* key) {
            if (root.has(key) && root[key].is_bool()) {
                dict->set(key, Value{root[key].as_bool()});
            }
        };

        set_string("event");
        set_string("event_type");
        set_string("data");
        set_number("x");
        set_number("y");
        set_string("button");
        set_bool("ctrl");
        set_bool("shift");
        set_bool("alt");
        set_bool("meta");
        // Storage-change fields (StorageEvent): the changed key plus its old/new
        // values.  oldValue / newValue are omitted by the browser (JSON null) when
        // a key is added or cleared, so set_string leaves them absent → none.
        set_string("key");
        set_string("oldValue");
        set_string("newValue");
        // Wheel-event deltas (WheelDelta).
        set_number("deltaX");
        set_number("deltaY");

        msg.dict_payload = std::move(dict);
    }

    return msg;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// Themeable error rendering
// ═══════════════════════════════════════════════════════════

void render_error(AppState& state, const std::string& error_message) {
    if (state.webview == nullptr) {
        return;
    }

    // Try the user-provided on_error function first.
    if (!state.on_error_fn.is_null() && state.on_error_fn.is_callable()) {
        try {
            std::vector<Value> err_args{Value{error_message}};
            auto error_tree = invoke_callable(state.on_error_fn, err_args, state.loc);
            auto json = value_to_json(error_tree);

            // Route the error view through the same host-side de-dup as a normal
            // render and record it as the current frame, so a subsequent
            // recovered render (a different tree) is correctly re-emitted.
            if (json != state.prev_tree_json) {
                auto js = std::format("__gui_render({})", json);
                webview_eval(state.webview, js.c_str());
                state.prev_tree_json = std::move(json);
            }

            return;
        } catch (const std::exception& e) {
            // on_error itself failed — fall through to default error display.
            std::cerr << "GraphicalUi: on_error handler failed: " << e.what() << "\n";
        } catch (...) {
            // on_error itself failed — fall through to default error display.
            std::cerr << "GraphicalUi: on_error handler failed (unknown error)\n";
        }
    }

    // Fallback: keep the last good frame on screen and surface the error as a
    // dismissible toast overlay instead of wiping gui-root.  A transient error
    // in view()/update() therefore no longer blanks the whole window — the last
    // successfully rendered UI stays visible underneath the toast.
    // __gui_error_toast sets textContent internally, so the message cannot
    // inject markup.
    auto msg = luma::js_string_escape(error_message);
    auto js = std::format("__gui_error_toast('{}')", msg);
    webview_eval(state.webview, js.c_str());

    // Invalidate the de-dup cache so the next successful render is always
    // re-emitted, even when its tree is byte-identical to the last good frame.
    // Only the JS performRender path dismisses this toast, and it runs only when
    // C++ emits __gui_render; if we kept the cache and a recovered frame matched
    // the last good one (e.g. a display-only view whose poll returns identical
    // data), render_view would skip the eval and the toast would stay up over a
    // fully recovered UI until manually dismissed. The toast is an overlay that
    // leaves gui-root intact, so re-emitting the unchanged frame is visually a
    // no-op — it only guarantees the toast-clearing render is not elided.
    state.prev_tree_json.clear();
}

// ═══════════════════════════════════════════════════════════
// Hot reload: poll dev asset files and re-inject on change
// ═══════════════════════════════════════════════════════════

void check_dev_asset_reload(AppState& state) {
    if (!state.dev_mode.enabled || state.webview == nullptr) {
        return;
    }

    try {
        const auto& dev_dir = state.cached_dev_dir;

        if (dev_dir.empty()) {
            return;
        }

        auto css_path = dev_dir / "gui-overrides.css";
        auto js_path = dev_dir / "gui-renderer.js";

        std::error_code ec;
        auto css_mtime = std::filesystem::last_write_time(css_path, ec);
        auto js_mtime = std::filesystem::last_write_time(js_path, ec);

        const bool reload_css = (css_mtime != state.dev_mode.css_mtime &&
                                 state.dev_mode.css_mtime != std::filesystem::file_time_type{});
        const bool reload_js = (js_mtime != state.dev_mode.js_mtime &&
                                state.dev_mode.js_mtime != std::filesystem::file_time_type{});

        state.dev_mode.css_mtime = css_mtime;
        state.dev_mode.js_mtime = js_mtime;

        if (reload_css) {
            auto css = build_gui_framework_css(state.cached_dev_dir);
            auto escaped = luma::json_escape(css);
            auto inject =
                std::format("document.getElementById('gui-style').textContent = \"{}\";", escaped);
            webview_eval(state.webview, inject.c_str());
        }

        if (reload_js) {
            auto js = build_gui_framework_js(state.cached_dev_dir);
            webview_eval(state.webview, js.c_str());
        }

        if (reload_css || reload_js) {
            // Force a re-render after asset reload.  Reloading the framework JS
            // re-runs the renderer and resets its front-end state, so invalidate
            // the host-side de-dup cache to guarantee render_view re-emits the
            // tree instead of skipping it as an unchanged frame.
            state.prev_tree_json.clear();
            render_view(state);
        }
    } catch (const std::exception& e) {
        std::cerr << "GraphicalUi: dev asset reload failed: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "GraphicalUi: dev asset reload failed (unknown error)\n";
    }
}

// ═══════════════════════════════════════════════════════════
// Render: convert widget tree → JSON → eval in webview
// ═══════════════════════════════════════════════════════════

void render_view(AppState& state) {
    if (state.webview == nullptr || state.view_fn.is_null()) {
        return;
    }

    // Clear render-lifetime callbacks so stale closures from the previous
    // render are released and fresh IDs are registered by widget constructors.
    state.clear_render_callbacks();

    try {
        // Call the view function with the current model.
        std::vector<Value> view_args{state.model};
        auto tree = invoke_callable(state.view_fn, view_args, state.loc);

        // Style-dict event handlers and ARIA attributes are now processed
        // inline during widget construction (finalize_widget), so no
        // separate tree walk is needed.

        // Serialize widget tree to JSON.
        auto json = value_to_json(tree);

        // Host-side render de-duplication.  The serialized tree is a sound skip
        // key because theming goes through the separate __gui_apply_theme path,
        // not __gui_render.  When the frame is byte-for-byte identical to what
        // the webview already shows, skip webview_eval entirely — eliding the
        // IPC round-trip, the webview's JSON.parse of the object literal, and
        // the front-end re-render.  The full skip fires for display-only views
        // whose model change leaves the tree unchanged (e.g. a poll returning
        // identical data); interactive widgets embed a fresh per-render callback
        // id, so their trees differ each frame and are still re-emitted.  Either
        // way the redundant per-frame JSON.stringify the JS renderer used to run
        // for change-detection is gone.
        if (json != state.prev_tree_json) {
            // Send to webview for rendering.
            // Pass JSON directly as a JS expression — no single-quote wrapping needed.
            auto js = std::format("__gui_render({})", json);
            webview_eval(state.webview, js.c_str());
            state.prev_tree_json = std::move(json);
        }
    } catch (const std::exception& ex) {
        // Show the error using the themeable error renderer.
        render_error(state, std::string{"View error:\n"} + ex.what());
    } catch (...) {
        render_error(state, "Unknown error in view function");
    }

    // Evaluate subscriptions after each render.
    manage_subscriptions(state);

    // In dev mode, check if assets have been modified and hot-reload them.
    check_dev_asset_reload(state);
}

// Single source of truth for subscription types: each descriptor names the JS
// setup function suffix (window.__gui_setup_<type>) and the ordered parameters
// it reads from the subscription dict. build_subscription_map() and the JS setup
// dispatch both consume this table so their keys/defaults can never drift apart.
enum class SubParamKind {
    interval_int,  // -> SubscriptionConfig::interval, rendered as a bare JS integer
    throttle_int,  // -> SubscriptionConfig::throttle_ms, rendered as a bare JS integer
    filter_string, // -> SubscriptionConfig::filter, rendered as an escaped JS string
};

struct SubParam {
    SubParamKind kind{};
    std::string_view key;         // dict key to read
    std::string_view str_default; // default for filter_string params
    std::int64_t int_default{};   // default for interval_int / throttle_int params
};

struct SubDescriptor {
    std::string_view type;            // sub:: constant / JS setup suffix
    std::array<SubParam, 2> params{}; // parameters in JS argument order
    std::uint8_t param_count{};       // number of active entries in params
};

// Descriptor table covering every subscription type. Simple (id-only) subscriptions
// have no parameters; parameterised ones list them in JS argument order.
constexpr std::array<SubDescriptor, 15> k_sub_descriptors{{
    {sub::resize, {}, 0},
    {sub::focus, {}, 0},
    {sub::visibility, {}, 0},
    {sub::online, {}, 0},
    {sub::offline, {}, 0},
    {sub::scroll, {}, 0},
    {sub::animation_frame, {}, 0},
    {sub::timer, {{{SubParamKind::interval_int, "interval", "", 1000}}}, 1},
    {sub::keyboard, {{{SubParamKind::filter_string, "filter", "*", 0}}}, 1},
    {sub::mouse,
     {{{SubParamKind::filter_string, "event", "move", 0},
       {SubParamKind::throttle_int, "throttle_ms", "", 16}}},
     2},
    {sub::media_query, {{{SubParamKind::filter_string, "query", "", 0}}}, 1},
    {sub::idle, {{{SubParamKind::interval_int, "timeout_ms", "", 30000}}}, 1},
    {sub::storage, {{{SubParamKind::filter_string, "key", "", 0}}}, 1},
    {sub::drag, {{{SubParamKind::filter_string, "event", "*", 0}}}, 1},
    {sub::wheel, {}, 0},
}};

// Look up the descriptor for a subscription type, or nullptr if unknown.
static const SubDescriptor* find_sub_descriptor(std::string_view sub_type) {
    for (const auto& desc : k_sub_descriptors) {
        if (desc.type == sub_type) {
            return &desc;
        }
    }

    return nullptr;
}

// Build the JS setup call for a subscription from its descriptor, reading the
// same dict keys/defaults that build_subscription_map() captured into the config.
static std::string build_subscription_setup_js(const SubDescriptor& desc,
                                               const std::string& escaped_id,
                                               const DictionaryValue& sd) {
    std::string js = std::format("window.__gui_setup_{}('{}'", desc.type, escaped_id);

    for (std::size_t i = 0; i < desc.param_count; ++i) {
        const auto& param = desc.params[i];

        switch (param.kind) {
            case SubParamKind::interval_int:
            case SubParamKind::throttle_int:
                js += std::format(",{}", dict_int(sd, param.key, param.int_default));
                break;
            case SubParamKind::filter_string:
                js += std::format(
                    ",'{}'", luma::js_string_escape(dict_string(sd, param.key, param.str_default)));
                break;
        }
    }

    js += ")";
    return js;
}

// Build the new subscription map (id → structured config) from the array
// returned by the user's subscribe function. The config captures every
// parameter so a changed value (e.g. timer interval) forces teardown/re-setup.
static StringMap<SubscriptionConfig> build_subscription_map(const ArrayValue& subs_arr) {
    StringMap<SubscriptionConfig> new_subs;
    new_subs.reserve(subs_arr.elements->size());

    for (const auto& sub : *subs_arr.elements) {
        if (!sub.is_dictionary()) {
            continue;
        }

        const auto& sd = *sub.as_dictionary();
        auto id = dict_string(sd, "id");
        auto sub_type = dict_string(sd, key::sub_type);

        if (id.empty() || sub_type.empty()) {
            continue;
        }

        // Build structured config from subscription parameters, driven by the
        // shared descriptor table so the captured keys/defaults stay in lockstep
        // with the JS setup dispatch.
        SubscriptionConfig config;
        config.sub_type = sub_type;
        config.typed = dict_bool(sd, key::typed);

        if (const auto* desc = find_sub_descriptor(sub_type)) {
            for (std::size_t i = 0; i < desc->param_count; ++i) {
                const auto& param = desc->params[i];

                switch (param.kind) {
                    case SubParamKind::interval_int:
                        config.interval = dict_int(sd, param.key, param.int_default);
                        break;
                    case SubParamKind::throttle_int:
                        config.throttle_ms = dict_int(sd, param.key, param.int_default);
                        break;
                    case SubParamKind::filter_string:
                        config.filter = dict_string(sd, param.key, param.str_default);
                        break;
                }
            }
        }

        new_subs.emplace(id, std::move(config));
    }

    return new_subs;
}

// Tear down JS-side listeners and drop callbacks for subscriptions that are no
// longer present, or whose configuration changed — single pass over the old map
// with O(1) lookups into new_subs.
static void remove_stale_subscriptions(AppState& state,
                                       const StringMap<SubscriptionConfig>& new_subs) {
    std::vector<std::string> to_remove;

    for (const auto& [id, old_config] : state.active_subs) {
        auto it = new_subs.find(id);

        if (it == new_subs.end() || it->second != old_config) {
            auto js = std::format("window.__gui_remove_sub('{}')", luma::js_string_escape(id));
            webview_eval(state.webview, js.c_str());
            to_remove.push_back(id);
        }
    }

    for (const auto& id : to_remove) {
        state.erase_sub_callback(id);
    }
}

// Register callbacks and set up JS-side listeners for new or changed
// subscriptions, skipping any already active with an identical config.
static void apply_new_subscriptions(AppState& state, const ArrayValue& subs_arr,
                                    const StringMap<SubscriptionConfig>& new_subs) {
    for (const auto& sub : *subs_arr.elements) {
        if (!sub.is_dictionary()) {
            continue;
        }

        const auto& sd = *sub.as_dictionary();
        auto id = dict_string(sd, "id");
        auto sub_type = dict_string(sd, key::sub_type);

        if (id.empty() || sub_type.empty()) {
            continue;
        }

        // Refresh the callback so closures over model state stay current.
        const auto* cb = sd.find(key::callback);

        if ((cb != nullptr) && cb->is_callable()) {
            state.register_sub_callback(id, *cb);
        }

        // Skip JS-side setup if already active with the same config.
        auto old_it = state.active_subs.find(id);
        auto new_it = new_subs.find(id);

        if (old_it != state.active_subs.end() && new_it != new_subs.end() &&
            old_it->second == new_it->second) {
            continue;
        }

        // Set up the JS-side listener.
        auto escaped_id = luma::js_string_escape(id);

        // Dispatch JS-side setup from the shared subscription table.
        if (const auto* desc = find_sub_descriptor(sub_type)) {
            auto js = build_subscription_setup_js(*desc, escaped_id, sd);
            webview_eval(state.webview, js.c_str());
        }
    }
}

// Forward subscription errors through the user's update function instead of
// silently swallowing them (§6).
static void dispatch_subscription_error(AppState& state, const std::exception& ex) {
    if (state.update_fn.is_null() || !state.update_fn.is_callable()) {
        return;
    }

    try {
        auto err_msg = make_dict();
        err_msg->set("_gui_error", Value{std::string{"subscription_error"}});
        err_msg->set("error", Value{std::string{ex.what()}});
        std::vector<Value> err_args{state.model, Value{std::move(err_msg)}};
        auto result = invoke_callable(state.update_fn, err_args, state.loc);

        if (!result.is_null()) {
            process_callback_result(state, std::move(result));
        }
    } catch (const std::exception& e) {
        // Last resort: prevent infinite error loops.
        std::cerr << "GraphicalUi: subscription error dispatch failed: " << e.what() << "\n";
    } catch (...) {
        // Last resort: prevent infinite error loops.
        std::cerr << "GraphicalUi: subscription error dispatch failed (unknown error)\n";
    }
}

void manage_subscriptions(AppState& state) {
    if (state.subscribe_fn.is_null() || state.managing_subscriptions) {
        return;
    }

    // AtomicBoolGuard restores the original value on all exit paths.
    const AtomicBoolGuard guard{state.managing_subscriptions};

    try {
        std::vector<Value> args{state.model};
        auto subs_val = invoke_callable(state.subscribe_fn, args, state.loc);

        auto subs = unwrap_array_arg(subs_val);

        if (subs.is_null()) {
            return;
        }

        const auto& subs_arr = *subs.as_array();

        auto new_subs = build_subscription_map(subs_arr);
        remove_stale_subscriptions(state, new_subs);
        apply_new_subscriptions(state, subs_arr, new_subs);

        state.active_subs = std::move(new_subs);

    } catch (const std::exception& ex) {
        dispatch_subscription_error(state, ex);
    }
}

// ═══════════════════════════════════════════════════════════
// Event handler bound to the webview
// ═══════════════════════════════════════════════════════════

// Convert a mouse-event payload dictionary (x, y, button, ctrl, shift, alt) into
// a typed GraphicalUi.MouseEvent record for GraphicalUi.on_mouse_typed callbacks.
// Mirrors the field/variant discipline of Terminal.parse_mouse_event: x / y are
// `number` (device pixels), the button becomes a GraphicalUi.MouseButton choice,
// and the three modifier keys become booleans.  Missing coordinates default to 0,
// missing modifiers to false, and any button other than "middle" / "right"
// (including a missing one) maps to Left — keeping the record total over whatever
// the browser emits.
Value build_mouse_event_record(const DictionaryValue& payload) {
    auto read_number = [&payload](const char* field_key) -> double {
        const auto* v = payload.find(field_key);

        if (v != nullptr) {
            if (v->is_number()) {
                return v->as_number();
            }

            if (v->is_integer()) {
                return static_cast<double>(v->as_integer());
            }
        }

        return 0.0;
    };

    auto button = std::make_shared<ChoiceValue>();
    button->type_name = "MouseButton";
    const auto button_str = dict_string(payload, "button", "left");

    if (button_str == "right") {
        button->variant = "Right";
    } else if (button_str == "middle") {
        button->variant = "Middle";
    } else {
        button->variant = "Left";
    }

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "MouseEvent";
    rec->fields.emplace_back("x", Value{read_number("x")});
    rec->fields.emplace_back("y", Value{read_number("y")});
    rec->fields.emplace_back("button", Value{std::move(button)});
    rec->fields.emplace_back("ctrl", Value{dict_bool(payload, "ctrl")});
    rec->fields.emplace_back("shift", Value{dict_bool(payload, "shift")});
    rec->fields.emplace_back("alt", Value{dict_bool(payload, "alt")});

    return Value{std::move(rec)};
}

// Convert a scroll-event payload dictionary (x, y) into a typed
// GraphicalUi.ScrollPosition record for GraphicalUi.on_scroll_typed callbacks.
// x / y are `number` (device-pixel scroll offsets); a missing field defaults to
// 0 so the record stays total.
Value build_scroll_position_record(const DictionaryValue& payload) {
    auto read_number = [&payload](const char* field_key) -> double {
        const auto* v = payload.find(field_key);

        if (v != nullptr) {
            if (v->is_number()) {
                return v->as_number();
            }

            if (v->is_integer()) {
                return static_cast<double>(v->as_integer());
            }
        }

        return 0.0;
    };

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "ScrollPosition";
    rec->fields.emplace_back("x", Value{read_number("x")});
    rec->fields.emplace_back("y", Value{read_number("y")});

    return Value{std::move(rec)};
}

// Convert a keyboard-event key string plus its modifier payload dictionary into
// a typed GraphicalUi.KeyEvent record for GraphicalUi.on_key_typed callbacks.
// `key` is the pressed key's name (KeyboardEvent.key); the four modifier flags
// are read from `mods` (the {ctrl, shift, alt, meta} dictionary the browser
// emits alongside the key).  A null `mods` — as passed by the headless test
// path — defaults every modifier to false, keeping the record total.
Value build_key_event_record(const std::string& key, const DictionaryValue* mods) {
    auto read_mod = [mods](const char* field_key) -> bool {
        return mods != nullptr && dict_bool(*mods, field_key);
    };

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "KeyEvent";
    rec->fields.emplace_back("key", Value{key});
    rec->fields.emplace_back("ctrl", Value{read_mod("ctrl")});
    rec->fields.emplace_back("shift", Value{read_mod("shift")});
    rec->fields.emplace_back("alt", Value{read_mod("alt")});
    rec->fields.emplace_back("meta", Value{read_mod("meta")});

    return Value{std::move(rec)};
}

// Convert a window-resize width/height pair into a typed GraphicalUi.WindowSize
// record for GraphicalUi.on_resize_typed callbacks.  width / height are
// `integer` (discrete device-pixel counts), mirroring the two loose integer
// arguments on_resize delivers.
Value build_window_size_record(std::int64_t width, std::int64_t height) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "WindowSize";
    rec->fields.emplace_back("width", Value{width});
    rec->fields.emplace_back("height", Value{height});

    return Value{std::move(rec)};
}

// Convert a drag-event payload dictionary into a typed GraphicalUi.DragEvent
// record for GraphicalUi.on_drag_typed callbacks.  x / y are `number` (device
// pixels), `data` the dragged payload string, and `phase` a GraphicalUi.DragPhase
// choice mapped from the payload's `event` key.  Missing coordinates default to
// 0, missing data to "", and an unrecognised/missing phase to Start — keeping the
// record total over whatever the browser emits.
Value build_drag_event_record(const DictionaryValue& payload) {
    auto read_number = [&payload](const char* field_key) -> double {
        const auto* v = payload.find(field_key);

        if (v != nullptr) {
            if (v->is_number()) {
                return v->as_number();
            }

            if (v->is_integer()) {
                return static_cast<double>(v->as_integer());
            }
        }

        return 0.0;
    };

    auto phase = std::make_shared<ChoiceValue>();
    phase->type_name = "DragPhase";
    phase->variant = drag_phase_from_string(dict_string(payload, "event", "start"));

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "DragEvent";
    rec->fields.emplace_back("x", Value{read_number("x")});
    rec->fields.emplace_back("y", Value{read_number("y")});
    rec->fields.emplace_back("data", Value{dict_string(payload, "data", "")});
    rec->fields.emplace_back("phase", Value{std::move(phase)});

    return Value{std::move(rec)};
}

// Convert a drop-event payload dictionary into a typed GraphicalUi.DropEvent
// record for GraphicalUi.drop_target_typed callbacks.  `data` is the dragged
// payload string and x / y are `number` (device-pixel drop coordinates,
// mirroring DragEvent).  Missing data defaults to "" and missing coordinates to
// 0 — keeping the record total over whatever the browser emits.
Value build_drop_event_record(const DictionaryValue& payload) {
    auto read_number = [&payload](const char* field_key) -> double {
        const auto* v = payload.find(field_key);

        if (v != nullptr) {
            if (v->is_number()) {
                return v->as_number();
            }

            if (v->is_integer()) {
                return static_cast<double>(v->as_integer());
            }
        }

        return 0.0;
    };

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "DropEvent";
    rec->fields.emplace_back("data", Value{dict_string(payload, "data", "")});
    rec->fields.emplace_back("x", Value{read_number("x")});
    rec->fields.emplace_back("y", Value{read_number("y")});

    return Value{std::move(rec)};
}

// Convert a storage-event payload dictionary (key, oldValue, newValue) into a
// typed GraphicalUi.StorageEvent record for GraphicalUi.on_storage_change_typed
// callbacks.  `key` is the changed localStorage key (default "").  old_value /
// new_value are optional<string>: present in the payload → some(string), absent
// → none (a null Value) — the browser omits oldValue for a newly-added key and
// newValue for a cleared one, honouring no-null.
Value build_storage_event_record(const DictionaryValue& payload) {
    // A present string field becomes some(string); an absent one becomes none,
    // which Luma represents as a null Value.
    auto optional_field = [&payload](const char* field_key) -> Value {
        const auto* v = payload.find(field_key);

        if (v != nullptr && v->is_string()) {
            return Value{v->as_string()};
        }

        return Value{};
    };

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "StorageEvent";
    rec->fields.emplace_back("key", Value{dict_string(payload, "key", "")});
    rec->fields.emplace_back("old_value", optional_field("oldValue"));
    rec->fields.emplace_back("new_value", optional_field("newValue"));

    return Value{std::move(rec)};
}

// Convert a wheel-event payload dictionary (deltaX, deltaY) into a typed
// GraphicalUi.WheelDelta record for GraphicalUi.on_wheel_typed callbacks.
// delta_x / delta_y are `number` (device-pixel wheel deltas, mirroring the DOM
// WheelEvent.deltaX / deltaY); a missing delta defaults to 0 so the record stays
// total.
Value build_wheel_delta_record(const DictionaryValue& payload) {
    auto read_number = [&payload](const char* field_key) -> double {
        const auto* v = payload.find(field_key);

        if (v != nullptr) {
            if (v->is_number()) {
                return v->as_number();
            }

            if (v->is_integer()) {
                return static_cast<double>(v->as_integer());
            }
        }

        return 0.0;
    };

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "WheelDelta";
    rec->fields.emplace_back("delta_x", Value{read_number("deltaX")});
    rec->fields.emplace_back("delta_y", Value{read_number("deltaY")});

    return Value{std::move(rec)};
}

namespace {

// ── Generic event dispatch helper ──────────────────────
// Validates the callback, invokes it with the provided arguments,
// and processes the result through the update cycle.
void dispatch_event(AppState& state, const Value& callback, std::vector<Value> args) {
    if (callback.is_null() || !callback.is_callable()) {
        return;
    }

    auto result = invoke_callable(callback, args, state.loc);

    if (!result.is_null()) {
        apply_event_result(state, std::move(result));
    }
}

// ── Individual event handlers ──────────────────────────

// Handler for command_result / command_error events (HTTP responses, delay callbacks).
void handle_command_result(AppState& state, const EventMessage& event, const std::string& /*raw*/) {
    Value result_val;

    if (event.type == "command_result") {
        result_val = Value{
            ResultValue::success(Value{event.has_string() ? event.string_value() : std::string{}})};
    } else {
        result_val = Value{ResultValue::failure(
            Value{event.has_string() ? event.string_value() : std::string{"unknown error"}})};
    }

    dispatch_event(state, state.find_command_callback(event.id), {std::move(result_val)});
}

// Handler for subscription timer events (no arguments).
void handle_subscription(AppState& state, const EventMessage& event, const std::string& /*raw*/) {
    dispatch_event(state, state.find_sub_callback(event.id), {});
}

// Handler for keyboard events (key string argument).
void handle_keyboard(AppState& state, const EventMessage& event, const std::string& /*raw*/) {
    // Guard the variant access: a malformed payload would otherwise throw
    // std::bad_variant_access and wipe the UI via the top-level error renderer.
    if (!event.has_string()) {
        return;
    }

    auto callback = state.find_sub_callback(event.id);

    // A GraphicalUi.on_key_typed subscription (flagged typed in active_subs)
    // receives a GraphicalUi.KeyEvent record built from the key name and the
    // {ctrl, shift, alt, meta} modifier payload; plain on_key gets the bare key
    // string.  active_subs is only mutated on this same UI thread, so the read
    // needs no extra locking (mirrors handle_dict_event / handle_scroll_event).
    auto it = state.active_subs.find(event.id);

    if (it != state.active_subs.end() && it->second.typed) {
        dispatch_event(state, callback,
                       {build_key_event_record(event.string_value(), event.dict_payload.get())});
        return;
    }

    dispatch_event(state, callback, {Value{event.string_value()}});
}

// Handler for resize events (width,height string → two integer arguments).
void handle_resize(AppState& state, const EventMessage& event, const std::string& /*raw*/) {
    auto callback = state.find_sub_callback(event.id);

    if (callback.is_null() || !callback.is_callable()) {
        return;
    }

    // Guard the variant access (see handle_keyboard) before reading the payload.
    if (!event.has_string()) {
        return;
    }

    std::int64_t w = 0;
    std::int64_t h = 0;
    const auto& resize_str = event.string_value();
    auto comma = resize_str.find(',');

    if (comma != std::string::npos) {
        try {
            w = std::stoll(resize_str.substr(0, comma));
            h = std::stoll(resize_str.substr(comma + 1));
        } catch (const std::exception& e) {
            // Invalid resize dimensions — log and keep defaults (0,0).
            std::cerr << "GraphicalUi: failed to parse resize dimensions: " << e.what() << "\n";
        }
    }

    // §1: Track window width for responsive().
    if (w > 0) {
        state.window_width.store(w);
    }

    // A GraphicalUi.on_resize_typed subscription (flagged typed in active_subs)
    // receives a single GraphicalUi.WindowSize record; plain on_resize gets the
    // two loose integer arguments.
    auto it = state.active_subs.find(event.id);

    if (it != state.active_subs.end() && it->second.typed) {
        dispatch_event(state, callback, {build_window_size_record(w, h)});
        return;
    }

    dispatch_event(state, callback, {Value{w}, Value{h}});
}

// Handler for focus change events (boolean argument).
void handle_focus_change(AppState& state, const EventMessage& event, const std::string& /*raw*/) {
    // Guard the variant access (see handle_keyboard): focus_change,
    // visibility_change, and media_query all deliver a boolean payload; drop
    // anything malformed rather than throwing and wiping the UI.
    if (!event.has_bool()) {
        return;
    }

    dispatch_event(state, state.find_sub_callback(event.id), {Value{event.bool_value()}});
}

// Handler for mouse and widget events (pre-parsed dict payload).
void handle_dict_event(AppState& state, const EventMessage& event, const std::string& /*raw*/) {
    const bool is_mouse = (event.type == "mouse_event");
    auto callback = is_mouse ? state.find_sub_callback(event.id) : state.find_callback(event.id);

    // Use the dict payload that was already extracted during the single parse pass.
    auto payload = event.dict_payload ? event.dict_payload : make_dict();

    // A GraphicalUi.on_mouse_typed subscription (flagged typed in active_subs)
    // receives a GraphicalUi.MouseEvent record; plain on_mouse gets the dict.
    // active_subs is only mutated on this same UI thread (manage_subscriptions),
    // so the read needs no extra locking.
    if (is_mouse) {
        auto it = state.active_subs.find(event.id);

        if (it != state.active_subs.end() && it->second.typed) {
            dispatch_event(state, callback, {build_mouse_event_record(*payload)});
            return;
        }
    }

    dispatch_event(state, callback, {Value{std::move(payload)}});
}

// Handler for window scroll events (GraphicalUi.on_scroll / on_scroll_typed).
// Delivers a raw {x, y} dictionary, or a typed GraphicalUi.ScrollPosition record
// when the subscription was created via on_scroll_typed.
void handle_scroll_event(AppState& state, const EventMessage& event, const std::string& /*raw*/) {
    auto callback = state.find_sub_callback(event.id);
    auto payload = event.dict_payload ? event.dict_payload : make_dict();

    auto it = state.active_subs.find(event.id);

    if (it != state.active_subs.end() && it->second.typed) {
        dispatch_event(state, callback, {build_scroll_position_record(*payload)});
        return;
    }

    dispatch_event(state, callback, {Value{std::move(payload)}});
}

// Handler for drag events (GraphicalUi.on_drag / on_drag_typed).
// Delivers the raw {event, x, y, data} dictionary, or a typed
// GraphicalUi.DragEvent record when the subscription was created via
// on_drag_typed.  Mirrors handle_scroll_event.
void handle_drag(AppState& state, const EventMessage& event, const std::string& /*raw*/) {
    auto callback = state.find_sub_callback(event.id);
    auto payload = event.dict_payload ? event.dict_payload : make_dict();

    auto it = state.active_subs.find(event.id);

    if (it != state.active_subs.end() && it->second.typed) {
        dispatch_event(state, callback, {build_drag_event_record(*payload)});
        return;
    }

    dispatch_event(state, callback, {Value{std::move(payload)}});
}

// Handler for typed drop events (GraphicalUi.drop_target_typed).  Builds a typed
// GraphicalUi.DropEvent record {data, x, y} from the payload and dispatches it to
// the drop target's on_drop callback (bound as a widget _callback_id).
void handle_drop(AppState& state, const EventMessage& event, const std::string& /*raw*/) {
    auto payload = event.dict_payload ? event.dict_payload : make_dict();
    dispatch_event(state, state.find_callback(event.id), {build_drop_event_record(*payload)});
}

// Handler for cross-tab storage change events (GraphicalUi.on_storage_change /
// on_storage_change_typed).  A typed subscription receives a
// GraphicalUi.StorageEvent record {key, old_value, new_value}; a plain one gets
// the bare new-value string ("" when the key was cleared).  Mirrors
// handle_scroll_event.
void handle_storage(AppState& state, const EventMessage& event, const std::string& /*raw*/) {
    auto callback = state.find_sub_callback(event.id);
    auto payload = event.dict_payload ? event.dict_payload : make_dict();

    auto it = state.active_subs.find(event.id);

    if (it != state.active_subs.end() && it->second.typed) {
        dispatch_event(state, callback, {build_storage_event_record(*payload)});
        return;
    }

    dispatch_event(state, callback, {Value{dict_string(*payload, "newValue", "")}});
}

// Handler for scroll-wheel delta events (GraphicalUi.on_wheel_typed).  Builds a
// typed GraphicalUi.WheelDelta record {delta_x, delta_y} from the payload — the
// subscription is typed-only (mirrors handle_drop for drop_target_typed).
void handle_wheel(AppState& state, const EventMessage& event, const std::string& /*raw*/) {
    auto payload = event.dict_payload ? event.dict_payload : make_dict();
    dispatch_event(state, state.find_sub_callback(event.id), {build_wheel_delta_record(*payload)});
}

// Handler for document visibility change events (GraphicalUi.on_visibility_change
// / on_visibility_change_typed).  A typed subscription receives a
// GraphicalUi.VisibilityState choice (Visible / Hidden); a plain one gets the
// bare boolean (!document.hidden).  Mirrors handle_scroll_event, but the payload
// is the boolean the browser emits in `value`.
void handle_visibility_change(AppState& state, const EventMessage& event,
                              const std::string& /*raw*/) {
    // Guard the variant access (see handle_keyboard): drop a malformed payload
    // rather than throwing and wiping the UI.
    if (!event.has_bool()) {
        return;
    }

    auto callback = state.find_sub_callback(event.id);
    auto it = state.active_subs.find(event.id);

    if (it != state.active_subs.end() && it->second.typed) {
        auto choice = std::make_shared<ChoiceValue>();
        choice->type_name = "VisibilityState";
        choice->variant = visibility_state_from_visible(event.bool_value());
        dispatch_event(state, callback, {Value{std::move(choice)}});
        return;
    }

    dispatch_event(state, callback, {Value{event.bool_value()}});
}

// ── Hash-based dispatch table for named event types ───

using EventHandler = void (*)(AppState&, const EventMessage&, const std::string&);

struct EventDispatchEntry {
    const char* type;
    EventHandler handler;
};

// Compile-time event dispatch table — maps event type strings to handler functions.
// Built into a runtime hash map via event_dispatch_map() for O(1) lookup.
// A constexpr sorted array with binary search would avoid the runtime map
// construction, but with only ~12 entries the hash map is simpler and the
// one-time construction cost is negligible.
// clang-format off
constexpr EventDispatchEntry event_dispatch_entries[] = {
    {.type="command_result",   .handler=handle_command_result},
    {.type="command_error",    .handler=handle_command_result},
    {.type="subscription",     .handler=handle_subscription},
    {.type="keyboard",         .handler=handle_keyboard},
    {.type="resize",           .handler=handle_resize},
    {.type="focus_change",     .handler=handle_focus_change},
    {.type="mouse_event",      .handler=handle_dict_event},
    {.type="widget_event",     .handler=handle_dict_event},
    // New subscription event types: visibility, online/offline, media query.
    {.type="visibility_change", .handler=handle_visibility_change},
    {.type="online",            .handler=handle_subscription},
    {.type="offline",           .handler=handle_subscription},
    {.type="media_query",       .handler=handle_focus_change},
    {.type="scroll_event",      .handler=handle_scroll_event},
    {.type="drag_event",        .handler=handle_drag},
    {.type="drop_event",        .handler=handle_drop},
    {.type="storage_change",    .handler=handle_storage},
    {.type="wheel_event",       .handler=handle_wheel},
};
// clang-format on

// Build a hash map from the constexpr table for O(1) dispatch.
[[nodiscard]] const std::unordered_map<std::string_view, EventHandler>& event_dispatch_map() {
    static const auto map = [] {
        std::unordered_map<std::string_view, EventHandler> m;

        for (const auto& entry : event_dispatch_entries) {
            m.emplace(entry.type, entry.handler);
        }

        return m;
    }();
    return map;
}

// ── Widget interaction event dispatch ──────────────────
// Maps widget event type strings to argument extractors that populate
// the callback argument vector from the event payload.
using ArgExtractor = void (*)(const EventMessage&, std::vector<Value>&);

// clang-format off
[[nodiscard]] const std::unordered_map<std::string_view, ArgExtractor>&
widget_event_dispatch() {
    static const std::unordered_map<std::string_view, ArgExtractor> map = {
        {"click",  [](const EventMessage& e, std::vector<Value>& a) {
            if (e.has_bool()) { a.emplace_back(e.bool_value()); } }},
        {"toggle", [](const EventMessage& e, std::vector<Value>& a) {
            if (e.has_bool()) { a.emplace_back(e.bool_value()); } }},
        {"change", [](const EventMessage& e, std::vector<Value>& a) {
            if (e.has_string()) { a.emplace_back(e.string_value()); } }},
        {"slide",  [](const EventMessage& e, std::vector<Value>& a) {
            if (e.has_number()) { a.emplace_back(e.number_value()); } }},
        {"select", [](const EventMessage& e, std::vector<Value>& a) {
            if (e.has_number()) { a.emplace_back(static_cast<std::int64_t>(e.number_value())); }
            else if (e.has_string()) { a.emplace_back(e.string_value()); } }},
        // Date/time/color pickers and file inputs emit their selected value via a
        // "callback_result" event; forward that value to the widget's callback so
        // handlers such as (string c) -> ... receive the picked value rather than none.
        {"callback_result", [](const EventMessage& e, std::vector<Value>& a) {
            if (e.has_string()) { a.emplace_back(e.string_value()); }
            else if (e.has_number()) { a.emplace_back(e.number_value()); }
            else if (e.has_bool()) { a.emplace_back(e.bool_value()); } }},
    };
    return map;
}

// clang-format on

} // anonymous namespace

void on_gui_event(const char* /*id*/, const char* req, void* arg) {
    auto* state = static_cast<AppState*>(arg);

    if (state == nullptr || req == nullptr) {
        return;
    }

    try {
        // req is a JSON array of arguments: ["{ ... }"]
        // Parse the outer array and extract the first element (the event JSON string).
        auto outer = luma::json::JsonValue::parse(req, ResourceLimits::max_json_nesting_depth);

        if (!outer.is_array() || outer.as_array().empty()) {
            return;
        }

        const auto& first_elem = outer.as_array()[0];

        if (!first_elem.is_string()) {
            return;
        }

        // Single parse of the event JSON — extracts all fields including dict
        // payloads for mouse/widget events in one pass.
        auto event_root = luma::json::JsonValue::parse(first_elem.as_string(),
                                                       ResourceLimits::max_json_nesting_depth);
        const auto event = parse_event(event_root);

        // ── Look up the event type in the hash-based dispatch map ──

        static const std::string empty_raw;
        const auto& dispatch = event_dispatch_map();
        auto dispatch_it = dispatch.find(event.type);

        if (dispatch_it != dispatch.end()) {
            dispatch_it->second(*state, event, empty_raw);
            return;
        }

        // ── Fallback: direct widget interaction events ──
        std::vector<Value> args;
        const auto& widget_dispatch = widget_event_dispatch();
        auto widget_it = widget_dispatch.find(event.type);

        if (widget_it != widget_dispatch.end()) {
            widget_it->second(event, args);
        }

        dispatch_event(*state, state->find_callback(event.id), std::move(args));

    } catch (const std::exception& ex) {
        render_error(*state, std::string{"Runtime error:\n"} + ex.what());
    } catch (...) {
        render_error(*state, "Unknown runtime error in event handler");
    }
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW
