// Regression tests for the embedded Solaris GUI prelude.
//
// The prelude source lives in core/analysis/prelude/gui_prelude.luma and is
// embedded verbatim into the analysis library by scripts/generate_prelude_asset.mjs
// (see gui_prelude_generated.hpp). These tests guard that relocation: the
// embedded bytes must survive intact and still lex and parse as valid Luma, so
// a drift between the .luma source and the generated header fails loudly here
// rather than as a baffling downstream error in a user's GUI program.

#include <string>

#include "analysis/prelude/gui_prelude.hpp"
#include "test_framework.hpp"
#include "test_parse_helper.hpp"

// The embedded surface is non-empty and preserves its exact byte boundaries:
// the raw-string literal it replaced began and ended with a newline, and that
// framing keeps prelude line numbers stable for diagnostic attribution.
static void test_prelude_source_byte_boundaries() {
    const std::string& source = luma::prelude::gui_prelude_source();

    ASSERT_FALSE(source.empty());
    ASSERT_EQ(source.front(), '\n');
    ASSERT_EQ(source.back(), '\n');
}

// The embedded surface still contains the load-bearing declarations, so a
// truncated or mis-encoded payload is caught.
static void test_prelude_source_contains_surface() {
    const std::string& source = luma::prelude::gui_prelude_source();

    ASSERT_NE(source.find("choice Emphasis {"), std::string::npos);
    ASSERT_NE(source.find("record View {"), std::string::npos);
    ASSERT_NE(source.find("namespace Solaris {"), std::string::npos);
    // Newer typed tokens/records must survive the relocation too.
    ASSERT_NE(source.find("choice InputType {"), std::string::npos);
    ASSERT_NE(source.find("choice TextDecoration {"), std::string::npos);
    ASSERT_NE(source.find("choice Motion {"), std::string::npos);
    ASSERT_NE(source.find("choice Orientation {"), std::string::npos);
    ASSERT_NE(source.find("record NavItem {"), std::string::npos);
    ASSERT_NE(source.find("record MenuItem {"), std::string::npos);
    ASSERT_NE(source.find("choice Variant {"), std::string::npos);
}

// The core relocation invariant: the embedded bytes must lex and parse cleanly.
// This mirrors the runtime assert in prepend_prelude but pins it as a test.
static void test_prelude_source_parses_without_errors() {
    const std::string& source = luma::prelude::gui_prelude_source();

    ASSERT_TRUE(parse_errors(source).empty());
}

// The trigger detection is unchanged: only a standalone `Solaris` identifier
// requests the prelude.
static void test_source_uses_gui_matches_trigger() {
    ASSERT_TRUE(luma::prelude::source_uses_gui("Solaris.app(config)"));
    ASSERT_FALSE(luma::prelude::source_uses_gui("mySolarisHelper()"));
    ASSERT_FALSE(luma::prelude::source_uses_gui("integer x = 1"));
}

int main() {
    RUN(test_prelude_source_byte_boundaries);
    RUN(test_prelude_source_contains_surface);
    RUN(test_prelude_source_parses_without_errors);
    RUN(test_source_uses_gui_matches_trigger);

    return SUMMARY();
}
