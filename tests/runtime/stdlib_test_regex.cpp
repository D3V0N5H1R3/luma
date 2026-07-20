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

int main() {
    RUN(test_regex_find);
    RUN(test_regex_find_all);
    RUN(test_regex_find_all_capture_groups);
    RUN(test_regex_find_capture_groups);
    RUN(test_regex_find_no_match);
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
    return SUMMARY();
}
