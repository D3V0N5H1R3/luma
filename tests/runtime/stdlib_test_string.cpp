// Standard library tests: String.

#include "common/resource_limits.hpp"
#include "stdlib_test_helpers.hpp"

static void test_native_type_error_is_catchable() {
    // A type-confused 'any' value reaching a native accessor used to raise
    // std::bad_variant_access — which the VM's RuntimeError-only dispatch could
    // not catch — bypassing Luma try/catch and aborting the whole process. The
    // call_native safety net converts it into a catchable RuntimeError, so the
    // surrounding Luma try/catch now runs and the program survives.
    const auto v = eval(R"(
        array<any> mixed = [42]
        mutable boolean caught = false
        try {
            boolean ignored = String.contains("hi", mixed[0])
        } catch (e) {
            caught = true
        }
        caught
    )");
    ASSERT_TRUE(v.as_bool());
}

static void test_string_byte_length() {
    ASSERT_EQ(eval("String.byte_length(\"hello\")").as_integer(), 5);
    ASSERT_EQ(eval("String.byte_length(\"\")").as_integer(), 0);
}

static void test_string_byte_length_multibyte() {
    // "café" — 5 bytes.
    ASSERT_EQ(eval("String.byte_length(Result.unwrap(String.from_codepoints([99, 97, 102, 233])))")
                  .as_integer(),
              5);
}

static void test_string_characters_multibyte() {
    // "Ñ" (U+00D1, codepoint 209) is 2 bytes — characters() should yield 1 element.
    const auto v = eval("String.characters(Result.unwrap(String.from_codepoints([209])))");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 1U);
}

static void test_string_contains() {
    const auto v = eval("String.contains(\"hello world\", \"world\")");

    ASSERT_EQ(v.as_bool(), true);
}

static void test_string_equals_ignore_case() {
    ASSERT_TRUE(eval("String.equals_ignore_case(\"Hello\", \"hello\")").as_bool());
    ASSERT_TRUE(eval("String.equals_ignore_case(\"HELLO\", \"hello\")").as_bool());
    ASSERT_FALSE(eval("String.equals_ignore_case(\"hello\", \"world\")").as_bool());
    ASSERT_TRUE(eval("String.equals_ignore_case(\"\", \"\")").as_bool());
    ASSERT_FALSE(eval("String.equals_ignore_case(\"abc\", \"ab\")").as_bool());
    // Non-ASCII bytes compare literally (case folding is ASCII-only).
    ASSERT_TRUE(eval("String.equals_ignore_case(\"café\", \"CAFé\")").as_bool());
    ASSERT_FALSE(eval("String.equals_ignore_case(\"café\", \"CAFÉ\")").as_bool());
}

static void test_string_contains_ignore_case() {
    ASSERT_TRUE(eval("String.contains_ignore_case(\"Hello World\", \"world\")").as_bool());
    ASSERT_TRUE(eval("String.contains_ignore_case(\"Hello World\", \"WORLD\")").as_bool());
    ASSERT_FALSE(eval("String.contains_ignore_case(\"Hello World\", \"xyz\")").as_bool());
    // An empty needle always matches.
    ASSERT_TRUE(eval("String.contains_ignore_case(\"abc\", \"\")").as_bool());
}

static void test_string_ends_with() {
    const auto v = eval("String.ends_with(\"hello world\", \"world\")");

    ASSERT_EQ(v.as_bool(), true);
}

static void test_string_from_bytes() {
    const auto v = eval("String.from_bytes([72, 105])");

    ASSERT_RESULT_SUCCESS(v);
}

static void test_string_from_bytes_above_255() {
    // Byte value above 255 is out of range.
    ASSERT_EVAL_FAILURE("String.from_bytes([256])");
}

static void test_string_from_bytes_empty() {
    ASSERT_EVAL_STR("String.from_bytes([])", "");
}

static void test_string_from_bytes_negative() {
    // Byte value below 0 is out of range.
    ASSERT_EVAL_FAILURE("String.from_bytes([-1])");
}

static void test_string_from_codepoints() {
    ASSERT_EVAL_STR("String.from_codepoints([72, 105])", "Hi");
}

static void test_string_from_codepoints_empty() {
    ASSERT_EVAL_STR("String.from_codepoints([])", "");
}

static void test_string_from_codepoints_invalid() {
    // Codepoint above U+10FFFF is invalid.
    ASSERT_EVAL_FAILURE("String.from_codepoints([1114112])");
}

static void test_string_index_of() {
    ASSERT_EVAL_INT("String.index_of(\"hello\", \"ll\")", 2);
    ASSERT_EVAL_FAILURE("String.index_of(\"hello\", \"xyz\")");
}

static void test_string_is_digit() {
    ASSERT_TRUE(eval("String.is_digit(\"12345\")").as_bool());
    ASSERT_FALSE(eval("String.is_digit(\"\")").as_bool());
    ASSERT_FALSE(eval("String.is_digit(\"12a\")").as_bool());
}

static void test_string_is_empty() {
    ASSERT_EQ(eval("String.is_empty(\"\")").as_bool(), true);
    ASSERT_EQ(eval("String.is_empty(\"a\")").as_bool(), false);
}

static void test_string_is_lowercase() {
    ASSERT_TRUE(eval("String.is_lowercase(\"hello\")").as_bool());
    ASSERT_FALSE(eval("String.is_lowercase(\"Hello\")").as_bool());
    ASSERT_FALSE(eval("String.is_lowercase(\"\")").as_bool());
}

static void test_string_is_uppercase() {
    ASSERT_TRUE(eval("String.is_uppercase(\"HELLO\")").as_bool());
    ASSERT_FALSE(eval("String.is_uppercase(\"Hello\")").as_bool());
    ASSERT_FALSE(eval("String.is_uppercase(\"\")").as_bool());
}

static void test_string_is_whitespace() {
    ASSERT_TRUE(eval("String.is_whitespace(\"   \")").as_bool());
    ASSERT_FALSE(eval("String.is_whitespace(\"\")").as_bool());
    ASSERT_FALSE(eval("String.is_whitespace(\"hello\")").as_bool());
}

static void test_string_length() {
    const auto v = eval("String.length(\"hello\")");

    ASSERT_EQ(v.as_integer(), 5);
}

static void test_string_length_multibyte() {
    // "café" has 4 codepoints but 5 bytes (é is 2 bytes in UTF-8).
    // Build via String.from_codepoints to avoid Luma lexer escape-sequence issues.
    ASSERT_EQ(eval("String.length(Result.unwrap(String.from_codepoints([99, 97, 102, 233])))")
                  .as_integer(),
              4);
}

static void test_string_matches_no_match() {
    ASSERT_EVAL_BOOL("String.matches(\"file.txt\", \"*.luma\")", false);
}

static void test_string_matches_ok() {
    ASSERT_EVAL_BOOL("String.matches(\"file.luma\", \"*.luma\")", true);
}

static void test_string_matches_redos_safe() {
    // Translating this glob to a regex backtracks catastrophically: every segment
    // is `*a` and the trailing `b` never appears in the all-`a` subject.  The
    // linear matcher rejects it in O(pattern * subject) with no backtracking, so
    // it returns promptly (a success result holding false) instead of hanging on
    // libstdc++/libc++ or raising regex_complexity on MSVC.
    ASSERT_EVAL_BOOL("String.matches(\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\", "
                     "\"*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*b\")",
                     false);
}

static void test_string_matches_wildcard_spans_newline() {
    // `*` means "any characters" per the documented glob contract, so it spans a
    // newline — unlike a regex `.*`, which stops at one.  The subject "a\nb" is
    // built via from_codepoints (10 = '\n') to avoid lexer escape-sequence quirks.
    ASSERT_EVAL_BOOL("String.matches(Result.unwrap(String.from_codepoints([97, 10, 98])), \"a*b\")",
                     true);
}

static void test_string_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("String.length"));
    ASSERT_TRUE(env->has("String.uppercase"));
    ASSERT_TRUE(env->has("String.lowercase"));
    ASSERT_TRUE(env->has("String.byte_length"));
    ASSERT_TRUE(env->has("String.is_whitespace"));
    ASSERT_TRUE(env->has("String.is_uppercase"));
    ASSERT_TRUE(env->has("String.is_lowercase"));
    ASSERT_TRUE(env->has("String.is_digit"));
    ASSERT_TRUE(env->has("String.to_codepoints"));
    ASSERT_TRUE(env->has("String.from_codepoints"));
    ASSERT_TRUE(env->has("String.to_bytes"));
    ASSERT_TRUE(env->has("String.from_bytes"));
}

static void test_string_pad_left() {
    ASSERT_EVAL_STR("String.pad_left(\"42\", 5, \"0\")", "00042");
}

static void test_string_pad_left_fail() {
    ASSERT_EVAL_FAILURE("String.pad_left(\"x\", 20000000, \" \")");
}

static void test_string_pad_right() {
    ASSERT_EVAL_STR("String.pad_right(\"hi\", 5, \".\")", "hi...");
}

static void test_string_parse_integer() {
    const auto v = eval("String.parse_integer(\"42\")");

    ASSERT_RESULT_SUCCESS(v);
}

static void test_string_parse_number() {
    const auto v = eval("String.parse_number(\"3.14\")");

    ASSERT_RESULT_SUCCESS(v);
}

// Regression: String.parse_number must reject non-finite parses (NaN / ±Inf)
// rather than silently returning Infinity/NaN, consistent with
// Converter.to_number and the stdlib numeric-validity contract.
static void test_string_parse_number_rejects_non_finite() {
    ASSERT_EVAL_FAILURE("String.parse_number(\"inf\")");
    ASSERT_EVAL_FAILURE("String.parse_number(\"infinity\")");
    ASSERT_EVAL_FAILURE("String.parse_number(\"nan\")");

    const auto v = eval("String.parse_number(\"2.5\")");
    ASSERT_RESULT_SUCCESS(v);
}

static void test_string_repeat() {
    ASSERT_EVAL_STR("String.repeat(\"ab\", 3)", "ababab");
}

static void test_string_repeat_fail() {
    ASSERT_EVAL_FAILURE("String.repeat(\"x\", 20000000)");
}

static void test_string_replace() {
    // replace replaces only the first occurrence
    const auto v = eval("String.replace(\"aXaXa\", \"X\", \"O\")");

    ASSERT_EQ(v.as_string(), "aOaXa");
}

static void test_string_replace_all() {
    const auto v = eval("String.replace_all(\"aXaXa\", \"X\", \"O\")");

    ASSERT_EQ(v.as_string(), "aOaOa");
}

static void test_string_replace_all_no_match() {
    const auto v = eval("String.replace_all(\"hello\", \"z\", \"O\")");

    ASSERT_EQ(v.as_string(), "hello");
}

static void test_string_replace_first_only() {
    const auto v = eval("String.replace(\"hello world world\", \"world\", \"luma\")");

    ASSERT_EQ(v.as_string(), "hello luma world");
}

static void test_string_reverse() {
    const auto v = eval("String.reverse(\"abc\")");

    ASSERT_EQ(v.as_string(), "cba");
}

static void test_string_reverse_multibyte() {
    // Reversing "café" should produce "éfac", not garbled bytes.
    const auto reversed =
        eval("String.reverse(Result.unwrap(String.from_codepoints([99, 97, 102, 233])))")
            .as_string();
    const auto expected =
        eval("Result.unwrap(String.from_codepoints([233, 102, 97, 99]))").as_string();

    ASSERT_EQ(reversed, expected);
}

static void test_string_split() {
    const auto v = eval("String.split(\"a,b,c\", \",\")");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "a");
    ASSERT_EQ((*v.as_array()->elements)[2].as_string(), "c");
}

static void test_string_starts_with() {
    const auto v = eval("String.starts_with(\"hello world\", \"hello\")");

    ASSERT_EQ(v.as_bool(), true);
}

static void test_string_substring() {
    const auto v = eval("String.substring(\"hello\", 1, 3)");

    ASSERT_EQ(v.as_string(), "el");
}

static void test_string_to_bytes() {
    const auto v = eval("String.to_bytes(\"AB\")");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 65);
}

static void test_string_to_bytes_empty() {
    const auto v = eval("String.to_bytes(\"\")");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 0U);
}

static void test_string_to_codepoints() {
    const auto v = eval("String.to_codepoints(\"AB\")");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 65);
    ASSERT_EQ((*v.as_array()->elements)[1].as_integer(), 66);
}

static void test_string_to_codepoints_empty() {
    const auto v = eval("String.to_codepoints(\"\")");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 0U);
}

static void test_string_to_codepoints_multibyte() {
    // "é" is U+00E9 = 233.
    const auto v = eval("String.to_codepoints(Result.unwrap(String.from_codepoints([233])))");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 1U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 233);
}

static void test_string_trim() {
    const auto v = eval("String.trim(\"  hello  \")");

    ASSERT_EQ(v.as_string(), "hello");
}

static void test_string_to_snake_case() {
    ASSERT_EQ(eval("String.to_snake_case(\"helloWorld\")").as_string(), "hello_world");
    ASSERT_EQ(eval("String.to_snake_case(\"HTTPServer\")").as_string(), "http_server");
    ASSERT_EQ(eval("String.to_snake_case(\"already_snake\")").as_string(), "already_snake");
    ASSERT_EQ(eval("String.to_snake_case(\"PascalCase\")").as_string(), "pascal_case");
    ASSERT_EQ(eval("String.to_snake_case(\"\")").as_string(), "");
}

static void test_string_to_camel_case() {
    ASSERT_EQ(eval("String.to_camel_case(\"hello_world\")").as_string(), "helloWorld");
    ASSERT_EQ(eval("String.to_camel_case(\"hello-world\")").as_string(), "helloWorld");
    ASSERT_EQ(eval("String.to_camel_case(\"PascalCase\")").as_string(), "pascalCase");
    ASSERT_EQ(eval("String.to_camel_case(\"\")").as_string(), "");
}

static void test_string_to_kebab_case() {
    ASSERT_EQ(eval("String.to_kebab_case(\"helloWorld\")").as_string(), "hello-world");
    ASSERT_EQ(eval("String.to_kebab_case(\"HTTPServer\")").as_string(), "http-server");
    ASSERT_EQ(eval("String.to_kebab_case(\"snake_case\")").as_string(), "snake-case");
    ASSERT_EQ(eval("String.to_kebab_case(\"\")").as_string(), "");
}

static void test_string_to_pascal_case() {
    ASSERT_EQ(eval("String.to_pascal_case(\"hello_world\")").as_string(), "HelloWorld");
    ASSERT_EQ(eval("String.to_pascal_case(\"hello-world\")").as_string(), "HelloWorld");
    ASSERT_EQ(eval("String.to_pascal_case(\"camelCase\")").as_string(), "CamelCase");
    ASSERT_EQ(eval("String.to_pascal_case(\"\")").as_string(), "");
}

static void test_string_is_palindrome() {
    ASSERT_TRUE(eval("String.is_palindrome(\"racecar\")").as_bool());
    ASSERT_TRUE(eval("String.is_palindrome(\"madam\")").as_bool());
    ASSERT_FALSE(eval("String.is_palindrome(\"hello\")").as_bool());
    ASSERT_TRUE(eval("String.is_palindrome(\"\")").as_bool());
    ASSERT_TRUE(eval("String.is_palindrome(\"a\")").as_bool());
}

static void test_string_levenshtein_distance() {
    ASSERT_EQ(eval("String.levenshtein_distance(\"kitten\", \"sitting\")").as_integer(), 3);
    ASSERT_EQ(eval("String.levenshtein_distance(\"hello\", \"hello\")").as_integer(), 0);
    ASSERT_EQ(eval("String.levenshtein_distance(\"\", \"abc\")").as_integer(), 3);
    ASSERT_EQ(eval("String.levenshtein_distance(\"abc\", \"\")").as_integer(), 3);
    ASSERT_EQ(eval("String.levenshtein_distance(\"\", \"\")").as_integer(), 0);
}

