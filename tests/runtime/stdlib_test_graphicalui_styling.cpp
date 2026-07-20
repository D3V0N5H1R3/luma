// GraphicalUi module C++ unit tests: styling functions, CSS/JSON white-box, and layout helpers.

#include <limits>
#include <string>

#include "runtime/stdlib/io/graphicalui_css.hpp"
#include "stdlib_test_helpers.hpp"

// ═══════════════════════════════════════════════════════════
// Style helper
// ═══════════════════════════════════════════════════════════

LUMA_TEST(style_returns_dict) {
    const auto v = eval(R"(
        dictionary<string> s = GraphicalUi.style({"color": "red"})
        Dictionary.get_or(s, "color", "")
    )");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "red");
}

// ═══════════════════════════════════════════════════════════
// CSS variable reference constants (VAR_*)
// ═══════════════════════════════════════════════════════════

LUMA_TEST(constant_var_primary) {
    const auto v = eval("GraphicalUi.VAR_PRIMARY");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-primary)");
}

LUMA_TEST(constant_var_primary_hover) {
    const auto v = eval("GraphicalUi.VAR_PRIMARY_HOVER");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-primary-hover)");
}

LUMA_TEST(constant_var_bg) {
    const auto v = eval("GraphicalUi.VAR_BG");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-bg)");
}

LUMA_TEST(constant_var_fg) {
    const auto v = eval("GraphicalUi.VAR_FG");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-fg)");
}

LUMA_TEST(constant_var_border) {
    const auto v = eval("GraphicalUi.VAR_BORDER");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-border)");
}

LUMA_TEST(constant_var_input_bg) {
    const auto v = eval("GraphicalUi.VAR_INPUT_BG");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-input-bg)");
}

LUMA_TEST(constant_var_input_border) {
    const auto v = eval("GraphicalUi.VAR_INPUT_BORDER");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-input-border)");
}

LUMA_TEST(constant_var_input_focus) {
    const auto v = eval("GraphicalUi.VAR_INPUT_FOCUS");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-input-focus)");
}

LUMA_TEST(constant_var_radius) {
    const auto v = eval("GraphicalUi.VAR_RADIUS");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-radius)");
}

LUMA_TEST(constant_var_shadow) {
    const auto v = eval("GraphicalUi.VAR_SHADOW");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-shadow)");
}

LUMA_TEST(constant_var_gap) {
    const auto v = eval("GraphicalUi.VAR_GAP");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-gap)");
}

LUMA_TEST(constant_var_disabled_bg) {
    const auto v = eval("GraphicalUi.VAR_DISABLED_BG");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-disabled-bg)");
}

LUMA_TEST(constant_var_disabled_fg) {
    const auto v = eval("GraphicalUi.VAR_DISABLED_FG");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-disabled-fg)");
}

LUMA_TEST(constant_var_success) {
    const auto v = eval("GraphicalUi.VAR_SUCCESS");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-success)");
}

LUMA_TEST(constant_var_warning) {
    const auto v = eval("GraphicalUi.VAR_WARNING");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-warning)");
}

LUMA_TEST(constant_var_error) {
    const auto v = eval("GraphicalUi.VAR_ERROR");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-error)");
}

LUMA_TEST(constant_var_font) {
    const auto v = eval("GraphicalUi.VAR_FONT");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "var(--gui-font)");
}

// ═══════════════════════════════════════════════════════════
// Styling functions
// ═══════════════════════════════════════════════════════════

LUMA_TEST(merge_styles_two_dicts) {
    const auto v = eval(R"(
        dictionary<string> merged = GraphicalUi.merge_styles(
            {"padding": "8px", "color": "white"},
            {"color": "yellow", "font_weight": "bold"}
        )
        Dictionary.get_or(merged, "color", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "yellow");
}

LUMA_TEST(merge_styles_preserves_base) {
    const auto v = eval(R"(
        dictionary<string> merged = GraphicalUi.merge_styles(
            {"padding": "8px", "color": "white"},
            {"color": "yellow"}
        )
        Dictionary.get_or(merged, "padding", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "8px");
}

LUMA_TEST(merge_styles_three_dicts) {
    const auto v = eval(R"(
        dictionary<string> merged = GraphicalUi.merge_styles(
            {"a": "1"}, {"b": "2"}, {"c": "3"}
        )
        Dictionary.length(merged)
    )");
    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 3);
}

LUMA_TEST(stylesheet_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.stylesheet(".test { color: red; }")
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "stylesheet");
}

LUMA_TEST(stylesheet_stores_css) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.stylesheet(".x { padding: 8px; }")
        Dictionary.get_or(cmd, "css", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), ".x { padding: 8px; }");
}

LUMA_TEST(stylesheet_rejects_script) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.stylesheet("<script>alert('xss')</script>")
    )"));
}

LUMA_TEST(stylesheet_rejects_javascript_url) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.stylesheet("a { background: url(javascript:alert(1)); }")
    )"));
}

LUMA_TEST(load_stylesheet_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.load_stylesheet("assets/theme.css")
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "load_stylesheet");
}

LUMA_TEST(load_stylesheet_stores_path) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.load_stylesheet("styles/app.css")
        Dictionary.get_or(cmd, "path", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "styles/app.css");
}

LUMA_TEST(load_stylesheet_rejects_non_css) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.load_stylesheet("styles/app.js")
    )"));
}

LUMA_TEST(load_stylesheet_rejects_absolute) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.load_stylesheet("/etc/styles.css")
    )"));
}

LUMA_TEST(load_stylesheet_rejects_url) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.load_stylesheet("https://example.com/style.css")
    )"));
}

LUMA_TEST(load_stylesheet_rejects_path_traversal) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.load_stylesheet("../secrets/style.css")
    )"));
}

// ─── font_face command shape and validation ───
// GraphicalUi.font_face is a command constructor: in eval() (no running app) it
// validates its arguments and returns the command dictionary without embedding
// the font. These tests exercise that construction/validation path.

LUMA_TEST(font_face_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.font_face("fonts/inter.woff2", "Inter")
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "font_face");
}

LUMA_TEST(font_face_stores_path) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.font_face("fonts/inter.woff2", "Inter")
        Dictionary.get_or(cmd, "path", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "fonts/inter.woff2");
}

LUMA_TEST(font_face_stores_family) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.font_face("fonts/inter.woff2", "Inter")
        Dictionary.get_or(cmd, "family", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "Inter");
}

LUMA_TEST(font_face_default_weight_is_normal) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.font_face("fonts/inter.woff2", "Inter")
        Dictionary.get_or(cmd, "weight", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "normal");
}

LUMA_TEST(font_face_default_style_is_normal) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.font_face("fonts/inter.woff2", "Inter")
        Dictionary.get_or(cmd, "style", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "normal");
}

LUMA_TEST(font_face_default_set_default_is_true) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.font_face("fonts/inter.woff2", "Inter")
        Dictionary.get_or(cmd, "set_default", false)
    )");
    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

LUMA_TEST(font_face_options_override_weight_and_style) {
    // A single-type (all-string) options dictionary type-checks directly.
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.font_face(
            "fonts/inter.woff2", "Inter", {"weight": "100 900", "style": "italic"})
        Dictionary.get_or(cmd, "weight", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "100 900");
}

LUMA_TEST(font_face_options_disable_default) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.font_face(
            "fonts/inter.woff2", "Inter", {"default": false})
        Dictionary.get_or(cmd, "set_default", true)
    )");
    ASSERT_TRUE(v.is_bool());
    ASSERT_FALSE(v.as_bool());
}

LUMA_TEST(font_face_rejects_non_font_extension) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.font_face("fonts/inter.css", "Inter")
    )"));
}

LUMA_TEST(font_face_rejects_absolute) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.font_face("/usr/share/fonts/inter.woff2", "Inter")
    )"));
}

LUMA_TEST(font_face_rejects_url) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.font_face("https://rsms.me/inter/font/Inter.woff2", "Inter")
    )"));
}

LUMA_TEST(font_face_rejects_path_traversal) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.font_face("../fonts/inter.woff2", "Inter")
    )"));
}

LUMA_TEST(font_face_rejects_family_with_quote) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.font_face("fonts/inter.woff2", "Inter\"; }")
    )"));
}

LUMA_TEST(font_face_rejects_bad_weight) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.font_face("fonts/inter.woff2", "Inter", {"weight": "heavy"})
    )"));
}

LUMA_TEST(font_face_rejects_bad_style) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.font_face("fonts/inter.woff2", "Inter", {"style": "slanted"})
    )"));
}

LUMA_TEST(stylesheet_rejects_css_url_function) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.stylesheet("body { background: url(https://evil.com/track.png); }")
    )"));
}

LUMA_TEST(stylesheet_rejects_css_import) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.stylesheet("@import 'https://evil.com/inject.css';")
    )"));
}

LUMA_TEST(set_theme_mode_command) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.set_theme_mode("dark")
        Dictionary.get_or(cmd, "_command_type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "set_theme_mode");
}

LUMA_TEST(set_theme_mode_stores_mode) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.set_theme_mode("light")
        Dictionary.get_or(cmd, "mode", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "light");
}

LUMA_TEST(set_theme_mode_auto) {
    const auto v = eval(R"(
        dictionary<string> cmd = GraphicalUi.set_theme_mode("auto")
        Dictionary.get_or(cmd, "mode", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "auto");
}

LUMA_TEST(set_theme_mode_rejects_invalid) {
    ASSERT_THROWS(eval(R"(
        GraphicalUi.set_theme_mode("neon")
    )"));
}

LUMA_TEST(validate_style_valid) {
    const auto v = eval(R"(
        result<dictionary> r = GraphicalUi.validate_style({"color": "red", "padding": "8px"})
        Result.is_success(r)
    )");
    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

LUMA_TEST(validate_style_invalid) {
    const auto v = eval(R"(
        result<dictionary> r = GraphicalUi.validate_style({"colour": "red"})
        Result.is_failure(r)
    )");
    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

LUMA_TEST(validate_style_suggestion) {
    const auto v = eval(R"(
        result<dictionary> r = GraphicalUi.validate_style({"colour": "red"})
        Result.error(r)
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_TRUE(v.as_string().find("color") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// CSS sanitisation (sanitise_loaded_css) — white-box tests.
//
// sanitise_loaded_css is the allowlist filter behind
// GraphicalUi.load_stylesheet.  It only runs against the contents of a real
// .css file at app runtime, so it cannot be reached through eval(); these
// tests call it directly via its luma::gui_detail header.  is_known_css_property
// and suggest_css_property back GraphicalUi.validate_style and the typo
// suggestions it returns.
// ═══════════════════════════════════════════════════════════

LUMA_TEST(sanitise_css_passes_safe) {
    const std::string css = ".a { color: red; padding: 8px; }";
    const auto out = gui_detail::sanitise_loaded_css(css);

    ASSERT_EQ(out, css);
}

LUMA_TEST(sanitise_css_preserves_safe_at_media) {
    const auto out =
        gui_detail::sanitise_loaded_css("@media (max-width: 600px) { .a { color: red; } }");

    ASSERT_TRUE(out.find("@media") != std::string::npos);
    ASSERT_TRUE(out.find("color: red") != std::string::npos);
}

LUMA_TEST(sanitise_css_preserves_safe_function) {
    const auto out = gui_detail::sanitise_loaded_css(".a { width: calc(8px + 1rem); }");

    ASSERT_TRUE(out.find("calc(") != std::string::npos);
    ASSERT_TRUE(out.find("8px + 1rem") != std::string::npos);
}

LUMA_TEST(sanitise_css_preserves_data_url) {
    const auto out =
        gui_detail::sanitise_loaded_css(".a { background: url(data:image/png;base64,AAAA); }");

    ASSERT_TRUE(out.find("url(data:image/png;base64,AAAA)") != std::string::npos);
}

LUMA_TEST(sanitise_css_preserves_relative_url) {
    const auto out = gui_detail::sanitise_loaded_css(".a { background: url(images/bg.png); }");

    ASSERT_TRUE(out.find("url(images/bg.png)") != std::string::npos);
}

LUMA_TEST(sanitise_css_strips_script_tag) {
    const auto out = gui_detail::sanitise_loaded_css(
        ".x { color: red; }<script>alert('xss')</script>.y { color: blue; }");

    ASSERT_TRUE(out.find('<') == std::string::npos);
    ASSERT_TRUE(out.find("script") == std::string::npos);
    ASSERT_TRUE(out.find("alert") == std::string::npos);
    // The safe rules on either side of the injected tag survive.
    ASSERT_TRUE(out.find("color: red") != std::string::npos);
    ASSERT_TRUE(out.find("color: blue") != std::string::npos);
}

LUMA_TEST(sanitise_css_strips_import) {
    const auto out = gui_detail::sanitise_loaded_css("@import \"evil.css\"; .ok { color: green; }");

    ASSERT_TRUE(out.find("import") == std::string::npos);
    ASSERT_TRUE(out.find(".ok") != std::string::npos);
}

LUMA_TEST(sanitise_css_strips_javascript_url) {
    const auto out =
        gui_detail::sanitise_loaded_css(".a { background: url(javascript:alert(1)); }");

    ASSERT_TRUE(out.find("javascript") == std::string::npos);
    ASSERT_TRUE(out.find("alert") == std::string::npos);
}

LUMA_TEST(sanitise_css_strips_escaped_url) {
    // CSS escape sequences in a url() body (e.g. \6A decoding to 'j') must be
    // dropped wholesale rather than passed through to the browser.
    const auto out =
        gui_detail::sanitise_loaded_css(".a { list-style: url(\\6A\\61 vascript:x); }");

    ASSERT_TRUE(out.find("\\6A") == std::string::npos);
    ASSERT_TRUE(out.find("\\61") == std::string::npos);
}

LUMA_TEST(sanitise_css_strips_expression) {
    const auto out = gui_detail::sanitise_loaded_css(".a { width: expression(alert(1)); }");

    ASSERT_TRUE(out.find("expression") == std::string::npos);
    ASSERT_TRUE(out.find("alert") == std::string::npos);
}

LUMA_TEST(sanitise_css_strips_unknown_function) {
    const auto out = gui_detail::sanitise_loaded_css(".a { color: evilfn(1, 2); }");

    ASSERT_TRUE(out.find("evilfn") == std::string::npos);
}

LUMA_TEST(sanitise_css_output_never_grows) {
    // The length-monotonicity invariant that the fuzzer's oracle enforces,
    // pinned as a regression test on a mixed safe / unsafe input.
    const std::string css =
        "@import 'x'; .a { width: expression(1); background: url(javascript:1); } "
        "<script>1</script> @media screen { .b { color: var(--c); } }";
    const auto out = gui_detail::sanitise_loaded_css(css);

    ASSERT_LE(out.size(), css.size());
}

LUMA_TEST(sanitise_css_handles_unterminated) {
    // Unterminated tags / comments / url() must not crash and must drop the
    // dangling remainder.
    const auto tag = gui_detail::sanitise_loaded_css(".a { color: red; } <div");
    ASSERT_TRUE(tag.find('<') == std::string::npos);

    const auto comment = gui_detail::sanitise_loaded_css(".a { color: red; } /* dangling");
    ASSERT_TRUE(comment.find("dangling") == std::string::npos);

    const auto url = gui_detail::sanitise_loaded_css(".a { background: url(");
    ASSERT_LE(url.size(), std::string(".a { background: url(").size());
}

LUMA_TEST(is_known_css_property_true) {
    ASSERT_TRUE(gui_detail::is_known_css_property("color"));
    ASSERT_TRUE(gui_detail::is_known_css_property("background_color"));
}

LUMA_TEST(is_known_css_property_custom) {
    // Custom properties (--*) and reserved keys are always accepted.
    ASSERT_TRUE(gui_detail::is_known_css_property("--my-var"));
    ASSERT_TRUE(gui_detail::is_known_css_property("hover_color"));
}

LUMA_TEST(is_known_css_property_false) {
    ASSERT_FALSE(gui_detail::is_known_css_property("colour"));
    ASSERT_FALSE(gui_detail::is_known_css_property("not_a_property"));
}

LUMA_TEST(suggest_css_property_typo) {
    ASSERT_EQ(gui_detail::suggest_css_property("colour"), "color");
}

LUMA_TEST(suggest_css_property_no_match) {
    ASSERT_EQ(gui_detail::suggest_css_property("zzzqqqxxx"), "");
}

// ─── Inline-CSS blocklist (validate_inline_css) ───
// Backs GraphicalUi.stylesheet.  Rejects dangerous substrings case-insensitively;
// only reachable directly via the luma::gui_detail header.

LUMA_TEST(validate_inline_css_accepts_safe) {
    bool threw = false;

    try {
        gui_detail::validate_inline_css(".a { color: red; padding: 8px; }", SourceLocation{});
    } catch (...) {
        threw = true;
    }

    ASSERT_FALSE(threw);
}

LUMA_TEST(validate_inline_css_rejects_script_tag) {
    ASSERT_THROWS(gui_detail::validate_inline_css("<script>alert(1)</script>", SourceLocation{}));
}

LUMA_TEST(validate_inline_css_rejects_url_and_import) {
    // url() (data/JS-scheme vector) and @import (external load) are both blocked.
    ASSERT_THROWS(gui_detail::validate_inline_css(".a { background: url(x); }", SourceLocation{}));
    ASSERT_THROWS(gui_detail::validate_inline_css("@import \"x.css\";", SourceLocation{}));
}

LUMA_TEST(validate_inline_css_case_insensitive) {
    // Matching lower-cases first, so upper-case "JavaScript:" is still rejected.
    ASSERT_THROWS(
        gui_detail::validate_inline_css(".a { x: JavaScript:alert(1); }", SourceLocation{}));
}

// ─── Stylesheet-path validation (validate_stylesheet_path) ───
// Backs GraphicalUi.load_stylesheet.  Enforces .css extension, relative-only
// paths, no URL scheme, and no ".." traversal.

LUMA_TEST(validate_stylesheet_path_accepts_relative_css) {
    bool threw = false;

    try {
        gui_detail::validate_stylesheet_path("styles/app.css", SourceLocation{});
    } catch (...) {
        threw = true;
    }

    ASSERT_FALSE(threw);
}

LUMA_TEST(validate_stylesheet_path_rejects_non_css) {
    ASSERT_THROWS(gui_detail::validate_stylesheet_path("styles/app.txt", SourceLocation{}));
}

LUMA_TEST(validate_stylesheet_path_rejects_absolute) {
    ASSERT_THROWS(gui_detail::validate_stylesheet_path("/etc/app.css", SourceLocation{}));
    ASSERT_THROWS(gui_detail::validate_stylesheet_path("C:\\app.css", SourceLocation{}));
}

LUMA_TEST(validate_stylesheet_path_rejects_url) {
    ASSERT_THROWS(
        gui_detail::validate_stylesheet_path("https://evil.test/app.css", SourceLocation{}));
}

LUMA_TEST(validate_stylesheet_path_rejects_traversal) {
    ASSERT_THROWS(gui_detail::validate_stylesheet_path("../secrets/app.css", SourceLocation{}));
}

// ─── Font format lookup and validation (font_face) ───
// font_format_for_path maps a file extension to its MIME type and CSS
// format() token; the validate_font_* helpers back GraphicalUi.font_face.

LUMA_TEST(font_format_for_path_maps_known_extensions) {
    const auto woff2 = gui_detail::font_format_for_path("fonts/Inter.woff2");
    ASSERT_TRUE(woff2.has_value());
    ASSERT_EQ(std::string(woff2->mime), "font/woff2");
    ASSERT_EQ(std::string(woff2->format), "woff2");

    const auto woff = gui_detail::font_format_for_path("fonts/Inter.woff");
    ASSERT_TRUE(woff.has_value());
    ASSERT_EQ(std::string(woff->mime), "font/woff");
    ASSERT_EQ(std::string(woff->format), "woff");

    const auto ttf = gui_detail::font_format_for_path("fonts/Inter.ttf");
    ASSERT_TRUE(ttf.has_value());
    ASSERT_EQ(std::string(ttf->mime), "font/ttf");
    ASSERT_EQ(std::string(ttf->format), "truetype");

    const auto otf = gui_detail::font_format_for_path("fonts/Inter.otf");
    ASSERT_TRUE(otf.has_value());
    ASSERT_EQ(std::string(otf->mime), "font/otf");
    ASSERT_EQ(std::string(otf->format), "opentype");
}

LUMA_TEST(font_format_for_path_is_case_insensitive) {
    const auto upper = gui_detail::font_format_for_path("fonts/Inter.WOFF2");
    ASSERT_TRUE(upper.has_value());
    ASSERT_EQ(std::string(upper->format), "woff2");
}

LUMA_TEST(font_format_for_path_rejects_unknown_extension) {
    ASSERT_FALSE(gui_detail::font_format_for_path("fonts/Inter.css").has_value());
    ASSERT_FALSE(gui_detail::font_format_for_path("fonts/Inter").has_value());
}

LUMA_TEST(validate_font_path_accepts_relative_font) {
    bool threw = false;

    try {
        gui_detail::validate_font_path("fonts/Inter.woff2", SourceLocation{});
    } catch (...) {
        threw = true;
    }

    ASSERT_FALSE(threw);
}

LUMA_TEST(validate_font_path_rejects_non_font) {
    ASSERT_THROWS(gui_detail::validate_font_path("fonts/Inter.css", SourceLocation{}));
}

LUMA_TEST(validate_font_path_rejects_absolute) {
    ASSERT_THROWS(gui_detail::validate_font_path("/usr/share/fonts/Inter.woff2", SourceLocation{}));
    ASSERT_THROWS(gui_detail::validate_font_path("C:\\fonts\\Inter.woff2", SourceLocation{}));
}

LUMA_TEST(validate_font_path_rejects_url) {
    ASSERT_THROWS(
        gui_detail::validate_font_path("https://rsms.me/inter/Inter.woff2", SourceLocation{}));
}

LUMA_TEST(validate_font_path_rejects_traversal) {
    ASSERT_THROWS(gui_detail::validate_font_path("../fonts/Inter.woff2", SourceLocation{}));
}

LUMA_TEST(validate_font_family_accepts_safe_names) {
    bool threw = false;

    try {
        gui_detail::validate_font_family("Inter Tight_2", SourceLocation{});
    } catch (...) {
        threw = true;
    }

    ASSERT_FALSE(threw);
}

LUMA_TEST(validate_font_family_rejects_empty) {
    ASSERT_THROWS(gui_detail::validate_font_family("", SourceLocation{}));
}

LUMA_TEST(validate_font_family_rejects_special_characters) {
    ASSERT_THROWS(gui_detail::validate_font_family("Inter\"; }", SourceLocation{}));
    ASSERT_THROWS(gui_detail::validate_font_family("Inter;", SourceLocation{}));
}

LUMA_TEST(validate_font_weight_accepts_keywords_and_numbers) {
    bool threw = false;

    try {
        gui_detail::validate_font_weight("normal", SourceLocation{});
        gui_detail::validate_font_weight("bold", SourceLocation{});
        gui_detail::validate_font_weight("400", SourceLocation{});
        gui_detail::validate_font_weight("100 900", SourceLocation{});
    } catch (...) {
        threw = true;
    }

    ASSERT_FALSE(threw);
}

LUMA_TEST(validate_font_weight_rejects_unknown) {
    ASSERT_THROWS(gui_detail::validate_font_weight("heavy", SourceLocation{}));
    ASSERT_THROWS(gui_detail::validate_font_weight("400x", SourceLocation{}));
}

LUMA_TEST(validate_font_style_accepts_known_values) {
    bool threw = false;

    try {
        gui_detail::validate_font_style("normal", SourceLocation{});
        gui_detail::validate_font_style("italic", SourceLocation{});
        gui_detail::validate_font_style("oblique", SourceLocation{});
    } catch (...) {
        threw = true;
    }

    ASSERT_FALSE(threw);
}

LUMA_TEST(validate_font_style_rejects_unknown) {
    ASSERT_THROWS(gui_detail::validate_font_style("slanted", SourceLocation{}));
}

// ═══════════════════════════════════════════════════════════
// JSON serialisation (value_to_json) — white-box tests
// ═══════════════════════════════════════════════════════════

// value_to_json is defined in graphicalui_serialization.cpp (guarded by
// LUMA_HAS_WEBVIEW). luma_core links the webview backend PRIVATE, so this test
// TU does not see the guarded declaration in graphicalui_helpers.hpp; declare
// the symbol here to exercise it directly, mirroring the CSS white-box tests.
namespace luma::gui_detail {
[[nodiscard]] std::string value_to_json(const Value& v, int depth);
} // namespace luma::gui_detail

LUMA_TEST(value_to_json_nan_is_null) {
    // The output is eval'd as JS in the webview; the bare token "nan" is invalid
    // JS and would abort the whole render, so non-finite numbers must become null.
    ASSERT_EQ(gui_detail::value_to_json(Value{std::numeric_limits<double>::quiet_NaN()}, 0),
              std::string("null"));
}

LUMA_TEST(value_to_json_infinity_is_null) {
    ASSERT_EQ(gui_detail::value_to_json(Value{std::numeric_limits<double>::infinity()}, 0),
              std::string("null"));
    ASSERT_EQ(gui_detail::value_to_json(Value{-std::numeric_limits<double>::infinity()}, 0),
              std::string("null"));
}

LUMA_TEST(value_to_json_finite_number) {
    ASSERT_EQ(gui_detail::value_to_json(Value{1.5}, 0), std::string("1.5"));
}

// ─── Divergent-branch characterization (choice / record / tuple / depth) ───
// These pin the GUI serialiser's behaviour where it deliberately differs from
// the Json module serialiser: choices serialise to the rich
// {"variant":…,"fields":[…]} form (all arities), strings slash-escape for
// <script> safety, and exceeding the nesting-depth limit throws.

LUMA_TEST(value_to_json_nullary_choice_rich) {
    // Nullary choice keeps the rich object form with an empty fields array.
    const auto v = eval(R"(choice Color { Red, Green, Blue } Color.Green)");
    ASSERT_EQ(gui_detail::value_to_json(v, 0), std::string(R"({"variant":"Green","fields":[]})"));
}

LUMA_TEST(value_to_json_choice_with_field_rich) {
    const auto v =
        eval(R"(choice Shape { Circle(number radius), Square(number side) } Shape.Circle(3.5))");
    ASSERT_EQ(gui_detail::value_to_json(v, 0),
              std::string(R"({"variant":"Circle","fields":[3.5]})"));
}

LUMA_TEST(value_to_json_choice_field_slash_escaped) {
    // Slash-escaping must apply to strings nested inside choice fields too.
    const auto v = eval(R"(choice Tag { Path(string p) } Tag.Path("a/b"))");
    ASSERT_EQ(gui_detail::value_to_json(v, 0),
              std::string(R"({"variant":"Path","fields":["a\/b"]})"));
}

LUMA_TEST(value_to_json_record_object) {
    const auto v = eval(R"(record Point { integer x, integer y } Point { x = 1, y = 2 })");
    ASSERT_EQ(gui_detail::value_to_json(v, 0), std::string(R"({"x":1,"y":2})"));
}

LUMA_TEST(value_to_json_tuple_array) {
    const auto v = eval(R"((1, 2, 3))");
    ASSERT_EQ(gui_detail::value_to_json(v, 0), std::string("[1,2,3]"));
}

LUMA_TEST(value_to_json_string_slash_escaped) {
    // SECURITY: "</script>" must become "<\/script>" so embedded model data
    // cannot break out of the <script> element the JSON is inlined into.
    const auto v = eval(R"("</script>")");
    ASSERT_EQ(gui_detail::value_to_json(v, 0), std::string(R"("<\/script>")"));
}

LUMA_TEST(value_to_json_depth_exceeded_throws) {
    // The GUI serialiser throws past the nesting-depth limit (default 128); the
    // depth parameter drives the same guard the recursive walk hits.  200 is
    // safely above the limit regardless of runtime configuration in tests.
    ASSERT_THROWS(gui_detail::value_to_json(Value{1.5}, 200));
}

// ═══════════════════════════════════════════════════════════
// Layout helpers (elm-ui inspired)
// ═══════════════════════════════════════════════════════════

LUMA_TEST(wrapped_row) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.wrapped_row([GraphicalUi.label("A"), GraphicalUi.label("B")])
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "wrapped_row");
}

LUMA_TEST(scroll_row) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.scroll_row([GraphicalUi.label("A")])
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "scroll_row");
}

LUMA_TEST(scroll_column) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.scroll_column([GraphicalUi.label("A")])
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "scroll_column");
}

// ═══════════════════════════════════════════════════════════
// Nearby / overlay elements
// ═══════════════════════════════════════════════════════════

LUMA_TEST(above) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.above(GraphicalUi.label("Main"), GraphicalUi.label("Overlay"))
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "nearby");
}

