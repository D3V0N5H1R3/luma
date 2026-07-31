#include <format>

#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/linter/linter.hpp"

namespace luma {

// ─────────── Variable & assignment statement handlers ───────────

void Linter::visit_variable_declaration(const VariableDeclStatement& var) {
    // Check for shadowed variables.
    if (!var.name.empty() && !var.name.starts_with("_")) {
        if (tracker_.is_shadowed(var.name)) {
            warn(std::format("Variable '{}' shadows a variable in an outer scope", var.name),
                 var.location, "consider renaming, or prefix with '_' if shadowing is intentional",
                 DiagnosticCode::ShadowedVariable);
        }

        tracker_.track_variable(var.name, var.location, false, var.is_mutable);
    }

    if (var.initializer) {
        lint_expression(*var.initializer);
    }
}

void Linter::visit_assignment(const AssignmentStatement& assign) {
    // Check for self-assignment (x = x).
    const auto* target_var = as_variable(*assign.target);
    const auto* value_var = as_variable(*assign.value);
    if ((target_var != nullptr) && (value_var != nullptr)) {
        if (target_var->name == value_var->name) {
            warn(std::format("Self-assignment: '{}' is assigned to itself", target_var->name),
                 assign.location, "This has no effect", DiagnosticCode::SelfAssignment);
        }
    }

    // Track mutation. Assignment to a field or element (e.g. 'p.x = 1' or
    // 'a[0] = 1') mutates the root variable, so mark the root.
    mark_target_mutated(*assign.target);

    lint_expression(*assign.target);
    lint_expression(*assign.value);
}

void Linter::visit_compound_assignment(const CompoundAssignmentStatement& assign) {
    // Track mutation. Compound assignment to a field or element (e.g. 'p.x += 1'
    // or 'a[0] += 1') mutates the root variable, so mark the root.
    mark_target_mutated(*assign.target);

    lint_expression(*assign.target);
    lint_expression(*assign.value);
}

void Linter::visit_tuple_destructuring(const TupleDestructuringStatement& td) {
    lint_expression(*td.initializer);

    for (const auto& [type, name] : td.bindings) {
        tracker_.track_variable(name, td.location, false, td.is_mutable);
    }
}

void Linter::visit_record_destructuring(const RecordDestructuringStatement& rd) {
    lint_expression(*rd.initializer);

    for (const auto& name : rd.fields) {
        tracker_.track_variable(name, rd.location, false, rd.is_mutable);
    }
}

void Linter::visit_increment(const IncrementStatement& inc) {
    lint_expression(*inc.target);

    // Track mutation. Increment of a field or element (e.g. 'p.x++' or 'a[0]++')
    // mutates the root variable, so mark the root.
    mark_target_mutated(*inc.target);
}

void Linter::visit_decrement(const DecrementStatement& dec) {
    lint_expression(*dec.target);

    // Track mutation. Decrement of a field or element (e.g. 'p.x--' or 'a[0]--')
    // mutates the root variable, so mark the root.
    mark_target_mutated(*dec.target);
}

void Linter::mark_target_mutated(const Expression& target) {
    if (const auto* root = root_variable_of(target, ChainTraversal::FieldsAndIndices)) {
        tracker_.mark_mutated(root->name);
    }
}

} // namespace luma
