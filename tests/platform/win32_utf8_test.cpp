// Unit tests for the Windows-only wstring_to_utf8 helper in
// core/common/platform_utils.hpp. This target is compiled only on Windows
// (see tests/CMakeLists.txt); cross-platform helpers are covered by
// safe_getenv_test.cpp.

#include <string>

#include "common/platform_utils.hpp"
#include "test_framework.hpp"

using namespace luma;

// ═══════════════════════════════════════════════════════════
// wstring_to_utf8
// ═══════════════════════════════════════════════════════════

static void test_wstring_to_utf8_empty() {
    ASSERT_EQ(wstring_to_utf8(L""), std::string(""));
}

static void test_wstring_to_utf8_ascii() {
    ASSERT_EQ(wstring_to_utf8(L"hello"), std::string("hello"));
}

static void test_wstring_to_utf8_non_ascii() {
    // L"ä" = U+00E4 → UTF-8: 0xC3 0xA4
    const std::string expected = "\xC3\xA4";
    ASSERT_EQ(wstring_to_utf8(L"\u00E4"), expected);
}

// ─── main ───

int main() {
    RUN(test_wstring_to_utf8_empty);
    RUN(test_wstring_to_utf8_ascii);
    RUN(test_wstring_to_utf8_non_ascii);

    return SUMMARY();
}
