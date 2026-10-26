// Unit tests for the cross-platform safe_getenv helper in
// core/common/platform_utils.hpp. This target is compiled on every platform
// (std::getenv on POSIX, _wdupenv_s on MSVC); Windows-only helpers are covered
// by win32_utf8_test.cpp.

#include <cstdlib>
#include <optional>
#include <string>

#include "common/platform_utils.hpp"
#include "test_framework.hpp"

using namespace luma;

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
    // Set through the wide API so the test does not depend on the code page.
    ASSERT_TRUE(_wputenv_s(L"LUMA_TEST_UTF8_VALUE", L"caf\u00E9-\u20AC") == 0);
#else
    ASSERT_TRUE(setenv("LUMA_TEST_UTF8_VALUE", utf8_value.c_str(), 1) ==
                0); // NOLINT(concurrency-mt-unsafe)
#endif

    const auto result = safe_getenv("LUMA_TEST_UTF8_VALUE");

#ifdef _WIN32
    (void)_wputenv_s(L"LUMA_TEST_UTF8_VALUE", L"");
#else
    (void)unsetenv("LUMA_TEST_UTF8_VALUE"); // NOLINT(concurrency-mt-unsafe)
#endif

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
