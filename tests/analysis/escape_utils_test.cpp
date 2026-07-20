// Unit tests for core/common/escape_utils.hpp.

#include <string>

#include "common/escape.hpp"
#include "test_framework.hpp"

using namespace luma;

// ═══════════════════════════════════════════════════════════
// json_escape
// ═══════════════════════════════════════════════════════════

static void test_json_escape_empty() {
    ASSERT_EQ(json_escape(""), std::string(""));
}

static void test_json_escape_plain_ascii() {
    ASSERT_EQ(json_escape("hello world"), std::string("hello world"));
}

static void test_json_escape_double_quote() {
    ASSERT_EQ(json_escape("say \"hi\""), std::string("say \\\"hi\\\""));
}

static void test_json_escape_backslash() {
    ASSERT_EQ(json_escape("a\\b"), std::string("a\\\\b"));
}

static void test_json_escape_newline() {
    ASSERT_EQ(json_escape("line1\nline2"), std::string("line1\\nline2"));
}

static void test_json_escape_carriage_return() {
    ASSERT_EQ(json_escape("line1\rline2"), std::string("line1\\rline2"));
}

static void test_json_escape_tab() {
    ASSERT_EQ(json_escape("col1\tcol2"), std::string("col1\\tcol2"));
}

static void test_json_escape_backspace() {
    std::string input = "a\bb";
    ASSERT_EQ(json_escape(input), std::string("a\\bb"));
}

static void test_json_escape_formfeed() {
    std::string input = "a\fb";
    ASSERT_EQ(json_escape(input), std::string("a\\fb"));
}

static void test_json_escape_forward_slash() {
    ASSERT_EQ(json_escape("a/b"), std::string("a\\/b"));
}

static void test_json_escape_closes_script_tag() {
    // GraphicalUi embeds value_to_json output inside a <script> element, so the
    // slash-escaping must turn "</script>" into "<\/script>" to prevent a
    // script-tag breakout / XSS. Pin that property at the escape layer.
    ASSERT_EQ(json_escape("</script>"), std::string("<\\/script>"));
}

static void test_json_escape_control_character() {
    // NUL (0x00)
    std::string input(1, '\0');
    ASSERT_EQ(json_escape(input), std::string("\\u0000"));
}

static void test_json_escape_control_char_0x1f() {
    // 0x1F = unit separator
    std::string input(1, '\x1F');
    ASSERT_EQ(json_escape(input), std::string("\\u001f"));
}

static void test_json_escape_mixed() {
    ASSERT_EQ(json_escape("key\t\"val\"\n"), std::string("key\\t\\\"val\\\"\\n"));
}

// ═══════════════════════════════════════════════════════════
// json_escape_string (append-to-buffer variant)
// ═══════════════════════════════════════════════════════════

static void test_json_escape_string_empty() {
    std::string out;
    json_escape_string("", out);
    ASSERT_EQ(out, std::string(""));
}

static void test_json_escape_string_plain_ascii() {
    std::string out;
    json_escape_string("hello world", out);
    ASSERT_EQ(out, std::string("hello world"));
}

static void test_json_escape_string_special_chars() {
    std::string out;
    json_escape_string("say \"hi\"\tnew\nline\\back", out);
    ASSERT_EQ(out, std::string("say \\\"hi\\\"\\tnew\\nline\\\\back"));
}

static void test_json_escape_string_control_char() {
    std::string out;
    std::string input(1, '\x01');
    json_escape_string(input, out);
    ASSERT_EQ(out, std::string("\\u0001"));
}

static void test_json_escape_string_appends() {
    std::string out = "prefix:";
    json_escape_string("a\"b", out);
    ASSERT_EQ(out, std::string("prefix:a\\\"b"));
}

static void test_json_escape_string_no_forward_slash() {
    // json_escape_string does NOT escape forward slashes.
    std::string out;
    json_escape_string("a/b", out);
    ASSERT_EQ(out, std::string("a/b"));
}

// ═══════════════════════════════════════════════════════════
// js_string_escape
// ═══════════════════════════════════════════════════════════

static void test_js_escape_empty() {
    ASSERT_EQ(js_string_escape(""), std::string(""));
}

static void test_js_escape_plain_ascii() {
    ASSERT_EQ(js_string_escape("hello"), std::string("hello"));
}

static void test_js_escape_single_quote() {
    ASSERT_EQ(js_string_escape("it's"), std::string("it\\'s"));
}

static void test_js_escape_backslash() {
    ASSERT_EQ(js_string_escape("a\\b"), std::string("a\\\\b"));
}

static void test_js_escape_newline() {
    ASSERT_EQ(js_string_escape("a\nb"), std::string("a\\nb"));
}

static void test_js_escape_carriage_return() {
    ASSERT_EQ(js_string_escape("a\rb"), std::string("a\\rb"));
}

static void test_js_escape_double_quote_preserved() {
    // Double quotes are NOT escaped by js_string_escape.
    ASSERT_EQ(js_string_escape("say \"hi\""), std::string("say \"hi\""));
}

static void test_js_escape_mixed() {
    ASSERT_EQ(js_string_escape("it's a\\path\n"), std::string("it\\'s a\\\\path\\n"));
}

// ─── main ───

int main() {
    // json_escape.
    RUN(test_json_escape_empty);
    RUN(test_json_escape_plain_ascii);
    RUN(test_json_escape_double_quote);
    RUN(test_json_escape_backslash);
    RUN(test_json_escape_newline);
    RUN(test_json_escape_carriage_return);
    RUN(test_json_escape_tab);
    RUN(test_json_escape_backspace);
    RUN(test_json_escape_formfeed);
    RUN(test_json_escape_forward_slash);
    RUN(test_json_escape_closes_script_tag);
    RUN(test_json_escape_control_character);
    RUN(test_json_escape_control_char_0x1f);
    RUN(test_json_escape_mixed);

    // json_escape_string.
    RUN(test_json_escape_string_empty);
    RUN(test_json_escape_string_plain_ascii);
    RUN(test_json_escape_string_special_chars);
    RUN(test_json_escape_string_control_char);
    RUN(test_json_escape_string_appends);
    RUN(test_json_escape_string_no_forward_slash);

    // js_string_escape.
    RUN(test_js_escape_empty);
    RUN(test_js_escape_plain_ascii);
    RUN(test_js_escape_single_quote);
    RUN(test_js_escape_backslash);
    RUN(test_js_escape_newline);
    RUN(test_js_escape_carriage_return);
    RUN(test_js_escape_double_quote_preserved);
    RUN(test_js_escape_mixed);

    return SUMMARY();
}
