// DAP custom-visualizer tests — JSON config loading, glob-to-regex matching,
// match caching, and reload semantics.  Config files are written through the
// TempFile RAII helper so no fixtures leak into the working tree.

#include <optional>
#include <string>
#include <vector>

#include "custom_visualizer.hpp"
#include "test_framework.hpp"

using namespace luma::dap;

namespace {

// ─── Loading and basic matching ───────────────────────────────────

void test_no_rules_before_load() {
    CustomVisualizer viz;
    ASSERT_FALSE(viz.has_rules());
    ASSERT_FALSE(viz.find_rule("integer").has_value());
}

void test_load_exact_match() {
    const TempFile config{R"([
        {"typePattern": "integer", "displayTemplate": "<int>", "summaryTemplate": "number"}
    ])"};

    CustomVisualizer viz;
    viz.load(config.path_string());

    ASSERT_TRUE(viz.has_rules());
    const auto rule = viz.find_rule("integer");
    ASSERT_TRUE(rule.has_value());
    ASSERT_EQ(rule->display_template, std::string("<int>"));
    ASSERT_EQ(rule->summary_template, std::string("number"));
}

void test_no_match_returns_nullopt() {
    const TempFile config{R"([{"typePattern": "integer", "displayTemplate": "<int>"}])"};

    CustomVisualizer viz;
    viz.load(config.path_string());

    ASSERT_FALSE(viz.find_rule("string").has_value());
}

void test_optional_templates_default_empty() {
    // displayTemplate / summaryTemplate are optional; absent keys yield "".
    const TempFile config{R"([{"typePattern": "boolean"}])"};

    CustomVisualizer viz;
    viz.load(config.path_string());

    const auto rule = viz.find_rule("boolean");
    ASSERT_TRUE(rule.has_value());
    ASSERT_TRUE(rule->display_template.empty());
    ASSERT_TRUE(rule->summary_template.empty());
}

// ─── Glob semantics ───────────────────────────────────────────────

void test_star_is_wildcard() {
    const TempFile config{R"([{"typePattern": "array<*>", "displayTemplate": "arr"}])"};

    CustomVisualizer viz;
    viz.load(config.path_string());

    ASSERT_TRUE(viz.find_rule("array<integer>").has_value());
    ASSERT_TRUE(viz.find_rule("array<string>").has_value());
    // Must match the whole string: a leading segment is not enough.
    ASSERT_FALSE(viz.find_rule("dictionary<integer>").has_value());
}

void test_bare_star_matches_everything() {
    const TempFile config{R"([{"typePattern": "*", "displayTemplate": "any"}])"};

    CustomVisualizer viz;
    viz.load(config.path_string());

    ASSERT_TRUE(viz.find_rule("integer").has_value());
    ASSERT_TRUE(viz.find_rule("array<queue<string>>").has_value());
    ASSERT_TRUE(viz.find_rule("").has_value());
}

void test_dot_is_literal_not_regex() {
    // Only '*' is a wildcard; regex metacharacters (including '.') are escaped
    // and match literally.
    const TempFile config{R"([{"typePattern": "a.b", "displayTemplate": "dot"}])"};

    CustomVisualizer viz;
    viz.load(config.path_string());

    ASSERT_TRUE(viz.find_rule("a.b").has_value());
    ASSERT_FALSE(viz.find_rule("axb").has_value()); // '.' is not "any char".
}

void test_question_mark_is_literal() {
    // '?' is not a single-char wildcard here — it is escaped and matches a
    // literal question mark.
    const TempFile config{R"([{"typePattern": "opt?", "displayTemplate": "q"}])"};

    CustomVisualizer viz;
    viz.load(config.path_string());

    ASSERT_TRUE(viz.find_rule("opt?").has_value());
    ASSERT_FALSE(viz.find_rule("opti").has_value());
}

void test_first_matching_rule_wins() {
    const TempFile config{R"([
        {"typePattern": "array<*>", "displayTemplate": "first"},
        {"typePattern": "array<integer>", "displayTemplate": "second"}
    ])"};

    CustomVisualizer viz;
    viz.load(config.path_string());

    const auto rule = viz.find_rule("array<integer>");
    ASSERT_TRUE(rule.has_value());
    ASSERT_EQ(rule->display_template, std::string("first"));
}

// ─── Malformed configs ────────────────────────────────────────────

void test_missing_file_throws() {
    CustomVisualizer viz;
    ASSERT_THROWS(viz.load("no_such_visualizer_config_zzz.json"));
}

