// Unit tests for the cross-platform safe_getenv helper in
// core/common/platform_utils.hpp. This target is compiled on every platform
// (std::getenv on POSIX, _dupenv_s on MSVC); Windows-only helpers are covered
// by win32_utf8_test.cpp.

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

// ─── main ───

int main() {
    RUN(test_safe_getenv_existing_var);
    RUN(test_safe_getenv_nonexistent_var);

    return SUMMARY();
}