static void test_string_slug() {
    ASSERT_EQ(eval("String.slug(\"Hello World!\")").as_string(), "hello-world");
    ASSERT_EQ(eval("String.slug(\"  Multiple   Spaces  \")").as_string(), "multiple-spaces");
    ASSERT_EQ(eval("String.slug(\"CamelCase Test\")").as_string(), "camelcase-test");
    ASSERT_EQ(eval("String.slug(\"\")").as_string(), "");
    ASSERT_EQ(eval("String.slug(\"already-slug\")").as_string(), "already-slug");
}

static void test_string_upper_lower() {
    ASSERT_EQ(eval("String.uppercase(\"hello\")").as_string(), "HELLO");
    ASSERT_EQ(eval("String.lowercase(\"HELLO\")").as_string(), "hello");
}

static void test_string_uppercase_multibyte() {
    // Multibyte UTF-8 characters should pass through unchanged.
    const auto v = eval("String.uppercase(\"caf\xC3\xA9\")");
    // 'é' is not ASCII, so it stays lowercase; ASCII chars uppercase.
    ASSERT_EQ(v.as_string(), "CAF\xC3\xA9");
}

static void test_string_trim_empty() {
    ASSERT_EQ(eval("String.trim(\"\")").as_string(), "");
    ASSERT_EQ(eval("String.trim(\"   \")").as_string(), "");
}

static void test_string_split_empty_delimiter() {
    // Empty delimiter returns the entire string as one element.
    const auto v = eval("String.split(\"hello\", \"\")");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 1U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "hello");
}

static void test_string_character_at_out_of_bounds() {
    ASSERT_EVAL_FAILURE("String.character_at(\"abc\", 5)");
}

static void test_string_character_at_negative() {
    ASSERT_EVAL_FAILURE("String.character_at(\"abc\", -1)");
}

static void test_string_substring_clamping() {
    // Start beyond end should return empty string.
    ASSERT_EQ(eval("String.substring(\"hello\", 10, 20)").as_string(), "");
    // Negative start clamps to 0.
    ASSERT_EQ(eval("String.substring(\"hello\", -5, 3)").as_string(), "hel");
}

static void test_string_count_empty_search() {
    // Searching for empty string returns 0.
    ASSERT_EQ(eval("String.count(\"hello\", \"\")").as_integer(), 0);
}

static void test_string_replace_empty_from() {
    // Replacing empty string returns original.
    ASSERT_EQ(eval("String.replace(\"hello\", \"\", \"x\")").as_string(), "hello");
}

static void test_string_index_of_not_found() {
    ASSERT_EVAL_FAILURE("String.index_of(\"hello\", \"xyz\")");
}

static void test_string_truncate_negative() {
    // Negative max_length clamps to 0, consistent with String.substring clamping.
    const auto v = eval("String.truncate(\"hello\", -1)");
    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "");
}

// ───────────────────────────────────────────────────────────
// Coverage for String functions previously exercised only by Luma
// feature tests: capitalize, center, chunk, common_prefix/suffix,
// dedent, format_number, indent, is_alpha/alphanumeric/ascii/blank/
// numeric, join, last_index_of, remove_prefix/suffix, split_n,
// template, title_case, trim_start/end, wrap.
// ───────────────────────────────────────────────────────────

static void test_string_capitalize() {
    ASSERT_EQ(eval("String.capitalize(\"hello\")").as_string(), "Hello");
    ASSERT_EQ(eval("String.capitalize(\"Hello\")").as_string(), "Hello");
    ASSERT_EQ(eval("String.capitalize(\"\")").as_string(), "");
    ASSERT_EQ(eval("String.capitalize(\"ABC\")").as_string(), "ABC");
}

static void test_string_title_case() {
    ASSERT_EQ(eval("String.title_case(\"hello world\")").as_string(), "Hello World");
    ASSERT_EQ(eval("String.title_case(\"\")").as_string(), "");
}

static void test_string_center() {
    // center returns result<string>; ASSERT_EVAL_STR unwraps the success.
    ASSERT_EVAL_STR("String.center(\"hi\", 6, \".\")", "..hi..");
    ASSERT_EVAL_STR("String.center(\"x\", 6, \".\")", "..x...");
    // Width <= length returns the original (still wrapped in success).
    ASSERT_EVAL_STR("String.center(\"long\", 2, \".\")", "long");
}

static void test_string_center_width_exceeds_max() {
    ASSERT_EVAL_FAILURE("String.center(\"hi\", 20000000, \" \")");
}

static void test_string_chunk() {
    const auto v = eval("String.chunk(\"abcdef\", 2)");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "ab");
    ASSERT_EQ((*v.as_array()->elements)[2].as_string(), "ef");
}

static void test_string_chunk_invalid_size() {
    ASSERT_THROWS(eval("String.chunk(\"abc\", 0)"));
}

static void test_string_common_prefix() {
    ASSERT_EQ(eval("String.common_prefix(\"foobar\", \"foobaz\")").as_string(), "fooba");
    ASSERT_EQ(eval("String.common_prefix(\"abc\", \"xyz\")").as_string(), "");
}

static void test_string_common_suffix() {
    ASSERT_EQ(eval("String.common_suffix(\"foobar\", \"bazbar\")").as_string(), "bar");
    ASSERT_EQ(eval("String.common_suffix(\"abc\", \"xyz\")").as_string(), "");
}

static void test_string_dedent() {
    ASSERT_EQ(eval("String.dedent(\"  a\\n  b\")").as_string(), "a\nb");
}