void test_non_array_root_throws() {
    const TempFile config{R"({"typePattern": "integer"})"}; // object, not array

    CustomVisualizer viz;
    ASSERT_THROWS(viz.load(config.path_string()));
}

void test_rule_without_type_pattern_skipped() {
    // The first entry lacks the required typePattern and is skipped; the second
    // is loaded normally.
    const TempFile config{R"([
        {"displayTemplate": "orphan"},
        {"typePattern": "string", "displayTemplate": "kept"}
    ])"};

    CustomVisualizer viz;
    viz.load(config.path_string());

    ASSERT_TRUE(viz.has_rules());
    const auto rule = viz.find_rule("string");
    ASSERT_TRUE(rule.has_value());
    ASSERT_EQ(rule->display_template, std::string("kept"));
}

void test_non_object_entries_skipped() {
    const TempFile config{R"([
        42,
        "not-an-object",
        {"typePattern": "number", "displayTemplate": "num"}
    ])"};

    CustomVisualizer viz;
    viz.load(config.path_string());

    ASSERT_TRUE(viz.find_rule("number").has_value());
}

// ─── Caching and reload ───────────────────────────────────────────

void test_repeated_lookup_is_consistent() {
    const TempFile config{R"([{"typePattern": "integer", "displayTemplate": "<int>"}])"};

    CustomVisualizer viz;
    viz.load(config.path_string());

    // Second lookup is served from the match cache and must agree with the first.
    const auto first = viz.find_rule("integer");
    const auto second = viz.find_rule("integer");
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(first->display_template, second->display_template);

    // Negative results are cached too.
    ASSERT_FALSE(viz.find_rule("string").has_value());
    ASSERT_FALSE(viz.find_rule("string").has_value());
}

void test_clear_cache_preserves_rules() {
    const TempFile config{R"([{"typePattern": "integer", "displayTemplate": "<int>"}])"};

    CustomVisualizer viz;
    viz.load(config.path_string());
    ASSERT_TRUE(viz.find_rule("integer").has_value()); // populate cache

    viz.clear_cache();

    // Rules survive a cache clear; only memoised match results are dropped.
    ASSERT_TRUE(viz.has_rules());
    ASSERT_TRUE(viz.find_rule("integer").has_value());
}

void test_reload_replaces_rules_and_cache() {
    const TempFile config_a{R"([{"typePattern": "foo", "displayTemplate": "A"}])"};
    const TempFile config_b{R"([{"typePattern": "bar", "displayTemplate": "B"}])"};

    CustomVisualizer viz;
    viz.load(config_a.path_string());
    ASSERT_TRUE(viz.find_rule("foo").has_value()); // caches a positive match

    // Reloading must swap the rule set and invalidate stale cache entries.
    viz.load(config_b.path_string());
    ASSERT_FALSE(viz.find_rule("foo").has_value());
    const auto rule = viz.find_rule("bar");
    ASSERT_TRUE(rule.has_value());
    ASSERT_EQ(rule->display_template, std::string("B"));
}

void test_diagnostic_callback_is_settable() {
    // Installing a diagnostic sink must not disturb normal loading/matching.
    std::vector<std::string> diagnostics;

    CustomVisualizer viz;
    viz.set_diagnostic_callback(
        [&diagnostics](std::string_view msg) { diagnostics.emplace_back(msg); });

    const TempFile config{R"([{"typePattern": "integer", "displayTemplate": "<int>"}])"};
    viz.load(config.path_string());

    ASSERT_TRUE(viz.find_rule("integer").has_value());
    // A well-formed config emits no diagnostics.
    ASSERT_TRUE(diagnostics.empty());
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Custom Visualizer Tests");

    // Loading and basic matching.
    RUN(test_no_rules_before_load);
    RUN(test_load_exact_match);
    RUN(test_no_match_returns_nullopt);
    RUN(test_optional_templates_default_empty);

    // Glob semantics.
    RUN(test_star_is_wildcard);
    RUN(test_bare_star_matches_everything);
    RUN(test_dot_is_literal_not_regex);
    RUN(test_question_mark_is_literal);
    RUN(test_first_matching_rule_wins);

    // Malformed configs.
    RUN(test_missing_file_throws);
    RUN(test_non_array_root_throws);
    RUN(test_rule_without_type_pattern_skipped);
    RUN(test_non_object_entries_skipped);

    // Caching and reload.
    RUN(test_repeated_lookup_is_consistent);
    RUN(test_clear_cache_preserves_rules);
    RUN(test_reload_replaces_rules_and_cache);
    RUN(test_diagnostic_callback_is_settable);

    return SUMMARY();
}
