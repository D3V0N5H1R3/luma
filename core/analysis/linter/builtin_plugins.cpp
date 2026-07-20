#include "analysis/linter/builtin_plugins.hpp"

#include <memory>

#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/linter/lint_rule.hpp"

namespace luma {

// ─────────── Division by literal zero (W0014) ───────────

void DivisionByZeroPlugin::check_expression(const Expression& expr,
                                            std::vector<LintFinding>& findings) const {
    const auto* bin = try_cast<BinaryExpression>(expr);
    if (bin == nullptr) {
        return;
    }

    // Only division and modulo operators.
    if (bin->op != TokenType::Slash && bin->op != TokenType::Percent &&
        bin->op != TokenType::SlashSlash) {
        return;
    }

    const auto* rhs = try_cast<LiteralExpression>(*bin->right);
    if (rhs == nullptr) {
        return;
    }

    if ((is_integer_literal(*bin->right) && rhs->integer_value() == 0) ||
        (is_number_literal(*bin->right) && rhs->number_value() == 0.0)) {
        findings.push_back({
            .message = "Division by zero",
            .location = bin->location,
            .hint = "This will always cause a runtime error",
            .code = DiagnosticCode::DivisionByLiteralZero,
        });
    }
}

// ─────────── Double negation (W0015) ───────────

void DoubleNegationPlugin::check_expression(const Expression& expr,
                                            std::vector<LintFinding>& findings) const {
    const auto* unary = try_cast<UnaryExpression>(expr);
    if (unary == nullptr) {
        return;
    }

    if (unary->op != TokenType::Bang) {
        return;
    }

    const auto* inner = try_cast<UnaryExpression>(*unary->operand);
    if (inner == nullptr) {
        return;
    }

    if (inner->op == TokenType::Bang) {
        findings.push_back({
            .message = "Double negation '!!' is redundant",
            .location = unary->location,
            .hint = "remove both '!' operators — '!!x' is the same as 'x'",
            .code = DiagnosticCode::DoubleNegation,
        });
    }
}

// ─────────── Floating-point equality (W0009) ───────────

void FloatingPointEqualityPlugin::check_expression(const Expression& expr,
                                                   std::vector<LintFinding>& findings) const {
    const auto* bin = try_cast<BinaryExpression>(expr);
    if (bin == nullptr) {
        return;
    }

    if (bin->op != TokenType::EqualsEquals && bin->op != TokenType::BangEquals) {
        return;
    }

    const bool left_is_float = is_number_literal(*bin->left);
    const bool right_is_float = is_number_literal(*bin->right);

    if (left_is_float || right_is_float) {
        findings.push_back({
            .message = "Floating-point equality comparison",
            .location = bin->location,
            .hint = "floating-point numbers should not be compared with == or !=; "
                    "use Math.abs(a - b) < epsilon instead",
            .code = DiagnosticCode::FloatingPointEquality,
        });
    }
}

// ─────────── Global plugin registry ───────────

// Thread-safe: C++11 magic statics guarantee that the local static
// variable is initialised exactly once, even under concurrent access.
LintPluginRegistry& lint_plugin_registry() {
    static LintPluginRegistry registry = [] {
        LintPluginRegistry reg;
        reg.register_plugin(std::make_unique<DivisionByZeroPlugin>());
        reg.register_plugin(std::make_unique<DoubleNegationPlugin>());
        reg.register_plugin(std::make_unique<FloatingPointEqualityPlugin>());
        return reg;
    }();
    return registry;
}

} // namespace luma