static void test_string_indent() {
    ASSERT_EQ(eval("String.indent(\"a\\nb\", \"  \")").as_string(), "  a\n  b");
}

static void test_string_format_number() {
    ASSERT_EQ(eval("String.format_number(3.14159, 2)").as_string(), "3.14");
    ASSERT_EQ(eval("String.format_number(1.0, 0)").as_string(), "1");
}

static void test_string_format_number_invalid_precision() {
    ASSERT_THROWS(eval("String.format_number(1.0, 200)"));
}

static void test_string_is_alpha() {
    ASSERT_TRUE(eval("String.is_alpha(\"hello\")").as_bool());
    ASSERT_FALSE(eval("String.is_alpha(\"hello1\")").as_bool());
    ASSERT_FALSE(eval("String.is_alpha(\"\")").as_bool());
}

static void test_string_is_alphanumeric() {
    ASSERT_TRUE(eval("String.is_alphanumeric(\"abc123\")").as_bool());
    ASSERT_FALSE(eval("String.is_alphanumeric(\"abc 123\")").as_bool());
    ASSERT_FALSE(eval("String.is_alphanumeric(\"\")").as_bool());
}

static void test_string_is_ascii() {
    ASSERT_TRUE(eval("String.is_ascii(\"hello\")").as_bool());
    ASSERT_TRUE(eval("String.is_ascii(\"\")").as_bool());
    // "café" contains a non-ASCII byte sequence.
    ASSERT_FALSE(eval("String.is_ascii(\"caf\xC3\xA9\")").as_bool());
}

static void test_string_is_blank() {
    ASSERT_TRUE(eval("String.is_blank(\"\")").as_bool());
    ASSERT_TRUE(eval("String.is_blank(\"   \")").as_bool());
    ASSERT_FALSE(eval("String.is_blank(\"x\")").as_bool());
}

static void test_string_is_numeric() {
    ASSERT_TRUE(eval("String.is_numeric(\"3.14\")").as_bool());
    ASSERT_TRUE(eval("String.is_numeric(\"-42\")").as_bool());
    ASSERT_FALSE(eval("String.is_numeric(\"abc\")").as_bool());
    ASSERT_FALSE(eval("String.is_numeric(\"\")").as_bool());
}

static void test_string_join() {
    ASSERT_EQ(eval("String.join([\"a\", \"b\", \"c\"], \", \")").as_string(), "a, b, c");
    ASSERT_EQ(eval("String.join([], \"-\")").as_string(), "");
    ASSERT_EQ(eval("String.join([1, 2, 3], \"-\")").as_string(), "1-2-3");
}

static void test_string_last_index_of() {
    ASSERT_EVAL_INT("String.last_index_of(\"banana\", \"an\")", 3);
}

static void test_string_last_index_of_not_found() {
    ASSERT_EVAL_FAILURE("String.last_index_of(\"hello\", \"xyz\")");
}

static void test_string_remove_prefix() {
    ASSERT_EQ(eval("String.remove_prefix(\"file.luma\", \"file\")").as_string(), ".luma");
    ASSERT_EQ(eval("String.remove_prefix(\"hello\", \"xyz\")").as_string(), "hello");
}

static void test_string_remove_suffix() {
    ASSERT_EQ(eval("String.remove_suffix(\"file.luma\", \".luma\")").as_string(), "file");
    ASSERT_EQ(eval("String.remove_suffix(\"hello\", \"xyz\")").as_string(), "hello");
}

static void test_string_split_n() {
    const auto v = eval("String.split_n(\"a:b:c:d\", \":\", 2)");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "a");
    ASSERT_EQ((*v.as_array()->elements)[1].as_string(), "b:c:d");
}

static void test_string_split_n_caps_array_size() {
    // Regression: split enforces max_array_size but split_n did not, so a huge
    // max_parts on a delimiter-heavy string could grow the result without bound
    // — an out-of-memory DoS on hostile input.  split_n now applies the same
    // cap.  Lower the limit so the test stays small, and restore it afterwards
    // even if an assertion throws.
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(4)};

    // Eight delimiters request up to nine parts, exceeding the lowered cap of 4.
    ASSERT_THROWS(eval("String.split_n(\"a,b,c,d,e,f,g,h,i\", \",\", 1000000)"));
}

static void test_string_template() {
    ASSERT_EQ(eval("String.template(\"Hello, {name}!\", {\"name\": \"World\"})").as_string(),
              "Hello, World!");
}

static void test_string_template_caps_output_size() {
    // Regression: template expanded placeholders with no output-size cap, so a
    // dictionary whose values re-expand (or a heavily repeated placeholder)
    // could blow the result up without bound — a "billion laughs" DoS.  Lower
    // the string-size limit so a small input trips the cap.
    const LimitGuard guard{ResourceLimits::max_string_size, static_cast<std::size_t>(64)};

    // Ten "{x}" placeholders each expand to ten characters (100 bytes > 64).
    ASSERT_THROWS(
        eval("String.template(\"{x}{x}{x}{x}{x}{x}{x}{x}{x}{x}\", {\"x\": \"0123456789\"})"));
}

static void test_string_to_array_ops_cap_array_size() {
    // Regression: to_bytes/characters/to_codepoints/chunk emitted one array
    // element per byte/codepoint with no max_array_size cap, so an input under
    // max_string_size could still build an array far past the array limit.
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(4)};

    ASSERT_THROWS(eval("String.to_bytes(\"abcdefghij\")"));
    ASSERT_THROWS(eval("String.characters(\"abcdefghij\")"));
    ASSERT_THROWS(eval("String.to_codepoints(\"abcdefghij\")"));
    ASSERT_THROWS(eval("String.chunk(\"abcdefghij\", 1)"));
}

static void test_string_trim_start() {
    ASSERT_EQ(eval("String.trim_start(\"  hi\")").as_string(), "hi");
    ASSERT_EQ(eval("String.trim_start(\"hi  \")").as_string(), "hi  ");
}

static void test_string_trim_end() {
    ASSERT_EQ(eval("String.trim_end(\"hi  \")").as_string(), "hi");
    ASSERT_EQ(eval("String.trim_end(\"  hi\")").as_string(), "  hi");
}

static void test_string_wrap() {
    // Greedy word-wrap breaks at the last space before the width is exceeded.
    ASSERT_EQ(eval("String.wrap(\"the quick brown fox\", 10)").as_string(), "the quick\nbrown fox");
}

static void test_string_pad_right_fail() {
    ASSERT_EVAL_FAILURE("String.pad_right(\"x\", 20000000, \" \")");
}

static void test_string_parse_integer_fail() {
    ASSERT_EVAL_FAILURE("String.parse_integer(\"abc\")");
}

static void test_string_parse_number_fail() {
    ASSERT_EVAL_FAILURE("String.parse_number(\"abc\")");
}

static void test_string_character_at_valid() {
    // Positive path: a valid index returns the codepoint wrapped in success.
    ASSERT_EVAL_STR("String.character_at(\"hello\", 0)", "h");
    ASSERT_EVAL_STR("String.character_at(\"hello\", 4)", "o");

    // Multibyte: "Ñ" (U+00D1) is a single codepoint at index 0.
    const auto expected = eval("Result.unwrap(String.from_codepoints([209]))").as_string();
    ASSERT_EVAL_STR("String.character_at(Result.unwrap(String.from_codepoints([209])), 0)",
                    expected);
}

static void test_string_count() {
    // Multiple non-overlapping occurrences.
    ASSERT_EQ(eval("String.count(\"banana\", \"a\")").as_integer(), 3);
    ASSERT_EQ(eval("String.count(\"banana\", \"an\")").as_integer(), 2);
    // Matches do not overlap: "aa" in "aaaa" is found twice, not three times.
    ASSERT_EQ(eval("String.count(\"aaaa\", \"aa\")").as_integer(), 2);
    // No match returns zero.
    ASSERT_EQ(eval("String.count(\"hello\", \"z\")").as_integer(), 0);
}

static void test_string_characters_ascii() {
    const auto v = eval("String.characters(\"abc\")");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "a");
    ASSERT_EQ((*v.as_array()->elements)[2].as_string(), "c");
}

static void test_string_truncate() {
    // Longer than the limit: keep (limit - 3) codepoints and append "...".
    ASSERT_EQ(eval("String.truncate(\"Hello World\", 8)").as_string(), "Hello...");
    // Shorter than or equal to the limit: returned unchanged.
    ASSERT_EQ(eval("String.truncate(\"Hi\", 10)").as_string(), "Hi");
    ASSERT_EQ(eval("String.truncate(\"Hello\", 5)").as_string(), "Hello");
    // Limit <= 3 leaves no room for an ellipsis, so it is omitted.
    ASSERT_EQ(eval("String.truncate(\"Hello\", 3)").as_string(), "Hel");
}

static void test_string_search_negative() {
    // contains / starts_with / ends_with return false when there is no match.
    ASSERT_FALSE(eval("String.contains(\"hello world\", \"xyz\")").as_bool());
    ASSERT_FALSE(eval("String.starts_with(\"hello world\", \"world\")").as_bool());
    ASSERT_FALSE(eval("String.ends_with(\"hello world\", \"hello\")").as_bool());
}

static void test_string_from_codepoints_surrogate() {
    // UTF-16 surrogate halves (U+D800-U+DFFF) are not valid scalar values.
    ASSERT_EVAL_FAILURE("String.from_codepoints([55296])");
}

static void test_string_lines() {
    // Universal newlines: \n, \r\n and lone \r all split; terminators stripped;
    // no trailing empty element after a final newline.
    const auto v = eval("String.lines(\"a\\r\\nb\\nc\\rd\\n\")");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 4U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "a");
    ASSERT_EQ((*v.as_array()->elements)[1].as_string(), "b");
    ASSERT_EQ((*v.as_array()->elements)[2].as_string(), "c");
    ASSERT_EQ((*v.as_array()->elements)[3].as_string(), "d");

    // Empty string -> no lines.
    ASSERT_EQ(eval("String.lines(\"\")").as_array()->elements->size(), 0U);

    // Blank interior lines are preserved.
    const auto blanks = eval("String.lines(\"a\\n\\nb\")");
    ASSERT_EQ(blanks.as_array()->elements->size(), 3U);
    ASSERT_EQ((*blanks.as_array()->elements)[1].as_string(), "");

    // A single newline yields one empty line.
    ASSERT_EQ(eval("String.lines(\"\\n\")").as_array()->elements->size(), 1U);

    // No trailing terminator: single line preserved.
    ASSERT_EQ(eval("String.lines(\"abc\")").as_array()->elements->size(), 1U);
}

