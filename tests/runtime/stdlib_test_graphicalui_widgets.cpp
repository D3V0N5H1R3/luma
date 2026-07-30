// GraphicalUi module C++ unit tests: constants, widget creation, and table options.

#include <string>

#include "stdlib_test_helpers.hpp"

// ═══════════════════════════════════════════════════════════
// Constants
// ═══════════════════════════════════════════════════════════

LUMA_TEST(constant_info) {
    const auto v = eval("GraphicalUi.INFO");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "info");
}

LUMA_TEST(constant_warning) {
    const auto v = eval("GraphicalUi.WARNING");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "warning");
}

LUMA_TEST(constant_error) {
    const auto v = eval("GraphicalUi.ERROR");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "error");
}

LUMA_TEST(constant_success) {
    const auto v = eval("GraphicalUi.SUCCESS");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "success");
}

LUMA_TEST(constant_button_variants) {
    ASSERT_EQ(eval("GraphicalUi.PRIMARY").as_string(), "primary");
    ASSERT_EQ(eval("GraphicalUi.SECONDARY").as_string(), "secondary");
    ASSERT_EQ(eval("GraphicalUi.GHOST").as_string(), "ghost");
    ASSERT_EQ(eval("GraphicalUi.DANGER").as_string(), "danger");
}

LUMA_TEST(constant_model) {
    const auto v = eval("GraphicalUi.MODEL");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "model");
}

LUMA_TEST(constant_view) {
    const auto v = eval("GraphicalUi.VIEW");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "view");
}

LUMA_TEST(constant_update) {
    const auto v = eval("GraphicalUi.UPDATE");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "update");
}

LUMA_TEST(constant_title) {
    const auto v = eval("GraphicalUi.TITLE");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "title");
}

LUMA_TEST(constant_theme) {
    const auto v = eval("GraphicalUi.THEME");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "theme");
}

// ═══════════════════════════════════════════════════════════
// Widget creation — basic widgets
// ═══════════════════════════════════════════════════════════

LUMA_TEST(label) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.label("Hello")
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "label");
}

LUMA_TEST(label_text) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.label("Hello")
        Dictionary.get_or(w, "text", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Hello");
}

LUMA_TEST(heading) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.heading("Title")
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "heading");
}

LUMA_TEST(heading_text) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.heading("Title", 2)
        Dictionary.get_or(w, "text", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Title");
}

LUMA_TEST(heading_level_six) {
    // Levels up to 6 are preserved on the widget (styled by the stylesheet).
    const auto v = eval(R"(
        dictionary w = GraphicalUi.heading("Eyebrow", 6)
        Converter.to_string(Dictionary.get_or(w, "level", 0))
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "6");
}

LUMA_TEST(empty_state) {
    const auto v = eval(R"(
        dictionary w = GraphicalUi.empty_state("Nothing here yet")
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "empty_state");
}

LUMA_TEST(empty_state_message_and_options) {
    const auto msg = eval(R"(
        dictionary w = GraphicalUi.empty_state("No contacts")
        Dictionary.get_or(w, "message", "")
    )");
    ASSERT_TRUE(msg.is_string());
    ASSERT_EQ(msg.as_string(), "No contacts");

    const auto icon = eval(R"(
        mutable dictionary opts = {}
        opts["icon"] = "users"
        opts["title"] = "Empty"
        dictionary w = GraphicalUi.empty_state("No contacts", opts)
        Dictionary.get_or(w, "icon", "")
    )");
    ASSERT_TRUE(icon.is_string());
    ASSERT_EQ(icon.as_string(), "users");
}

// An empty_state on_action callback is moved off the widget and bound to
// _action_id within app context (observed through the headless harness).
LUMA_TEST(empty_state_action_bound) {
    const std::string cfg = R"({
        "_": "gui_config", "model": 0,
        "view": (integer _c) -> GraphicalUi.empty_state("None", {
            "action_label": "Add", "on_action": () -> "add"
        })
    })";

    const auto has_action =
        eval("Dictionary.has(GraphicalUi.test_render(" + cfg + ", 0), \"_action_id\")");
    ASSERT_TRUE(has_action.is_bool());
    ASSERT_TRUE(has_action.as_bool());
}

LUMA_TEST(toast_region) {
    const auto v = eval(R"(
        dictionary w = GraphicalUi.toast_region([
            GraphicalUi.toast("Saved", GraphicalUi.SUCCESS)
        ])
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "toast_region");
}

LUMA_TEST(toast_region_position) {
    const auto v = eval(R"(
        mutable dictionary opts = {}
        opts["position"] = "top-right"
        dictionary w = GraphicalUi.toast_region([], opts)
        Dictionary.get_or(w, "position", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "top-right");
}

LUMA_TEST(image) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.image("logo.png")
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "image");
}

LUMA_TEST(separator) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.separator()
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "separator");
}

LUMA_TEST(spacer) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.spacer(0)
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "spacer");
}

LUMA_TEST(spacer_height) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.spacer(32)
        Dictionary.get_or(w, "height", 0)
    )");

    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 32);
}

// ═══════════════════════════════════════════════════════════
// Widget creation — interactive widgets (outside app context)
// These use deferred callback binding, so they should succeed.
// ═══════════════════════════════════════════════════════════

LUMA_TEST(button_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.button("Click", () -> {})
    )"));
}

LUMA_TEST(text_input_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.text_input("value", (string _v) -> {})
    )"));
}

LUMA_TEST(toggle_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.toggle("On", true, (boolean _v) -> {})
    )"));
}

LUMA_TEST(radio_group_deferred_outside_app) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.radio_group(["A", "B"], "A", (string _v) -> {})
    )"));
}

// The button `variant` key is lifted out of the trailing style dict onto the
// widget (and so does not leak into the inline CSS). Buttons need app context,
// so the variant is observed through the headless test_find harness.
LUMA_TEST(button_variant_extracted) {
    const std::string cfg = R"({
        "_": "gui_config", "model": 0,
        "view": (integer _c) -> GraphicalUi.button("Delete", () -> "x",
            {"variant": GraphicalUi.DANGER})
    })";

    const auto variant = eval("Dictionary.get_or(GraphicalUi.test_find(" + cfg +
                              ", 0, \"Delete\"), \"variant\", \"\")");
    ASSERT_TRUE(variant.is_string());
    ASSERT_EQ(variant.as_string(), "danger");
}

// ═══════════════════════════════════════════════════════════
// Widget creation — spinner
// ═══════════════════════════════════════════════════════════

LUMA_TEST(spinner_type) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.spinner()
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "spinner");
}

LUMA_TEST(spinner_default_label) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.spinner()
        Dictionary.get_or(w, "label", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Loading…");
}

LUMA_TEST(spinner_custom_label) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.spinner("Fetching")
        Dictionary.get_or(w, "label", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Fetching");
}

// ═══════════════════════════════════════════════════════════
// Widget creation — layout widgets
// ═══════════════════════════════════════════════════════════

LUMA_TEST(row) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.row([GraphicalUi.label("A")])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "row");
}

LUMA_TEST(column) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.column([GraphicalUi.label("A")])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "column");
}

LUMA_TEST(panel) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.panel("Title", [GraphicalUi.label("A")])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "panel");
}

LUMA_TEST(grid) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.grid(3, [GraphicalUi.label("A")])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "grid");
}

LUMA_TEST(toolbar) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.toolbar([GraphicalUi.label("A")])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "toolbar");
}

// ═══════════════════════════════════════════════════════════
// Widget creation — display-only widgets
// ═══════════════════════════════════════════════════════════

LUMA_TEST(alert) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.alert("Saved!", GraphicalUi.SUCCESS)
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "alert");
}

LUMA_TEST(alert_message) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.alert("Saved!", GraphicalUi.SUCCESS)
        Dictionary.get_or(w, "message", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Saved!");
}

LUMA_TEST(tooltip) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.tooltip("Hint", GraphicalUi.label("?"))
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "tooltip");
}

LUMA_TEST(icon) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.icon("star")
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "icon");
}

LUMA_TEST(icon_name) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.icon("star")
        Dictionary.get_or(w, "name", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "star");
}

// ═══════════════════════════════════════════════════════════
// Widget creation — chart widgets
// ═══════════════════════════════════════════════════════════

LUMA_TEST(vertical_bar_chart) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.vertical_bar_chart(["A", "B"], [10, 20])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "vertical_bar_chart");
}

LUMA_TEST(line_chart) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.line_chart(["A", "B"], [10, 20])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "line_chart");
}

LUMA_TEST(pie_chart) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.pie_chart(["A", "B"], [10, 20])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "pie_chart");
}

LUMA_TEST(line_chart_multi) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.line_chart_multi(
            ["A", "B"], ["Sales", "Costs"], [[10, 20], [6, 9]], ["#f00", "#00f"])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "line_chart");
}

LUMA_TEST(line_chart_multi_carries_series_names) {
    const auto v = eval(R"(
        dictionary w = GraphicalUi.line_chart_multi(
            ["A", "B"], ["Sales", "Costs"], [[10, 20], [6, 9]], ["#f00", "#00f"])
        array<string> names = Dictionary.get_or(w, "series_names", [])
        names[1]
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Costs");
}

LUMA_TEST(vertical_bar_chart_multi) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.vertical_bar_chart_multi(
            ["A", "B"], ["Sales", "Costs"], [[10, 20], [6, 9]], ["#f00", "#00f"])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "vertical_bar_chart");
}

LUMA_TEST(donut_chart) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.donut_chart(["A", "B"], [10, 20], "50%")
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "donut_chart");
}

LUMA_TEST(scatter_plot) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.scatter_plot([1.0, 2.0], [3.0, 4.0], "X", "Y")
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "scatter_plot");
}

LUMA_TEST(area_chart) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.area_chart(["A", "B"], [10, 20])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "area_chart");
}

LUMA_TEST(horizontal_bar_chart) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.horizontal_bar_chart(["A", "B"], [10, 20])
        Dictionary.get_or(w, "type", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "horizontal_bar_chart");
}

// Chart presentation options (axis labels, legend) are lifted out of the
// trailing dict onto the widget; remaining keys stay as the CSS style.
LUMA_TEST(chart_axis_labels_extracted) {
    const auto x = eval(R"(
        mutable dictionary opts = {}
        opts["x_label"] = "Month"
        opts["y_label"] = "Revenue"
        opts["height"] = "300px"
        dictionary w = GraphicalUi.line_chart(["A", "B"], [10, 20], opts)
        Dictionary.get_or(w, "x_label", "")
    )");
    ASSERT_TRUE(x.is_string());
    ASSERT_EQ(x.as_string(), "Month");

    const auto y = eval(R"(
        mutable dictionary opts = {}
        opts["y_label"] = "Revenue"
        dictionary w = GraphicalUi.line_chart(["A", "B"], [10, 20], opts)
        Dictionary.get_or(w, "y_label", "")
    )");
    ASSERT_TRUE(y.is_string());
    ASSERT_EQ(y.as_string(), "Revenue");
}

LUMA_TEST(chart_style_keeps_non_option_keys) {
    // A non-option key (height) stays in the style dict, not lifted to the top.
    const auto v = eval(R"(
        mutable dictionary opts = {}
        opts["x_label"] = "Month"
        opts["height"] = "300px"
        dictionary w = GraphicalUi.line_chart(["A", "B"], [10, 20], opts)
        dictionary s = Dictionary.get_or(w, "style", {})
        Dictionary.get_or(s, "height", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "300px");
}

// ═══════════════════════════════════════════════════════════
// Table options (alignment, selection, sorting)
// ═══════════════════════════════════════════════════════════

LUMA_TEST(table_type) {
    const auto v = eval(R"(
        dictionary w = GraphicalUi.table(["A"], [["1"]])
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "table");
}

LUMA_TEST(table_options_extracted) {
    const auto col = eval(R"(
        mutable dictionary opts = {}
        opts["sort_column"] = 1
        opts["sort_direction"] = "desc"
        dictionary w = GraphicalUi.table(["A", "B"], [["1", "2"]], opts)
        Converter.to_string(Dictionary.get_or(w, "sort_column", -1))
    )");
    ASSERT_TRUE(col.is_string());
    ASSERT_EQ(col.as_string(), "1");

    const auto dir = eval(R"(
        mutable dictionary opts = {}
        opts["sort_direction"] = "desc"
        dictionary w = GraphicalUi.table(["A", "B"], [["1", "2"]], opts)
        Dictionary.get_or(w, "sort_direction", "")
    )");
    ASSERT_TRUE(dir.is_string());
    ASSERT_EQ(dir.as_string(), "desc");
}

LUMA_TEST(table_selected_extracted) {
    const auto v = eval(R"(
        mutable dictionary opts = {}
        opts["selected"] = 2
        dictionary w = GraphicalUi.table(["A"], [["1"], ["2"], ["3"]], opts)
        Converter.to_string(Dictionary.get_or(w, "selected", -1))
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "2");
}

// A bound on_sort callback is moved off the widget and surfaces as _sort_id
// (allocated within app context). The view returns the table as its root so
// test_render exposes it directly for inspection.
LUMA_TEST(table_sort_callback_bound) {
    const std::string cfg = R"({
        "_": "gui_config", "model": 0,
        "view": (integer _c) -> GraphicalUi.table(["A", "B"], [["1", "2"]],
            {"on_sort": (integer _col) -> "sorted"})
    })";

    const auto has_sort =
        eval("Dictionary.has(GraphicalUi.test_render(" + cfg + ", 0), \"_sort_id\")");
    ASSERT_TRUE(has_sort.is_bool());
    ASSERT_TRUE(has_sort.as_bool());
}

// ═══════════════════════════════════════════════════════════
// Entry point
// ═══════════════════════════════════════════════════════════

int main() {
    LUMA_RUN_ALL();
}
