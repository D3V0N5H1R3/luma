// Unit tests for the Windows-only build_windows_cmdline helper in
// core/runtime/stdlib/system/platform_process_win32.cpp. This target is
// compiled only on Windows (see tests/CMakeLists.txt).
//
// build_windows_cmdline re-quotes a tokenised argv vector into a single command
// line for CreateProcessA, so it is the output-side mirror of tokenize_command
// (whose grammar is pinned by stdlib_test_process.cpp). Getting the MSVC C
// runtime backslash/quote rules wrong reopens an argument-injection vector, so
// the tricky cases are verified semantically: each generated command line is
// handed back to the OS parser (CommandLineToArgvW) and must reconstruct the
// exact original arguments.

#include <cstddef>
#include <string>
#include <vector>

#include "common/platform_utils.hpp"
#include "runtime/stdlib/system/platform_process.hpp"
#include "test_framework.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <shellapi.h>
#include <windows.h>

using namespace luma::platform_process;

namespace {

// Round-trip an argv vector through build_windows_cmdline and the OS command
// line parser. A synthetic program name is prepended because CommandLineToArgvW
// parses argv[0] with different (program-name) rules; every argument after it
// must survive unchanged.
void check_roundtrip(const std::vector<std::string>& argv) {
    const std::string cmdline = "prog " + build_windows_cmdline(argv);

    // Test inputs are ASCII, so a byte-wise widen reproduces the command line.
    const std::wstring wide(cmdline.begin(), cmdline.end());

    int argc = 0;
    wchar_t** parsed = CommandLineToArgvW(wide.c_str(), &argc);
    ASSERT_TRUE(parsed != nullptr);

    // Copy the parsed arguments out before freeing so an assertion failure below
    // cannot leak the OS-allocated block.
    std::vector<std::string> got;
    got.reserve(static_cast<std::size_t>(argc));

    for (int i = 0; i < argc; ++i) {
        got.push_back(luma::wstring_to_utf8(parsed[i]));
    }

    LocalFree(parsed);

    // got[0] is the synthetic program name; the remainder must match the input.
    ASSERT_EQ(got.size(), argv.size() + 1);

    for (std::size_t i = 0; i < argv.size(); ++i) {
        ASSERT_EQ(got[i + 1], argv[i]);
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════
// build_windows_cmdline — literal output (documents when quoting is applied)
// ═══════════════════════════════════════════════════════════

static void test_cmdline_plain_args_not_quoted() {
    // No argument needs quoting, so each is emitted verbatim, space-separated.
    ASSERT_EQ(build_windows_cmdline({"cmd", "/c", "echo"}), std::string("cmd /c echo"));
}

static void test_cmdline_empty_argv_is_empty() {
    ASSERT_EQ(build_windows_cmdline({}), std::string(""));
}

static void test_cmdline_single_empty_arg_becomes_quotes() {
    // An empty argument must survive as an explicit empty token.
    ASSERT_EQ(build_windows_cmdline({""}), std::string("\"\""));
}

static void test_cmdline_arg_with_space_is_quoted() {
    ASSERT_EQ(build_windows_cmdline({"a b"}), std::string("\"a b\""));
}

static void test_cmdline_empty_middle_arg_is_quoted() {
    ASSERT_EQ(build_windows_cmdline({"a", "", "b"}), std::string("a \"\" b"));
}

static void test_cmdline_backslash_without_special_not_quoted() {
    // "a\\b" is the two-character sequence a-backslash-b; with no space or quote
    // it needs no quoting and no backslash doubling.
    ASSERT_EQ(build_windows_cmdline({"a\\b"}), std::string("a\\b"));
}

static void test_cmdline_trailing_backslash_without_space_not_quoted() {
    // A path ending in a backslash but containing no space is emitted verbatim.
    ASSERT_EQ(build_windows_cmdline({"c:\\dir\\"}), std::string("c:\\dir\\"));
}

// ═══════════════════════════════════════════════════════════
// build_windows_cmdline — OS round-trip (verifies the escaping is correct)
// ═══════════════════════════════════════════════════════════

static void test_roundtrip_embedded_quote() {
    check_roundtrip({"a\"b"});
}

static void test_roundtrip_embedded_backslash() {
    check_roundtrip({"a\\b"});
}

static void test_roundtrip_trailing_backslash_no_space() {
    check_roundtrip({"c:\\dir\\"});
}

static void test_roundtrip_trailing_backslash_with_space() {
    // Space forces quoting; the trailing backslash must be doubled so it does
    // not escape the closing quote.
    check_roundtrip({"a b\\"});
}

static void test_roundtrip_backslash_before_quote() {
    check_roundtrip({"a\\\"b"});
}

static void test_roundtrip_many_backslashes_before_quote() {
    check_roundtrip({"x\\\\\\\"y"});
}

static void test_roundtrip_quotes_and_spaces() {
    check_roundtrip({"he said \"hi\" now"});
}

static void test_roundtrip_embedded_tab() {
    check_roundtrip({"tab\ttab"});
}

static void test_roundtrip_windows_path_with_flags() {
    check_roundtrip({"c:\\program files\\app.exe", "--out", "file with spaces.txt"});
}

static void test_roundtrip_unc_path_with_space_and_trailing_backslash() {
    check_roundtrip({"\\\\server\\share\\path with space\\"});
}

static void test_roundtrip_injection_attempt_stays_single_arg() {
    // A naive concatenation would let the embedded quote close the argument and
    // expose "& calc.exe" to a shell; correct quoting keeps it one argument.
    check_roundtrip({"foo\" & calc.exe"});
}

static void test_roundtrip_multiple_mixed_args() {
    check_roundtrip({"plain", "with space", "with\"quote", "trailing\\", ""});
}

// ─── main ───

int main() {
    RUN(test_cmdline_plain_args_not_quoted);
    RUN(test_cmdline_empty_argv_is_empty);
    RUN(test_cmdline_single_empty_arg_becomes_quotes);
    RUN(test_cmdline_arg_with_space_is_quoted);
    RUN(test_cmdline_empty_middle_arg_is_quoted);
    RUN(test_cmdline_backslash_without_special_not_quoted);
    RUN(test_cmdline_trailing_backslash_without_space_not_quoted);

    RUN(test_roundtrip_embedded_quote);
    RUN(test_roundtrip_embedded_backslash);
    RUN(test_roundtrip_trailing_backslash_no_space);
    RUN(test_roundtrip_trailing_backslash_with_space);
    RUN(test_roundtrip_backslash_before_quote);
    RUN(test_roundtrip_many_backslashes_before_quote);
    RUN(test_roundtrip_quotes_and_spaces);
    RUN(test_roundtrip_embedded_tab);
    RUN(test_roundtrip_windows_path_with_flags);
    RUN(test_roundtrip_unc_path_with_space_and_trailing_backslash);
    RUN(test_roundtrip_injection_attempt_stays_single_arg);
    RUN(test_roundtrip_multiple_mixed_args);

    return SUMMARY();
}
