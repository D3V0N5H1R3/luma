#include "runtime/stdlib/io/graphicalui_constants.hpp"
#include "runtime/stdlib/io/graphicalui_internal.hpp"

#ifdef LUMA_HAS_WEBVIEW

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>

#include "runtime/stdlib/text/json_module.hpp"

namespace luma::gui_detail {

namespace {

// Parse a #rgb / #rgba / #rrggbb / #rrggbbaa colour string into 0-255 RGB
// components.  Returns false for anything we do not understand (rgb()/hsl()/
// named colours), so the caller can skip the contrast check rather than emit a
// misleading warning.
[[nodiscard]] bool parse_hex_color(std::string_view s, double& r, double& g, double& b) {
    const auto trim = [](std::string_view v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
            v.remove_prefix(1);
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) {
            v.remove_suffix(1);
        }
        return v;
    };
    s = trim(s);

    if (s.empty() || s.front() != '#') {
        return false;
    }
    s.remove_prefix(1);

    const auto nibble = [](char c, int& out) {
        if (c >= '0' && c <= '9') {
            out = c - '0';
            return true;
        }
        if (c >= 'a' && c <= 'f') {
            out = c - 'a' + 10;
            return true;
        }
        if (c >= 'A' && c <= 'F') {
            out = c - 'A' + 10;
            return true;
        }
        return false;
    };

    if (s.size() == 3 || s.size() == 4) {
        // #rgb / #rgba — expand each nibble to a full byte (0xF -> 0xFF).
        int v[3];
        for (std::size_t i = 0; i < 3; ++i) {
            int n = 0;
            if (!nibble(s[i], n)) {
                return false;
            }
            v[i] = n * 17;
        }
        r = v[0];
        g = v[1];
        b = v[2];
        return true;
    }

    if (s.size() == 6 || s.size() == 8) {
        int n[6];
        for (std::size_t i = 0; i < 6; ++i) {
            if (!nibble(s[i], n[i])) {
                return false;
            }
        }
        r = n[0] * 16 + n[1];
        g = n[2] * 16 + n[3];
        b = n[4] * 16 + n[5];
        return true;
    }

    return false;
}

// Linearise one sRGB channel (0-255) per the WCAG 2.x relative-luminance
// definition.
[[nodiscard]] double srgb_to_linear(double c) {
    c /= 255.0;
    return (c <= 0.03928) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double relative_luminance(double r, double g, double b) {
    return 0.2126 * srgb_to_linear(r) + 0.7152 * srgb_to_linear(g) + 0.0722 * srgb_to_linear(b);
}

[[nodiscard]] double contrast_ratio(double l1, double l2) {
    const double lighter = std::max(l1, l2);
    const double darker = std::min(l1, l2);
    return (lighter + 0.05) / (darker + 0.05);
}

// Fetch a theme colour by key and parse it as hex.  Returns false when the key
// is missing, not a string, or not a hex colour we can parse.
[[nodiscard]] bool theme_hex_color(const DictionaryValue& theme, const char* name, double& r,
                                   double& g, double& b) {
    const auto* v = theme.find(name);

    if (v == nullptr || !v->is_string()) {
        return false;
    }

    return parse_hex_color(v->as_string(), r, g, b);
}

} // namespace

std::string read_file_to_string(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};

    if (!file) {
        return {};
    }

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string contents(static_cast<std::size_t>(size), '\0');
    file.read(contents.data(), size);
    return contents;
}

AppConfig extract_app_config(const DictionaryValue& config, SourceLocation loc) {
    AppConfig cfg;
    cfg.title = dict_string(config, "title", "Luma Application");
    cfg.width = static_cast<int>(dict_int(config, "width", 800));
    cfg.height = static_cast<int>(dict_int(config, "height", 600));
    cfg.min_width = static_cast<int>(dict_int(config, "min_width", 0));
    cfg.min_height = static_cast<int>(dict_int(config, "min_height", 0));
    cfg.max_width = static_cast<int>(dict_int(config, "max_width", 0));
    cfg.max_height = static_cast<int>(dict_int(config, "max_height", 0));
    cfg.resizable = dict_bool(config, "resizable", true);
    cfg.devtools = dict_bool(config, "devtools", false);

    // Either "fullscreen" or "maximized" starts the window maximized.
    cfg.maximized = dict_bool(config, "fullscreen", false) || dict_bool(config, "maximized", false);

    // Remote images are off by default: opt in to load http(s) <img> sources.
    cfg.allow_remote_images = dict_bool(config, "allow_remote_images", false);

    // One-line state persistence: when a "persist" file path is given, the model
    // is restored from it on launch and written back to it on exit.
    cfg.persist_path = dict_string(config, "persist", "");

    // Extract model value (may be overridden by init() in init_app_state).
    const auto* model_ptr = config.find("model");
    cfg.initial_model = (model_ptr != nullptr) ? *model_ptr : Value{NullValue{}};

    // Store the init function for deferred invocation in init_app_state.
    const auto* init_ptr = config.find("init");
    cfg.init_fn =
        ((init_ptr != nullptr) && init_ptr->is_callable()) ? *init_ptr : Value{NullValue{}};

    // Extract view function (required).
    const auto* view_ptr = config.find("view");

    if ((view_ptr == nullptr) || !view_ptr->is_callable()) {
        throw RuntimeError{"GraphicalUi.app: config must include a 'view' function", loc,
                           "add a 'view' key with a function(model) -> widget"};
    }

    cfg.view_fn = *view_ptr;

    // Extract update function (optional).
    const auto* update_ptr = config.find("update");
    cfg.update_fn =
        ((update_ptr != nullptr) && update_ptr->is_callable()) ? *update_ptr : Value{NullValue{}};

    // Extract on_error function (optional) for themeable error display.
    const auto* on_error_ptr = config.find("on_error");
    cfg.on_error_fn = ((on_error_ptr != nullptr) && on_error_ptr->is_callable())
                          ? *on_error_ptr
                          : Value{NullValue{}};

    // Extract subscribe function (optional).
    const auto* subscribe_ptr = config.find("subscribe");
    cfg.subscribe_fn = ((subscribe_ptr != nullptr) && subscribe_ptr->is_callable())
                           ? *subscribe_ptr
                           : Value{NullValue{}};

    cfg.theme_ptr = config.find("theme");

    return cfg;
}