LUMA_TEST(above_position) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.above(GraphicalUi.label("Main"), GraphicalUi.label("Top"))
        Dictionary.get_or(w, "position", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "above");
}

LUMA_TEST(below) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.below(GraphicalUi.label("Main"), GraphicalUi.label("Below"))
        Dictionary.get_or(w, "position", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "below");
}

LUMA_TEST(on_left) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.on_left(GraphicalUi.label("Main"), GraphicalUi.label("Left"))
        Dictionary.get_or(w, "position", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "left");
}

LUMA_TEST(on_right) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.on_right(GraphicalUi.label("Main"), GraphicalUi.label("Right"))
        Dictionary.get_or(w, "position", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "right");
}

LUMA_TEST(in_front) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.in_front(GraphicalUi.label("Main"), GraphicalUi.label("Overlay"))
        Dictionary.get_or(w, "position", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "in-front");
}

LUMA_TEST(behind) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.behind(GraphicalUi.label("Main"), GraphicalUi.label("BG"))
        Dictionary.get_or(w, "position", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "behind");
}

// ═══════════════════════════════════════════════════════════
// Layout debugging
// ═══════════════════════════════════════════════════════════

LUMA_TEST(debug) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.debug(GraphicalUi.label("Test"))
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "debug");
}

// ═══════════════════════════════════════════════════════════
// Animation primitives
// ═══════════════════════════════════════════════════════════

