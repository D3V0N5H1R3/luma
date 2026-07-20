// GraphicalUi stub C++ unit tests.
//
// Exercises the GraphicalUi module's stub path — the code compiled when no
// WebView backend is available (LUMA_FEATURE_WEBVIEW=OFF, or webkit2gtk/WebKit
// not found). In that configuration luma_core defines no LUMA_HAS_WEBVIEW, so
// graphicalui_module.cpp compiles its stub: constants still resolve, every
// function is registered but throws a descriptive "not available" error, and the
// shared CSS validation helpers remain available.
//
// This is the counterpart to the live-module suites
// (stdlib_test_graphicalui_widgets/_styling/_app). The test registration in
// tests/CMakeLists.txt selects either this stub or those three based on
// LUMA_WEBVIEW_AVAILABLE, so the active macro-scope is always covered.

#include "runtime/stdlib/io/graphicalui_css.hpp"
#include "stdlib_test_helpers.hpp"

// ═══════════════════════════════════════════════════════════
// Constants — registered in both the live and stub builds.
// ═══════════════════════════════════════════════════════════

static void test_stub_constant_info() {
    const auto v = eval("GraphicalUi.INFO");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "info");
}

static void test_stub_constant_theme() {
    const auto v = eval("GraphicalUi.THEME");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "theme");
}

static void test_stub_constant_model() {
    const auto v = eval("GraphicalUi.MODEL");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "model");
}

// ═══════════════════════════════════════════════════════════
// Function registration — every function is registered (so name
// resolution succeeds) but throws at call time.
// ═══════════════════════════════════════════════════════════

static void test_stub_functions_registered() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("GraphicalUi.label"));
    ASSERT_TRUE(env->has("GraphicalUi.button"));
    ASSERT_TRUE(env->has("GraphicalUi.style"));
    ASSERT_TRUE(env->has("GraphicalUi.app"));
    ASSERT_TRUE(env->has("GraphicalUi.stylesheet"));
}

// ═══════════════════════════════════════════════════════════
// Unavailable behaviour — every function throws a descriptive error.
// ═══════════════════════════════════════════════════════════

static void test_stub_label_throws_unavailable() {
    ASSERT_THROWS_WITH_MESSAGE(eval(R"(GraphicalUi.label("Hello"))"), "not available");
}

static void test_stub_button_throws() {
    ASSERT_THROWS(eval(R"(GraphicalUi.button("Click", () -> {}))"));
}

static void test_stub_style_throws() {
    ASSERT_THROWS(eval(R"(GraphicalUi.style({"color": "red"}))"));
}

static void test_stub_merge_styles_throws() {
    ASSERT_THROWS(eval(R"(GraphicalUi.merge_styles({"a": "1"}, {"b": "2"}))"));
}

static void test_stub_row_throws() {
    ASSERT_THROWS(eval(R"(GraphicalUi.row([]))"));
}

static void test_stub_app_throws() {
    ASSERT_THROWS(eval(R"(GraphicalUi.app({"title": "x"}))"));
}

// ═══════════════════════════════════════════════════════════
// Shared CSS validation helpers — compiled unconditionally, so they
// still work in the stub build (white-box, no module dependency).
// ═══════════════════════════════════════════════════════════

static void test_stub_css_known_property() {
    ASSERT_TRUE(gui_detail::is_known_css_property("color"));
    ASSERT_FALSE(gui_detail::is_known_css_property("zzzqqqxxx"));
}

static void test_stub_css_suggest_property() {
    ASSERT_EQ(gui_detail::suggest_css_property("colour"), "color");
}

static void test_stub_css_sanitise_passes_safe() {
    const std::string css = ".a { color: red; padding: 8px; }";

    ASSERT_EQ(gui_detail::sanitise_loaded_css(css), css);
}

static void test_stub_css_sanitise_strips_script() {
    const auto out = gui_detail::sanitise_loaded_css("<script>evil()</script> .a { color: red; }");

    ASSERT_TRUE(out.find("<script") == std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// Entry point
// ═══════════════════════════════════════════════════════════

int main() {
    // Constants.
    RUN(test_stub_constant_info);
    RUN(test_stub_constant_theme);
    RUN(test_stub_constant_model);

    // Registration and unavailable behaviour.
    RUN(test_stub_functions_registered);
    RUN(test_stub_label_throws_unavailable);
    RUN(test_stub_button_throws);
    RUN(test_stub_style_throws);
    RUN(test_stub_merge_styles_throws);
    RUN(test_stub_row_throws);
    RUN(test_stub_app_throws);

    // Shared CSS helpers.
    RUN(test_stub_css_known_property);
    RUN(test_stub_css_suggest_property);
    RUN(test_stub_css_sanitise_passes_safe);
    RUN(test_stub_css_sanitise_strips_script);

    return SUMMARY();
}
