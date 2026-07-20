#include <format>

#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/linter/linter.hpp"
#include "stdlib/stdlib_catalog.hpp"

namespace luma {

// ─────────── Control flow & structural statement handlers ───────────

void Linter::visit_expression_statement(const ExpressionStatement& expr_stmt) {
    // Check for discarded result values from function calls.
    // Tail-position expression-statements in value blocks (e.g. the last
    // statement in a match arm) are handled by lint_statement directly and
    // never reach this handler, so no tail-position check is needed here.
    if (expr_stmt.expression->kind == ExpressionKind::Call) {
        const auto& call = static_cast<const CallExpression&>(*expr_stmt.expression);

        // Check if the callee is a stdlib function that returns result<T>.
        if (call.callee->kind == ExpressionKind::FieldAccess) {
            const auto& fa = static_cast<const FieldAccessExpression&>(*call.callee);

            if (const auto* ns = as_variable(*fa.object)) {
                const auto full_name = ns->name + "." + fa.field_name;

                if (stdlib::result_returning_functions().contains(full_name)) {
                    warn(std::format("Result of '{}' is discarded", full_name), expr_stmt.location,
                         "assign the result to a variable and handle it with 'match' or "
                         "'Result.unwrap_or', or suppress with '_ = ...'",
                         DiagnosticCode::DiscardedResult);
                }
            }
        }
    }

    lint_expression(*expr_stmt.expression);
}

void Linter::visit_return(const ReturnStatement& ret) {
    if (ret.value) {
        lint_expression(*ret.value);
    }
}

void Linter::visit_for(const ForStatement& for_stmt) {
    lint_expression(*for_stmt.iterable);

    // Check for empty loop body.
    if (for_stmt.body.empty()) {
        warn("Empty for-loop body", for_stmt.location,
             "add a body to the loop or remove it if not needed", DiagnosticCode::EmptyBody);
    }

    auto guard = make_scope_guard();

    // Register loop variables.
    if (!for_stmt.destructure_variables.empty()) {
        for (const auto& var : for_stmt.destructure_variables) {
            tracker_.track_variable(var, for_stmt.location, false);
        }
    } else if (!for_stmt.loop_variable.empty()) {
        tracker_.track_variable(for_stmt.loop_variable, for_stmt.location, false);
    }

    if (!for_stmt.index_variable.empty()) {
        tracker_.track_variable(for_stmt.index_variable, for_stmt.location, false);
    }

    lint_block(for_stmt.body);
}

void Linter::visit_if_statement(const IfStatement& if_stmt) {
    lint_expression(*if_stmt.condition);

    // Check for constant condition.
    if (if_stmt.condition->kind == ExpressionKind::Literal) {
        if (is_boolean_literal(*if_stmt.condition)) {
            const auto& lit = static_cast<const LiteralExpression&>(*if_stmt.condition);
            warn(std::format("Condition is always {}", lit.boolean_value() ? "true" : "false"),
                 if_stmt.condition->location, "This branch is never/always taken",
                 DiagnosticCode::AlwaysTrueFalse);
        }
    }

    // Check for empty then-body.
    if (if_stmt.then_body.empty()) {
        warn("Empty if-body", if_stmt.location, "add statements to the if-body or remove the if",
             DiagnosticCode::EmptyBody);
    }

    // Check for redundant else after return.
    if (!if_stmt.then_body.empty() && !if_stmt.else_body.empty()) {
        const auto& last_then = *if_stmt.then_body.back();

        if (last_then.kind == StatementKind::Return) {
            warn("Redundant else after return", if_stmt.else_body.front()->location,
                 "the else block can be removed — move its contents after the if",
                 DiagnosticCode::RedundantElse);
        }
    }

    lint_scoped_block(if_stmt.then_body);
    lint_scoped_block(if_stmt.else_body);
}

void Linter::visit_while(const WhileStatement& while_stmt) {
    lint_expression(*while_stmt.condition);

    if (while_stmt.body.empty()) {
        warn("Empty while-loop body", while_stmt.location,
             "add a body to the loop or remove it if not needed", DiagnosticCode::EmptyBody);
    }

    lint_scoped_block(while_stmt.body);
}

void Linter::visit_match_statement(const MatchStatement& match_stmt) {
    lint_expression(*match_stmt.subject);
    lint_match_arms(match_stmt.arms, match_stmt.location);
}

void Linter::visit_try(const TryStatement& try_stmt) {
    {
        auto guard = make_scope_guard();
        lint_block(try_stmt.try_body);
    }

    // Check for empty catch body — silently swallowing exceptions is
    // a common beginner mistake.
    if (!try_stmt.catch_var.empty() && try_stmt.catch_body.empty()) {
        warn("Empty catch block silently swallows errors", try_stmt.location,
             "handle the error, e.g. 'catch (err) { print(err) }', "
             "or add a comment explaining why it is safe to ignore",
             DiagnosticCode::EmptyCatch);
    }

    {
        auto guard = make_scope_guard();

        if (!try_stmt.catch_var.empty()) {
            tracker_.track_variable(try_stmt.catch_var, try_stmt.location, false);
        }

        lint_block(try_stmt.catch_body);
    }

    if (!try_stmt.finally_body.empty()) {
        auto guard = make_scope_guard();
        lint_block(try_stmt.finally_body);
    }
}

void Linter::visit_block(const BlockStatement& block) {
    lint_scoped_block(block.statements, /*last_is_value=*/true);
}

// ─────────── Shared match arm traversal ───────────

void Linter::lint_match_arms(const std::vector<MatchArm>& arms, SourceLocation match_loc) {
    for (const auto& arm : arms) {
        if (arm.comparison_value()) {
            lint_expression(*arm.comparison_value());
        }

        for (const auto& alt : arm.alternatives) {
            if (alt.comparison_value()) {
                lint_expression(*alt.comparison_value());
            }
        }

        auto guard = make_scope_guard();

        if (arm.has_binding()) {
            tracker_.track_variable(arm.binding_name(), match_loc, false);
        }

        for (const auto& binding : arm.choice_bindings()) {
            tracker_.track_variable(binding, match_loc, false);
        }

        // The guard is evaluated within the arm scope and may reference the
        // arm's bindings, so lint it after the bindings are tracked.
        if (arm.guard) {
            lint_expression(*arm.guard);
        }

        lint_block(arm.body, /*last_is_value=*/true);

        if (arm.body_expr) {
            lint_expression(*arm.body_expr);
        }
    }
}

} // namespace luma