LUMA_TEST(transition) {
    const auto v = eval(R"(
        dictionary<string> props = {
            "property": "opacity",
            "duration": "0.5s",
            "easing": "ease-in-out"
        }
        dictionary<string> w = GraphicalUi.transition(
            GraphicalUi.label("Hello"),
            props
        )
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "transition");
}

LUMA_TEST(transition_properties) {
    const auto v = eval(R"(
        dictionary<string> props = {
            "property": "all",
            "duration": "1s"
        }
        dictionary<string> w = GraphicalUi.transition(
            GraphicalUi.label("Hello"),
            props
        )
        dictionary<string> p = Dictionary.get_or(w, "properties", {})
        Dictionary.get_or(p, "property", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "all");
}

LUMA_TEST(animate) {
    const auto v = eval(R"(
        array<dictionary<string>> kf = [
            {"opacity": "0"},
            {"opacity": "1"}
        ]
        dictionary<string> w = GraphicalUi.animate(
            GraphicalUi.label("Hello"),
            kf
        )
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "animate");
}

LUMA_TEST(animate_with_options) {
    const auto v = eval(R"LUMA(
        array<dictionary<string>> kf = [
            {"transform": "scale(0)"},
            {"transform": "scale(1)"}
        ]
        dictionary<string> opts = {
            "duration": "2s",
            "easing": "ease-out",
            "iterations": "infinite"
        }
        dictionary<string> w = GraphicalUi.animate(
            GraphicalUi.label("Hello"),
            kf,
            opts
        )
        Dictionary.get_or(w, "type", "")
    )LUMA");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "animate");
}

// ═══════════════════════════════════════════════════════════
// Typed sizing helpers
// ═══════════════════════════════════════════════════════════

LUMA_TEST(fill) {
    const auto v = eval("GraphicalUi.fill()");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "1 1 0%");
}

