#ifndef LUMA_LINTER_LINT_RULE_HPP
#define LUMA_LINTER_LINT_RULE_HPP

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "analysis/diagnostics/diagnostic_codes.hpp"

namespace luma {

// Metadata for a single lint rule.
//
// Each lint check in the Linter is associated with a LintRuleInfo that
// provides a stable machine-readable ID, a kebab-case name, a human-
// readable description, and a default severity.  This information is
// used by the diagnostic renderer, the language server, and (in the
// future) suppression comments.
struct LintRuleInfo {
    std::string_view id;          // e.g. "W0001"
    std::string_view name;        // e.g. "unused-variable"
    std::string_view description; // e.g. "Variable is declared but never used"
    Severity severity;            // Default severity (Warning, Info, Hint)
    DiagnosticCode code;          // Corresponding DiagnosticCode enum value
};

// Registry of all known lint rules.
//
// The registry owns no heap memory — it stores pointers into a
// statically-allocated rule table.  It is populated once at startup
// and then queried by the Linter and LSP.
class LintRuleRegistry {
public:
    // Register a rule.  The pointed-to LintRuleInfo must outlive the registry.
    void register_rule(const LintRuleInfo& info);

    // Look up a rule by its stable ID (e.g. "W0001").
    //
    // Returns a non-owning pointer to the matching LintRuleInfo, or
    // nullptr if no rule with that ID is registered.  The returned
    // pointer remains valid for the lifetime of the registry.
    //
    // Callers should check for nullptr before dereferencing:
    //
    //   if (const auto* rule = registry.find_by_id("W0001")) {
    //       // use rule->name, rule->description, etc.
    //   }
    [[nodiscard]] const LintRuleInfo* find_by_id(std::string_view id) const;

    // Look up a rule by its kebab-case name (e.g. "unused-variable").
    //
    // Returns nullptr if no rule with that name is registered.
    // See find_by_id() for ownership and lifetime guarantees.
    [[nodiscard]] const LintRuleInfo* find_by_name(std::string_view name) const;

    // Look up a rule by its DiagnosticCode.  O(1) via direct index lookup.
    //
    // Returns nullptr if no rule maps to the given code.
    // See find_by_id() for ownership and lifetime guarantees.
    [[nodiscard]] const LintRuleInfo* find_by_code(DiagnosticCode code) const;

    // All registered rules (unordered).
    [[nodiscard]] const std::vector<const LintRuleInfo*>& all_rules() const;

private:
    std::vector<const LintRuleInfo*> rules_;
    // O(1) lookup by DiagnosticCode.  Indexed by the code's offset from the
    // start of the lint-warning range.  The window spans the entire W0xxx range
    // (k_lint_warning_min..k_lint_warning_max), so every lint code is representable;
    // a static_assert in lint_rule.cpp proves all built-in rules are indexable.
    static constexpr int k_code_index_offset = k_lint_warning_min;
    static constexpr std::size_t k_code_index_size =
        static_cast<std::size_t>(k_lint_warning_max - k_lint_warning_min);
    std::array<const LintRuleInfo*, k_code_index_size> code_index_{};
};

// Return the global lint rule registry, lazily populated on first call.
[[nodiscard]] const LintRuleRegistry& lint_rule_registry();

// Return a span over the built-in lint rule table.
[[nodiscard]] std::span<const LintRuleInfo> builtin_lint_rules();

// Look up canonical built-in rule metadata by DiagnosticCode.
//
// Returns a reference into the static built-in rule table.  Aborts if the
// code is not a built-in lint rule — callers pass compile-time constant
// codes, so a miss is a programming error rather than invalid input.
[[nodiscard]] const LintRuleInfo& find_builtin_rule(DiagnosticCode code);

} // namespace luma

#endif // LUMA_LINTER_LINT_RULE_HPP
