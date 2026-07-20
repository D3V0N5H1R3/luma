#ifndef LUMA_LINTER_BUILTIN_PLUGINS_HPP
#define LUMA_LINTER_BUILTIN_PLUGINS_HPP

#include <vector>

#include "analysis/diagnostics/diagnostic_codes.hpp"
#include "analysis/linter/lint_plugin.hpp"

namespace luma {

// ─────────── Division by literal zero (W0014) ───────────

class DivisionByZeroPlugin final : public BuiltinLintPlugin<DiagnosticCode::DivisionByLiteralZero> {
public:
    void check_expression(const Expression& expr,
                          std::vector<LintFinding>& findings) const override;
};

// ─────────── Double negation (W0015) ───────────

class DoubleNegationPlugin final : public BuiltinLintPlugin<DiagnosticCode::DoubleNegation> {
public:
    void check_expression(const Expression& expr,
                          std::vector<LintFinding>& findings) const override;
};

// ─────────── Floating-point equality (W0009) ───────────

class FloatingPointEqualityPlugin final
    : public BuiltinLintPlugin<DiagnosticCode::FloatingPointEquality> {
public:
    void check_expression(const Expression& expr,
                          std::vector<LintFinding>& findings) const override;
};

} // namespace luma

#endif // LUMA_LINTER_BUILTIN_PLUGINS_HPP