void init_app_state(AppState& state, AppConfig& cfg, webview_t w, SourceLocation loc) {
    state.webview = w;
    state.view_fn = std::move(cfg.view_fn);
    state.update_fn = std::move(cfg.update_fn);
    state.on_error_fn = std::move(cfg.on_error_fn);
    state.subscribe_fn = std::move(cfg.subscribe_fn);
    state.loc = loc;
    state.window_width.store(static_cast<std::int64_t>(cfg.width));

    // One-line persistence: when a saved model exists, restore it as the initial
    // model so init() (if any) receives it for optional migration or validation.
    if (!cfg.persist_path.empty()) {
        if (auto restored = load_persisted_model(cfg.persist_path)) {
            cfg.initial_model = std::move(*restored);
        }
    }

    // Invoke the init function (if provided) to produce the initial model
    // and optional startup command.  This is done here rather than in
    // extract_app_config to keep config extraction side-effect-free.
    // Per the documented contract (func(model) -> model|pair), the init
    // function receives the configured initial model as its argument.
    if (!cfg.init_fn.is_null() && cfg.init_fn.is_callable()) {
        std::vector<Value> init_args{cfg.initial_model};
        auto init_result = invoke_callable(cfg.init_fn, init_args, loc);

        if (is_command_pair(init_result)) {
            auto& d = *init_result.as_dictionary();
            state.model = *d.find(key::gui_model);
            cfg.init_command = *d.find(key::gui_command);
        } else {
            state.model = std::move(init_result);
        }
    } else {
        state.model = std::move(cfg.initial_model);
    }

    // Cache dev asset directory and detect dev mode for hot reload.
    state.cached_dev_dir = dev_asset_dir();
    state.dev_mode.enabled = !state.cached_dev_dir.empty();

    // Developer aid: when devtools are enabled, warn at startup about theme
    // colour choices that fail the WCAG AA contrast minimum for normal text.
    if (cfg.devtools && cfg.theme_ptr != nullptr) {
        validate_theme_contrast(*cfg.theme_ptr);
    }

    if (state.dev_mode.enabled) {
        std::error_code ec;
        state.dev_mode.css_mtime =
            std::filesystem::last_write_time(state.cached_dev_dir / "gui-overrides.css", ec);
        state.dev_mode.js_mtime =
            std::filesystem::last_write_time(state.cached_dev_dir / "gui-renderer.js", ec);
    }
}

void update_model_and_render(AppState& state, Value new_model) {
    state.model = std::move(new_model);

    if (!state.render_suppressed) {
        render_view(state);
    }
}

std::optional<Value> load_persisted_model(const std::filesystem::path& path) {
    std::error_code ec;

    if (!std::filesystem::exists(path, ec) || ec) {
        return std::nullopt;
    }

    const auto contents = read_file_to_string(path);

    if (contents.empty()) {
        return std::nullopt;
    }

    try {
        return json_parse_string(contents);
    } catch (const std::exception& e) {
        std::cerr << "GraphicalUi: could not restore persisted state from " << path.string() << ": "
                  << e.what() << "\n";
        return std::nullopt;
    }
}

void save_persisted_model(const std::filesystem::path& path, const Value& model) {
    try {
        std::string out;
        json_serialize_value(model, out, /*indent=*/0, /*depth=*/0, /*pretty=*/true);

        std::ofstream file{path, std::ios::binary | std::ios::trunc};

        if (!file) {
            std::cerr << "GraphicalUi: could not open " << path.string() << " to persist state\n";
            return;
        }

        file.write(out.data(), static_cast<std::streamsize>(out.size()));
    } catch (const std::exception& e) {
        std::cerr << "GraphicalUi: could not persist state to " << path.string() << ": " << e.what()
                  << "\n";
    }
}

void validate_theme_contrast(const Value& theme_value) {
    if (!theme_value.is_dictionary()) {
        return;
    }

    const auto& theme = *theme_value.as_dictionary();

    double br = 0;
    double bg = 0;
    double bb = 0;

    if (!theme_hex_color(theme, "background", br, bg, bb)) {
        // No parseable background colour — nothing to measure against.
        return;
    }

    const double background_luminance = relative_luminance(br, bg, bb);

    struct ColorPair {
        const char* key;
        const char* label;
    };

    // Foreground/accent colours that carry normal-size text on the background.
    static constexpr ColorPair pairs[] = {
        {"text_color", "text_color on background"},
        {"accent", "accent on background"},
    };

    for (const auto& pair : pairs) {
        double r = 0;
        double g = 0;
        double b = 0;

        if (!theme_hex_color(theme, pair.key, r, g, b)) {
            continue;
        }

        const double ratio = contrast_ratio(relative_luminance(r, g, b), background_luminance);

        if (ratio < 4.5) {
            std::cerr << std::format("GraphicalUi: low colour contrast — {} is {:.2f}:1 "
                                     "(WCAG AA requires >= 4.5:1 for normal text)\n",
                                     pair.label, ratio);
        }
    }
}

} // namespace luma::gui_detail

#endif // LUMA_HAS_WEBVIEW