static void test_string_split_whitespace() {
    // Leading, trailing, and repeated whitespace collapse; no empty tokens.
    const auto v = eval("String.split_whitespace(\"  the quick\\tbrown\\nfox  \")");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 4U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "the");
    ASSERT_EQ((*v.as_array()->elements)[1].as_string(), "quick");
    ASSERT_EQ((*v.as_array()->elements)[2].as_string(), "brown");
    ASSERT_EQ((*v.as_array()->elements)[3].as_string(), "fox");

    // All-whitespace and empty inputs yield no tokens.
    ASSERT_EQ(eval("String.split_whitespace(\"   \")").as_array()->elements->size(), 0U);
    ASSERT_EQ(eval("String.split_whitespace(\"\")").as_array()->elements->size(), 0U);
}

static void test_string_words() {
    // words is an alias of split_whitespace.
    const auto v = eval("String.words(\"a  b\\tc\")");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "a");
    ASSERT_EQ((*v.as_array()->elements)[2].as_string(), "c");
}

static void test_string_word_count() {
    ASSERT_EQ(eval("String.word_count(\"  the quick\\tbrown\\nfox  \")").as_integer(), 4);
    ASSERT_EQ(eval("String.word_count(\"\")").as_integer(), 0);
    ASSERT_EQ(eval("String.word_count(\"   \")").as_integer(), 0);
    ASSERT_EQ(eval("String.word_count(\"solo\")").as_integer(), 1);
}

static void test_string_insert() {
    ASSERT_EVAL_STR("String.insert(\"hello\", 0, \">>\")", ">>hello");
    ASSERT_EVAL_STR("String.insert(\"hello\", 5, \"!\")", "hello!");
    ASSERT_EVAL_STR("String.insert(\"hello\", 2, \"XY\")", "heXYllo");
    // Codepoint indexing: insert after the 'é' (1 codepoint, 2 bytes).
    ASSERT_EVAL_STR("String.insert(Result.unwrap(String.from_codepoints([233, 233])), 1, \"-\")",
                    "\xC3\xA9-\xC3\xA9");
}

static void test_string_insert_out_of_bounds() {
    ASSERT_EVAL_FAILURE("String.insert(\"hello\", -1, \"x\")");
    ASSERT_EVAL_FAILURE("String.insert(\"hello\", 6, \"x\")");
}

static void test_string_delete() {
    ASSERT_EVAL_STR("String.delete(\"hello\", 1, 3)", "hlo");
    ASSERT_EVAL_STR("String.delete(\"hello\", 0, 5)", "");
    // Empty range is a no-op.
    ASSERT_EVAL_STR("String.delete(\"hello\", 2, 2)", "hello");
}

static void test_string_delete_out_of_bounds() {
    ASSERT_EVAL_FAILURE("String.delete(\"hello\", -1, 2)");
    ASSERT_EVAL_FAILURE("String.delete(\"hello\", 0, 6)");
    ASSERT_EVAL_FAILURE("String.delete(\"hello\", 3, 1)");
}

static void test_string_replace_range() {
    ASSERT_EVAL_STR("String.replace_range(\"hello\", 1, 3, \"XY\")", "hXYlo");
    ASSERT_EVAL_STR("String.replace_range(\"hello\", 0, 5, \"bye\")", "bye");
    // Empty range acts as an insert.
    ASSERT_EVAL_STR("String.replace_range(\"hello\", 2, 2, \"__\")", "he__llo");
}

static void test_string_replace_range_out_of_bounds() {
    ASSERT_EVAL_FAILURE("String.replace_range(\"hello\", -1, 2, \"x\")");
    ASSERT_EVAL_FAILURE("String.replace_range(\"hello\", 0, 6, \"x\")");
    ASSERT_EVAL_FAILURE("String.replace_range(\"hello\", 3, 1, \"x\")");
}

static void test_string_starts_with_any() {
    ASSERT_TRUE(eval("String.starts_with_any(\"hello\", [\"he\", \"xy\"])").as_bool());
    ASSERT_TRUE(eval("String.starts_with_any(\"hello\", [\"hello\"])").as_bool());
    ASSERT_FALSE(eval("String.starts_with_any(\"hello\", [\"xy\", \"z\"])").as_bool());
    // An empty prefix always matches.
    ASSERT_TRUE(eval("String.starts_with_any(\"hello\", [\"\"])").as_bool());
}

static void test_string_ends_with_any() {
    ASSERT_TRUE(eval("String.ends_with_any(\"hello\", [\"lo\", \"xy\"])").as_bool());
    ASSERT_TRUE(eval("String.ends_with_any(\"hello\", [\"hello\"])").as_bool());
    ASSERT_FALSE(eval("String.ends_with_any(\"hello\", [\"xy\", \"z\"])").as_bool());
    ASSERT_TRUE(eval("String.ends_with_any(\"hello\", [\"\"])").as_bool());
}

static void test_string_character_class_constants() {
    ASSERT_EQ(eval("String.digits").as_string(), "0123456789");
    ASSERT_EQ(eval("String.hex_digits").as_string(), "0123456789abcdef");
    ASSERT_EQ(eval("String.ascii_lowercase").as_string(), "abcdefghijklmnopqrstuvwxyz");
    ASSERT_EQ(eval("String.ascii_uppercase").as_string(), "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    ASSERT_EQ(eval("String.ascii_letters").as_string(),
              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    ASSERT_EQ(eval("String.whitespace").as_string(), std::string(" \t\n\r\f\v"));
    ASSERT_EQ(eval("String.punctuation").as_string(),
              std::string(R"(!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~)"));
    // Composes with existing predicates.
    ASSERT_TRUE(eval("String.is_digit(String.digits)").as_bool());
    ASSERT_TRUE(eval("String.is_ascii(String.punctuation)").as_bool());
}

int main() {
    RUN(test_string_byte_length);
    RUN(test_string_byte_length_multibyte);
    RUN(test_string_characters_multibyte);
    RUN(test_string_contains);
    RUN(test_string_equals_ignore_case);
    RUN(test_string_contains_ignore_case);
    RUN(test_string_ends_with);
    RUN(test_string_from_bytes);
    RUN(test_string_from_bytes_above_255);
    RUN(test_string_from_bytes_empty);
    RUN(test_string_from_bytes_negative);
    RUN(test_string_from_codepoints);
    RUN(test_string_from_codepoints_empty);
    RUN(test_string_from_codepoints_invalid);
    RUN(test_string_index_of);
    RUN(test_string_is_digit);
    RUN(test_string_is_empty);
    RUN(test_string_is_lowercase);
    RUN(test_string_is_uppercase);
    RUN(test_string_is_whitespace);
    RUN(test_string_length);
    RUN(test_string_length_multibyte);
    RUN(test_string_matches_no_match);
    RUN(test_string_matches_ok);
    RUN(test_string_matches_redos_safe);
    RUN(test_string_matches_wildcard_spans_newline);
    RUN(test_string_module);
    RUN(test_string_pad_left);
    RUN(test_string_pad_left_fail);
    RUN(test_string_pad_right);
    RUN(test_string_parse_integer);
    RUN(test_string_parse_number);
    RUN(test_string_parse_number_rejects_non_finite);
    RUN(test_string_repeat);
    RUN(test_string_repeat_fail);
    RUN(test_string_replace);
    RUN(test_string_replace_all);
    RUN(test_string_replace_all_no_match);
    RUN(test_string_replace_first_only);
    RUN(test_string_reverse);
    RUN(test_string_reverse_multibyte);
    RUN(test_string_split);
    RUN(test_string_starts_with);
    RUN(test_string_substring);
    RUN(test_string_to_bytes);
    RUN(test_string_to_bytes_empty);
    RUN(test_string_to_codepoints);
    RUN(test_string_to_codepoints_empty);
    RUN(test_string_to_codepoints_multibyte);
    RUN(test_string_to_camel_case);
    RUN(test_string_to_kebab_case);
    RUN(test_string_to_pascal_case);
    RUN(test_string_to_snake_case);
    RUN(test_string_trim);
    RUN(test_string_upper_lower);
    RUN(test_string_uppercase_multibyte);
    RUN(test_string_trim_empty);
    RUN(test_string_split_empty_delimiter);
    RUN(test_string_character_at_out_of_bounds);
    RUN(test_string_character_at_negative);
    RUN(test_string_substring_clamping);
    RUN(test_string_count_empty_search);
    RUN(test_string_replace_empty_from);
    RUN(test_string_index_of_not_found);
    RUN(test_string_truncate_negative);
    RUN(test_string_is_palindrome);
    RUN(test_string_levenshtein_distance);
    RUN(test_string_slug);
    RUN(test_string_capitalize);
    RUN(test_string_title_case);
    RUN(test_string_center);
    RUN(test_string_center_width_exceeds_max);
    RUN(test_string_chunk);
    RUN(test_string_chunk_invalid_size);
    RUN(test_string_common_prefix);
    RUN(test_string_common_suffix);
    RUN(test_string_dedent);
    RUN(test_string_indent);
    RUN(test_string_format_number);
    RUN(test_string_format_number_invalid_precision);
    RUN(test_string_is_alpha);
    RUN(test_string_is_alphanumeric);
    RUN(test_string_is_ascii);
    RUN(test_string_is_blank);
    RUN(test_string_is_numeric);
    RUN(test_string_join);
    RUN(test_string_last_index_of);
    RUN(test_string_last_index_of_not_found);
    RUN(test_string_remove_prefix);
    RUN(test_string_remove_suffix);
    RUN(test_string_split_n);
    RUN(test_string_split_n_caps_array_size);
    RUN(test_string_template);
    RUN(test_string_template_caps_output_size);
    RUN(test_string_to_array_ops_cap_array_size);
    RUN(test_string_trim_start);
    RUN(test_string_trim_end);
    RUN(test_string_wrap);
    RUN(test_string_pad_right_fail);
    RUN(test_string_parse_integer_fail);
    RUN(test_string_parse_number_fail);
    RUN(test_string_character_at_valid);
    RUN(test_string_count);
    RUN(test_string_characters_ascii);
    RUN(test_string_truncate);
    RUN(test_string_search_negative);
    RUN(test_string_from_codepoints_surrogate);
    RUN(test_string_lines);
    RUN(test_string_split_whitespace);
    RUN(test_string_words);
    RUN(test_string_word_count);
    RUN(test_string_insert);
    RUN(test_string_insert_out_of_bounds);
    RUN(test_string_delete);
    RUN(test_string_delete_out_of_bounds);
    RUN(test_string_replace_range);
    RUN(test_string_replace_range_out_of_bounds);
    RUN(test_string_starts_with_any);
    RUN(test_string_ends_with_any);
    RUN(test_native_type_error_is_catchable);

    RUN(test_string_character_class_constants);

    return SUMMARY();
}
