#ifndef LUMA_LINTER_LINT_PLUGIN_HPP
#define LUMA_LINTER_LINT_PLUGIN_HPP

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/linter/lint_rule.hpp"
#include "analysis/source/source_location.hpp"

namespace luma {

struct Expression;
struct Statement;
struct Declaration;

// A single diagnostic finding produced by a lint plugin.
struct LintFinding {
    std::string message;
    SourceLocation location;
    std::string hint;
    DiagnosticCode code{DiagnosticCode::None};
};

// Abstract interface for pluggable lint rules.
//
// Each plugin implements one or more of the check_* methods.  The Linter
// calls these hooks during AST traversal; any findings are collected and
// emitted as warnings.
//
// Plugins are stateless — they receive the AST node and produce zero or
// more findings without modifying shared state.  This makes them safe to
// run in any order and easy to test in isolation.
//
// To implement a new lint rule:
//   1. Add a new entry to the builtin_rules table in lint_rule.cpp
//      (assign the next W0xxx ID and a DiagnosticCode).
//   2. Subclass LintPlugin
//   3. Override the relevant check_* method(s)
//   4. Return the canonical rule info from rule_info() by looking it up
//      from builtin_lint_rules() (see DivisionByZeroPlugin in builtin_plugins.cpp
//      for a complete example)
//   5. Register the plugin in lint_plugin_registry() in builtin_plugins.cpp
class LintPlugin {
public:
    virtual ~LintPlugin() = default;

    LintPlugin(const LintPlugin&) = delete;
    LintPlugin& operator=(const LintPlugin&) = delete;

    // The rule metadata for this plugin.
    [[nodiscard]] virtual const LintRuleInfo& rule_info() const = 0;

    // Check an expression node.  Append any findings to the output vector.
    // Default implementation does nothing.
    virtual void check_expression(const Expression& expr, std::vector<LintFinding>& findings) const;

    // Check a statement node.  Append any findings to the output vector.
    // Default implementation does nothing.
    virtual void check_statement(const Statement& stmt, std::vector<LintFinding>& findings) const;

    // Check a declaration node.  Append any findings to the output vector.
    // Default implementation does nothing.
    virtual void check_declaration(const Declaration& decl,
                                   std::vector<LintFinding>& findings) const;

protected:
    LintPlugin() = default;
    LintPlugin(LintPlugin&&) = default;
    LintPlugin& operator=(LintPlugin&&) = default;
};

// Convenience base for built-in plugins.
//
// Supplies the canonical rule_info() by looking the plugin's DiagnosticCode
// up in the built-in rule table, removing the identical one-line override
// that every plugin would otherwise repeat.  Derive a plugin from
// BuiltinLintPlugin<Code> and only override the relevant check_* method(s).
template <DiagnosticCode Code> class BuiltinLintPlugin : public LintPlugin {
public:
    [[nodiscard]] const LintRuleInfo& rule_info() const final {
        static const LintRuleInfo& info = find_builtin_rule(Code);
        return info;
    }
};

// Registry for lint plugins.
//
// Plugins are registered at startup and queried during linting.
// The registry owns the plugin instances via unique_ptr.
//
// Thread safety: the registry is safe for concurrent reads (including
// enabled_plugins()) after initialisation.  Calling register_plugin()
// or set_enabled() concurrently with any other method is not supported.
class LintPluginRegistry {
public:
    // Register a plugin.  The registry takes ownership.
    void register_plugin(std::unique_ptr<LintPlugin> plugin);

    // Number of registered plugins.
    [[nodiscard]] std::size_t size() const;

    // Look up a plugin by its rule ID (e.g. "W0014").
    [[nodiscard]] const LintPlugin* find_by_id(std::string_view id) const;

    // Enable or disable a rule by ID.  Returns true if the rule was found.
    [[nodiscard]] bool set_enabled(std::string_view id, bool enabled);

    // Check whether a rule is enabled.  Unknown IDs return false.
    [[nodiscard]] bool is_enabled(std::string_view id) const;

    // All enabled plugins (for iteration during linting).
    // Returns a cached span — no allocation on each call.
    [[nodiscard]] std::span<const LintPlugin* const> enabled_plugins() const;

private:
    struct PluginEntry {
        std::unique_ptr<LintPlugin> plugin;
        bool enabled{true};
    };

    // Finds the entry whose rule ID matches, or entries_.end() if none does.
    // The non-const overload lets set_enabled() mutate the entry in place; the
    // lookup predicate itself lives in a single place (the const overload).
    [[nodiscard]] std::vector<PluginEntry>::const_iterator find_entry(std::string_view id) const;
    [[nodiscard]] std::vector<PluginEntry>::iterator find_entry(std::string_view id);

    // Rebuilds the cached enabled-plugins list after any enable/disable change.
    void rebuild_enabled_cache() const;

    std::vector<PluginEntry> entries_;
    mutable std::vector<const LintPlugin*> enabled_cache_;
    mutable bool cache_dirty_{true};
};

// Return the global lint plugin registry, lazily populated with built-in plugins.
[[nodiscard]] LintPluginRegistry& lint_plugin_registry();

} // namespace luma

#endif // LUMA_LINTER_LINT_PLUGIN_HPP
