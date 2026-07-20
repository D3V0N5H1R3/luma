#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

// Embedded JS/CSS assets (lit-html, Pico CSS, uPlot, GUI framework, Lucide icons).
#include "common/platform_utils.hpp"
#include "common/resource_limits.hpp"
#include "runtime/stdlib/io/graphicalui_assets.hpp"
#include "runtime/stdlib/text/json_value_writer.hpp"

namespace luma::gui_detail {

// ═══════════════════════════════════════════════════════════
// Thread-local active app pointer (definition)
// ═══════════════════════════════════════════════════════════

thread_local AppState* active_app{nullptr};

// ═══════════════════════════════════════════════════════════
// Widget event and accessibility processing
// ═══════════════════════════════════════════════════════════

// Bind a deferred callback stored under `deferred_key` on a widget dict.
void bind_deferred_callback(DictionaryValue& widget, std::string_view deferred_key,
                            std::string_view id_key, std::string_view widget_name) {
    const std::string dk{deferred_key};
    const auto* deferred = widget.find(dk);

    if ((deferred == nullptr) || !deferred->is_callable()) {
        return;
    }

    auto& app = require_active_app(widget_name);

    auto cb_id = app.allocate_id();
    app.register_callback(cb_id, *deferred);
    widget.set(std::string{id_key}, Value{cb_id});
    widget.erase(dk);
}

// Process style-dict keys that represent event handlers (on_click, etc.),
// ARIA attributes (aria_*), element IDs, and roles.  Registers callable
// event handlers as callbacks and stores their IDs on the widget dict.
// Called inline during widget construction — no separate tree walk needed.
//
// Also handles deferred callback binding: if the widget has a key::deferred_callback
// key (set by interactive widget constructors), bind it here when app context
// is available.  Throws a RuntimeError if an interactive widget is constructed
// outside a view function (no active app context).
void apply_widget_events(DictionaryValue& widget) {
    auto widget_type = dict_string(widget, "type", "widget");

    bind_deferred_callback(widget, key::deferred_callback, "_callback_id", widget_type);
    bind_deferred_callback(widget, key::deferred_commit_callback, "_commit_id", widget_type);
    bind_deferred_callback(widget, key::deferred_close_callback, "_close_id", widget_type);
    bind_deferred_callback(widget, key::deferred_clear_callback, "_clear_id", widget_type);
    bind_deferred_callback(widget, key::deferred_select_callback, "_select_id", widget_type);
    bind_deferred_callback(widget, key::deferred_action_callback, "_action_id", widget_type);
    bind_deferred_callback(widget, key::deferred_sort_callback, "_sort_id", widget_type);

    if (active_app == nullptr) {
        return;
    }

    const auto* style_val = widget.find("style");

    if ((style_val == nullptr) || !style_val->is_dictionary()) {
        return;
    }

    const auto& style = *style_val->as_dictionary();

    // Mouse/pointer event handlers.
    static constexpr const char* event_keys[] = {"on_click",       "on_double_click",
                                                 "on_right_click", "on_mouse_enter",
                                                 "on_mouse_leave", "on_mouse_move"};

    for (const auto* key : event_keys) {
        const auto* v = style.find(key);

        if ((v != nullptr) && v->is_callable()) {
            auto cb_id = active_app->allocate_id();
            active_app->register_callback(cb_id, *v);
            widget.set(std::string{"_"} + key + "_id", Value{cb_id});
        }
    }

    // Element id (for focus targets).
    const auto* id_val = style.find("id");

    if ((id_val != nullptr) && id_val->is_string()) {
        widget.set("_element_id", *id_val);
    }

    // ARIA attributes and role.
    for (const auto& [k, v] : style.entries) {
        if (k.starts_with("aria_")) {
            widget.set("_" + k, v);
        } else if (k == "role") {
            widget.set("_role", v);
        }
    }
}

[[nodiscard]] Value finalize_widget(std::shared_ptr<DictionaryValue> w) {
    apply_widget_events(*w);
    return Value{std::move(w)};
}

// ═══════════════════════════════════════════════════════════
// Dev asset loading — reads JS/CSS from files when the
// LUMA_GUI_DEV_ASSETS environment variable is set.
// This allows iterating on the GUI framework without
// rebuilding the C++ binary.
// ═══════════════════════════════════════════════════════════

// Return the dev asset directory, or empty path if not set.
[[nodiscard]] std::filesystem::path dev_asset_dir() {
    const auto env = safe_getenv("LUMA_GUI_DEV_ASSETS");

    if (env && !env->empty()) {
        return std::filesystem::path{*env};
    }

    return {};
}

// ═══════════════════════════════════════════════════════════
// Headless execution mode helpers
// ═══════════════════════════════════════════════════════════

// Read an environment variable into a std::string ("" when unset/empty).
[[nodiscard]] static std::string read_env_string(const char* name) {
    return safe_getenv(name).value_or(std::string{});
}

[[nodiscard]] bool gui_headless_enabled() {
    const auto value = read_env_string("LUMA_GUI_HEADLESS");

    return !value.empty() && value != "0" && value != "false" && value != "FALSE";
}

[[nodiscard]] std::vector<std::string> gui_headless_messages() {
    std::vector<std::string> messages;
    const auto raw = read_env_string("LUMA_GUI_MESSAGES");

    if (raw.empty()) {
        return messages;
    }

    for (const auto& part : luma::split_string(raw, ',')) {
        auto trimmed = luma::trim(part);

        if (!trimmed.empty()) {
            messages.push_back(std::move(trimmed));
        }
    }

    return messages;
}

// Resolve the dev asset directory: uses the provided cached_dir if non-empty,
// otherwise falls back to the LUMA_GUI_DEV_ASSETS environment variable.
[[nodiscard]] static std::filesystem::path
resolve_dev_dir(const std::filesystem::path& cached_dir) {
    if (!cached_dir.empty()) {
        return cached_dir;
    }

    return dev_asset_dir();
}

[[nodiscard]] std::string build_gui_framework_css(const std::filesystem::path& cached_dir) {
    const auto dev_dir = resolve_dev_dir(cached_dir);

    if (!dev_dir.empty()) {
        // In dev mode, read Pico CSS and uPlot CSS from the external/ tree.
        auto pico = read_file_to_string(dev_dir / ".." / "pico" / "pico.min.css");
        auto uplot = read_file_to_string(dev_dir / ".." / "uplot" / "uPlot.min.css");
        auto overrides = read_file_to_string(dev_dir / "gui-overrides.css");

        if (!pico.empty() || !overrides.empty()) {
            std::string css;
            css.reserve(std::size_t{128} * 1024);
            css += pico;
            css += '\n';
            css += uplot;
            css += '\n';
            css += overrides;
            return css;
        }
    }

    // Production: use embedded compressed assets.
    std::string css;
    css.reserve(std::size_t{128} * 1024);
    css += gui_assets::pico_css_string();
    css += '\n';
    css += gui_assets::uplot_css_string();
    css += '\n';
    css += gui_assets::gui_overrides_css_string();
    return css;
}

// Reconstruct the GUI renderer from the shell + per-category fragment files,
// mirroring the splice performed by scripts/generate_gui_assets.mjs. The
// fragment order matches the generator (basic, layout, advanced, interaction);
// each fragment only Object.assign()s into the shared WIDGET_RENDERERS table, so
// the order affects readability only.
//
// Returns "" whenever a complete renderer cannot be produced — a missing shell,
// a missing splice marker, or a missing/empty fragment — so the caller falls
// back to the known-good embedded assets. This mirrors the fail-fast behaviour
// of the generator (which aborts on the same conditions) instead of serving a
// half-populated table where non-chart widgets silently render "[unknown widget]".
[[nodiscard]] static std::string build_gui_renderer_js_dev(const std::filesystem::path& dev_dir) {
    auto shell = read_file_to_string(dev_dir / "gui-renderer.js");

    static constexpr std::string_view marker = "// __GUI_WIDGET_RENDERER_FRAGMENTS__";
    const auto pos = shell.find(marker);

    if (shell.empty() || pos == std::string::npos) {
        return {};
    }

    static constexpr const char* fragment_files[] = {"basic.js", "layout.js", "advanced.js",
                                                     "interaction.js"};

    std::string fragments;

    for (const auto* name : fragment_files) {
        auto fragment = read_file_to_string(dev_dir / "renderers" / name);

        // A missing/misnamed fragment reads empty; fall back to the embedded
        // assets rather than silently drop that category's renderers.
        if (fragment.empty()) {
            return {};
        }

        fragments += fragment;
        fragments += '\n';
    }

    shell.replace(pos, marker.size(), fragments);
    return shell;
}

[[nodiscard]] std::string build_gui_framework_js(const std::filesystem::path& cached_dir) {
    const auto dev_dir = resolve_dev_dir(cached_dir);

    if (!dev_dir.empty()) {
        auto renderer = build_gui_renderer_js_dev(dev_dir);

        // Only use dev assets if the renderer file is present.
        if (!renderer.empty()) {
            std::string js;
            js.reserve(std::size_t{128} * 1024);
            js += read_file_to_string(dev_dir / ".." / "lit-html" / "lit-html.iife.min.js");
            js += '\n';
            js += read_file_to_string(dev_dir / ".." / "uplot" / "uPlot.iife.min.js");
            js += '\n';

            // Lucide icons: the source data is JSON; wrap in the same JS
            // variable declarations the asset generator produces.
            auto lucide_dir = dev_dir / ".." / "lucide";
            auto p1 = read_file_to_string(lucide_dir / "lucide-icons-part1.json");
            auto p2 = read_file_to_string(lucide_dir / "lucide-icons-part2.json");
            js += "    const __lucide_p1 = " + p1 + ";\n";
            js += "    const __lucide_p2 = " + p2 + ";\n";
            js += "    const __lucide_icons = Object.assign({}, __lucide_p1, __lucide_p2);\n";

            js += renderer;
            js += '\n';
            js += read_file_to_string(dev_dir / "gui-charts.js");
            js += '\n';
            js += read_file_to_string(dev_dir / "gui-subscriptions.js");
            return js;
        }
    }

    // Production: use embedded compressed assets.
    std::string js;
    js.reserve(std::size_t{128} * 1024);
    // lit-html rendering engine (IIFE, exposes window.litHtml).
    js += gui_assets::lit_html_js_string();
    js += '\n';
    // uPlot charting library (IIFE, exposes window.uPlot).
    js += gui_assets::uplot_js_string();
    js += '\n';
    // Lucide icon data + renderer.
    js += gui_assets::lucide_icons_js_string();
    js += '\n';
    // GUI renderer (lit-html based widget rendering). The embedded asset already
    // includes the per-category WIDGET_RENDERERS fragments (renderers/*.js),
    // spliced in at build time by scripts/generate_gui_assets.mjs.
    js += gui_assets::gui_renderer_js_string();
    js += '\n';
    // Chart renderer (uPlot bridge for charts).
    js += gui_assets::gui_charts_js_string();
    js += '\n';
    // Subscription manager (timers, keyboard, resize, etc.).
    js += gui_assets::gui_subscriptions_js_string();
    return js;
}

// ═══════════════════════════════════════════════════════════
// Widget-tree to JSON serialisation (direct string buffer)
// ═══════════════════════════════════════════════════════════

// The traversal itself lives in json_writer::write_value (json_value_writer.hpp),
// shared with the Json module's serialiser.  GuiJsonPolicy carries the GUI's
// differences: slash-escaping for <script> safety, a throw on exceeding the
// nesting-depth limit, and the rich {"variant":…,"fields":[…]} choice form.
//
// SECURITY: escape() escapes '/' as '\/' (JsonEscapePolicy<true>).  The output
// is embedded directly inside a <script> element in the initial HTML page (see
// GraphicalUi.app in graphicalui_module.cpp), so a "</script>" inside user data
// becomes "<\/script>" and cannot break out of the script context.  Do NOT
// switch to the non-slash json_escape_string variant.
namespace {

struct GuiJsonPolicy {
    static void escape(std::string_view s, std::string& out) {
        escape_string_impl<JsonEscapePolicy<true>>(s, out);
    }

    static bool depth_exceeded(int depth) {
        return depth > static_cast<int>(ResourceLimits::max_json_nesting_depth);
    }

    // Not [[noreturn]] on purpose — see the Policy contract in json_value_writer.hpp.
    static void on_depth_exceeded(std::string& /*out*/) {
        throw RuntimeError{"GraphicalUi: widget tree exceeds maximum nesting depth",
                           SourceLocation{},
                           "reduce nesting depth or split deeply nested views into components"};
    }

    static constexpr json_writer::JsonChoiceMode choice_mode = json_writer::JsonChoiceMode::rich;
    static constexpr bool result_increments_depth = true;
};

} // namespace

// Serialise a widget tree / model to a JSON string for the webview.
[[nodiscard]] std::string value_to_json(const Value& v, int depth) {
    std::string out;
    out.reserve(4096);
    json_writer::write_value<GuiJsonPolicy>(v, out, /*indent=*/0, depth, /*pretty=*/false);
    return out;
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW
