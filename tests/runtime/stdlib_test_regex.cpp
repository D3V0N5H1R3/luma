// Standard library tests: RegularExpression.

#include <cstddef>
#include <string>

#include "common/resource_limits.hpp"
#include "runtime/stdlib/text/regularexpression_module.hpp"
#include "stdlib_test_helpers.hpp"

static void test_regex_find() {
    const auto v = eval("RegularExpression.find(\"abc123def\", \"[0-9]+\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;

    ASSERT_TRUE(inner.is_record());
    ASSERT_EQ(inner.as_record()->type_name, "Match");
    ASSERT_EQ(inner.as_record()->find_field("text")->as_string(), "123");
    ASSERT_EQ(inner.as_record()->find_field("position")->as_integer(), 3);
    ASSERT_EQ(inner.as_record()->find_field("length")->as_integer(), 3);

    // No capture groups — groups array should be empty.
    const auto* groups_field = inner.as_record()->find_field("groups");

    ASSERT_TRUE(groups_field != nullptr);
    ASSERT_TRUE(groups_field->is_array());
    ASSERT_EQ(groups_field->as_array()->elements->size(), 0U);
}

static void test_regex_find_all() {
    const auto v = eval("RegularExpression.find_all(\"one1two2three3\", \"[0-9]\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& elems = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(elems.size(), 3U);

    // Each element should be a Match record with text, position, length, groups
    for (const auto& elem : elems) {
        ASSERT_TRUE(elem.is_record());
        ASSERT_EQ(elem.as_record()->type_name, "Match");
        ASSERT_TRUE(elem.as_record()->find_field("text") != nullptr);
        ASSERT_TRUE(elem.as_record()->find_field("position") != nullptr);
        ASSERT_TRUE(elem.as_record()->find_field("length") != nullptr);
        ASSERT_TRUE(elem.as_record()->find_field("groups") != nullptr);
        ASSERT_TRUE(elem.as_record()->find_field("groups")->is_array());
        ASSERT_EQ(elem.as_record()->find_field("groups")->as_array()->elements->size(), 0u);
    }

    ASSERT_EVAL_FAILURE("RegularExpression.find_all(\"text\", \"[\")");
}

static void test_regex_find_all_capture_groups() {
    const auto v =
        eval("RegularExpression.find_all(\"width=100 height=200\", \"([a-z]+)=([0-9]+)\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& elems = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(elems.size(), 2U);

    // First match: "width=100"
    const auto& m0_groups = *elems[0].as_record()->find_field("groups")->as_array()->elements;

    ASSERT_EQ(m0_groups.size(), 2U);
    ASSERT_EQ(m0_groups[0].as_record()->find_field("text")->as_string(), "width");
    ASSERT_EQ(m0_groups[1].as_record()->find_field("text")->as_string(), "100");

    // Second match: "height=200"
    const auto& m1_groups = *elems[1].as_record()->find_field("groups")->as_array()->elements;

    ASSERT_EQ(m1_groups.size(), 2U);
    ASSERT_EQ(m1_groups[0].as_record()->find_field("text")->as_string(), "height");
    ASSERT_EQ(m1_groups[1].as_record()->find_field("text")->as_string(), "200");
}

static void test_regex_find_capture_groups() {
    const auto v =
        eval("RegularExpression.find(\"alice@example.com\", \"([a-z]+)@([a-z]+)\\\\.([a-z]+)\")");
    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;

    ASSERT_TRUE(inner.is_record());
    ASSERT_EQ(inner.as_record()->find_field("text")->as_string(), "alice@example.com");

    const auto* groups_field = inner.as_record()->find_field("groups");

    ASSERT_TRUE(groups_field != nullptr);
    ASSERT_TRUE(groups_field->is_array());

    const auto& groups = *groups_field->as_array()->elements;

    ASSERT_EQ(groups.size(), 3U);

    // Group 1: "alice"
    ASSERT_TRUE(groups[0].is_record());
    ASSERT_EQ(groups[0].as_record()->find_field("text")->as_string(), "alice");
    ASSERT_EQ(groups[0].as_record()->find_field("position")->as_integer(), 0);
    ASSERT_EQ(groups[0].as_record()->find_field("length")->as_integer(), 5);

    // Group 2: "example"
    ASSERT_TRUE(groups[1].is_record());
    ASSERT_EQ(groups[1].as_record()->find_field("text")->as_string(), "example");
    ASSERT_EQ(groups[1].as_record()->find_field("position")->as_integer(), 6);
    ASSERT_EQ(groups[1].as_record()->find_field("length")->as_integer(), 7);

    // Group 3: "com"
    ASSERT_TRUE(groups[2].is_record());
    ASSERT_EQ(groups[2].as_record()->find_field("text")->as_string(), "com");
    ASSERT_EQ(groups[2].as_record()->find_field("position")->as_integer(), 14);
    ASSERT_EQ(groups[2].as_record()->find_field("length")->as_integer(), 3);
}

static void test_regex_find_named_groups_dotnet_style() {
    // (?<name>...) -- .NET/PCRE2-style named group syntax.
    const auto v = eval("RegularExpression.find(\"2024-01-15\", "
                        "\"(?<year>[0-9]+)-(?<month>[0-9]+)-(?<day>[0-9]+)\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;

    ASSERT_TRUE(inner.is_record());

    // Positional groups array is unchanged.
    const auto& groups = *inner.as_record()->find_field("groups")->as_array()->elements;

    ASSERT_EQ(groups.size(), 3U);
    ASSERT_EQ(groups[0].as_record()->find_field("text")->as_string(), "2024");
    ASSERT_EQ(groups[1].as_record()->find_field("text")->as_string(), "01");
    ASSERT_EQ(groups[2].as_record()->find_field("text")->as_string(), "15");

    // named_groups additionally maps name -> Capture.
    const auto* named_groups_field = inner.as_record()->find_field("named_groups");

    ASSERT_TRUE(named_groups_field != nullptr);
    ASSERT_TRUE(named_groups_field->is_dictionary());

    const auto& named = *named_groups_field->as_dictionary();

    ASSERT_TRUE(named.find("year") != nullptr);
    ASSERT_EQ(named.find("year")->as_record()->type_name, "Capture");
    ASSERT_EQ(named.find("year")->as_record()->find_field("name")->as_string(), "year");
    ASSERT_EQ(named.find("year")->as_record()->find_field("text")->as_string(), "2024");
    ASSERT_EQ(named.find("year")->as_record()->find_field("position")->as_integer(), 0);
    ASSERT_EQ(named.find("year")->as_record()->find_field("length")->as_integer(), 4);

    ASSERT_EQ(named.find("month")->as_record()->find_field("text")->as_string(), "01");
    ASSERT_EQ(named.find("day")->as_record()->find_field("text")->as_string(), "15");
}

static void test_regex_find_named_groups_python_style() {
    // (?P<name>...) -- Python-style named group syntax.
    const auto v = eval("RegularExpression.find(\"alice@example.com\", "
                        "\"(?P<user>[a-z]+)@(?P<domain>[a-z.]+)\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;
    const auto& named = *inner.as_record()->find_field("named_groups")->as_dictionary();

    ASSERT_EQ(named.find("user")->as_record()->find_field("text")->as_string(), "alice");
    ASSERT_EQ(named.find("domain")->as_record()->find_field("text")->as_string(), "example.com");
}

static void test_regex_find_named_and_unnamed_groups_mixed() {
    // Mixing named and unnamed groups: unnamed groups still appear positionally
    // in `groups` but are absent from `named_groups`.
    const auto v = eval("RegularExpression.find(\"width=100\", \"([a-z]+)=(?<value>[0-9]+)\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;
    const auto& groups = *inner.as_record()->find_field("groups")->as_array()->elements;

    ASSERT_EQ(groups.size(), 2U);
    ASSERT_EQ(groups[0].as_record()->find_field("text")->as_string(), "width");
    ASSERT_EQ(groups[1].as_record()->find_field("text")->as_string(), "100");

    const auto& named = *inner.as_record()->find_field("named_groups")->as_dictionary();

    ASSERT_TRUE(named.find("value") != nullptr);
    ASSERT_EQ(named.find("value")->as_record()->find_field("text")->as_string(), "100");

    // The unnamed group ("width") never appears as a named_groups key.
    ASSERT_TRUE(named.find("width") == nullptr);
}

static void test_regex_find_no_named_groups_empty_dictionary() {
    // A pattern with no named groups still produces an (empty) named_groups
    // dictionary, never a missing field.
    const auto v = eval("RegularExpression.find(\"abc123\", \"([a-z]+)([0-9]+)\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;
    const auto* named_groups_field = inner.as_record()->find_field("named_groups");

    ASSERT_TRUE(named_groups_field != nullptr);
    ASSERT_TRUE(named_groups_field->is_dictionary());
    ASSERT_EQ(named_groups_field->as_dictionary()->entries.size(), 0U);
}

static void test_regex_find_all_named_groups() {
    // find_all builds named_groups independently for each match.
    const auto v = eval(
        "RegularExpression.find_all(\"width=100 height=200\", \"(?<key>[a-z]+)=(?<val>[0-9]+)\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& elems = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(elems.size(), 2U);

    const auto& m0_named = *elems[0].as_record()->find_field("named_groups")->as_dictionary();

    ASSERT_EQ(m0_named.find("key")->as_record()->find_field("text")->as_string(), "width");
    ASSERT_EQ(m0_named.find("val")->as_record()->find_field("text")->as_string(), "100");

    const auto& m1_named = *elems[1].as_record()->find_field("named_groups")->as_dictionary();

    ASSERT_EQ(m1_named.find("key")->as_record()->find_field("text")->as_string(), "height");
    ASSERT_EQ(m1_named.find("val")->as_record()->find_field("text")->as_string(), "200");
}

static void test_regex_named_groups_do_not_break_lookbehind() {
    // (?<=...) and (?<!...) are lookbehind assertions, not named groups -- the
    // named-group detector must not mistake them for `(?<name>...)` and strip
    // the '=' / '!'.  std::regex's ECMAScript grammar has no lookbehind support
    // at all (only lookahead), so these patterns still fail to compile -- that
    // is a pre-existing engine limitation, not something introduced here.
    // What matters is that a genuine named group is unaffected by the
    // lookbehind exclusion check.
    ASSERT_EQ(eval("RegularExpression.is_valid(\"(?<=foo)bar\")").as_bool(), false);
    ASSERT_EQ(eval("RegularExpression.is_valid(\"(?<!foo)bar\")").as_bool(), false);

    const auto v = eval("RegularExpression.find(\"bar\", \"(?<value>bar)\")");

    ASSERT_RESULT_SUCCESS(v);

    const auto& named =
        *v.as_result()->owned_inner->as_record()->find_field("named_groups")->as_dictionary();

    ASSERT_EQ(named.find("value")->as_record()->find_field("text")->as_string(), "bar");
}

static void test_regex_unterminated_named_group_fails() {
    // A malformed/unterminated named-group annotation falls through unchanged
    // and relies on std::regex_error to surface the failure -- no crash.
    ASSERT_EQ(eval("RegularExpression.is_valid(\"(?<name[a-z]+)\")").as_bool(), false);
    ASSERT_EVAL_FAILURE("RegularExpression.find(\"abc\", \"(?<name[a-z]+)\")");
}

static void test_regex_is_valid() {
    ASSERT_EQ(eval("RegularExpression.is_valid(\"[a-z]+\")").as_bool(), true);
    ASSERT_EQ(eval("RegularExpression.is_valid(\"[\")").as_bool(), false);
}

static void test_regex_matches() {
    ASSERT_EVAL_BOOL("RegularExpression.matches(\"hello123\", \"[a-z]+[0-9]+\")", true);

    ASSERT_EVAL_BOOL("RegularExpression.matches(\"hello\", \"^[0-9]+$\")", false);

    ASSERT_EVAL_FAILURE("RegularExpression.matches(\"hello\", \"[\")");
}

static void test_regex_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("RegularExpression.matches"));
    ASSERT_TRUE(env->has("RegularExpression.find"));
    ASSERT_TRUE(env->has("RegularExpression.replace"));
    ASSERT_TRUE(env->has("RegularExpression.split"));
    ASSERT_TRUE(env->has("RegularExpression.is_valid"));
}

static void test_regex_replace_all() {
    ASSERT_EVAL_STR("RegularExpression.replace_all(\"abc123def456\", \"[0-9]+\", \"#\")",
                    "abc#def#");

    ASSERT_EVAL_FAILURE("RegularExpression.replace_all(\"text\", \"[\", \"#\")");
}

static void test_regex_split() {
    const auto v = eval("RegularExpression.split(\"a1b2c3\", \"[0-9]\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ((*v.as_result()->owned_inner->as_array()->elements)[0].as_string(), "a");

    ASSERT_EVAL_FAILURE("RegularExpression.split(\"abc\", \"[\")");
}

static void test_regex_replace() {
    // Single replace substitutes only the first match (format_first_only).
    ASSERT_EVAL_STR("RegularExpression.replace(\"foo bar baz\", \"o+\", \"0\")", "f0 bar baz");

    // Distinguishes replace (first only) from replace_all: only the first run
    // of 'a's is substituted, the trailing one is left intact.
    ASSERT_EVAL_STR("RegularExpression.replace(\"aaa bbb aaa\", \"a+\", \"X\")", "X bbb aaa");

    ASSERT_EVAL_FAILURE("RegularExpression.replace(\"text\", \"[\", \"#\")");
}

static void test_regex_find_no_match() {
    // find returns a failure result (not a crash or empty success) when the
    // pattern does not occur in the text.
    ASSERT_EVAL_FAILURE("RegularExpression.find(\"abcdef\", \"[0-9]+\")");
}

static void test_regex_redos_rejected() {
    // is_valid rejects nested quantifiers outright.
    ASSERT_EQ(eval("RegularExpression.is_valid(\"(a+)+\")").as_bool(), false);
    ASSERT_EQ(eval("RegularExpression.is_valid(\"((a+))+\")").as_bool(), false);
    ASSERT_EQ(eval("RegularExpression.is_valid(\"(?:a+)+\")").as_bool(), false);

    // Single (non-nested) quantifiers remain valid.
    ASSERT_EQ(eval("RegularExpression.is_valid(\"(a+)b\")").as_bool(), true);

    // Every operation refuses a dangerous pattern with a failure result whose
    // message explains the rejection.
    const char* const ops[] = {
        "RegularExpression.matches(\"aaaa\", \"(a+)+\")",
        "RegularExpression.find(\"aaaa\", \"(a+)+\")",
        "RegularExpression.find_all(\"aaaa\", \"(a+)+\")",
        "RegularExpression.replace(\"aaaa\", \"(a+)+\", \"x\")",
        "RegularExpression.replace_all(\"aaaa\", \"(a+)+\", \"x\")",
        "RegularExpression.split(\"aaaa\", \"(a+)+\")",
    };

    for (const auto* op : ops) {
        const auto v = eval(op);

        ASSERT_RESULT_FAILURE(v);
        ASSERT_TRUE(v.as_result()->owned_inner->as_string().find("nested quantifiers") !=
                    std::string::npos);
    }
}

static void test_regex_oversized_pattern() {
    // A pattern beyond max_regex_pattern_size (10,000 bytes) is rejected by
    // is_valid before std::regex sees it, even though it is otherwise benign.
    const std::string too_big = "RegularExpression.is_valid(\"" + std::string(10'001, 'a') + "\")";

    ASSERT_EQ(eval(too_big).as_bool(), false);

    // Comfortably under the limit, the same shape is accepted.
    const std::string ok = "RegularExpression.is_valid(\"" + std::string(9'000, 'a') + "\")";

    ASSERT_EQ(eval(ok).as_bool(), true);
}

static void test_regex_redos_heuristic() {
    // Direct coverage of the trust-boundary walk exposed for testing.
    // Safe: no group, a single quantifier, a quantified group with no inner
    // quantifier, a quantifier inside a character class, and escaped parens.
    ASSERT_FALSE(has_dangerous_quantifier_nesting(""));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("abc"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("a+"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("a{2,3}"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("(a)+"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("(a+)"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("[a+]+"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("\\(a+\\)+"));

    // Dangerous: a group that contains a quantifier and is itself quantified,
    // including deep, non-capturing and {n,m} variants.
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(a+)+"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(a+)+b"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("((a+))+"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(?:a+)+"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(a*)*"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(a{2,3}){2,3}"));

    // Dangerous: ambiguous alternation under repetition -- alternatives that
    // can match the same text, repeated, drive catastrophic backtracking.
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(a|aa)+"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(a|aa)+b"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(aa|a)*"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(.|a)+"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(\\d|5)+"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(a|)+"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("(?:a|aa)+"));
    ASSERT_TRUE(has_dangerous_quantifier_nesting("((a|aa))+"));

    // Safe: disjoint alternation (distinct first characters) is left alone, and
    // alternation that is not repeated is harmless.
    ASSERT_FALSE(has_dangerous_quantifier_nesting("(a|b)+"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("(a|b|c)+"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("(cat|dog)+"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("(ab|cd)*"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("a|aa"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("(a|aa)"));
    ASSERT_FALSE(has_dangerous_quantifier_nesting("(a|b)c"));
}

static void test_regex_compiled_cache_reuse() {
    // The compiled-regex cache is an internal optimisation, so it must be
    // observationally transparent: repeatedly using the same pattern (the path
    // that hits the cache after the first compile) keeps returning identical,
    // correct results rather than a stale or corrupted match.
    for (int iteration = 0; iteration < 100; ++iteration) {
        const auto v = eval("RegularExpression.find(\"abc123def\", \"[0-9]+\")");

        ASSERT_RESULT_SUCCESS(v);

        const auto& inner = *v.as_result()->owned_inner;

        ASSERT_EQ(inner.as_record()->find_field("text")->as_string(), "123");
        ASSERT_EQ(inner.as_record()->find_field("position")->as_integer(), 3);
    }

    // A pattern reused across different operations shares one cached automaton
    // yet each operation still behaves correctly.
    ASSERT_EQ(
        eval("RegularExpression.matches(\"a1b2\", \"[0-9]\")").as_result()->owned_inner->as_bool(),
        true);
    ASSERT_EQ(eval("RegularExpression.replace_all(\"a1b2\", \"[0-9]\", \"#\")")
                  .as_result()
                  ->owned_inner->as_string(),
              "a#b#");
    ASSERT_EQ(eval("RegularExpression.is_valid(\"[0-9]\")").as_bool(), true);
}

static void test_regex_invalid_pattern_not_cached() {
    // An invalid pattern throws during compilation and is never cached, so it
    // must fail identically every time rather than being wrongly remembered as
    // valid (or as a stale success) on a later call.
    for (int iteration = 0; iteration < 10; ++iteration) {
        ASSERT_RESULT_FAILURE(eval("RegularExpression.find(\"text\", \"[\")"));
        ASSERT_EQ(eval("RegularExpression.is_valid(\"[\")").as_bool(), false);
    }

    // A valid pattern sharing a prefix with the invalid one is still accepted,
    // confirming the failed compile left no poisoned cache entry behind.
    ASSERT_EQ(eval("RegularExpression.is_valid(\"[a-z]\")").as_bool(), true);
}

static void test_regex_find_all_match_limit_propagates() {
    // find_all routes through run_regex_collection, which deliberately rethrows
    // a RuntimeError (the match cap) rather than catching it into a failure
    // result the way wrap_result_operation does.  With the cap lowered, a
    // pattern matching every character overruns it, and the error must
    // propagate out of eval (throw) instead of degrading to a recoverable
    // failure Value -- this pins the DoS-abort semantics that separate
    // find_all/split from matches/replace/replace_all.
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(3)};

    ASSERT_TRUE(luma::test::eval_throws("RegularExpression.find_all(\"aaaaaaaa\", \"a\")"));
}

static void test_regex_split_token_limit_propagates() {
    // Same invariant for split: exceeding the token cap must abort (throw)
    // rather than be swallowed into a failure result.
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(3)};

    ASSERT_TRUE(luma::test::eval_throws("RegularExpression.split(\"a,b,c,d,e\", \",\")"));
}

// ─── RegularExpression.compile_typed (typed error via the RegularExpression.Error
// choice) ────────────────────────────────────────────────────────────────────

// Reads the RegularExpression.Error variant name from a compile_typed failure.
[[nodiscard]] static std::string regex_error_variant_of(const luma::Value& v) {
    const auto& inner = v.as_result()->owned_inner;
    if (!inner->is_choice()) {
        return "<not-a-choice>";
    }
    return inner->as_choice()->type_name + "." + inner->as_choice()->variant;
}

static void test_regex_compile_typed_registered() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("RegularExpression.compile_typed"));
}

static void test_regex_compile_typed_valid_returns_pattern() {
    // A safe, well-formed pattern succeeds and carries the validated pattern.
    const auto v = eval("RegularExpression.compile_typed(\"[0-9]+\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_string(), "[0-9]+");
}

static void test_regex_compile_typed_invalid_syntax() {
    // An unbalanced group is a syntax error, surfaced as InvalidSyntax with a
    // non-empty diagnostic message payload.
    const auto v = eval("RegularExpression.compile_typed(\"(unclosed\")");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(regex_error_variant_of(v), "Error.InvalidSyntax");

    const auto& inner = v.as_result()->owned_inner;
    ASSERT_TRUE(inner->is_choice());
    ASSERT_FALSE(inner->as_choice()->fields.empty());
    ASSERT_TRUE(inner->as_choice()->fields.at(0).is_string());
    ASSERT_FALSE(inner->as_choice()->fields.at(0).as_string().empty());
}

static void test_regex_compile_typed_unsafe() {
    // A nested quantifier is rejected by the ReDoS guard as Unsafe.
    const auto v = eval("RegularExpression.compile_typed(\"(a+)+\")");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(regex_error_variant_of(v), "Error.Unsafe");
}

static void test_regex_compile_typed_too_large() {
    // A pattern beyond max_regex_pattern_size (10,000 bytes) is TooLarge.
    const std::string too_big =
        "RegularExpression.compile_typed(\"" + std::string(10'001, 'a') + "\")";
    const auto v = eval(too_big);

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(regex_error_variant_of(v), "Error.TooLarge");
}

// ─── Flag-accepting variants (matches_with / find_with / … with
// RegularExpression.Flags) ──────────────────────────────────────────────────

static void test_regex_with_module_registered() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("RegularExpression.matches_with"));
    ASSERT_TRUE(env->has("RegularExpression.find_with"));
    ASSERT_TRUE(env->has("RegularExpression.find_all_with"));
    ASSERT_TRUE(env->has("RegularExpression.replace_with"));
    ASSERT_TRUE(env->has("RegularExpression.replace_all_with"));
    ASSERT_TRUE(env->has("RegularExpression.split_with"));

    // The Flags choice variants are bound as callable-free global values.
    ASSERT_TRUE(env->has("RegularExpression.Flags.CaseInsensitive"));
    ASSERT_TRUE(env->has("RegularExpression.Flags.MultiLine"));
    ASSERT_TRUE(env->has("RegularExpression.Flags.DotAll"));
}

static void test_regex_matches_with_case_insensitive() {
    // Without the flag the mixed-case text does not match a lowercase pattern.
    ASSERT_EQ(eval("RegularExpression.matches_with(\"HELLO\", \"hello\", [])")
                  .as_result()
                  ->owned_inner->as_bool(),
              false);

    // CaseInsensitive makes it match.
    ASSERT_EQ(eval("RegularExpression.matches_with(\"HELLO\", \"hello\", "
                   "[RegularExpression.Flags.CaseInsensitive])")
                  .as_result()
                  ->owned_inner->as_bool(),
              true);
}

static void test_regex_find_with_case_insensitive() {
    const auto v = eval("RegularExpression.find_with(\"Foo BAR baz\", \"bar\", "
                        "[RegularExpression.Flags.CaseInsensitive])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;

    ASSERT_EQ(inner.as_record()->find_field("text")->as_string(), "BAR");
    ASSERT_EQ(inner.as_record()->find_field("position")->as_integer(), 4);
}

static void test_regex_find_all_with_case_insensitive() {
    const auto v = eval("RegularExpression.find_all_with(\"aAbBaA\", \"a\", "
                        "[RegularExpression.Flags.CaseInsensitive])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& elems = *v.as_result()->owned_inner->as_array()->elements;

    // Every 'a'/'A' matches: positions 0, 1, 4, 5.
    ASSERT_EQ(elems.size(), 4U);
}

static void test_regex_replace_with_case_insensitive() {
    // replace_with substitutes only the first match, honouring the flag.
    ASSERT_EQ(eval("RegularExpression.replace_with(\"Cat cat CAT\", \"cat\", \"dog\", "
                   "[RegularExpression.Flags.CaseInsensitive])")
                  .as_result()
                  ->owned_inner->as_string(),
              "dog cat CAT");
}

static void test_regex_replace_all_with_case_insensitive() {
    ASSERT_EQ(eval("RegularExpression.replace_all_with(\"Cat cat CAT\", \"cat\", \"dog\", "
                   "[RegularExpression.Flags.CaseInsensitive])")
                  .as_result()
                  ->owned_inner->as_string(),
              "dog dog dog");
}

static void test_regex_split_with_case_insensitive() {
    const auto v = eval("RegularExpression.split_with(\"aXbxc\", \"x\", "
                        "[RegularExpression.Flags.CaseInsensitive])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& elems = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(elems.size(), 3U);
    ASSERT_EQ(elems[0].as_string(), "a");
    ASSERT_EQ(elems[1].as_string(), "b");
    ASSERT_EQ(elems[2].as_string(), "c");
}

static void test_regex_multiline_flag() {
    // Without MultiLine, ^ and $ anchor to the whole string, so a line-anchored
    // pattern matches only the first line.
    ASSERT_EQ(eval("RegularExpression.find_all(\"one\\ntwo\\nthree\", \"^[a-z]+$\")")
                  .as_result()
                  ->owned_inner->as_array()
                  ->elements->size(),
              0U);

    // With MultiLine, ^ and $ match at every line boundary.
    ASSERT_EQ(eval("RegularExpression.find_all_with(\"one\\ntwo\\nthree\", \"^[a-z]+$\", "
                   "[RegularExpression.Flags.MultiLine])")
                  .as_result()
                  ->owned_inner->as_array()
                  ->elements->size(),
              3U);
}

static void test_regex_dotall_flag() {
    // Without DotAll, '.' does not cross a newline.
    ASSERT_EQ(eval("RegularExpression.matches_with(\"a\\nb\", \"a.b\", [])")
                  .as_result()
                  ->owned_inner->as_bool(),
              false);

    // With DotAll, '.' matches the newline too.
    ASSERT_EQ(eval("RegularExpression.matches_with(\"a\\nb\", \"a.b\", "
                   "[RegularExpression.Flags.DotAll])")
                  .as_result()
                  ->owned_inner->as_bool(),
              true);

    // DotAll leaves an escaped dot as a literal — "a.b" still needs a real dot.
    ASSERT_EQ(eval("RegularExpression.matches_with(\"axb\", \"a\\\\.b\", "
                   "[RegularExpression.Flags.DotAll])")
                  .as_result()
                  ->owned_inner->as_bool(),
              false);
}

static void test_regex_combined_flags() {
    // CaseInsensitive + DotAll together.
    ASSERT_EQ(eval("RegularExpression.matches_with(\"A\\nB\", \"a.b\", "
                   "[RegularExpression.Flags.CaseInsensitive, RegularExpression.Flags.DotAll])")
                  .as_result()
                  ->owned_inner->as_bool(),
              true);
}

static void test_regex_with_empty_flags_matches_flagless() {
    // An empty flags array reproduces the flagless behaviour exactly.
    ASSERT_EQ(eval("RegularExpression.matches_with(\"hello\", \"^[0-9]+$\", [])")
                  .as_result()
                  ->owned_inner->as_bool(),
              false);
    ASSERT_EQ(eval("RegularExpression.matches_with(\"abc123\", \"[a-z]+[0-9]+\", [])")
                  .as_result()
                  ->owned_inner->as_bool(),
              true);
}

static void test_regex_with_redos_rejected() {
    // The ReDoS guard still runs for the flag-accepting variants.
    ASSERT_RESULT_FAILURE(eval("RegularExpression.matches_with(\"aaaa\", \"(a+)+\", [])"));
    ASSERT_RESULT_FAILURE(eval("RegularExpression.find_with(\"aaaa\", \"(a+)+\", "
                               "[RegularExpression.Flags.CaseInsensitive])"));
}

static void test_regex_with_invalid_pattern_fails() {
    // An unbalanced group still surfaces as a failure result.
    ASSERT_RESULT_FAILURE(eval("RegularExpression.matches_with(\"text\", \"[\", [])"));
}

// ─── RegularExpression.count ────────────────────────────────────────────────

static void test_regex_count() {
    // Counts non-overlapping matches without building the Match array.
    ASSERT_EQ(eval("RegularExpression.count(\"one1two2three3\", \"[0-9]\")")
                  .as_result()
                  ->owned_inner->as_integer(),
              3);

    // No match → 0 (a success carrying zero, not a failure).
    const auto zero = eval("RegularExpression.count(\"abcdef\", \"[0-9]+\")");

    ASSERT_RESULT_SUCCESS(zero);
    ASSERT_EQ(zero.as_result()->owned_inner->as_integer(), 0);

    // Multi-character matches count as one each.
    ASSERT_EQ(eval("RegularExpression.count(\"aaa bbb aaa\", \"a+\")")
                  .as_result()
                  ->owned_inner->as_integer(),
              2);

    // Invalid and ReDoS-unsafe patterns fail like every other operation.
    ASSERT_RESULT_FAILURE(eval("RegularExpression.count(\"text\", \"[\")"));
    ASSERT_RESULT_FAILURE(eval("RegularExpression.count(\"aaaa\", \"(a+)+\")"));
}

int main() {
    RUN(test_regex_find);
    RUN(test_regex_find_all);
    RUN(test_regex_find_all_capture_groups);
    RUN(test_regex_find_capture_groups);
    RUN(test_regex_find_no_match);
    RUN(test_regex_find_named_groups_dotnet_style);
    RUN(test_regex_find_named_groups_python_style);
    RUN(test_regex_find_named_and_unnamed_groups_mixed);
    RUN(test_regex_find_no_named_groups_empty_dictionary);
    RUN(test_regex_find_all_named_groups);
    RUN(test_regex_named_groups_do_not_break_lookbehind);
    RUN(test_regex_unterminated_named_group_fails);
    RUN(test_regex_compiled_cache_reuse);
    RUN(test_regex_invalid_pattern_not_cached);
    RUN(test_regex_find_all_match_limit_propagates);
    RUN(test_regex_split_token_limit_propagates);
    RUN(test_regex_is_valid);
    RUN(test_regex_matches);
    RUN(test_regex_module);
    RUN(test_regex_oversized_pattern);
    RUN(test_regex_redos_heuristic);
    RUN(test_regex_redos_rejected);
    RUN(test_regex_replace);
    RUN(test_regex_replace_all);
    RUN(test_regex_split);
    RUN(test_regex_compile_typed_registered);
    RUN(test_regex_compile_typed_valid_returns_pattern);
    RUN(test_regex_compile_typed_invalid_syntax);
    RUN(test_regex_compile_typed_unsafe);
    RUN(test_regex_compile_typed_too_large);
    RUN(test_regex_with_module_registered);
    RUN(test_regex_matches_with_case_insensitive);
    RUN(test_regex_find_with_case_insensitive);
    RUN(test_regex_find_all_with_case_insensitive);
    RUN(test_regex_replace_with_case_insensitive);
    RUN(test_regex_replace_all_with_case_insensitive);
    RUN(test_regex_split_with_case_insensitive);
    RUN(test_regex_multiline_flag);
    RUN(test_regex_dotall_flag);
    RUN(test_regex_combined_flags);
    RUN(test_regex_with_empty_flags_matches_flagless);
    RUN(test_regex_with_redos_rejected);
    RUN(test_regex_with_invalid_pattern_fails);
    RUN(test_regex_count);
    return SUMMARY();
}
