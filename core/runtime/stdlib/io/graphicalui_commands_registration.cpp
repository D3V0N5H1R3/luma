#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

#include <functional>

namespace luma::gui_detail {

// ═══════════════════════════════════════════════════════════
// Table-driven registration helpers (file-local)
// ═══════════════════════════════════════════════════════════

// Register an HTTP command: name(url, callback, headers?, timeout?) or
//                           name(url, body, callback, headers?, timeout?)
// has_body=true means the second positional arg is a body string.
static void register_http_command(const EnvPtr& env, const char* name, const char* type,
                                  bool has_body) {
    define_native(env, name,
                  [name, type, has_body](std::span<const Value> args, SourceLocation loc) -> Value {
                      const int min_args = has_body ? 3 : 2;
                      expect_min_args(name, args, min_args, loc);

                      auto url = expect_string(args[0], name, loc);
                      auto w = make_command_dict(type);
                      w->set("url", Value{url});

                      int cb_idx = 1;

                      if (has_body) {
                          auto body = expect_string(args[1], name, loc);
                          w->set("body", Value{body});
                          cb_idx = 2;
                      }

                      register_or_defer_command_callback(w, args[cb_idx]);

                      // Optional headers dictionary.
                      const auto hdrs_idx = static_cast<std::size_t>(cb_idx) + 1;

                      if (args.size() > hdrs_idx && args[hdrs_idx].is_dictionary()) {
                          w->set("headers", args[hdrs_idx]);

                          if (args.size() > hdrs_idx + 1 && args[hdrs_idx + 1].is_integer()) {
                              w->set("timeout", args[hdrs_idx + 1]);
                          }
                      } else if (args.size() > hdrs_idx && args[hdrs_idx].is_integer()) {
                          w->set("timeout", args[hdrs_idx]);
                      }

                      return Value{std::move(w)};
                  });
}

// Kind of positional parameter a subscription accepts between its id and callback.
enum class SubscriptionParam {
    none,
    string,
    integer
};

// Register a lifecycle subscription.  With SubscriptionParam::none the shape is
// name(id, callback); otherwise it is name(id, param, callback) and the extra
// string/integer argument is stored under param_key.  Covers on_resize/on_focus/…
// (none), on_key/on_media_query/on_drag/on_storage_change (string), and
// on_tick/on_idle (integer).
static void register_subscription(const EnvPtr& env, const char* name, const char* sub_type,
                                  SubscriptionParam param_kind = SubscriptionParam::none,
                                  const char* param_key = nullptr) {
    define_native(env, name,
                  [name, sub_type, param_kind, param_key](std::span<const Value> args,
                                                          SourceLocation loc) -> Value {
                      const std::size_t arity = param_kind == SubscriptionParam::none ? 2 : 3;
                      expect_args(name, args, arity, loc);

                      auto id = expect_string(args[0], name, loc);

                      auto w = make_dict();
                      w->set(key::sub_type, Value{std::string{sub_type}});
                      w->set("id", Value{id});

                      if (param_kind == SubscriptionParam::string) {
                          w->set(param_key, Value{expect_string(args[1], name, loc)});
                      } else if (param_kind == SubscriptionParam::integer) {
                          w->set(param_key, Value{expect_integer(args[1], name, loc)});
                      }

                      const std::size_t callback_index =
                          param_kind == SubscriptionParam::none ? 1 : 2;
                      w->set(key::callback, args[callback_index]);

                      return Value{std::move(w)};
                  });
}

// ═══════════════════════════════════════════════════════════
// Commands (side effects) and subscriptions (lifecycle events)
// ═══════════════════════════════════════════════════════════

void register_commands_and_subscriptions(const EnvPtr& env, bool sandbox) {
    // ─── Commands (side effects) ─────────────────────────

    define_native(env, "GraphicalUi.none",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.none", args, 0, loc);
                      auto w = make_command_dict(cmd::none);
                      return Value{std::move(w)};
                  });

    define_native(env, "GraphicalUi.batch",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.batch", args, 1, loc);
                      auto w = make_command_dict(cmd::batch);
                      auto cmds = unwrap_array_arg(args[0]);
                      w->set("commands", cmds.is_null() ? args[0] : cmds);
                      return Value{std::move(w)};
                  });

    // HTTP commands are disabled in sandbox mode (network access).
    if (!sandbox) {
        register_http_command(env, "GraphicalUi.http_get", cmd::http_get, false);
        register_http_command(env, "GraphicalUi.http_post", cmd::http_post, true);

        // Typed variants: deliver result<GraphicalUi.HttpResponse> (status/headers/body).
        register_http_command(env, "GraphicalUi.http_get_full", cmd::http_get_full, false);
        register_http_command(env, "GraphicalUi.http_post_full", cmd::http_post_full, true);
    }

    define_native(env, "GraphicalUi.delay",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.delay", args, 2, loc);

                      auto ms = expect_integer(args[0], "GraphicalUi.delay", loc);
                      auto w = make_command_dict(cmd::delay);
                      w->set("milliseconds", Value{ms});

                      register_or_defer_command_callback(w, args[1]);

                      return Value{std::move(w)};
                  });

    // Clipboard access is disabled in sandbox mode.
    if (!sandbox) {
        define_native(env, "GraphicalUi.write_clipboard",
                      [](std::span<const Value> args, SourceLocation loc) -> Value {
                          expect_args("GraphicalUi.write_clipboard", args, 1, loc);
                          auto text = expect_string(args[0], "GraphicalUi.write_clipboard", loc);
                          auto w = make_command_dict(cmd::write_clipboard);
                          w->set("text", Value{text});
                          return Value{std::move(w)};
                      });

        // GraphicalUi.read_clipboard(callback) -> command
        define_native(env, "GraphicalUi.read_clipboard",
                      [](std::span<const Value> args, SourceLocation loc) -> Value {
                          expect_args("GraphicalUi.read_clipboard", args, 1, loc);
                          auto w = make_command_dict(cmd::read_clipboard);

                          register_or_defer_command_callback(w, args[0]);

                          return Value{std::move(w)};
                      });
    }

    // GraphicalUi.get_local_storage(key, callback) -> command
    define_native(env, "GraphicalUi.get_local_storage",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.get_local_storage", args, 2, loc);
                      auto key = expect_string(args[0], "GraphicalUi.get_local_storage", loc);
                      auto w = make_command_dict(cmd::get_local_storage);
                      w->set("key", Value{key});

                      register_or_defer_command_callback(w, args[1]);

                      return Value{std::move(w)};
                  });

    // GraphicalUi.set_local_storage(key, value) -> command
    define_native(env, "GraphicalUi.set_local_storage",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.set_local_storage", args, 2, loc);
                      auto key = expect_string(args[0], "GraphicalUi.set_local_storage", loc);
                      auto val = expect_string(args[1], "GraphicalUi.set_local_storage", loc);
                      auto w = make_command_dict(cmd::set_local_storage);
                      w->set("key", Value{key});
                      w->set("value", Value{val});
                      return Value{std::move(w)};
                  });

    // GraphicalUi.remove_local_storage(key) -> command
    define_native(env, "GraphicalUi.remove_local_storage",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.remove_local_storage", args, 1, loc);
                      auto key = expect_string(args[0], "GraphicalUi.remove_local_storage", loc);
                      auto w = make_command_dict(cmd::remove_local_storage);
                      w->set("key", Value{key});
                      return Value{std::move(w)};
                  });

    // GraphicalUi.clear_local_storage() -> command
    define_native(env, "GraphicalUi.clear_local_storage",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.clear_local_storage", args, 0, loc);
                      return Value{make_command_dict(cmd::clear_local_storage)};
                  });

    // GraphicalUi.scroll_to(widget_id, behavior?) -> command
    define_native(
        env, "GraphicalUi.scroll_to", [](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_min_args("GraphicalUi.scroll_to", args, 1, loc);
            auto widget_id = expect_string(args[0], "GraphicalUi.scroll_to", loc);
            auto w = make_command_dict(cmd::scroll_to);
            w->set("widget_id", Value{widget_id});
            if (args.size() >= 2) {
                w->set("behavior", Value{expect_string(args[1], "GraphicalUi.scroll_to", loc)});
            }
            return Value{std::move(w)};
        });

    // GraphicalUi.scroll_to_of(widget_id, behavior) -> command
    // Typo-proof companion to scroll_to: takes a GraphicalUi.ScrollBehavior choice
    // and lowers it to the same "smooth"/"instant"/"auto" behavior string the
    // string form accepts.  Mirrors on_mouse_of/mouse_event_type bridging.
    define_native(env, "GraphicalUi.scroll_to_of",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.scroll_to_of", args, 2, loc);
                      auto widget_id = expect_string(args[0], "GraphicalUi.scroll_to_of", loc);
                      auto w = make_command_dict(cmd::scroll_to);
                      w->set("widget_id", Value{widget_id});
                      w->set("behavior", Value{scroll_behavior_to_lower(args[1])});
                      return Value{std::move(w)};
                  });
    define_native(env, "GraphicalUi.blur",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.blur", args, 1, loc);
                      auto widget_id = expect_string(args[0], "GraphicalUi.blur", loc);
                      auto w = make_command_dict(cmd::blur);
                      w->set("widget_id", Value{widget_id});
                      return Value{std::move(w)};
                  });

    // download_file, open_url, and remaining HTTP methods are disabled in sandbox mode.
    if (!sandbox) {
        // GraphicalUi.download_file(url, filename) -> command
        define_native(env, "GraphicalUi.download_file",
                      [](std::span<const Value> args, SourceLocation loc) -> Value {
                          expect_args("GraphicalUi.download_file", args, 2, loc);
                          auto url = expect_string(args[0], "GraphicalUi.download_file", loc);
                          auto filename = expect_string(args[1], "GraphicalUi.download_file", loc);
                          auto w = make_command_dict(cmd::download_file);
                          w->set("url", Value{url});
                          w->set("filename", Value{filename});
                          return Value{std::move(w)};
                      });
    }

    // GraphicalUi.notify(title, body?, icon?) -> command
    define_native(env, "GraphicalUi.notify",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.notify", args, 1, loc);
                      auto title = expect_string(args[0], "GraphicalUi.notify", loc);
                      auto w = make_command_dict(cmd::notify);
                      w->set("title", Value{title});

                      if (args.size() >= 2 && args[1].is_string()) {
                          w->set("body", args[1]);
                      }

                      if (args.size() >= 3 && args[2].is_string()) {
                          w->set("icon", args[2]);
                      }

                      return Value{std::move(w)};
                  });

    define_native(env, "GraphicalUi.random",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.random", args, 3, loc);
                      auto w = make_command_dict(cmd::random);
                      w->set("min", args[0]);
                      w->set("max", args[1]);

                      register_or_defer_command_callback(w, args[2]);

                      return Value{std::move(w)};
                  });

    define_native(env, "GraphicalUi.with_command",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.with_command", args, 2, loc);
                      auto w = make_dict();
                      w->set(key::gui_model, args[0]);
                      w->set(key::gui_command, args[1]);
                      return Value{std::move(w)};
                  });

    if (!sandbox) {
        register_http_command(env, "GraphicalUi.http_put", cmd::http_put, true);
        register_http_command(env, "GraphicalUi.http_delete", cmd::http_delete, false);
        register_http_command(env, "GraphicalUi.http_patch", cmd::http_patch, true);

        // GraphicalUi.open_url(url) -> command
        define_native(env, "GraphicalUi.open_url",
                      [](std::span<const Value> args, SourceLocation loc) -> Value {
                          expect_args("GraphicalUi.open_url", args, 1, loc);
                          auto url = expect_string(args[0], "GraphicalUi.open_url", loc);
                          auto w = make_command_dict(cmd::open_url);
                          w->set("url", Value{url});
                          return Value{std::move(w)};
                      });
    }

    // GraphicalUi.set_title(title) -> command
    define_native(env, "GraphicalUi.set_title",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.set_title", args, 1, loc);
                      auto title = expect_string(args[0], "GraphicalUi.set_title", loc);
                      auto w = make_command_dict(cmd::set_title);
                      w->set("title", Value{title});
                      return Value{std::move(w)};
                  });

    // GraphicalUi.print() -> command
    define_native(env, "GraphicalUi.print",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.print", args, 0, loc);
                      auto w = make_command_dict(cmd::print);
                      return Value{std::move(w)};
                  });

    // GraphicalUi.debounce(id, milliseconds, callback) -> command
    define_native(env, "GraphicalUi.debounce",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.debounce", args, 3, loc);
                      auto id = expect_string(args[0], "GraphicalUi.debounce", loc);
                      auto ms = expect_integer(args[1], "GraphicalUi.debounce", loc);
                      auto w = make_command_dict(cmd::debounce);
                      w->set("debounce_id", Value{id});
                      w->set("milliseconds", Value{ms});

                      register_or_defer_command_callback(w, args[2]);

                      return Value{std::move(w)};
                  });

    // Shared undo/redoimplementation — both pop the last entry from a history
    // stack and use it as the restored model.
    auto history_pop = [](std::span<const Value> args, SourceLocation loc,
                          const char* name) -> Value {
        expect_args(name, args, 2, loc);

        auto w = make_dict();

        if (!args[1].is_array() || args[1].as_array()->elements->empty()) {
            w->set("model", args[0]);
            auto empty = std::make_shared<ArrayValue>();
            w->set("history", Value{std::move(empty)});
            return Value{std::move(w)};
        }

        auto history = std::make_shared<ArrayValue>();
        history->elements = args[1].as_array()->elements;
        history->ensure_unique();
        auto restored_model = history->elements->back();
        history->elements->pop_back();

        w->set("model", restored_model);
        w->set("history", Value{std::move(history)});
        return Value{std::move(w)};
    };

    // GraphicalUi.undo(model, history) -> {model, history}
    define_native(env, "GraphicalUi.undo",
                  [history_pop](std::span<const Value> args, SourceLocation loc) -> Value {
                      return history_pop(args, loc, "GraphicalUi.undo");
                  });

    // GraphicalUi.redo(model, history) -> {model, history}
    define_native(env, "GraphicalUi.redo",
                  [history_pop](std::span<const Value> args, SourceLocation loc) -> Value {
                      return history_pop(args, loc, "GraphicalUi.redo");
                  });

    // ─── Subscriptions (lifecycle events) ────────────────

    register_subscription(env, "GraphicalUi.on_tick", sub::timer, SubscriptionParam::integer,
                          "interval");

    register_subscription(env, "GraphicalUi.on_key", sub::keyboard, SubscriptionParam::string,
                          "filter");

    // GraphicalUi.on_key_typed(id, key_filter, callback) -> subscription
    // Additive typed companion to on_key: identical wiring (same sub::keyboard JS
    // listener and filter), but flagged so the runtime hands the callback a typed
    // GraphicalUi.KeyEvent record (key + modifier flags) instead of the bare key
    // string.  The `_typed` flag is read back by build_subscription_map /
    // handle_keyboard.
    define_native(env, "GraphicalUi.on_key_typed",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.on_key_typed", args, 3, loc);
                      auto id = expect_string(args[0], "GraphicalUi.on_key_typed", loc);
                      auto filter = expect_string(args[1], "GraphicalUi.on_key_typed", loc);
                      auto w = make_dict();
                      w->set(key::sub_type, Value{std::string{sub::keyboard}});
                      w->set(key::typed, Value{true});
                      w->set("id", Value{id});
                      w->set("filter", Value{filter});
                      w->set(key::callback, args[2]);

                      return Value{std::move(w)};
                  });

    register_subscription(env, "GraphicalUi.on_resize", sub::resize);

    // GraphicalUi.on_resize_typed(id, callback) -> subscription
    // Additive typed companion to on_resize: same resize subscription, but flagged
    // so the runtime delivers a single GraphicalUi.WindowSize record instead of two
    // loose integer arguments.
    define_native(env, "GraphicalUi.on_resize_typed",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.on_resize_typed", args, 2, loc);
                      auto id = expect_string(args[0], "GraphicalUi.on_resize_typed", loc);
                      auto w = make_dict();
                      w->set(key::sub_type, Value{std::string{sub::resize}});
                      w->set(key::typed, Value{true});
                      w->set("id", Value{id});
                      w->set(key::callback, args[1]);

                      return Value{std::move(w)};
                  });

    register_subscription(env, "GraphicalUi.on_focus", sub::focus);

    define_native(env, "GraphicalUi.on_mouse",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.on_mouse", args, 3, loc);
                      auto id = expect_string(args[0], "GraphicalUi.on_mouse", loc);
                      auto event_type = expect_string(args[1], "GraphicalUi.on_mouse", loc);
                      auto w = make_dict();
                      w->set(key::sub_type, Value{std::string{sub::mouse}});
                      w->set("id", Value{id});
                      w->set("event", Value{event_type});
                      w->set(key::callback, args[2]);

                      // Optional throttle interval (4th arg, default 16ms).
                      if (args.size() > 3 && args[3].is_integer()) {
                          w->set("throttle_ms", args[3]);
                      }

                      return Value{std::move(w)};
                  });

    // GraphicalUi.on_mouse_typed(id, event_type, callback, throttle_ms?) -> subscription
    // Additive typed companion to on_mouse: identical wiring (same sub::mouse JS
    // listener), but flagged so the runtime hands the callback a typed
    // GraphicalUi.MouseEvent record instead of the raw dictionary.  The `_typed`
    // flag is read back by build_subscription_map / handle_dict_event.
    define_native(env, "GraphicalUi.on_mouse_typed",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.on_mouse_typed", args, 3, loc);
                      auto id = expect_string(args[0], "GraphicalUi.on_mouse_typed", loc);
                      auto event_type = expect_string(args[1], "GraphicalUi.on_mouse_typed", loc);
                      auto w = make_dict();
                      w->set(key::sub_type, Value{std::string{sub::mouse}});
                      w->set(key::typed, Value{true});
                      w->set("id", Value{id});
                      w->set("event", Value{event_type});
                      w->set(key::callback, args[2]);

                      // Optional throttle interval (4th arg, default 16ms).
                      if (args.size() > 3 && args[3].is_integer()) {
                          w->set("throttle_ms", args[3]);
                      }

                      return Value{std::move(w)};
                  });

    // GraphicalUi.on_mouse_of(id, event_type, callback, throttle_ms?) -> subscription
    // Typo-proof companion to on_mouse: takes a GraphicalUi.MouseEventType choice
    // instead of an open event-type string, lowering it to the same "event" key
    // on_mouse builds so the JS wiring is identical.  It is NOT flagged typed —
    // the callback still receives the raw dictionary; the choice only enforces the
    // closed event-type set at compile time (pair with on_mouse_typed for a typed
    // payload).  Mirrors alert_of/severity bridging.
    define_native(env, "GraphicalUi.on_mouse_of",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.on_mouse_of", args, 3, loc);
                      auto id = expect_string(args[0], "GraphicalUi.on_mouse_of", loc);
                      auto w = make_dict();
                      w->set(key::sub_type, Value{std::string{sub::mouse}});
                      w->set("id", Value{id});
                      w->set("event", Value{mouse_event_type_to_lower(args[1])});
                      w->set(key::callback, args[2]);

                      // Optional throttle interval (4th arg, default 16ms).
                      if (args.size() > 3 && args[3].is_integer()) {
                          w->set("throttle_ms", args[3]);
                      }

                      return Value{std::move(w)};
                  });

    // GraphicalUi.mouse_event_type_to_string(event_type) -> string
    // Bridge from the GraphicalUi.MouseEventType choice to the "click"/"move"/
    // "down"/"up"/"scroll" string the string-based on_mouse API accepts.
    define_native(env, "GraphicalUi.mouse_event_type_to_string",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.mouse_event_type_to_string", args, 1, loc);
                      return Value{mouse_event_type_to_lower(args[0])};
                  });

    register_subscription(env, "GraphicalUi.on_visibility_change", sub::visibility);
    register_subscription(env, "GraphicalUi.on_online", sub::online);
    register_subscription(env, "GraphicalUi.on_offline", sub::offline);

    // GraphicalUi.on_visibility_change_typed(id, callback) -> subscription
    // Additive typed companion to on_visibility_change: same visibility
    // subscription, but flagged so the runtime delivers a typed
    // GraphicalUi.VisibilityState choice (Visible / Hidden) instead of the bare
    // boolean — removing the "which way does the flag point?" trap.
    define_native(env, "GraphicalUi.on_visibility_change_typed",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.on_visibility_change_typed", args, 2, loc);
                      auto id =
                          expect_string(args[0], "GraphicalUi.on_visibility_change_typed", loc);
                      auto w = make_dict();
                      w->set(key::sub_type, Value{std::string{sub::visibility}});
                      w->set(key::typed, Value{true});
                      w->set("id", Value{id});
                      w->set(key::callback, args[1]);

                      return Value{std::move(w)};
                  });

    // GraphicalUi.on_media_query(id, query, callback) -> subscription
    register_subscription(env, "GraphicalUi.on_media_query", sub::media_query,
                          SubscriptionParam::string, "query");

    register_subscription(env, "GraphicalUi.on_scroll", sub::scroll);

    // GraphicalUi.on_scroll_typed(id, callback) -> subscription
    // Additive typed companion to on_scroll: same scroll subscription, but flagged
    // so the runtime delivers a typed GraphicalUi.ScrollPosition record instead of
    // the raw {x, y} dictionary.
    define_native(env, "GraphicalUi.on_scroll_typed",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.on_scroll_typed", args, 2, loc);
                      auto id = expect_string(args[0], "GraphicalUi.on_scroll_typed", loc);
                      auto w = make_dict();
                      w->set(key::sub_type, Value{std::string{sub::scroll}});
                      w->set(key::typed, Value{true});
                      w->set("id", Value{id});
                      w->set(key::callback, args[1]);

                      return Value{std::move(w)};
                  });

    // GraphicalUi.on_wheel_typed(id, callback) -> subscription
    // Reports scroll-wheel deltas as a typed GraphicalUi.WheelDelta record
    // {delta_x, delta_y} — the wheel delta the untyped mouse "scroll" event never
    // exposes, enabling custom zoom / horizontal-scroll / carousel interactions.
    // Typed-only (no untyped companion), so it is always flagged typed.
    define_native(env, "GraphicalUi.on_wheel_typed",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.on_wheel_typed", args, 2, loc);
                      auto id = expect_string(args[0], "GraphicalUi.on_wheel_typed", loc);
                      auto w = make_dict();
                      w->set(key::sub_type, Value{std::string{sub::wheel}});
                      w->set(key::typed, Value{true});
                      w->set("id", Value{id});
                      w->set(key::callback, args[1]);

                      return Value{std::move(w)};
                  });

    // GraphicalUi.on_idle(id, timeout_ms, callback) -> subscription
    register_subscription(env, "GraphicalUi.on_idle", sub::idle, SubscriptionParam::integer,
                          "timeout_ms");

    // GraphicalUi.on_storage_change(id, key, callback) -> subscription
    register_subscription(env, "GraphicalUi.on_storage_change", sub::storage,
                          SubscriptionParam::string, "key");

    // GraphicalUi.on_storage_change_typed(id, key, callback) -> subscription
    // Additive typed companion to on_storage_change: same storage subscription
    // (filtered by `key`), but flagged so the runtime delivers a typed
    // GraphicalUi.StorageEvent record — {key, old_value, new_value} — instead of
    // the bare new-value string, so a beginner can see what a value changed from
    // and confirm which key fired.
    define_native(env, "GraphicalUi.on_storage_change_typed",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.on_storage_change_typed", args, 3, loc);
                      auto id = expect_string(args[0], "GraphicalUi.on_storage_change_typed", loc);
                      auto storage_key =
                          expect_string(args[1], "GraphicalUi.on_storage_change_typed", loc);
                      auto w = make_dict();
                      w->set(key::sub_type, Value{std::string{sub::storage}});
                      w->set(key::typed, Value{true});
                      w->set("id", Value{id});
                      w->set("key", Value{storage_key});
                      w->set(key::callback, args[2]);

                      return Value{std::move(w)};
                  });

    // GraphicalUi.on_animation_frame(id, callback) -> subscription
    register_subscription(env, "GraphicalUi.on_animation_frame", sub::animation_frame);

    // GraphicalUi.on_drag(id, event_type, callback) -> subscription
    // event_type: "start", "move", "end", "enter", "leave", "drop", or "*" for all.
    register_subscription(env, "GraphicalUi.on_drag", sub::drag, SubscriptionParam::string,
                          "event");

    // GraphicalUi.on_drag_typed(id, callback) -> subscription
    // Additive typed companion to on_drag: same drag subscription (all phases, "*"
    // filter), but flagged so the runtime delivers a typed GraphicalUi.DragEvent
    // record — {x, y, data, phase} — instead of the untyped position dictionary,
    // with the drag phase as an exhaustively-matchable GraphicalUi.DragPhase.
    define_native(env, "GraphicalUi.on_drag_typed",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.on_drag_typed", args, 2, loc);
                      auto id = expect_string(args[0], "GraphicalUi.on_drag_typed", loc);
                      auto w = make_dict();
                      w->set(key::sub_type, Value{std::string{sub::drag}});
                      w->set(key::typed, Value{true});
                      w->set("id", Value{id});
                      w->set("event", Value{std::string{"*"}});
                      w->set(key::callback, args[1]);

                      return Value{std::move(w)};
                  });
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW
