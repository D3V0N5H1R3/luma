// Unit tests for the lint plugin and rule registries.
//
// These lock in the lookup/enable behaviour of LintPluginRegistry
// (find_by_id / set_enabled / is_enabled) and the O(1) code-index lookup of
// LintRuleRegistry (find_by_id / find_by_name / find_by_code), which are
// otherwise only exercised indirectly through the default registries.

#include <algorithm>
#include <memory>
#include <string_view>

#include "analysis/diagnostics/diagnostic_codes.hpp"
#include "analysis/linter/builtin_plugins.hpp"
#include "analysis/linter/lint_plugin.hpp"
#include "analysis/linter/lint_rule.hpp"
#include "test_framework.hpp"

using luma::builtin_lint_rules;
using luma::DiagnosticCode;
using luma::DivisionByZeroPlugin;
using luma::FloatingPointEqualityPlugin;
using luma::lint_rule_registry;
using luma::LintPlugin;
using luma::LintPluginRegistry;

// ─── Helpers ───

// Builds an isolated registry with two known built-in plugins so tests never
// mutate the shared global registry via set_enabled().
[[nodiscard]] static LintPluginRegistry make_plugin_registry() {
    LintPluginRegistry reg;
    reg.register_plugin(std::make_unique<DivisionByZeroPlugin>());        // W0014
    reg.register_plugin(std::make_unique<FloatingPointEqualityPlugin>()); // W0009
    return reg;
}

// ─── LintPluginRegistry: find_by_id ───

static void test_plugin_find_by_id_known() {
    const auto reg = make_plugin_registry();

    const auto* plugin = reg.find_by_id("W0014");

    ASSERT_NE(plugin, nullptr);
    ASSERT_EQ(plugin->rule_info().id, std::string_view{"W0014"});
}

static void test_plugin_find_by_id_unknown_returns_null() {
    const auto reg = make_plugin_registry();

    ASSERT_EQ(reg.find_by_id("W9999"), nullptr);
}

// ─── LintPluginRegistry: is_enabled / set_enabled ───

static void test_plugin_enabled_by_default() {
    const auto reg = make_plugin_registry();

    ASSERT_TRUE(reg.is_enabled("W0014"));
    ASSERT_TRUE(reg.is_enabled("W0009"));
}

static void test_plugin_is_enabled_unknown_returns_false() {
    const auto reg = make_plugin_registry();

    ASSERT_FALSE(reg.is_enabled("W9999"));
}

static void test_plugin_set_enabled_disables_rule() {
    auto reg = make_plugin_registry();

    const bool changed = reg.set_enabled("W0014", /*enabled=*/false);

    ASSERT_TRUE(changed);
    ASSERT_FALSE(reg.is_enabled("W0014"));
    // Unrelated rules are unaffected.
    ASSERT_TRUE(reg.is_enabled("W0009"));
}

static void test_plugin_set_enabled_reenables_rule() {
    auto reg = make_plugin_registry();

    ASSERT_TRUE(reg.set_enabled("W0014", /*enabled=*/false));
    const bool changed = reg.set_enabled("W0014", /*enabled=*/true);

    ASSERT_TRUE(changed);
    ASSERT_TRUE(reg.is_enabled("W0014"));
}

static void test_plugin_set_enabled_unknown_returns_false() {
    auto reg = make_plugin_registry();

    ASSERT_FALSE(reg.set_enabled("W9999", /*enabled=*/false));
}

static void test_plugin_disabled_rule_excluded_from_enabled() {
    auto reg = make_plugin_registry();

    ASSERT_TRUE(reg.set_enabled("W0014", /*enabled=*/false));

    // The disabled plugin must not appear in the enabled-plugins span; the
    // other plugin must still be there.
    const auto enabled = reg.enabled_plugins();
    const auto has_id = [](std::string_view id) {
        return [id](const LintPlugin* plugin) { return plugin->rule_info().id == id; };
    };

    ASSERT_EQ(enabled.size(), 1);
    ASSERT_TRUE(std::ranges::none_of(enabled, has_id("W0014")));
    ASSERT_TRUE(std::ranges::any_of(enabled, has_id("W0009")));
}

// ─── LintRuleRegistry: find_by_id / find_by_name ───

static void test_rule_find_by_id_known() {
    const auto& reg = lint_rule_registry();

    const auto* rule = reg.find_by_id("W0001");

    ASSERT_NE(rule, nullptr);
    ASSERT_EQ(rule->name, std::string_view{"unused-variable"});
}

static void test_rule_find_by_id_unknown_returns_null() {
    const auto& reg = lint_rule_registry();

    ASSERT_EQ(reg.find_by_id("W9999"), nullptr);
}

static void test_rule_find_by_name_known() {
    const auto& reg = lint_rule_registry();

    const auto* rule = reg.find_by_name("unused-variable");

    ASSERT_NE(rule, nullptr);
    ASSERT_EQ(rule->id, std::string_view{"W0001"});
}

// ─── LintRuleRegistry: find_by_code (O(1) index) ───

static void test_rule_find_by_code_first() {
    const auto& reg = lint_rule_registry();

    const auto* rule = reg.find_by_code(DiagnosticCode::UnusedVariable);

    ASSERT_NE(rule, nullptr);
    ASSERT_EQ(rule->id, std::string_view{"W0001"});
}

static void test_rule_find_by_code_last() {
    // BooleanLiteralComparison (5017 / W0017) is the highest lint code and must
    // remain inside the code-index window — a regression guard for R05.
    const auto& reg = lint_rule_registry();

    const auto* rule = reg.find_by_code(DiagnosticCode::BooleanLiteralComparison);

    ASSERT_NE(rule, nullptr);
    ASSERT_EQ(rule->id, std::string_view{"W0017"});
    ASSERT_EQ(rule->name, std::string_view{"boolean-literal-comparison"});
}

static void test_rule_find_by_code_all_builtins_indexable() {
    // Every built-in lint rule must be reachable through the O(1) code index.
    const auto& reg = lint_rule_registry();

    ASSERT_FALSE(builtin_lint_rules().empty());

    for (const auto& rule : builtin_lint_rules()) {
        const auto* found = reg.find_by_code(rule.code);
        ASSERT_NE(found, nullptr);
        ASSERT_EQ(found->id, rule.id);
    }
}

static void test_rule_find_by_code_out_of_range_returns_null() {
    const auto& reg = lint_rule_registry();

    // DiagnosticCode::None (0) is far below the lint range and must not resolve.
    ASSERT_EQ(reg.find_by_code(DiagnosticCode::None), nullptr);
}

// ─── main ───

int main() {
    // LintPluginRegistry
    RUN(test_plugin_find_by_id_known);
    RUN(test_plugin_find_by_id_unknown_returns_null);
    RUN(test_plugin_enabled_by_default);
    RUN(test_plugin_is_enabled_unknown_returns_false);
    RUN(test_plugin_set_enabled_disables_rule);
    RUN(test_plugin_set_enabled_reenables_rule);
    RUN(test_plugin_set_enabled_unknown_returns_false);
    RUN(test_plugin_disabled_rule_excluded_from_enabled);

    // LintRuleRegistry
    RUN(test_rule_find_by_id_known);
    RUN(test_rule_find_by_id_unknown_returns_null);
    RUN(test_rule_find_by_name_known);
    RUN(test_rule_find_by_code_first);
    RUN(test_rule_find_by_code_last);
    RUN(test_rule_find_by_code_all_builtins_indexable);
    RUN(test_rule_find_by_code_out_of_range_returns_null);

    return SUMMARY();
}
