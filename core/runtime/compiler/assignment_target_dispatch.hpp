// ─────────────────────────────────────────────────────────────────────────────
// Assignment target dispatch — shared template for Variable/Field/Index targets
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_ASSIGNMENT_TARGET_DISPATCH_HPP
#define LUMA_COMPILER_ASSIGNMENT_TARGET_DISPATCH_HPP

#include "analysis/ast/expression.hpp"

namespace luma {

// Dispatches on the assignment target kind and invokes the appropriate handler.
// Each handler receives a reference to the concrete expression type.
template <typename VariableHandler, typename FieldHandler, typename IndexHandler>
void dispatch_assignment_target(const Expression& target, VariableHandler&& on_variable,
                                FieldHandler&& on_field, IndexHandler&& on_index) {
    if (target.kind == ExpressionKind::Variable) {
        on_variable(static_cast<const VariableExpression&>(target));
    } else if (target.kind == ExpressionKind::FieldAccess) {
        on_field(static_cast<const FieldAccessExpression&>(target));
    } else if (target.kind == ExpressionKind::IndexAccess) {
        on_index(static_cast<const IndexAccessExpression&>(target));
    }
}

} // namespace luma

#endif // LUMA_COMPILER_ASSIGNMENT_TARGET_DISPATCH_HPP
