#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

namespace luma::gui_detail {

// ═══════════════════════════════════════════════════════════
// Basic / input widgets
// ═══════════════════════════════════════════════════════════

void register_basic_widgets(const EnvPtr& env) {
    // ─── Simple widgets (table-driven) ───────────────────

    // GraphicalUi.label(text, style?) -> widget
    register_simple_widget(env, "GraphicalUi.label", wtype::label, {{.name = "text"}});

    // GraphicalUi.button(label, on_click, style?) -> widget
    //
    // The optional trailing dictionary doubles as a style bag and a carrier for
    // the `variant` key (primary / secondary / ghost / danger). The variant is
    // lifted onto the widget so the renderer can pick a button hierarchy class
    // without the value leaking into the inline CSS.
    define_native(env, "GraphicalUi.button",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.button", args, 2, loc);

                      auto w = make_widget(wtype::button);
                      w->set("label", args[0]);
                      w->set(key::deferred_callback, args[1]);

                      auto style = split_widget_options(*w, get_style_arg(args, 2), {"variant"});
                      w->set("style", Value{std::move(style)});

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.button_of(label, on_click, variant, style?) -> widget
    // Typed companion to button: takes a GraphicalUi.ButtonVariant choice for the
    // button hierarchy instead of burying a "variant" string in the style
    // dictionary.  The variant is lifted onto the widget exactly as the string
    // form does; a trailing dictionary is treated purely as style.
    define_native(env, "GraphicalUi.button_of",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.button_of", args, 3, loc);

                      auto w = make_widget(wtype::button);
                      w->set("label", args[0]);
                      w->set(key::deferred_callback, args[1]);
                      w->set("variant", Value{button_variant_to_lower(args[2])});
                      w->set("style", get_style_arg(args, 3));

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.button_variant_to_string(variant) -> string
    // Bridge from the GraphicalUi.ButtonVariant choice to the "primary"/
    // "secondary"/"ghost"/"danger" style key accepted by the button style
    // dictionary and matching the GraphicalUi.PRIMARY/SECONDARY/GHOST/DANGER
    // constants.
    define_native(env, "GraphicalUi.button_variant_to_string",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args("GraphicalUi.button_variant_to_string", args, 1, loc);
                      return Value{button_variant_to_lower(args[0])};
                  });

    // GraphicalUi.text_area(value, on_change, placeholder?, on_commit?, style?) -> widget
    //
    // Mirrors text_input's trailing-argument handling: a string is the
    // placeholder, a second callable is the on_commit handler (fired on blur /
    // Ctrl+Enter, so a plain Enter still inserts a newline), a dictionary is the
    // style bag.
    define_native(env, "GraphicalUi.text_area",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.text_area", args, 2, loc);

                      auto w = make_widget(wtype::text_area);
                      w->set("value", args[0]);
                      w->set(key::deferred_callback, args[1]);

                      Value style{make_dict()};

                      for (std::size_t i = 2; i < args.size(); ++i) {
                          const auto& a = args[i];

                          if (a.is_string()) {
                              w->set("placeholder", a);
                          } else if (a.is_callable()) {
                              w->set(key::deferred_commit_callback, a);
                          } else if (a.is_dictionary()) {
                              style = a;
                          }
                      }

                      w->set("style", std::move(style));

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.progress(value, max, style?) -> widget
    register_simple_widget(env, "GraphicalUi.progress", wtype::progress,
                           {{.name = "value"}, {.name = "max"}});

    // GraphicalUi.image(source, style?) -> widget
    register_simple_widget(env, "GraphicalUi.image", wtype::image, {{.name = "source"}});

    // GraphicalUi.separator(style?) -> widget
    register_simple_widget(env, "GraphicalUi.separator", wtype::separator, {});

    // GraphicalUi.checkbox(label, checked, on_toggle, style?) -> widget
    register_simple_widget(
        env, "GraphicalUi.checkbox", wtype::checkbox,
        {{.name = "label"}, {.name = "checked"}, {.name = nullptr, .is_callback = true}});

    // GraphicalUi.dropdown(options, value, on_select, style?) -> widget
    register_simple_widget(
        env, "GraphicalUi.dropdown", wtype::dropdown,
        {{.name = "options"}, {.name = "value"}, {.name = nullptr, .is_callback = true}});

    // GraphicalUi.slider(value, min, max, on_change, style?) -> widget
    register_simple_widget(env, "GraphicalUi.slider", wtype::slider,
                           {{.name = "value"},
                            {.name = "min"},
                            {.name = "max"},
                            {.name = nullptr, .is_callback = true}});

    // GraphicalUi.date_picker(value, on_change, style?) -> widget
    register_simple_widget(env, "GraphicalUi.date_picker", wtype::date_picker,
                           {{.name = "value"}, {.name = nullptr, .is_callback = true}});

    // GraphicalUi.time_picker(value, on_change, style?) -> widget
    register_simple_widget(env, "GraphicalUi.time_picker", wtype::time_picker,
                           {{.name = "value"}, {.name = nullptr, .is_callback = true}});

    // GraphicalUi.color_picker(value, on_change, style?) -> widget
    register_simple_widget(env, "GraphicalUi.color_picker", wtype::color_picker,
                           {{.name = "value"}, {.name = nullptr, .is_callback = true}});

    // ─── Widgets with non-uniform argument handling ──────

    // GraphicalUi.heading(text, level?, style?) -> widget
    define_native(env, "GraphicalUi.heading",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.heading", args, 1, loc);

                      auto w = make_widget(wtype::heading);
                      w->set("text", args[0]);

                      if (args.size() >= 2 && args[1].is_integer()) {
                          w->set("level", args[1]);
                          w->set("style", get_style_arg(args, 2));
                      } else {
                          w->set("level", Value{static_cast<std::int64_t>(1)});
                          w->set("style", get_style_arg(args, 1));
                      }

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.text_input(value, on_change, placeholder?, on_commit?, style?) -> widget
    //
    // The trailing optional arguments are matched by type so callers can supply
    // any combination in a natural order: a string is the placeholder, a second
    // callable is the on_commit handler (fired on blur / Enter, not on every
    // keystroke), and a dictionary is the style bag.
    define_native(env, "GraphicalUi.text_input",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.text_input", args, 2, loc);

                      auto w = make_widget(wtype::text_input);
                      w->set("value", args[0]);

                      // Store callback for deferred binding in finalize_widget.
                      w->set(key::deferred_callback, args[1]);

                      Value style{make_dict()};

                      for (std::size_t i = 2; i < args.size(); ++i) {
                          const auto& a = args[i];

                          if (a.is_string()) {
                              w->set("placeholder", a);
                          } else if (a.is_callable()) {
                              w->set(key::deferred_commit_callback, a);
                          } else if (a.is_dictionary()) {
                              style = a;
                          }
                      }

                      // Lift the reserved `input_type` key (Solaris.input_type,
                      // T01) from the style dict onto the widget so the renderer
                      // can emit the HTML input `type`; it never leaks as CSS.
                      auto input_style = split_widget_options(*w, style, {"input_type"});
                      w->set("style", Value{std::move(input_style)});

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.progress_bar — alias for GraphicalUi.progress.
    env->define("GraphicalUi.progress_bar", env->get("GraphicalUi.progress", SourceLocation{}),
                false);

    // GraphicalUi.spinner(label?, style?) -> widget
    //
    // Indeterminate busy indicator for the "working, unknown duration" case that
    // the determinate progress bar cannot express. Carries an accessible label
    // (default "Loading…") exposed through role="status" so screen-reader users
    // are told the app is busy.
    define_native(env, "GraphicalUi.spinner",
                  [](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
                      auto w = make_widget(wtype::spinner);

                      if (!args.empty() && args[0].is_string()) {
                          w->set("label", args[0]);
                          w->set("style", get_style_arg(args, 1));
                      } else {
                          w->set("label", Value{std::string{"Loading…"}});
                          w->set("style", get_style_arg(args, 0));
                      }

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.spacer(height?, style?) -> widget
    define_native(env, "GraphicalUi.spacer",
                  [](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
                      auto w = make_widget(wtype::spacer);

                      if (!args.empty() && args[0].is_integer()) {
                          w->set("height", args[0]);
                          w->set("style", get_style_arg(args, 1));
                      } else {
                          w->set("style", get_style_arg(args, 0));
                      }

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.file_input(on_select, accept?, style?) -> widget
    define_native(env, "GraphicalUi.file_input",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.file_input", args, 1, loc);

                      auto w = make_widget(wtype::file_input);
                      w->set(key::deferred_callback, args[0]);

                      if (args.size() >= 2 && args[1].is_string()) {
                          w->set("accept", args[1]);
                          w->set("style", get_style_arg(args, 2));
                      } else {
                          w->set("style", get_style_arg(args, 1));
                      }

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.horizontal_spacer(width?, style?) -> widget
    define_native(env, "GraphicalUi.horizontal_spacer",
                  [](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
                      auto w = make_widget(wtype::spacer);

                      if (!args.empty() && args[0].is_integer()) {
                          w->set("width", args[0]);
                          w->set("style", get_style_arg(args, 1));
                      } else {
                          w->set("width", Value{std::int64_t{16}});
                          w->set("style", get_style_arg(args, 0));
                      }

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.flexible_space() -> widget
    define_native(env, "GraphicalUi.flexible_space",
                  [](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
                      auto w = make_widget(wtype::spacer);
                      w->set("flex", Value{std::int64_t{1}});
                      w->set("style", get_style_arg(args, 0));

                      return finalize_widget(std::move(w));
                  });
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW
