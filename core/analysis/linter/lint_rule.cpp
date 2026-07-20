#include "analysis/linter/lint_rule.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>

namespace luma {

// ─────────── Built-in lint rule table ───────────
//
// Each entry corresponds to one DiagnosticCode in the W0xxx range.
// The IDs and names are stable — external tools (LSP, suppression
// comments) may reference them by string.

// clang-format off
static constexpr std::array builtin_rules = std::to_array<LintRuleInfo>({
    {.id="W0001", .name="unused-variable",
     .description="Variable is declared but never used",
     .severity=Severity::Warning, .code=DiagnosticCode::UnusedVariable},

    {.id="W0002", .name="unused-function",
     .description="Function is declared but never called",
     .severity=Severity::Warning, .code=DiagnosticCode::UnusedFunction},

    {.id="W0003", .name="unused-parameter",
     .description="Parameter is declared but never used",
     .severity=Severity::Warning, .code=DiagnosticCode::UnusedParameter},

    {.id="W0004", .name="mutable-never-mutated",
     .description="Mutable variable is never mutated — remove the 'mutable' keyword",
     .severity=Severity::Warning, .code=DiagnosticCode::MutableNeverMutated},

    {.id="W0005", .name="self-assignment",
     .description="Variable is assigned to itself, which has no effect",
     .severity=Severity::Warning, .code=DiagnosticCode::SelfAssignment},

    {.id="W0006", .name="unreachable-code",
     .description="Code after return/break/continue is never executed",
     .severity=Severity::Warning, .code=DiagnosticCode::UnreachableCode},

    {.id="W0007", .name="empty-body",
     .description="Block body is empty — add statements or remove the construct",
     .severity=Severity::Warning, .code=DiagnosticCode::EmptyBody},

    {.id="W0008", .name="always-true-false",
     .description="Condition is always true or always false",
     .severity=Severity::Warning, .code=DiagnosticCode::AlwaysTrueFalse},

    {.id="W0009", .name="floating-point-equality",
     .description="Floating-point values compared with == or != — use epsilon comparison",
     .severity=Severity::Warning, .code=DiagnosticCode::FloatingPointEquality},

    {.id="W0010", .name="discarded-result",
     .description="Result of a function call is discarded without handling",
     .severity=Severity::Warning, .code=DiagnosticCode::DiscardedResult},

    {.id="W0011", .name="redundant-else",
     .description="Else branch after a return is redundant — flatten the control flow",
     .severity=Severity::Hint, .code=DiagnosticCode::RedundantElse},

    {.id="W0012", .name="shadowed-variable",
     .description="Variable shadows a variable in an outer scope",
     .severity=Severity::Info, .code=DiagnosticCode::ShadowedVariable},

    {.id="W0013", .name="empty-catch",
     .description="Empty catch block silently swallows errors",
     .severity=Severity::Warning, .code=DiagnosticCode::EmptyCatch},

    {.id="W0014", .name="division-by-literal-zero",
     .description="Division or modulo by literal zero always causes a runtime error",
     .severity=Severity::Warning, .code=DiagnosticCode::DivisionByLiteralZero},

    {.id="W0015", .name="double-negation",
     .description="Double negation '!!' is redundant — simplify to the original expression",
     .severity=Severity::Hint, .code=DiagnosticCode::DoubleNegation},

    {.id="W0016", .name="unused-include",
     .description="Included file appears unused — remove if not needed",
     .severity=Severity::Warning, .code=DiagnosticCode::UnusedInclude},
});
// clang-format on

// Every built-in lint rule must fall inside the lint-warning code range, which
// is exactly the span covered by LintRuleRegistry's O(1) code index.  This
// guards against a rule being added with a code outside the range (e.g. a typo
// in the W0xxx value), which would make find_by_code silently miss it and drop
// the rule's custom severity.
static_assert(
    [] {
        for (const auto& rule : builtin_rules) {
            const auto code = static_cast<int>(rule.code);
            if (code < k_lint_warning_min || code >= k_lint_warning_max) {
                return false;
            }
        }
        return true;
    }(),
    "a built-in lint rule has a DiagnosticCode outside the W0xxx lint range");

std::span<const LintRuleInfo> builtin_lint_rules() {
    return builtin_rules;
}

const LintRuleInfo& find_builtin_rule(DiagnosticCode code) {
    for (const auto& rule : builtin_rules) {
        if (rule.code == code) {
            return rule;
        }
    }
    assert(false && "find_builtin_rule: code not found in builtin rule table");
    std::abort(); // silence -Wreturn-type in release builds
}

// ─────────── LintRuleRegistry ───────────

void LintRuleRegistry::register_rule(const LintRuleInfo& info) {
    rules_.push_back(&info);

    // Populate the O(1) code index for lint codes.
    const auto code_val = static_cast<int>(info.code);
    const auto idx = code_val - k_code_index_offset;
    if (idx >= 0 && static_cast<std::size_t>(idx) < k_code_index_size) {
        code_index_[static_cast<std::size_t>(idx)] = &info;
    }
}

const LintRuleInfo* LintRuleRegistry::find_by_id(std::string_view id) const {
    const auto it =
        std::ranges::find_if(rules_, [&](const LintRuleInfo* rule) { return rule->id == id; });
    return it != rules_.end() ? *it : nullptr;
}

const LintRuleInfo* LintRuleRegistry::find_by_name(std::string_view name) const {
    const auto it =
        std::ranges::find_if(rules_, [&](const LintRuleInfo* rule) { return rule->name == name; });
    return it != rules_.end() ? *it : nullptr;
}

const LintRuleInfo* LintRuleRegistry::find_by_code(DiagnosticCode code) const {
    const auto code_val = static_cast<int>(code);
    const auto idx = code_val - k_code_index_offset;
    if (idx >= 0 && static_cast<std::size_t>(idx) < k_code_index_size) {
        return code_index_[static_cast<std::size_t>(idx)];
    }
    return nullptr;
}

const std::vector<const LintRuleInfo*>& LintRuleRegistry::all_rules() const {
    return rules_;
}

// ─────────── Global registry ───────────

// Thread-safe: C++11 magic statics guarantee that the local static
// variable is initialised exactly once, even under concurrent access.
const LintRuleRegistry& lint_rule_registry() {
    static const auto registry = [] {
        LintRuleRegistry reg;
        for (const auto& rule : builtin_rules) {
            reg.register_rule(rule);
        }
        return reg;
    }();
    return registry;
}

} // namespace luma