LUMA_TEST(fill_portion) {
    const auto v = eval("GraphicalUi.fill_portion(3)");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "3 1 0%");
}

LUMA_TEST(shrink) {
    const auto v = eval("GraphicalUi.shrink()");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "0 1 auto");
}

LUMA_TEST(px) {
    const auto v = eval("GraphicalUi.px(200)");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "200px");
}

LUMA_TEST(constrained_fill) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.constrained_fill(100, 500)
        Dictionary.get_or(d, "flex", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "1 1 0%");
}

LUMA_TEST(constrained_fill_min) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.constrained_fill(100, 500)
        Dictionary.get_or(d, "min_width", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "100px");
}

LUMA_TEST(constrained_fill_max) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.constrained_fill(100, 500)
        Dictionary.get_or(d, "max_width", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "500px");
}

// ═══════════════════════════════════════════════════════════
// Typed alignment helpers
// ═══════════════════════════════════════════════════════════

LUMA_TEST(center) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.center()
        Dictionary.get_or(d, "align_items", "") + "," + Dictionary.get_or(d, "justify_content", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "center,center");
}

LUMA_TEST(center_x) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.center_x()
        Dictionary.get_or(d, "justify_content", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "center");
}

LUMA_TEST(center_y) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.center_y()
        Dictionary.get_or(d, "align_items", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "center");
}

LUMA_TEST(align_left) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.align_left()
        Dictionary.get_or(d, "align_self", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "flex-start");
}

LUMA_TEST(align_right) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.align_right()
        Dictionary.get_or(d, "align_self", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "flex-end");
}

LUMA_TEST(align_top) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.align_top()
        Dictionary.get_or(d, "align_self", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "flex-start");
}

LUMA_TEST(align_bottom) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.align_bottom()
        Dictionary.get_or(d, "align_self", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "flex-end");
}

// ═══════════════════════════════════════════════════════════
// Spacing helpers
// ═══════════════════════════════════════════════════════════

LUMA_TEST(space_evenly) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.space_evenly()
        Dictionary.get_or(d, "justify_content", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "space-evenly");
}

LUMA_TEST(space_between) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.space_between()
        Dictionary.get_or(d, "justify_content", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "space-between");
}

LUMA_TEST(space_around) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.space_around()
        Dictionary.get_or(d, "justify_content", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "space-around");
}

LUMA_TEST(spacing) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.spacing(16)
        Dictionary.get_or(d, "gap", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "16px");
}

LUMA_TEST(spacing_xy) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.spacing_xy(10, 20)
        Dictionary.get_or(d, "column_gap", "") + "," + Dictionary.get_or(d, "row_gap", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "10px,20px");
}

LUMA_TEST(padding_helper) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.padding(12)
        Dictionary.get_or(d, "padding", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "12px");
}

LUMA_TEST(padding_xy) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.padding_xy(8, 16)
        Dictionary.get_or(d, "padding", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "16px 8px");
}

// ═══════════════════════════════════════════════════════════
// Horizontal spacer and flexible space
// ═══════════════════════════════════════════════════════════

LUMA_TEST(horizontal_spacer) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.horizontal_spacer(24)
        Dictionary.get_or(w, "width", 0)
    )");
    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 24);
}

LUMA_TEST(horizontal_spacer_default) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.horizontal_spacer()
        Dictionary.get_or(w, "width", 0)
    )");
    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 16);
}

LUMA_TEST(flexible_space) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.flexible_space()
        Dictionary.get_or(w, "flex", 0)
    )");
    ASSERT_TRUE(v.is_integer());
    ASSERT_EQ(v.as_integer(), 1);
}

// ═══════════════════════════════════════════════════════════
// Device classification
// ═══════════════════════════════════════════════════════════

LUMA_TEST(classify_device_phone) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.classify_device(375, 812)
        Dictionary.get_or(d, "class", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "phone");
}

LUMA_TEST(classify_device_tablet) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.classify_device(768, 1024)
        Dictionary.get_or(d, "class", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "tablet");
}

LUMA_TEST(classify_device_desktop) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.classify_device(1440, 900)
        Dictionary.get_or(d, "class", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "desktop");
}

LUMA_TEST(classify_device_big_desktop) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.classify_device(2560, 1440)
        Dictionary.get_or(d, "class", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "big_desktop");
}

LUMA_TEST(classify_device_orientation) {
    const auto v = eval(R"(
        dictionary<string> d = GraphicalUi.classify_device(375, 812)
        Dictionary.get_or(d, "orientation", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "portrait");
}

// ═══════════════════════════════════════════════════════════
// Conditional rendering
// ═══════════════════════════════════════════════════════════

LUMA_TEST(when_truthy) {
    const auto v = eval(R"(
        dictionary<string> w = GraphicalUi.when(true, GraphicalUi.label("Shown"))
        Dictionary.get_or(w, "type", "")
    )");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "label");
}

LUMA_TEST(when_falsy) {
    const auto v = eval(R"(
        optional<dictionary<string>> w = GraphicalUi.when(false, GraphicalUi.label("Hidden"))
        Optional.is_none(w)
    )");
    ASSERT_TRUE(v.is_bool());
    ASSERT_EQ(v.as_bool(), true);
}

// ═══════════════════════════════════════════════════════════
// Entry point
// ═══════════════════════════════════════════════════════════

int main() {
    LUMA_RUN_ALL();
}
