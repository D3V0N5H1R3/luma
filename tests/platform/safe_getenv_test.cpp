// Unit tests for the cross-platform safe_getenv helper in
// core/common/platform_utils.hpp. This target is compiled on every platform
// (std::getenv on POSIX, _wdupenv_s on MSVC); Windows-only helpers are covered
// by win32_utf8_test.cpp.

#include <cstdlib>
#include <optional>
#include <string>

#include "common/platform_utils.hpp"
#include "test_framework.hpp"

using luma::safe_getenv;
using luma::test::ScopedEnv;

// ═══════════════════════════════════════════════════════════
// safe_getenv
// ═══════════════════════════════════════════════════════════

static void test_safe_getenv_existing_var() {
    // PATH is set on virtually every OS.
    const auto result = safe_getenv("PATH");
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->empty());
}

static void test_safe_getenv_nonexistent_var() {
    const auto result = safe_getenv("LUMA_TEST_NONEXISTENT_VAR_XYZ_999");
    ASSERT_FALSE(result.has_value());
}

static void test_safe_getenv_returns_utf8() {
    // Regression: on MSVC the value came from _dupenv_s in the ANSI code page,
    // so non-ASCII text (e.g. a LUMA_PATH entry) was not UTF-8.
    const std::string utf8_value = "caf\xC3\xA9-\xE2\x82\xAC";
#ifdef _WIN32
    // ScopedEnv removes the variable again on exit. The value itself is then set
    // through the wide API so the test does not depend on the ANSI code page.
    const ScopedEnv cleanup{"LUMA_TEST_UTF8_VALUE", "placeholder"};
    ASSERT_EQ(_wputenv_s(L"LUMA_TEST_UTF8_VALUE", L"caf\u00E9-\u20AC"), 0);
#else
    const ScopedEnv env{"LUMA_TEST_UTF8_VALUE", utf8_value};
#endif

    const auto result = safe_getenv("LUMA_TEST_UTF8_VALUE");

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, utf8_value);
}

// ─── main ───

int main() {
    RUN(test_safe_getenv_existing_var);
    RUN(test_safe_getenv_nonexistent_var);
    RUN(test_safe_getenv_returns_utf8);

    return SUMMARY();
}
