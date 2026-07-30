#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

#include <functional> // std::hash<std::string> for component memoization.

namespace luma::gui_detail {

// ═══════════════════════════════════════════════════════════
// Advanced widgets: components, routing, accessibility,
// forms, drag-drop, theming, data, and developer tools
// ═══════════════════════════════════════════════════════════

static void register_component_widgets(const EnvPtr& env) {
    // ─── Custom components (with hash-based memoization) ─

    // GraphicalUi.component(id, model_slice, render_fn) -> widget
    define_native(env, "GraphicalUi.component",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.component", args, 3, loc);

                      if (!args[2].is_callable()) {
                          throw RuntimeError{
                              "GraphicalUi.component: third argument must be a function", loc,
                              "pass a function value, e.g. component(\"id\", model, render_fn)"};
                      }

                      auto comp_id = expect_string(args[0], "GraphicalUi.component", loc);
                      auto slice_json = value_to_json(args[1]);
                      auto slice_hash = std::hash<std::string>{}(slice_json);

                      // Check the component cache for a memoized result (hash comparison).
                      // Note: lookup and insert are separate critical sections because
                      // invoke_callable (between them) may re-enter the mutex via
                      // nested widget construction.  A cache miss between the two locks
                      // can only cause a redundant render, not a correctness issue.
                      if (active_app) {
                          const std::scoped_lock lock{active_app->mutex};
                          auto* cached = active_app->find_cached_component(comp_id);

                          if (cached != nullptr && cached->slice_hash == slice_hash) {
                              return cached->rendered;
                          }
                      }

                      std::vector<Value> render_args{args[1]};
                      auto result = invoke_callable(args[2], render_args, loc);

                      // Cache the rendered result with its hash (LRU eviction if full).
                      if (active_app) {
                          const std::scoped_lock lock{active_app->mutex};
                          active_app->cache_component(comp_id, slice_hash, result);
                      }

                      return result;
                  });

    // GraphicalUi.error_boundary(fallback_fn, view_fn) -> widget
    define_native(
        env, "GraphicalUi.error_boundary",
        [](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_args("GraphicalUi.error_boundary", args, 2, loc);

            if (!args[0].is_callable()) {
                throw RuntimeError{"GraphicalUi.error_boundary: first argument must be a function",
                                   loc, "pass a fallback function as the first argument"};
            }

            if (!args[1].is_callable()) {
                throw RuntimeError{"GraphicalUi.error_boundary: second argument must be a function",
                                   loc, "pass a view function as the second argument"};
            }

            try {
                std::vector<Value> view_args;
                auto result = invoke_callable(args[1], view_args, loc);

                // If inside app context, wrap in an error_boundary widget
                // so the renderer can handle retry.
                if (active_app) {
                    auto w = make_widget(wtype::error_boundary);
                    w->set("child", result);
                    w->set("style", Value{make_dict()});

                    // Store both functions for retry support.
                    w->set(key::deferred_callback, args[1]);
                    auto retry_id = active_app->allocate_id();
                    active_app->register_callback(retry_id, args[1]);
                    w->set("_retry_view_id", Value{retry_id});

                    return finalize_widget(std::move(w));
                }

                return result;
            } catch (const std::exception& ex) {
                // Invoke the fallback with the error message.
                std::vector<Value> fallback_args{Value{std::string{ex.what()}}};
                auto fallback_result = invoke_callable(args[0], fallback_args, loc);

                if (active_app) {
                    auto w = make_widget(wtype::error_boundary);
                    w->set("child", fallback_result);
                    w->set("_error", Value{std::string{ex.what()}});
                    w->set("style", Value{make_dict()});

                    // Register a retry callback that re-invokes the view fn.
                    auto retry_id = active_app->allocate_id();
                    active_app->register_callback(retry_id, args[1]);
                    w->set("_retry_id", Value{retry_id});

                    return finalize_widget(std::move(w));
                }

                return fallback_result;
            }
        });
}

static void register_navigation_widgets(const EnvPtr& env) {
    // ─── Routing / navigation ────────────────────────────

    define_native(env, "GraphicalUi.router",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.router", args, 2, loc);
                      auto route_str = expect_string(args[0], "GraphicalUi.router", loc);
                      (void)expect_dict(args[1], "GraphicalUi.router", loc);

                      const auto& routes = *args[1].as_dictionary();

                      // 1. Try exact match first.
                      const auto* match = routes.find(route_str);

                      // 2. Try parameterised route patterns (e.g. "/user/{id}").
                      std::shared_ptr<DictionaryValue> params;

                      if (!match) {
                          for (const auto& [pattern, handler] : routes.entries) {
                              // Parameterised patterns use either "{id}" or
                              // Express-style ":id" segments; skip plain patterns.
                              if (pattern.find('{') == std::string::npos &&
                                  pattern.find(':') == std::string::npos) {
                                  continue;
                              }

                              auto pattern_parts = split_path(pattern);
                              auto route_parts = split_path(route_str);

                              if (pattern_parts.size() != route_parts.size()) {
                                  continue;
                              }

                              bool matched = true;
                              auto candidate_params = make_dict();

                              for (std::size_t i = 0; i < pattern_parts.size(); ++i) {
                                  const auto& pp = pattern_parts[i];
                                  const auto& rp = route_parts[i];

                                  if (pp.size() >= 3 && pp.front() == '{' && pp.back() == '}') {
                                      auto name = pp.substr(1, pp.size() - 2);
                                      candidate_params->set(name, Value{rp});
                                  } else if (pp.size() >= 2 && pp.front() == ':') {
                                      auto name = pp.substr(1);
                                      candidate_params->set(name, Value{rp});
                                  } else if (pp != rp) {
                                      matched = false;
                                      break;
                                  }
                              }

                              if (matched) {
                                  match = &handler;
                                  params = std::move(candidate_params);
                                  break;
                              }
                          }
                      }

                      if (!match) {
                          auto w = make_widget(wtype::label);
                          w->set("text", Value{std::string{"Route not found: "} + route_str});
                          w->set("style", Value{make_dict()});
                          return finalize_widget(std::move(w));
                      }

                      if (match->is_callable()) {
                          std::vector<Value> view_args;

                          if (params) {
                              view_args.emplace_back(std::move(params));
                          }

                          return invoke_callable(*match, view_args, loc);
                      }

                      return *match;
                  });

    define_native(env, "GraphicalUi.navigate",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.navigate", args, 1, loc);
                      auto route = expect_string(args[0], "GraphicalUi.navigate", loc);
                      auto w = make_command_dict(cmd::navigate);
                      w->set("route", Value{route});
                      return Value{std::move(w)};
                  });

    define_native(env, "GraphicalUi.navigate_back",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.navigate_back", args, 0, loc);
                      auto w = make_command_dict(cmd::navigate_back);
                      return Value{std::move(w)};
                  });

    define_native(
        env, "GraphicalUi.navigation_link",
        [](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_min_args("GraphicalUi.navigation_link", args, 2, loc);

            auto text = expect_string(args[0], "GraphicalUi.navigation_link", loc);
            auto w = make_widget(wtype::navigation_link);
            w->set("text", Value{text});

            // Wrap the message in a callback for deferred binding.
            auto msg = args[1];
            w->set(key::deferred_callback,
                   Value{std::make_shared<NativeFunctionValue>(
                       "navigation_link_cb",
                       [msg](std::span<const Value>, SourceLocation) -> Value { return msg; })});

            w->set("style", get_style_arg(args, 2));
            return finalize_widget(std::move(w));
        });
}

static void register_accessibility_widgets(const EnvPtr& env) {
    // ─── Accessibility ───────────────────────────────────

    define_native(env, "GraphicalUi.accessible",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.accessible", args, 2, loc);
                      auto w = make_widget(wtype::accessible);
                      w->set("child", args[0]);

                      if (args[1].is_dictionary()) {
                          w->set("attributes", args[1]);
                      }

                      w->set("style", Value{make_dict()});
                      return finalize_widget(std::move(w));
                  });

    define_native(env, "GraphicalUi.keyed",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.keyed", args, 2, loc);
                      auto key = expect_string(args[0], "GraphicalUi.keyed", loc);
                      auto w = make_widget(wtype::keyed);
                      w->set("_key", Value{key});
                      w->set("child", args[1]);
                      w->set("style", Value{make_dict()});
                      return finalize_widget(std::move(w));
                  });

    define_native(env, "GraphicalUi.focus",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.focus", args, 1, loc);
                      auto id = expect_string(args[0], "GraphicalUi.focus", loc);
                      auto w = make_command_dict(cmd::focus);
                      w->set("widget_id", Value{id});
                      return Value{std::move(w)};
                  });

    define_native(env, "GraphicalUi.announce",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.announce", args, 1, loc);
                      auto text = expect_string(args[0], "GraphicalUi.announce", loc);
                      auto w = make_command_dict(cmd::announce);
                      w->set("text", Value{text});
                      return Value{std::move(w)};
                  });

    // GraphicalUi.aria_live(level, widget) -> widget
    define_native(env, "GraphicalUi.aria_live",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.aria_live", args, 2, loc);
                      auto level = expect_string(args[0], "GraphicalUi.aria_live", loc);
                      auto w = make_widget(wtype::aria_live);
                      w->set("level", Value{level});
                      w->set("child", args[1]);
                      w->set("style", Value{make_dict()});
                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.aria_describedby(desc_id, widget) -> widget
    define_native(env, "GraphicalUi.aria_describedby",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.aria_describedby", args, 2, loc);
                      auto desc_id = expect_string(args[0], "GraphicalUi.aria_describedby", loc);
                      auto w = make_widget(wtype::aria_describedby);
                      w->set("desc_id", Value{desc_id});
                      w->set("child", args[1]);
                      w->set("style", Value{make_dict()});
                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.when(condition, widget) -> widget | null
    define_native(env, "GraphicalUi.when",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.when", args, 2, loc);

                      if (args[0].is_truthy()) {
                          return args[1];
                      }

                      return Value{NullValue{}};
                  });

    // ─── Virtual list (windowed rendering) ──────────────

    // GraphicalUi.virtual_list(items, item_height, visible_count, style?) -> widget
    define_native(env, "GraphicalUi.virtual_list",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.virtual_list", args, 3, loc);

                      auto w = make_widget(wtype::virtual_list);

                      auto items = unwrap_array_arg(args[0]);
                      w->set("items", items.is_null() ? args[0] : items);
                      w->set("item_height",
                             Value{expect_integer(args[1], "GraphicalUi.virtual_list", loc)});
                      w->set("visible_count",
                             Value{expect_integer(args[2], "GraphicalUi.virtual_list", loc)});
                      w->set("style", get_style_arg(args, 3));

                      return finalize_widget(std::move(w));
                  });
}

static void register_display_widgets(const EnvPtr& env) {
    // ─── New widgets ─────────────────────────────────────

    // GraphicalUi.badge(text, style?) -> widget
    register_simple_widget(env, "GraphicalUi.badge", wtype::badge, {{.name = "text"}});

    // GraphicalUi.accordion(sections, style?) -> widget
    define_native(env, "GraphicalUi.accordion",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.accordion", args, 1, loc);
                      auto w = make_widget(wtype::accordion);
                      auto sections = unwrap_array_arg(args[0]);
                      w->set("sections", sections.is_null() ? args[0] : sections);
                      w->set("style", get_style_arg(args, 1));
                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.breadcrumb(items, on_navigate?, style?) -> widget
    define_native(env, "GraphicalUi.breadcrumb",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.breadcrumb", args, 1, loc);
                      auto w = make_widget(wtype::breadcrumb);
                      auto items = unwrap_array_arg(args[0]);
                      w->set("items", items.is_null() ? args[0] : items);

                      if (args.size() >= 2 && args[1].is_callable()) {
                          w->set(key::deferred_callback, args[1]);
                          w->set("style", get_style_arg(args, 2));
                      } else {
                          w->set("style", get_style_arg(args, 1));
                      }

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.avatar(name, url?, style?) -> widget
    define_native(env, "GraphicalUi.avatar",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.avatar", args, 1, loc);
                      auto w = make_widget(wtype::avatar);
                      w->set("name", args[0]);

                      if (args.size() >= 2 && args[1].is_string()) {
                          w->set("url", args[1]);
                          w->set("style", get_style_arg(args, 2));
                      } else {
                          w->set("style", get_style_arg(args, 1));
                      }

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.skeleton(width?, height?, style?) -> widget
    define_native(env, "GraphicalUi.skeleton",
                  [](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
                      auto w = make_widget(wtype::skeleton);

                      if (args.size() >= 2 && args[0].is_integer() && args[1].is_integer()) {
                          w->set("width", args[0]);
                          w->set("height", args[1]);
                          w->set("style", get_style_arg(args, 2));
                      } else if (!args.empty() && args[0].is_integer()) {
                          w->set("width", args[0]);
                          w->set("style", get_style_arg(args, 1));
                      } else {
                          w->set("style", get_style_arg(args, 0));
                      }

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.number_input(value, min, max, on_change, style?) -> widget
    define_native(env, "GraphicalUi.number_input",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.number_input", args, 4, loc);
                      auto w = make_widget(wtype::number_input);
                      w->set("value", args[0]);
                      w->set("min", args[1]);
                      w->set("max", args[2]);
                      w->set(key::deferred_callback, args[3]);
                      w->set("style", get_style_arg(args, 4));
                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.search_input(value, on_change, on_clear?, style?) -> widget
    define_native(env, "GraphicalUi.search_input",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.search_input", args, 2, loc);
                      auto w = make_widget(wtype::search_input);
                      w->set("value", args[0]);
                      w->set(key::deferred_callback, args[1]);

                      if (args.size() >= 3 && args[2].is_callable()) {
                          w->set(key::deferred_clear_callback, args[2]);
                          w->set("style", get_style_arg(args, 3));
                      } else {
                          w->set("style", get_style_arg(args, 2));
                      }

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.toast(message, severity?, duration?, action_label?, on_action?, style?) -> widget
    define_native(
        env, "GraphicalUi.toast", [](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_min_args("GraphicalUi.toast", args, 1, loc);
            auto w = make_widget(wtype::toast);
            w->set("message", args[0]);

            std::size_t next = 1;

            // A string immediately followed by a callable is an action
            // label + handler, not a severity — so only consume a leading
            // string as severity when it is not part of an action pair.
            const bool severity_is_action =
                args.size() > next + 1 && args[next].is_string() && args[next + 1].is_callable();

            if (args.size() > next && args[next].is_string() && !severity_is_action) {
                w->set("severity", args[next]);
                ++next;
            } else {
                w->set("severity", Value{std::string{"info"}});
            }

            if (args.size() > next && args[next].is_integer()) {
                w->set("duration", args[next]);
                ++next;
            } else {
                w->set("duration", Value{std::int64_t{3000}});
            }

            // Optional action affordance: a label paired with a handler
            // (e.g. an "Undo" button). on_action binds to _action_id.
            if (args.size() > next + 1 && args[next].is_string() && args[next + 1].is_callable()) {
                w->set("action_label", args[next]);
                w->set(key::deferred_action_callback, args[next + 1]);
                next += 2;
            }

            w->set("style", get_style_arg(args, next));
            return finalize_widget(std::move(w));
        });

    // GraphicalUi.toast_of(message, severity, duration?, action_label?, on_action?, style?)
    //   -> widget
    // Typed companion to toast: takes a GraphicalUi.Severity choice instead of a
    // severity string, then reuses the same trailing arguments as toast (an
    // optional integer duration, an optional action_label + on_action pair, and
    // an optional trailing style dictionary).
    define_native(
        env, "GraphicalUi.toast_of", [](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_min_args("GraphicalUi.toast_of", args, 2, loc);
            auto w = make_widget(wtype::toast);
            w->set("message", args[0]);
            w->set("severity", Value{severity_to_lower(args[1])});

            std::size_t next = 2;

            if (args.size() > next && args[next].is_integer()) {
                w->set("duration", args[next]);
                ++next;
            } else {
                w->set("duration", Value{std::int64_t{3000}});
            }

            if (args.size() > next + 1 && args[next].is_string() && args[next + 1].is_callable()) {
                w->set("action_label", args[next]);
                w->set(key::deferred_action_callback, args[next + 1]);
                next += 2;
            }

            w->set("style", get_style_arg(args, next));
            return finalize_widget(std::move(w));
        });

    //
    // A fixed-position stacking region for transient notifications. Compose the
    // stack from GraphicalUi.toast(...) widgets and pass them as `toasts`; the
    // region positions them in a screen corner (the `position` option, default
    // "bottom-right"). Each toast keeps its own role/aria-live, so the region is
    // a plain labelled landmark rather than a second live region. Auto-dismiss
    // is the app's concern — schedule a removal message with GraphicalUi.delay.
    define_native(env, "GraphicalUi.toast_region",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.toast_region", args, 1, loc);
                      auto w = make_widget(wtype::toast_region);
                      auto children = unwrap_array_arg(args[0]);

                      if (!children.is_null()) {
                          w->set("children", children);
                      }

                      auto style = split_widget_options(*w, get_style_arg(args, 1), {"position"});
                      w->set("style", Value{std::move(style)});

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.empty_state(message, options?) -> widget
    //
    // A friendly placeholder for a blank list, panel, or search result — the
    // pattern the layout guide recommends instead of leaving a void. `message`
    // is the body line; the `options` dictionary carries an optional `title`,
    // an `icon` name (default "inbox"), and an optional call-to-action
    // (`action_label` + `on_action`). The action callback is bound like a
    // toast's, dispatching through the same update cycle.
    define_native(env, "GraphicalUi.empty_state",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.empty_state", args, 1, loc);
                      auto w = make_widget(wtype::empty_state);
                      w->set("message", args[0]);

                      auto style =
                          split_widget_options(*w, get_style_arg(args, 1),
                                               {"title", "icon", "action_label", "on_action"});

                      // A callable `on_action` becomes the deferred action callback so
                      // the optional button dispatches through update like a toast action.
                      if (auto* on_action = w->find("on_action");
                          on_action != nullptr && on_action->is_callable()) {
                          w->set(key::deferred_action_callback, *on_action);
                      }

                      w->erase("on_action");
                      w->set("style", Value{std::move(style)});

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.wizard(steps, active_step, on_step_change, style?) -> widget
    define_native(env, "GraphicalUi.wizard",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.wizard", args, 3, loc);
                      auto w = make_widget(wtype::wizard);
                      auto steps = unwrap_array_arg(args[0]);
                      w->set("steps", steps.is_null() ? args[0] : steps);
                      w->set("active_step", args[1]);
                      w->set(key::deferred_callback, args[2]);
                      w->set("style", get_style_arg(args, 3));
                      return finalize_widget(std::move(w));
                  });
}

static void register_form_and_dragdrop_widgets(const EnvPtr& env) {
    // ─── Form handling ───────────────────────────────────

    // GraphicalUi.form(children, on_submit, style?) -> widget
    define_native(env, "GraphicalUi.form",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.form", args, 2, loc);
                      auto w = make_widget(wtype::form);
                      auto children = unwrap_array_arg(args[0]);

                      if (!children.is_null()) {
                          w->set("children", children);
                      }

                      w->set(key::deferred_callback, args[1]);
                      w->set("style", get_style_arg(args, 2));
                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.field_error(message, style?) -> widget
    register_simple_widget(env, "GraphicalUi.field_error", wtype::field_error,
                           {{.name = "message"}});

    // ─── Drag and drop ───────────────────────────────────

    // GraphicalUi.draggable(child, data, style?) -> widget
    register_simple_widget(env, "GraphicalUi.draggable", wtype::draggable,
                           {{.name = "child"}, {.name = "data"}});

    // GraphicalUi.drop_target(child, on_drop, style?) -> widget
    define_native(env, "GraphicalUi.drop_target",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.drop_target", args, 2, loc);
                      auto w = make_widget(wtype::drop_target);
                      w->set("child", args[0]);
                      w->set(key::deferred_callback, args[1]);
                      w->set("style", get_style_arg(args, 2));
                      return finalize_widget(std::move(w));
                  });
}

static void register_theming_and_data_widgets(const EnvPtr& env) {
    // ─── Theming helpers ─────────────────────────────────

    // GraphicalUi.if_dark(dark_value, light_value) -> string
    define_native(env, "GraphicalUi.if_dark",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.if_dark", args, 2, loc);
                      auto dark_val = expect_string(args[0], "GraphicalUi.if_dark", loc);
                      auto light_val = expect_string(args[1], "GraphicalUi.if_dark", loc);
                      auto d = make_dict();
                      d->set("_theme_conditional", Value{std::string{"dark"}});
                      d->set("dark", Value{dark_val});
                      d->set("light", Value{light_val});
                      return Value{std::move(d)};
                  });

    // GraphicalUi.transition_preset(name) -> dictionary
    define_native(env, "GraphicalUi.transition_preset",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.transition_preset", args, 1, loc);
                      auto name = expect_string(args[0], "GraphicalUi.transition_preset", loc);

                      struct Preset {
                          const char* name;
                          const char* property;
                          const char* duration;
                          const char* easing;
                      };

                      static constexpr Preset presets[] = {
                          {.name = "ease", .property = "all", .duration = "0.3s", .easing = "ease"},
                          {.name = "spring",
                           .property = "all",
                           .duration = "0.5s",
                           .easing = "cubic-bezier(0.175, 0.885, 0.32, 1.275)"},
                          {.name = "bounce",
                           .property = "all",
                           .duration = "0.6s",
                           .easing = "cubic-bezier(0.68, -0.55, 0.265, 1.55)"},
                          {.name = "fade",
                           .property = "opacity",
                           .duration = "0.3s",
                           .easing = "ease-in-out"},
                          {.name = "slide",
                           .property = "transform",
                           .duration = "0.3s",
                           .easing = "ease-out"},
                          {.name = "scale",
                           .property = "transform",
                           .duration = "0.2s",
                           .easing = "ease-in-out"},
                      };

                      const Preset* found = nullptr;

                      for (const auto& p : presets) {
                          if (name == p.name) {
                              found = &p;
                              break;
                          }
                      }

                      // Default to "ease" for unknown names.
                      if (!found) {
                          found = &presets[0];
                      }

                      auto d = make_dict();
                      d->set("property", Value{std::string{found->property}});
                      d->set("duration", Value{std::string{found->duration}});
                      d->set("easing", Value{std::string{found->easing}});
                      return Value{std::move(d)};
                  });

    // ─── Data & state widgets ────────────────────────────

    // GraphicalUi.paginator(current_page, total_pages, on_page_change, style?) -> widget
    define_native(env, "GraphicalUi.paginator",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.paginator", args, 3, loc);
                      auto w = make_widget(wtype::paginator);
                      w->set("current_page", args[0]);
                      w->set("total_pages", args[1]);
                      w->set(key::deferred_callback, args[2]);
                      w->set("style", get_style_arg(args, 3));
                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.infinite_scroll(items, item_height, on_load_more, style?) -> widget
    define_native(env, "GraphicalUi.infinite_scroll",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.infinite_scroll", args, 3, loc);
                      auto w = make_widget(wtype::infinite_scroll);
                      auto items = unwrap_array_arg(args[0]);
                      w->set("items", items.is_null() ? args[0] : items);
                      w->set("item_height",
                             Value{expect_integer(args[1], "GraphicalUi.infinite_scroll", loc)});
                      w->set(key::deferred_callback, args[2]);
                      w->set("style", get_style_arg(args, 3));
                      return finalize_widget(std::move(w));
                  });

    // ─── Developer experience ────────────────────────────

    // GraphicalUi.inspect(child) -> widget
    register_simple_widget(env, "GraphicalUi.inspect", wtype::inspect, {{.name = "child"}});
}

// Advanced widgets namespace: wires up component, navigation, accessibility,
// display, form, and theming/data widget registrations.
void register_advanced_widgets(const EnvPtr& env) {
    register_component_widgets(env);
    register_navigation_widgets(env);
    register_accessibility_widgets(env);
    register_display_widgets(env);
    register_form_and_dragdrop_widgets(env);
    register_theming_and_data_widgets(env);
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW
