#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

namespace luma::gui_detail {

// ═══════════════════════════════════════════════════════════
// Table-driven chart registration helper (file-local)
// ═══════════════════════════════════════════════════════════

// Register a chart widget: name(labels, values, options?) -> widget
// Covers: vertical_bar_chart, line_chart, pie_chart, area_chart, horizontal_bar_chart.
// The trailing dictionary doubles as a style bag and a carrier for presentation
// options (axis labels, legend, tooltip) lifted onto the widget for the renderer.
static void register_chart_widget(const EnvPtr& env, const char* name, const char* type) {
    define_native(env, name,
                  [name, type](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args(name, args, 2, loc);
                      auto w = make_widget(type);
                      auto labels = unwrap_array_arg(args[0]);
                      w->set("labels", labels.is_null() ? args[0] : labels);
                      auto values = unwrap_array_arg(args[1]);
                      w->set("values", values.is_null() ? args[1] : values);

                      auto style = split_widget_options(
                          *w, get_style_arg(args, 2), {"x_label", "y_label", "legend", "tooltip"});
                      w->set("style", Value{std::move(style)});

                      return finalize_widget(std::move(w));
                  });
}

// Register a multi-series chart widget:
//   name(labels, series_names, series_values, series_colors, options?) -> widget
// Covers the line_chart / vertical_bar_chart types in a grouped, multi-series
// form. `series_values` is a 2-D array (one numeric row per series); the three
// parallel series arrays line up by index. The renderer detects the multi-series
// form by the presence of `series_names` and plots one series per row.
static void register_multi_chart_widget(const EnvPtr& env, const char* name, const char* type) {
    define_native(env, name,
                  [name, type](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args(name, args, 4, loc);
                      auto w = make_widget(type);
                      auto labels = unwrap_array_arg(args[0]);
                      w->set("labels", labels.is_null() ? args[0] : labels);
                      auto names = unwrap_array_arg(args[1]);
                      w->set("series_names", names.is_null() ? args[1] : names);
                      auto values = unwrap_array_arg(args[2]);
                      w->set("series_values", values.is_null() ? args[2] : values);
                      auto colors = unwrap_array_arg(args[3]);
                      w->set("series_colors", colors.is_null() ? args[3] : colors);

                      auto style = split_widget_options(
                          *w, get_style_arg(args, 4), {"x_label", "y_label", "legend", "tooltip"});
                      w->set("style", Value{std::move(style)});

                      return finalize_widget(std::move(w));
                  });
}

// ═══════════════════════════════════════════════════════════
// Chart widgets
// ═══════════════════════════════════════════════════════════

void register_chart_widgets(const EnvPtr& env) {
    register_chart_widget(env, "GraphicalUi.vertical_bar_chart", wtype::vertical_bar_chart);
    register_chart_widget(env, "GraphicalUi.line_chart", wtype::line_chart);
    register_chart_widget(env, "GraphicalUi.pie_chart", wtype::pie_chart);
    register_chart_widget(env, "GraphicalUi.area_chart", wtype::area_chart);
    register_chart_widget(env, "GraphicalUi.horizontal_bar_chart", wtype::horizontal_bar_chart);

    // Multi-series variants (grouped bars / multiple lines).
    register_multi_chart_widget(env, "GraphicalUi.line_chart_multi", wtype::line_chart);
    register_multi_chart_widget(env, "GraphicalUi.vertical_bar_chart_multi",
                                wtype::vertical_bar_chart);

    // GraphicalUi.donut_chart — special: optional center_label string arg.
    define_native(env, "GraphicalUi.donut_chart",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.donut_chart", args, 2, loc);
                      auto w = make_widget(wtype::donut_chart);
                      auto labels = unwrap_array_arg(args[0]);
                      w->set("labels", labels.is_null() ? args[0] : labels);
                      auto values = unwrap_array_arg(args[1]);
                      w->set("values", values.is_null() ? args[1] : values);

                      std::size_t style_index = 2;

                      if (args.size() >= 3 && args[2].is_string()) {
                          w->set("center_label", args[2]);
                          style_index = 3;
                      }

                      auto style = split_widget_options(*w, get_style_arg(args, style_index),
                                                        {"legend", "tooltip"});
                      w->set("style", Value{std::move(style)});

                      return finalize_widget(std::move(w));
                  });

    // GraphicalUi.scatter_plot — special: optional axis label string args.
    define_native(env, "GraphicalUi.scatter_plot",
                  [](std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_min_args("GraphicalUi.scatter_plot", args, 2, loc);
                      auto w = make_widget(wtype::scatter_plot);
                      auto x_vals = unwrap_array_arg(args[0]);
                      w->set("x_values", x_vals.is_null() ? args[0] : x_vals);
                      auto y_vals = unwrap_array_arg(args[1]);
                      w->set("y_values", y_vals.is_null() ? args[1] : y_vals);

                      std::size_t style_index = 2;

                      if (args.size() >= 3 && args[2].is_string()) {
                          w->set("x_label", args[2]);
                          style_index = 3;

                          if (args.size() >= 4 && args[3].is_string()) {
                              w->set("y_label", args[3]);
                              style_index = 4;
                          }
                      }

                      auto style = split_widget_options(*w, get_style_arg(args, style_index),
                                                        {"legend", "tooltip"});
                      w->set("style", Value{std::move(style)});

                      return finalize_widget(std::move(w));
                  });
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW
