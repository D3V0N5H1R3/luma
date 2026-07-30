#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

namespace luma::gui_detail {

// ═══════════════════════════════════════════════════════════
// Table-driven registration helpers (file-local)
// ═══════════════════════════════════════════════════════════

// Register a layout widget: name(children, style?) -> widget
// Covers: row, column, toolbar, wrapped_row, scroll_row, scroll_column, card.
static void register_layout_widget(const EnvPtr& env, const char* name, const char* type) {
    define_native(env, name,
                  [name, type](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args(name, args, 1, loc);
                      auto w = make_widget(type);
                      auto children = unwrap_array_arg(args[0]);

                      if (!children.is_null()) {
                          w->set("children", children);
                      }

                      w->set("style", get_style_arg(args, 1));
                      return finalize_widget(std::move(w));
                  });
}

// Register a nearby/overlay widget: name(child, overlay, style?) -> widget
// Covers: above, below, on_left, on_right, in_front, behind.
static void register_nearby_widget(const EnvPtr& env, const char* name, const char* position) {
    define_native(env, name,
                  [name, position](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args(name, args, 2, loc);
                      auto w = make_widget(wtype::nearby);
                      w->set("child", args[0]);
                      w->set("overlay", args[1]);
                      w->set("position", Value{std::string{position}});
                      w->set("style", get_style_arg(args, 2));
                      return finalize_widget(std::move(w));
                  });
}

// Register a style-dict constant: name() -> dict with key=value entries.
// The dictionary is built once during registration and returned by copy each call.
struct StyleEntry {
    const char* key;
    const char* value;
};

static void register_style_constant(const EnvPtr& env, const char* name, const StyleEntry* entries,
                                    std::size_t count) {
    auto cached = make_dict();

    for (std::size_t i = 0; i < count; ++i) {
        cached->set(entries[i].key, Value{std::string{entries[i].value}});
    }

    auto cached_value = Value{std::move(cached)};

    define_native(env, name,
                  [cached_value](std::span<const Value>, SourceLocation /*loc*/) -> Value {
                      return cached_value;
                  });
}

// Register a toggle-style widget: name(label, checked, on_toggle, style?) -> widget.
// Covers: toggle, switch.
static void register_toggle_widget(const EnvPtr& env, const char* name, const char* type) {
    define_native(env, name,
                  [name, type](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args(name, args, 3, loc);
                      auto w = make_widget(type);
                      w->set("label", args[0]);
                      w->set("checked", args[1]);
                      w->set(key::deferred_callback, args[2]);
                      w->set("style", get_style_arg(args, 3));
                      return finalize_widget(std::move(w));
                  });
}

// ═══════════════════════════════════════════════════════════
// Per-widget registration helpers (file-local)
//
// Each registers one GraphicalUi.<name> builtin. The inline body was
// hoisted out of register_layout_widgets so that function reads as a
// thin registration table; see the signature comment at each call site.
// ═══════════════════════════════════════════════════════════

static void register_panel(const EnvPtr& env) {
    define_native(env, "GraphicalUi.panel",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.panel", args, 2, loc);

                      auto w = make_widget(wtype::panel);
                      w->set("title", args[0]);

                      auto children = unwrap_array_arg(args[1]);

                      if (!children.is_null()) {
                          w->set("children", children);
                      }

                      w->set("style", get_style_arg(args, 2));

                      return finalize_widget(std::move(w));
                  });
}

static void register_list(const EnvPtr& env) {
    define_native(env, "GraphicalUi.list",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.list", args, 1, loc);

                      auto w = make_widget(wtype::list);
                      auto items = unwrap_array_arg(args[0]);
                      w->set("items", items.is_null() ? args[0] : items);

                      if (args.size() >= 2 && args[1].is_callable()) {
                          // Store callback for deferred binding in finalize_widget.
                          w->set(key::deferred_callback, args[1]);
                          w->set("style", get_style_arg(args, 2));
                      } else {
                          w->set("style", get_style_arg(args, 1));
                      }

                      return finalize_widget(std::move(w));
                  });
}

static void register_radio_group(const EnvPtr& env) {
    define_native(env, "GraphicalUi.radio_group",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.radio_group", args, 3, loc);

                      auto w = make_widget(wtype::radio_group);
                      auto options = unwrap_array_arg(args[0]);
                      w->set("options", options.is_null() ? args[0] : options);
                      w->set("selected", args[1]);

                      // Store callback for deferred binding in finalize_widget.
                      w->set(key::deferred_callback, args[2]);
                      w->set("style", get_style_arg(args, 3));

                      return finalize_widget(std::move(w));
                  });
}

static void register_tabs(const EnvPtr& env) {
    define_native(env, "GraphicalUi.tabs",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.tabs", args, 4, loc);

                      auto w = make_widget(wtype::tabs);
                      auto labels = unwrap_array_arg(args[0]);
                      w->set("labels", labels.is_null() ? args[0] : labels);
                      w->set("active", args[1]);

                      // Store callback for deferred binding in finalize_widget.
                      w->set(key::deferred_callback, args[2]);

                      auto children = unwrap_array_arg(args[3]);

                      if (!children.is_null()) {
                          w->set("children", children);
                      }

                      w->set("style", get_style_arg(args, 4));

                      return finalize_widget(std::move(w));
                  });
}

static void register_table(const EnvPtr& env) {
    define_native(
        env, "GraphicalUi.table", [](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_min_args("GraphicalUi.table", args, 2, loc);

            auto w = make_widget(wtype::table);
            auto headers = unwrap_array_arg(args[0]);
            w->set("headers", headers.is_null() ? args[0] : headers);
            auto rows = unwrap_array_arg(args[1]);
            w->set("rows", rows.is_null() ? args[1] : rows);

            std::size_t style_index = 2;

            if (args.size() >= 3 && args[2].is_callable()) {
                // Store callback for deferred binding in finalize_widget.
                w->set(key::deferred_callback, args[2]);
                style_index = 3;
            }

            auto style = split_widget_options(
                *w, get_style_arg(args, style_index),
                {"align", "selected", "sort_column", "sort_direction", "on_sort"});

            // A callable `on_sort` is re-bound as a deferred sort callback so
            // header clicks dispatch the clicked column index through update.
            if (auto* on_sort = w->find("on_sort"); on_sort != nullptr && on_sort->is_callable()) {
                w->set(key::deferred_sort_callback, *on_sort);
            }

            w->erase("on_sort");
            w->set("style", Value{std::move(style)});

            return finalize_widget(std::move(w));
        });
}

static void register_dialog(const EnvPtr& env) {
    define_native(env, "GraphicalUi.dialog",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.dialog", args, 3, loc);

                      auto w = make_widget(wtype::dialog);
                      w->set("title", args[0]);

                      auto children = unwrap_array_arg(args[1]);

                      if (!children.is_null()) {
                          w->set("children", children);
                      }

                      w->set("is_open", args[2]);

                      if (args.size() >= 4 && args[3].is_callable()) {
                          // Store close callback for deferred binding in finalize_widget.
                          w->set(key::deferred_close_callback, args[3]);
                          w->set("style", get_style_arg(args, 4));
                      } else {
                          w->set("style", get_style_arg(args, 3));
                      }

                      return finalize_widget(std::move(w));
                  });
}

static void register_alert(const EnvPtr& env) {
    define_native(env, "GraphicalUi.alert",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.alert", args, 1, loc);

                      auto w = make_widget(wtype::alert);
                      w->set("message", args[0]);

                      if (args.size() >= 2 && args[1].is_string()) {
                          w->set("severity", args[1]);
                          w->set("style", get_style_arg(args, 2));
                      } else {
                          w->set("severity", Value{std::string{"info"}});
                          w->set("style", get_style_arg(args, 1));
                      }

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.alert_of(message, severity, style?) -> widget
    // Typed companion to alert: takes a GraphicalUi.Severity choice instead of a
    // severity string, lowering it to the same widget the string form builds.
    define_native(env, "GraphicalUi.alert_of",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.alert_of", args, 2, loc);

                      auto w = make_widget(wtype::alert);
                      w->set("message", args[0]);
                      w->set("severity", Value{severity_to_lower(args[1])});
                      w->set("style", get_style_arg(args, 2));

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.severity_to_string(severity) -> string
    // Bridge from the GraphicalUi.Severity choice to the "info"/"warning"/
    // "error"/"success" string accepted by the string-based alert/toast API and
    // matching the GraphicalUi.INFO/WARNING/ERROR/SUCCESS constants.
    define_native(env, "GraphicalUi.severity_to_string",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.severity_to_string", args, 1, loc);
                      return Value{severity_to_lower(args[0])};
                  });
}

static void register_menu(const EnvPtr& env) {
    define_native(env, "GraphicalUi.menu",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.menu", args, 3, loc);

                      auto w = make_widget(wtype::menu);
                      w->set("label", args[0]);
                      auto items = unwrap_array_arg(args[1]);
                      w->set("items", items.is_null() ? args[1] : items);

                      // Store select callback for deferred binding in finalize_widget.
                      w->set(key::deferred_callback, args[2]);
                      w->set("style", get_style_arg(args, 3));

                      return finalize_widget(std::move(w));
                  });
}

static void register_popover(const EnvPtr& env) {
    define_native(env, "GraphicalUi.popover",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.popover", args, 2, loc);

                      auto w = make_widget(wtype::popover);
                      w->set("label", args[0]);
                      w->set("child", args[1]);
                      w->set("style", get_style_arg(args, 2));

                      return finalize_widget(std::move(w));
                  });
}

static void register_combobox(const EnvPtr& env) {
    define_native(env, "GraphicalUi.combobox",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.combobox", args, 3, loc);

                      auto w = make_widget(wtype::combobox);
                      w->set("value", args[0]);
                      auto options = unwrap_array_arg(args[1]);
                      w->set("options", options.is_null() ? args[1] : options);

                      // Typing fires on_change (bound as _callback_id).
                      w->set(key::deferred_callback, args[2]);

                      if (args.size() >= 4 && args[3].is_callable()) {
                          // Choosing an option fires on_select (bound as _select_id).
                          w->set(key::deferred_select_callback, args[3]);
                          w->set("style", get_style_arg(args, 4));
                      } else {
                          w->set("style", get_style_arg(args, 3));
                      }

                      return finalize_widget(std::move(w));
                  });
}

static void register_field(const EnvPtr& env) {
    define_native(env, "GraphicalUi.field",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.field", args, 2, loc);

                      auto w = make_widget(wtype::field);
                      w->set("label", args[0]);
                      w->set("child", args[1]);

                      if (args.size() >= 3 && args[2].is_dictionary()) {
                          const auto& opts = *args[2].as_dictionary();

                          if (const auto* r = opts.find("required"); r != nullptr && r->is_bool()) {
                              w->set("required", *r);
                          }

                          if (const auto* h = opts.find("help"); h != nullptr && h->is_string()) {
                              w->set("help", *h);
                          }

                          if (const auto* e = opts.find("error"); e != nullptr && e->is_string()) {
                              w->set("error", *e);
                          }
                      }

                      w->set("style", get_style_arg(args, 3));

                      return finalize_widget(std::move(w));
                  });
}

static void register_confirm(const EnvPtr& env) {
    define_native(
        env, "GraphicalUi.confirm", [](std::span<const Value> args, SourceLocation loc) -> Value {
            expect_min_args("GraphicalUi.confirm", args, 3, loc);

            auto w = make_widget(wtype::confirm);
            w->set("title", args[0]);
            w->set("message", args[1]);
            w->set(key::deferred_callback, args[2]);

            std::size_t next = 3;

            if (args.size() > next && args[next].is_callable()) {
                w->set(key::deferred_close_callback, args[next]);
                ++next;
            }

            if (args.size() > next && args[next].is_dictionary()) {
                const auto& opts = *args[next].as_dictionary();

                if (const auto* c = opts.find("confirm_label"); c != nullptr && c->is_string()) {
                    w->set("confirm_label", *c);
                }

                if (const auto* c = opts.find("cancel_label"); c != nullptr && c->is_string()) {
                    w->set("cancel_label", *c);
                }

                if (const auto* d = opts.find("danger"); d != nullptr && d->is_bool()) {
                    w->set("danger", *d);
                }

                ++next;
            }

            w->set("style", get_style_arg(args, next));

            return finalize_widget(std::move(w));
        });
}

static void register_link(const EnvPtr& env) {
    define_native(env, "GraphicalUi.link",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.link", args, 2, loc);

                      auto w = make_widget(wtype::link);
                      w->set("text", args[0]);

                      if (args[1].is_string()) {
                          w->set("href", args[1]);
                      } else if (args[1].is_callable()) {
                          // Store callback for deferred binding in finalize_widget.
                          w->set(key::deferred_callback, args[1]);
                      }

                      w->set("style", get_style_arg(args, 2));

                      return finalize_widget(std::move(w));
                  });
}

static void register_icon(const EnvPtr& env) {
    define_native(env, "GraphicalUi.icon",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.icon", args, 1, loc);

                      auto w = make_widget(wtype::icon);
                      w->set("name", args[0]);
                      if (args.size() >= 2 && args[1].is_integer()) {
                          w->set("size", args[1]);
                          w->set("style", get_style_arg(args, 2));
                      } else {
                          w->set("style", get_style_arg(args, 1));
                      }

                      return finalize_widget(std::move(w));
                  });
}

static void register_grid(const EnvPtr& env) {
    define_native(env, "GraphicalUi.grid",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.grid", args, 2, loc);

                      auto w = make_widget(wtype::grid);
                      auto children = unwrap_array_arg(args[1]);

                      if (!children.is_null()) {
                          w->set("children", children);
                      }

                      w->set("columns", args[0]);
                      w->set("style", get_style_arg(args, 2));

                      return finalize_widget(std::move(w));
                  });
}

static void register_transition(const EnvPtr& env) {
    define_native(env, "GraphicalUi.transition",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.transition", args, 2, loc);

                      auto w = make_widget(wtype::transition);
                      w->set("child", args[0]);
                      (void)expect_dict(args[1], "GraphicalUi.transition", loc);
                      w->set("properties", args[1]);
                      w->set("style", Value{make_dict()});

                      return finalize_widget(std::move(w));
                  });
}

static void register_animate(const EnvPtr& env) {
    define_native(env, "GraphicalUi.animate",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.animate", args, 2, loc);

                      auto w = make_widget(wtype::animate);
                      w->set("child", args[0]);
                      w->set("keyframes", args[1]);
                      w->set("options", get_style_arg(args, 2));
                      w->set("style", Value{make_dict()});

                      return finalize_widget(std::move(w));
                  });
}

static void register_fill(const EnvPtr& env) {
    define_native(env, "GraphicalUi.fill",
                  [](std::span<const Value>, SourceLocation /*loc*/) -> Value {
                      return Value{std::string{"1 1 0%"}};
                  });
}

static void register_fill_portion(const EnvPtr& env) {
    define_native(env, "GraphicalUi.fill_portion",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.fill_portion", args, 1, loc);
                      auto n = args[0].as_integer();
                      return Value{std::to_string(n) + " 1 0%"};
                  });
}

static void register_shrink(const EnvPtr& env) {
    define_native(env, "GraphicalUi.shrink",
                  [](std::span<const Value>, SourceLocation /*loc*/) -> Value {
                      return Value{std::string{"0 1 auto"}};
                  });
}

static void register_px(const EnvPtr& env) {
    define_native(env, "GraphicalUi.px",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.px", args, 1, loc);
                      auto n = args[0].as_integer();
                      return Value{std::to_string(n) + "px"};
                  });
}

static void register_constrained_fill(const EnvPtr& env) {
    define_native(env, "GraphicalUi.constrained_fill",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.constrained_fill", args, 2, loc);

                      auto min_val = args[0].as_integer();
                      auto max_val = args[1].as_integer();

                      auto d = make_dict();
                      d->set("flex", Value{std::string{"1 1 0%"}});
                      d->set("min_width", Value{std::to_string(min_val) + "px"});
                      d->set("max_width", Value{std::to_string(max_val) + "px"});

                      return Value{std::move(d)};
                  });
}

static void register_spacing(const EnvPtr& env) {
    define_native(env, "GraphicalUi.spacing",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.spacing", args, 1, loc);
                      auto n = args[0].as_integer();
                      auto d = make_dict();
                      d->set("gap", Value{std::to_string(n) + "px"});
                      return Value{std::move(d)};
                  });
}

static void register_spacing_xy(const EnvPtr& env) {
    define_native(env, "GraphicalUi.spacing_xy",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.spacing_xy", args, 2, loc);
                      auto x = args[0].as_integer();
                      auto y = args[1].as_integer();
                      auto d = make_dict();
                      d->set("column_gap", Value{std::to_string(x) + "px"});
                      d->set("row_gap", Value{std::to_string(y) + "px"});
                      return Value{std::move(d)};
                  });
}

static void register_padding(const EnvPtr& env) {
    define_native(env, "GraphicalUi.padding",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.padding", args, 1, loc);
                      auto n = args[0].as_integer();
                      auto d = make_dict();
                      d->set("padding", Value{std::to_string(n) + "px"});
                      return Value{std::move(d)};
                  });
}

static void register_padding_xy(const EnvPtr& env) {
    define_native(env, "GraphicalUi.padding_xy",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.padding_xy", args, 2, loc);
                      auto x = args[0].as_integer();
                      auto y = args[1].as_integer();
                      auto d = make_dict();
                      d->set("padding",
                             Value{std::to_string(y) + "px " + std::to_string(x) + "px"});
                      return Value{std::move(d)};
                  });
}

static void register_classify_device(const EnvPtr& env) {
    define_native(env, "GraphicalUi.classify_device",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.classify_device", args, 2, loc);

                      auto width = args[0].as_integer();
                      auto height = args[1].as_integer();

                      std::string device_class;

                      if (width < 640) {
                          device_class = "phone";
                      } else if (width < 1024) {
                          device_class = "tablet";
                      } else if (width < 1920) {
                          device_class = "desktop";
                      } else {
                          device_class = "big_desktop";
                      }

                      std::string orientation = (width >= height) ? "landscape" : "portrait";

                      auto d = make_dict();
                      d->set("class", Value{std::move(device_class)});
                      d->set("orientation", Value{std::move(orientation)});
                      d->set("width", args[0]);
                      d->set("height", args[1]);

                      return Value{std::move(d)};
                  });
}

// GraphicalUi.classify_device_typed(width, height) -> GraphicalUi.DeviceInfo
// Typed companion to classify_device: reuses the identical breakpoint thresholds
// but returns a DeviceInfo record whose `class` / `orientation` are closed
// choices (GraphicalUi.DeviceClass / GraphicalUi.Orientation) rather than magic
// strings, so responsive layout becomes an exhaustive match.  width / height are
// `integer` (discrete pixel counts).
static void register_classify_device_typed(const EnvPtr& env) {
    define_native(env, "GraphicalUi.classify_device_typed",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.classify_device_typed", args, 2, loc);

                      auto width = args[0].as_integer();
                      auto height = args[1].as_integer();

                      auto device_class = std::make_shared<ChoiceValue>();
                      device_class->type_name = "DeviceClass";

                      if (width < 640) {
                          device_class->variant = "Phone";
                      } else if (width < 1024) {
                          device_class->variant = "Tablet";
                      } else if (width < 1920) {
                          device_class->variant = "Desktop";
                      } else {
                          device_class->variant = "BigDesktop";
                      }

                      auto orientation = std::make_shared<ChoiceValue>();
                      orientation->type_name = "Orientation";
                      orientation->variant = (width >= height) ? "Landscape" : "Portrait";

                      auto rec = std::make_shared<RecordValue>();
                      rec->type_name = "DeviceInfo";
                      rec->fields.emplace_back("class", Value{std::move(device_class)});
                      rec->fields.emplace_back("orientation", Value{std::move(orientation)});
                      rec->fields.emplace_back("width", args[0]);
                      rec->fields.emplace_back("height", args[1]);

                      return Value{std::move(rec)};
                  });
}

// ═══════════════════════════════════════════════════════════
// Layout, overlay, animation, sizing, alignment, and spacing
// ═══════════════════════════════════════════════════════════

void register_layout_widgets(const EnvPtr& env) {
    // ─── Layout widgets (table-driven) ───────────────────

    register_layout_widget(env, "GraphicalUi.row", wtype::row);
    register_layout_widget(env, "GraphicalUi.column", wtype::column);

    // GraphicalUi.panel(title, children, style?) -> widget
    register_panel(env);

    // GraphicalUi.list(items, on_select?, style?) -> widget
    register_list(env);

    // GraphicalUi.radio_group(options, selected, on_select, style?) -> widget
    register_radio_group(env);

    // GraphicalUi.toggle(label, checked, on_toggle, style?) -> widget
    register_toggle_widget(env, "GraphicalUi.toggle", wtype::toggle);

    // GraphicalUi.switch(label, checked, on_toggle, style?) -> widget
    // Visually distinct from toggle — renders as an iOS-style sliding switch.
    register_toggle_widget(env, "GraphicalUi.switch", wtype::switch_);

    // GraphicalUi.tabs(labels, active_index, on_select, children, style?) -> widget
    register_tabs(env);

    // GraphicalUi.table(headers, rows, on_row_click?, options?) -> widget
    //
    // The trailing dictionary is both a style bag and a carrier for table
    // options: per-column `align` (array of "left"/"right"/"center"), a
    // `selected` row (integer or array of integers), and sorting via `on_sort`
    // (a callable receiving the clicked column index) together with the current
    // `sort_column` / `sort_direction`. Sticky headers and zebra striping are
    // applied by the stylesheet and need no options.
    register_table(env);

    // GraphicalUi.dialog(title, children, is_open, on_close?, style?) -> widget
    register_dialog(env);

    // GraphicalUi.alert(message, severity?, style?) -> widget
    register_alert(env);

    // GraphicalUi.tooltip(text, child, style?) -> widget
    register_simple_widget(env, "GraphicalUi.tooltip", wtype::tooltip,
                           {{.name = "text"}, {.name = "child"}});

    // GraphicalUi.menu(label, items, on_select, style?) -> widget
    // A button that discloses a list of actions; on_select receives the chosen
    // item.  The renderer makes the trigger and every item keyboard-operable.
    register_menu(env);

    // GraphicalUi.popover(label, content, style?) -> widget
    // A button that discloses a floating panel of arbitrary content; open/close
    // is managed by the renderer (click, outside-click, and Escape).
    register_popover(env);

    // GraphicalUi.combobox(value, options, on_change, on_select?, style?) -> widget
    // A text input paired with a filterable listbox.  Typing fires on_change
    // (so the host can filter `options` and re-render); choosing an option fires
    // on_select.  When on_select is omitted, the commit reuses on_change.
    register_combobox(env);

    // GraphicalUi.field(label, control, options?) -> widget
    // Wraps a single control in an accessible <label> (implicit association),
    // so any input gains a real, persistent, programmatically-linked label
    // instead of relying on placeholder text.  The optional `options` dict
    // carries validation affordances: {required, help, error}.
    register_field(env);

    // GraphicalUi.confirm(title, message, on_confirm, on_cancel?, options?) -> widget
    // A modal confirmation dialog for (destructive) actions.  Reuses the dialog's
    // accessibility machinery: on_confirm binds to the primary _callback_id and
    // on_cancel to _close_id (also fired by Escape and the backdrop).  The
    // optional `options` dict carries {confirm_label, cancel_label, danger}.
    register_confirm(env);

    // GraphicalUi.link(text, on_click_or_url, style?) -> widget
    register_link(env);

    // GraphicalUi.icon(name, size?, style?) -> widget
    register_icon(env);

    // GraphicalUi.toolbar — registered via register_layout_widget.
    register_layout_widget(env, "GraphicalUi.toolbar", wtype::toolbar);

    // GraphicalUi.grid(columns, children, style?) -> widget
    register_grid(env);

    // ─── Layout helpers (elm-ui inspired, table-driven) ─

    register_layout_widget(env, "GraphicalUi.wrapped_row", wtype::wrapped_row);
    register_layout_widget(env, "GraphicalUi.scroll_row", wtype::scroll_row);
    register_layout_widget(env, "GraphicalUi.scroll_column", wtype::scroll_column);

    // ─── Nearby/overlay widgets (table-driven) ─────────

    register_nearby_widget(env, "GraphicalUi.above", pos::above);
    register_nearby_widget(env, "GraphicalUi.below", pos::below);
    register_nearby_widget(env, "GraphicalUi.on_left", pos::left);
    register_nearby_widget(env, "GraphicalUi.on_right", pos::right);
    register_nearby_widget(env, "GraphicalUi.in_front", pos::in_front);
    register_nearby_widget(env, "GraphicalUi.behind", pos::behind);

    // GraphicalUi.debug(child, style?) -> widget
    register_simple_widget(env, "GraphicalUi.debug", wtype::debug, {{.name = "child"}});

    // ─── Animation primitives ────────────────────────────

    // GraphicalUi.transition(child, properties) -> widget
    register_transition(env);

    // GraphicalUi.animate(child, keyframes, options?) -> widget
    register_animate(env);

    // ─── Typed sizing helpers ────────────────────────────

    register_fill(env);

    register_fill_portion(env);

    register_shrink(env);

    register_px(env);

    register_constrained_fill(env);

    // ─── Typed alignment helpers (table-driven) ────────

    static constexpr StyleEntry center_entries[] = {{.key = "align_items", .value = "center"},
                                                    {.key = "justify_content", .value = "center"}};
    register_style_constant(env, "GraphicalUi.center", center_entries, 2);

    static constexpr StyleEntry center_x_entries[] = {
        {.key = "justify_content", .value = "center"}};
    register_style_constant(env, "GraphicalUi.center_x", center_x_entries, 1);

    static constexpr StyleEntry center_y_entries[] = {{.key = "align_items", .value = "center"}};
    register_style_constant(env, "GraphicalUi.center_y", center_y_entries, 1);

    static constexpr StyleEntry align_left_entries[] = {
        {.key = "align_self", .value = "flex-start"}};
    register_style_constant(env, "GraphicalUi.align_left", align_left_entries, 1);

    static constexpr StyleEntry align_right_entries[] = {
        {.key = "align_self", .value = "flex-end"}};
    register_style_constant(env, "GraphicalUi.align_right", align_right_entries, 1);

    static constexpr StyleEntry align_top_entries[] = {
        {.key = "align_self", .value = "flex-start"}};
    register_style_constant(env, "GraphicalUi.align_top", align_top_entries, 1);

    static constexpr StyleEntry align_bottom_entries[] = {
        {.key = "align_self", .value = "flex-end"}};
    register_style_constant(env, "GraphicalUi.align_bottom", align_bottom_entries, 1);

    // ─── Spacing helpers (table-driven) ──────────────────

    static constexpr StyleEntry space_evenly_entries[] = {
        {.key = "justify_content", .value = "space-evenly"}};
    register_style_constant(env, "GraphicalUi.space_evenly", space_evenly_entries, 1);

    static constexpr StyleEntry space_between_entries[] = {
        {.key = "justify_content", .value = "space-between"}};
    register_style_constant(env, "GraphicalUi.space_between", space_between_entries, 1);

    static constexpr StyleEntry space_around_entries[] = {
        {.key = "justify_content", .value = "space-around"}};
    register_style_constant(env, "GraphicalUi.space_around", space_around_entries, 1);

    register_spacing(env);

    register_spacing_xy(env);

    register_padding(env);

    register_padding_xy(env);

    // ─── Device classification ───────────────────────────

    register_classify_device(env);
    register_classify_device_typed(env);

    // GraphicalUi.card(children, style?) -> widget
    register_layout_widget(env, "GraphicalUi.card", wtype::card);
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW
