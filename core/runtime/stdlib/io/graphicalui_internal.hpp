#ifndef LUMA_STDLIB_GRAPHICALUI_INTERNAL_HPP
#define LUMA_STDLIB_GRAPHICALUI_INTERNAL_HPP

// Internal umbrella header for the GraphicalUi module.
// This is NOT a public API header — it is shared between graphicalui_module.cpp,
// graphicalui_serialization.cpp, graphicalui_commands.cpp, graphicalui_events.cpp,
// and graphicalui_widgets*.cpp.
//
// Sub-headers:
//   graphicalui_types.hpp   — struct/class/enum definitions (AppState, etc.)
//   graphicalui_helpers.hpp — inline helper functions and forward declarations

#include <cstddef>
#include <string>

#include "common/escape.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/io/graphicalui_helpers.hpp"
#include "runtime/stdlib/io/graphicalui_types.hpp"

// ═══════════════════════════════════════════════════════════
// Shared constants — available to both real and stub builds.
// ═══════════════════════════════════════════════════════════

namespace luma::gui_detail {

// Register all GraphicalUi constants (severity, config keys, CSS variables).
// Shared between the real and stub implementations to avoid duplication.
inline void register_graphicalui_constants(const EnvPtr& env) {
    // Table-driven constant registration — avoids repetitive define() calls.
    static constexpr struct {
        const char* name;
        const char* value;
    } constants[] = {
        // Severity constants for GraphicalUi.alert().
        {"GraphicalUi.INFO", "info"},
        {"GraphicalUi.WARNING", "warning"},
        {"GraphicalUi.ERROR", "error"},
        {"GraphicalUi.SUCCESS", "success"},
        // Button-variant constants for GraphicalUi.button()'s `variant` style key.
        // Give actions a clear hierarchy: one PRIMARY per view, SECONDARY/GHOST
        // for supporting actions, DANGER for destructive ones.
        {"GraphicalUi.PRIMARY", "primary"},
        {"GraphicalUi.SECONDARY", "secondary"},
        {"GraphicalUi.GHOST", "ghost"},
        {"GraphicalUi.DANGER", "danger"},
        // Config key constants for GraphicalUi.app().
        {"GraphicalUi.MODEL", "model"},
        {"GraphicalUi.VIEW", "view"},
        {"GraphicalUi.UPDATE", "update"},
        {"GraphicalUi.TITLE", "title"},
        {"GraphicalUi.THEME", "theme"},
        {"GraphicalUi.SUBSCRIBE", "subscribe"},
        {"GraphicalUi.INIT", "init"},
        // CSS variable reference constants for theme-aware styling.
        {"GraphicalUi.VAR_PRIMARY", "var(--gui-primary)"},
        {"GraphicalUi.VAR_PRIMARY_HOVER", "var(--gui-primary-hover)"},
        {"GraphicalUi.VAR_BG", "var(--gui-bg)"},
        {"GraphicalUi.VAR_FG", "var(--gui-fg)"},
        {"GraphicalUi.VAR_BORDER", "var(--gui-border)"},
        {"GraphicalUi.VAR_INPUT_BG", "var(--gui-input-bg)"},
        {"GraphicalUi.VAR_INPUT_BORDER", "var(--gui-input-border)"},
        {"GraphicalUi.VAR_INPUT_FOCUS", "var(--gui-input-focus)"},
        {"GraphicalUi.VAR_RADIUS", "var(--gui-radius)"},
        {"GraphicalUi.VAR_RADIUS_NONE", "var(--gui-radius-none)"},
        {"GraphicalUi.VAR_RADIUS_SM", "var(--gui-radius-sm)"},
        {"GraphicalUi.VAR_RADIUS_MD", "var(--gui-radius-md)"},
        {"GraphicalUi.VAR_RADIUS_LG", "var(--gui-radius-lg)"},
        {"GraphicalUi.VAR_RADIUS_FULL", "var(--gui-radius-full)"},
        {"GraphicalUi.VAR_SHADOW", "var(--gui-shadow)"},
        // Semantic elevation ladder for the Solaris `Shadow` token.
        {"GraphicalUi.VAR_SHADOW_NONE", "none"},
        {"GraphicalUi.VAR_SHADOW_SM", "var(--gui-elevation-1)"},
        {"GraphicalUi.VAR_SHADOW_MD", "var(--gui-elevation-3)"},
        {"GraphicalUi.VAR_SHADOW_LG", "var(--gui-elevation-6)"},
        {"GraphicalUi.VAR_GAP", "var(--gui-gap)"},
        {"GraphicalUi.VAR_SPACE_XS", "var(--gui-space-xs)"},
        {"GraphicalUi.VAR_SPACE_SM", "var(--gui-space-sm)"},
        {"GraphicalUi.VAR_SPACE_MD", "var(--gui-space-md)"},
        {"GraphicalUi.VAR_SPACE_LG", "var(--gui-space-lg)"},
        {"GraphicalUi.VAR_SPACE_XL", "var(--gui-space-xl)"},
        // Type-scale references mirroring the spacing scale (xs/sm/md/lg/xl/2xl).
        {"GraphicalUi.VAR_TEXT_XS", "var(--gui-font-size-xs)"},
        {"GraphicalUi.VAR_TEXT_SM", "var(--gui-font-size-sm)"},
        {"GraphicalUi.VAR_TEXT_MD", "var(--gui-font-size-md)"},
        {"GraphicalUi.VAR_TEXT_LG", "var(--gui-font-size-lg)"},
        {"GraphicalUi.VAR_TEXT_XL", "var(--gui-font-size-xl)"},
        {"GraphicalUi.VAR_TEXT_2XL", "var(--gui-font-size-2xl)"},
        {"GraphicalUi.VAR_TEXT_MUTED", "var(--gui-text-muted)"},
        // Ideal line-length cap for forms and prose (CSS `ch` measure).
        {"GraphicalUi.VAR_MEASURE", "var(--gui-measure)"},
        {"GraphicalUi.VAR_DISABLED_BG", "var(--gui-disabled-bg)"},
        {"GraphicalUi.VAR_DISABLED_FG", "var(--gui-disabled-fg)"},
        {"GraphicalUi.VAR_SUCCESS", "var(--gui-success)"},
        {"GraphicalUi.VAR_WARNING", "var(--gui-warning)"},
        {"GraphicalUi.VAR_ERROR", "var(--gui-error)"},
        {"GraphicalUi.VAR_FONT", "var(--gui-font)"},
    };

    for (const auto& [name, value] : constants) {
        env->define(name, Value{std::string{value}}, false);
    }
}

} // namespace luma::gui_detail

// ═══════════════════════════════════════════════════════════
// Authoritative list of all GraphicalUi function names.
// Available to both the real and stub implementations so the
// stub stays in sync automatically.
// ═══════════════════════════════════════════════════════════

namespace luma::gui_detail {

// clang-format off
inline constexpr const char* graphicalui_function_names[] = {
    // Basic widgets.
    "GraphicalUi.label",
    "GraphicalUi.heading",
    "GraphicalUi.button",
    "GraphicalUi.button_of",
    "GraphicalUi.button_variant_to_string",
    "GraphicalUi.text_input",
    "GraphicalUi.text_area",
    "GraphicalUi.checkbox",
    "GraphicalUi.dropdown",
    "GraphicalUi.slider",
    "GraphicalUi.progress",
    "GraphicalUi.progress_bar",
    "GraphicalUi.spinner",
    "GraphicalUi.file_input",
    "GraphicalUi.date_picker",
    "GraphicalUi.time_picker",
    "GraphicalUi.color_picker",
    "GraphicalUi.image",
    "GraphicalUi.separator",
    "GraphicalUi.badge",
    "GraphicalUi.avatar",
    "GraphicalUi.card",
    "GraphicalUi.skeleton",
    "GraphicalUi.number_input",
    "GraphicalUi.search_input",
    "GraphicalUi.toast",
    "GraphicalUi.toast_of",
    "GraphicalUi.toast_region",
    "GraphicalUi.empty_state",
    "GraphicalUi.accordion",
    "GraphicalUi.breadcrumb",
    "GraphicalUi.field_error",
    "GraphicalUi.spacer",
    "GraphicalUi.horizontal_spacer",
    "GraphicalUi.flexible_space",
    // Layout widgets.
    "GraphicalUi.row",
    "GraphicalUi.column",
    "GraphicalUi.wrapped_row",
    "GraphicalUi.scroll_row",
    "GraphicalUi.scroll_column",
    "GraphicalUi.panel",
    "GraphicalUi.list",
    "GraphicalUi.radio_group",
    "GraphicalUi.toggle",
    "GraphicalUi.switch",
    "GraphicalUi.tabs",
    "GraphicalUi.table",
    "GraphicalUi.dialog",
    "GraphicalUi.alert",
    "GraphicalUi.alert_of",
    "GraphicalUi.severity_to_string",
    "GraphicalUi.sort_direction_to_string",
    "GraphicalUi.visibility_state_to_string",
    "GraphicalUi.tooltip",
    "GraphicalUi.menu",
    "GraphicalUi.popover",
    "GraphicalUi.combobox",
    "GraphicalUi.field",
    "GraphicalUi.confirm",
    "GraphicalUi.link",
    "GraphicalUi.icon",
    "GraphicalUi.toolbar",
    "GraphicalUi.grid",
    // Form handling.
    "GraphicalUi.form",
    // Drag and drop.
    "GraphicalUi.draggable",
    "GraphicalUi.drop_target",
    "GraphicalUi.drop_target_typed",
    // Nearby/overlay widgets.
    "GraphicalUi.above",
    "GraphicalUi.below",
    "GraphicalUi.on_left",
    "GraphicalUi.on_right",
    "GraphicalUi.in_front",
    "GraphicalUi.behind",
    // Debug.
    "GraphicalUi.debug",
    // Sizing helpers.
    "GraphicalUi.fill",
    "GraphicalUi.fill_portion",
    "GraphicalUi.shrink",
    "GraphicalUi.px",
    "GraphicalUi.constrained_fill",
    // Alignment helpers.
    "GraphicalUi.center",
    "GraphicalUi.center_x",
    "GraphicalUi.center_y",
    "GraphicalUi.align_left",
    "GraphicalUi.align_right",
    "GraphicalUi.align_top",
    "GraphicalUi.align_bottom",
    // Spacing helpers.
    "GraphicalUi.space_evenly",
    "GraphicalUi.space_between",
    "GraphicalUi.space_around",
    "GraphicalUi.spacing",
    "GraphicalUi.spacing_xy",
    "GraphicalUi.padding",
    "GraphicalUi.padding_xy",
    // Device classification.
    "GraphicalUi.classify_device",
    "GraphicalUi.classify_device_typed",
    // Chart widgets.
    "GraphicalUi.vertical_bar_chart",
    "GraphicalUi.line_chart",
    "GraphicalUi.pie_chart",
    "GraphicalUi.donut_chart",
    "GraphicalUi.scatter_plot",
    "GraphicalUi.area_chart",
    "GraphicalUi.horizontal_bar_chart",
    // Commands.
    "GraphicalUi.none",
    "GraphicalUi.batch",
    "GraphicalUi.http_get",
    "GraphicalUi.http_post",
    "GraphicalUi.http_get_full",
    "GraphicalUi.http_post_full",
    "GraphicalUi.delay",
    "GraphicalUi.write_clipboard",
    "GraphicalUi.read_clipboard",
    "GraphicalUi.get_local_storage",
    "GraphicalUi.set_local_storage",
    "GraphicalUi.remove_local_storage",
    "GraphicalUi.clear_local_storage",
    "GraphicalUi.scroll_to",
    "GraphicalUi.scroll_to_of",
    "GraphicalUi.blur",
    "GraphicalUi.download_file",
    "GraphicalUi.notify",
    "GraphicalUi.random",
    "GraphicalUi.with_command",
    "GraphicalUi.http_put",
    "GraphicalUi.http_delete",
    "GraphicalUi.http_patch",
    "GraphicalUi.open_url",
    "GraphicalUi.set_title",
    "GraphicalUi.print",
    "GraphicalUi.debounce",
    "GraphicalUi.undo",
    "GraphicalUi.redo",
    // Subscriptions.
    "GraphicalUi.on_tick",
    "GraphicalUi.on_key",
    "GraphicalUi.on_key_typed",
    "GraphicalUi.on_resize",
    "GraphicalUi.on_resize_typed",
    "GraphicalUi.on_focus",
    "GraphicalUi.on_mouse",
    "GraphicalUi.on_mouse_typed",
    "GraphicalUi.on_mouse_of",
    "GraphicalUi.mouse_event_type_to_string",
    "GraphicalUi.on_visibility_change",
    "GraphicalUi.on_visibility_change_typed",
    "GraphicalUi.on_online",
    "GraphicalUi.on_offline",
    "GraphicalUi.on_media_query",
    "GraphicalUi.on_scroll",
    "GraphicalUi.on_scroll_typed",
    "GraphicalUi.on_wheel_typed",
    "GraphicalUi.on_idle",
    "GraphicalUi.on_storage_change",
    "GraphicalUi.on_storage_change_typed",
    "GraphicalUi.on_animation_frame",
    "GraphicalUi.on_drag",
    "GraphicalUi.on_drag_typed",
    // Components and routing.
    "GraphicalUi.component",
    "GraphicalUi.error_boundary",
    "GraphicalUi.router",
    "GraphicalUi.navigate",
    "GraphicalUi.navigate_back",
    // Data and state.
    "GraphicalUi.paginator",
    "GraphicalUi.infinite_scroll",
    "GraphicalUi.wizard",
    // Theming helpers.
    "GraphicalUi.if_dark",
    "GraphicalUi.transition_preset",
    // Developer experience.
    "GraphicalUi.inspect",
    "GraphicalUi.navigation_link",
    // Accessibility.
    "GraphicalUi.accessible",
    "GraphicalUi.keyed",
    "GraphicalUi.focus",
    "GraphicalUi.announce",
    "GraphicalUi.aria_live",
    "GraphicalUi.aria_describedby",
    // Conditional rendering.
    "GraphicalUi.when",
    // Virtual list (windowed rendering for large datasets).
    "GraphicalUi.virtual_list",
    // Animation primitives.
    "GraphicalUi.transition",
    "GraphicalUi.animate",
    // Application lifecycle (registered in graphicalui_module.cpp).
    "GraphicalUi.app",
    "GraphicalUi.style",
    "GraphicalUi.merge_styles",
    "GraphicalUi.stylesheet",
    "GraphicalUi.load_stylesheet",
    "GraphicalUi.font_face",
    "GraphicalUi.set_theme_mode",
    "GraphicalUi.set_theme_mode_of",
    "GraphicalUi.theme_mode_to_string",
    "GraphicalUi.responsive",
    "GraphicalUi.validate_style",
    // Headless interaction-testing API (registered in graphicalui_testing.cpp).
    "GraphicalUi.test_init",
    "GraphicalUi.test_render",
    "GraphicalUi.test_click",
    "GraphicalUi.test_input",
    "GraphicalUi.test_message",
    "GraphicalUi.test_count",
    "GraphicalUi.test_event",
    "GraphicalUi.test_find",
    "GraphicalUi.test_key",
    "GraphicalUi.test_drag",
    "GraphicalUi.test_storage",
    "GraphicalUi.test_wheel",
    "GraphicalUi.test_visibility",
};
// clang-format on

inline constexpr std::size_t graphicalui_function_count =
    sizeof(graphicalui_function_names) / sizeof(graphicalui_function_names[0]);

} // namespace luma::gui_detail

#endif // LUMA_STDLIB_GRAPHICALUI_INTERNAL_HPP
