#include <algorithm>
#include <format>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/types/match_arm_binding.hpp"
#include "analysis/types/statement_type_checker.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checker.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "analysis/types/type_info.hpp"

namespace luma {

void StatementTypeChecker::visit_for(const ForStatement& stmt) {
    // Ownership: snapshot before the loop to detect consumption of
    // outer unique variables inside the loop body.
    const auto before_loop = tc_.context().current_scope->snapshot_ownership();

    ++tc_.context().loop_depth;

    tc_.push_scope();

    // Single cleanup for every exit path (the dictionary early-return and the
    // normal fall-through): pop the loop scope, decrement the loop-depth
    // counter, check for unique consumption inside the body, and restore the
    // pre-loop ownership state.
    const ScopeGuard loop_cleanup{[&] {
        tc_.pop_scope();
        --tc_.context().loop_depth;
        check_unique_consumption_in_loop(before_loop, stmt.location);
        tc_.context().current_scope->restore_ownership(before_loop);
    }};

    const auto iterable_type = tc_.infer_expression_type(*stmt.iterable);

    // Infer element type from iterable for the loop variable binding.
    // Supports: array<T> → T, range → integer, string → string,
    // dictionary → (key: string, value: T) pair via index_variable.
    TypeInfo element_type = TypeInfo::make(TypeInfo::Kind::StdlibAny);

    if (iterable_type.kind == TypeInfo::Kind::Array) {
        if (!iterable_type.inner_types.empty()) {
            element_type = iterable_type.inner_types[0];
        }
    } else if (iterable_type.kind == TypeInfo::Kind::Range) {
        element_type = TypeInfo::make(TypeInfo::Kind::Integer);
    } else if (iterable_type.kind == TypeInfo::Kind::String) {
        element_type = TypeInfo::make(TypeInfo::Kind::String);
    } else if (iterable_type.kind == TypeInfo::Kind::Dictionary) {
        if (!stmt.index_variable.empty()) {
            // for key, value in dict  →  index_variable = key (string), loop_variable = value type
            const TypeInfo value_type = (!iterable_type.inner_types.empty())
                                            ? iterable_type.inner_types[0]
                                            : TypeInfo::make(TypeInfo::Kind::StdlibAny);

            tc_.context().current_scope->define(stmt.index_variable,
                                                TypeInfo::make(TypeInfo::Kind::String), {});
            tc_.context().current_scope->define(stmt.loop_variable, value_type, {});
        } else {
            // Single-variable for over dictionary is not allowed.
            tc_.error("for over a dictionary requires two loop variables — "
                      "use 'for key, value in dict', 'Dictionary.keys(d)', "
                      "or 'Dictionary.values(d)' to iterate",
                      stmt.location, "use 'for key, value in dict' to iterate over a dictionary");

            // Recover: define the variable as string for further analysis.
            element_type = TypeInfo::make(TypeInfo::Kind::String);

            tc_.context().current_scope->define(stmt.loop_variable, element_type, {});
        }
    } else {
        (void)type_check_helpers::check_iterable_type(tc_, iterable_type, stmt.location);
    }

    if (iterable_type.kind == TypeInfo::Kind::Dictionary) {
        check_statement_list(stmt.body);

        return;
    }

    // Handle tuple destructuring: for (a, b) in array_of_tuples
    if (!stmt.destructure_variables.empty()) {
        // Element type should be a tuple with matching arity.
        if (element_type.kind == TypeInfo::Kind::Tuple &&
            element_type.inner_types.size() == stmt.destructure_variables.size()) {
            for (std::size_t i{0}; i < stmt.destructure_variables.size(); ++i) {
                tc_.context().current_scope->define(stmt.destructure_variables[i],
                                                    element_type.inner_types[i], {});
            }
        } else if (element_type.kind == TypeInfo::Kind::Tuple) {
            tc_.error(
                std::format("destructuring expects {} variable(s) but tuple has {} element(s)",
                            stmt.destructure_variables.size(), element_type.inner_types.size()),
                stmt.location, "match the number of loop variables to the tuple size");

            // Recover: define all vars as unknown.
            for (const auto& var : stmt.destructure_variables) {
                tc_.context().current_scope->define(var, TypeInfo::make(TypeInfo::Kind::StdlibAny),
                                                    {});
            }
        } else {
            // Not a tuple — define vars as the element type for recovery.
            for (const auto& var : stmt.destructure_variables) {
                tc_.context().current_scope->define(var, TypeInfo::make(TypeInfo::Kind::StdlibAny),
                                                    {});
            }
        }
    } else {
        tc_.context().current_scope->define(stmt.loop_variable, element_type, {});

        if (!stmt.index_variable.empty()) {
            tc_.context().current_scope->define(stmt.index_variable,
                                                TypeInfo::make(TypeInfo::Kind::Integer), {});
        }
    }

    check_statement_list(stmt.body);
}

void StatementTypeChecker::visit_if_statement(const IfStatement& stmt) {
    const auto cond_type = tc_.infer_expression_type(*stmt.condition);

    if (cond_type.kind != TypeInfo::Kind::Boolean && cond_type.kind != TypeInfo::Kind::StdlibAny &&
        cond_type.kind != TypeInfo::Kind::Unknown) {
        tc_.error(std::format("if condition must be boolean, got '{}'", cond_type.to_string()),
                  stmt.condition->location, "use a comparison (==, !=) or convert to boolean");
    }

    // Flow-sensitive type narrowing: check for is<T>(variable) condition.
    std::string narrowed_var;
    TypeInfo narrowed_type{TypeInfo::make(TypeInfo::Kind::Unknown)};
    const bool has_refinement =
        tc_.try_extract_is_refinement(*stmt.condition, narrowed_var, narrowed_type);

    // Flow-sensitive ownership: snapshot before branches.
    const auto before = tc_.context().current_scope->snapshot_ownership();

    {
        auto scope = tc_.make_scope_guard();

        const auto mark = tc_.refinement_mark();

        if (has_refinement) {
            tc_.push_refinement(narrowed_var, narrowed_type);
        }

        check_statement_list(stmt.then_body);

        tc_.pop_refinements(mark);
    }

    // Capture ownership state after then-branch.
    const auto after_then = tc_.context().current_scope->snapshot_ownership();

    if (!stmt.else_body.empty()) {
        // Restore to pre-branch state before checking else.
        tc_.context().current_scope->restore_ownership(before);

        {
            auto scope = tc_.make_scope_guard();
            check_statement_list(stmt.else_body);
        }

        const auto after_else = tc_.context().current_scope->snapshot_ownership();

        // Merge: a unique variable is consumed after if/else only if
        // it was consumed in BOTH branches.  If consumed in only one
        // branch, mark it consumed (conservative — prevents use-after-
        // move on the path where it was consumed).
        for (const auto& entry : before) {
            // Bind plain locals rather than capturing the structured binding
            // by reference in the lambda below: clang-analyzer mis-models a
            // captured structured-binding reference as an undefined pointer
            // (a known core.NullDereference false positive).
            const auto& name = entry.first;
            const bool was_consumed_before = entry.second;

            // A variable's consumed flag after a branch, falling back to its
            // pre-branch state when the branch left it untouched.
            const auto consumed_after = [&](const TypeScope::OwnershipSnapshot& snapshot) {
                for (const auto& snapshot_entry : snapshot) {
                    if (snapshot_entry.first == name) {
                        return snapshot_entry.second;
                    }
                }

                return was_consumed_before;
            };

            const bool consumed_in_then = consumed_after(after_then);
            const bool consumed_in_else = consumed_after(after_else);

            const auto* sym = tc_.lookup_variable(name);

            if ((sym != nullptr) && sym->is_unique) {
                // If consumed on any path, mark consumed (safe default).
                tc_.context().current_scope->mark_consumed(name,
                                                           consumed_in_then || consumed_in_else);
            }
        }
    } else {
        // No else branch: restore to pre-branch state.  A unique variable
        // consumed only in the then-branch is NOT reliably consumed
        // (the else path did not consume it), so restore original state.
        tc_.context().current_scope->restore_ownership(before);
    }
}

void StatementTypeChecker::visit_match_statement(const MatchStatement& stmt) {
    const auto subject_type = tc_.infer_expression_type(*stmt.subject);

    tc_.check_match_exhaustiveness(stmt.arms, subject_type, stmt.location);

    // Flow-sensitive ownership: snapshot before arms.
    const auto before = tc_.context().current_scope->snapshot_ownership();

    std::vector<TypeScope::OwnershipSnapshot> arm_snapshots;

    for (const auto& arm : stmt.arms) {
        // Restore to pre-match state before each arm.
        tc_.context().current_scope->restore_ownership(before);

        tc_.push_scope();

        check_match_arm_comparison(arm, subject_type);
        match_arm_binding::bind_arm_names(tc_, arm, subject_type);
        match_arm_binding::bind_choice_fields(tc_, arm, subject_type, stmt.location, true);

        // Type-check optional guard expression.
        if (arm.guard) {
            const auto guard_type = tc_.infer_expression_type(*arm.guard);

            (void)type_check_helpers::require_boolean_operand(tc_, guard_type, "match guard",
                                                              arm.guard->location);
        }

        check_statement_list(arm.body);

        if (arm.body_expr) {
            (void)tc_.infer_expression_type(*arm.body_expr);
        }

        tc_.pop_scope();

        // Capture ownership state after this arm.
        arm_snapshots.push_back(tc_.context().current_scope->snapshot_ownership());
    }

    match_arm_binding::merge_arm_ownership(tc_, before, arm_snapshots);
}

void StatementTypeChecker::check_match_arm_comparison(const MatchArm& arm,
                                                      const TypeInfo& subject_type) {
    if (!arm.comparison_value()) {
        return;
    }

    const auto arm_val_type = tc_.infer_expression_type(*arm.comparison_value());

    // Validate comparison type compatibility with subject.
    if (arm.kind() == MatchArm::Kind::IntegerCase && subject_type.kind != TypeInfo::Kind::Integer &&
        subject_type.kind != TypeInfo::Kind::StdlibAny &&
        subject_type.kind != TypeInfo::Kind::Unknown) {
        tc_.error(
            std::format("match arm has integer case but subject is '{}'", subject_type.to_string()),
            arm.comparison_value()->location, "integer case arms can only match integer values");
    } else if (arm.kind() == MatchArm::Kind::StringCase &&
               subject_type.kind != TypeInfo::Kind::String &&
               subject_type.kind != TypeInfo::Kind::StdlibAny &&
               subject_type.kind != TypeInfo::Kind::Unknown) {
        tc_.error(
            std::format("match arm has string case but subject is '{}'", subject_type.to_string()),
            arm.comparison_value()->location, "string case arms can only match string values");
    } else if (arm.kind() == MatchArm::Kind::Comparison &&
               !tc_.is_assignable(subject_type, arm_val_type) &&
               !tc_.is_assignable(arm_val_type, subject_type) &&
               subject_type.kind != TypeInfo::Kind::StdlibAny &&
               subject_type.kind != TypeInfo::Kind::Unknown) {
        tc_.error(std::format("match comparison type '{}' is not compatible with subject "
                              "type '{}'",
                              arm_val_type.to_string(), subject_type.to_string()),
                  arm.comparison_value()->location,
                  "ensure the case value type matches the match subject type");
    }
}

void StatementTypeChecker::visit_block(const BlockStatement& stmt) {
    auto scope = tc_.make_scope_guard();
    check_statement_list(stmt.statements);
}

void StatementTypeChecker::visit_while(const WhileStatement& stmt) {
    ++tc_.context().loop_depth;

    // Balance the loop-depth counter on every exit path.
    const ScopeGuard loop_depth_guard{[this] {
        --tc_.context().loop_depth;
    }};

    const auto cond_type = tc_.infer_expression_type(*stmt.condition);

    if (cond_type.kind != TypeInfo::Kind::Boolean && cond_type.kind != TypeInfo::Kind::StdlibAny &&
        cond_type.kind != TypeInfo::Kind::Unknown) {
        tc_.error(std::format("while condition must be boolean, got '{}'", cond_type.to_string()),
                  stmt.condition->location, "use a comparison (==, !=) or convert to boolean");
    }

    // Linter: always-false condition (while false is always dead code).
    if (stmt.condition->kind == ExpressionKind::Literal) {
        if (is_boolean_literal(*stmt.condition)) {
            const auto& lit = static_cast<const LiteralExpression&>(*stmt.condition);

            if (!lit.boolean_value()) {
                tc_.warn("condition is always false; loop body will never execute",
                         stmt.condition->location, "remove the while loop or change the condition");
            }
        }
    }

    // Ownership: snapshot before loop body.  If a unique variable from
    // an outer scope is consumed inside the loop, the second iteration
    // would use a consumed value — that is an error.
    const auto before = tc_.context().current_scope->snapshot_ownership();

    {
        auto scope = tc_.make_scope_guard();
        check_statement_list(stmt.body);
    }

    // Check: any outer unique variable newly consumed inside the loop?
    check_unique_consumption_in_loop(before, stmt.location);

    // Restore original state — the loop may not execute, so ownership
    // state should not change based on the loop body.
    tc_.context().current_scope->restore_ownership(before);
}

void StatementTypeChecker::visit_try(const TryStatement& stmt) {
    {
        auto scope = tc_.make_scope_guard();
        check_statement_list(stmt.try_body);
    }

    if (!stmt.catch_body.empty()) {
        auto scope = tc_.make_scope_guard();

        if (!stmt.catch_var.empty()) {
            // The caught error message is always a string.
            tc_.context().current_scope->define(stmt.catch_var,
                                                TypeInfo::make(TypeInfo::Kind::String), {});
        }

        check_statement_list(stmt.catch_body);
    }

    if (!stmt.finally_body.empty()) {
        auto scope = tc_.make_scope_guard();
        check_statement_list(stmt.finally_body);
    }
}

// ═══════════════════════════════════════════════════════════
// Return path analysis
// ═══════════════════════════════════════════════════════════

// Returns true if the given statement block is guaranteed to reach a
// return statement on every path, so the function cannot fall through.
bool StatementTypeChecker::definitely_returns(std::span<const StatementPtr> stmts) const {
    for (const auto& stmt : stmts) {
        switch (stmt->kind) {
            case StatementKind::Return:
                return true;

            case StatementKind::If: {
                const auto& if_stmt = static_cast<const IfStatement&>(*stmt);

                // Only definite if BOTH branches are present and both return.
                if (!if_stmt.else_body.empty() && definitely_returns(if_stmt.then_body) &&
                    definitely_returns(if_stmt.else_body)) {
                    return true;
                }

                break;
            }

            case StatementKind::Block: {
                const auto& block = static_cast<const BlockStatement&>(*stmt);

                if (definitely_returns(block.statements)) {
                    return true;
                }

                break;
            }

            case StatementKind::While: {
                // while true { return x } is guaranteed to return (or loop
                // forever — which is also non-falling-through).  But a `break`
                // that can exit the loop makes fall-through possible, so it is
                // only definite when no reachable break targets this loop.
                const auto& while_stmt = static_cast<const WhileStatement&>(*stmt);

                if (while_stmt.condition->kind == ExpressionKind::Literal) {
                    if (is_boolean_literal(*while_stmt.condition)) {
                        const auto& lit =
                            static_cast<const LiteralExpression&>(*while_stmt.condition);

                        if (lit.boolean_value() && !body_can_break(while_stmt.body) &&
                            definitely_returns(while_stmt.body)) {
                            return true;
                        }
                    }
                }

                break;
            }

            case StatementKind::Try: {
                const auto& try_stmt = static_cast<const TryStatement&>(*stmt);

                // Definite if try body returns AND (no catch, or catch also returns).
                if (definitely_returns(try_stmt.try_body) &&
                    (try_stmt.catch_body.empty() || definitely_returns(try_stmt.catch_body))) {
                    return true;
                }

                break;
            }

            case StatementKind::Match: {
                const auto& match_stmt = static_cast<const MatchStatement&>(*stmt);

                if (match_definitely_returns(match_stmt)) {
                    return true;
                }

                break;
            }

            default:
                break;
        }
    }
    return false;
}

bool StatementTypeChecker::match_definitely_returns(const MatchStatement& match_stmt) const {
    // Definite if the match is exhaustive and every arm definitely returns.
    if (!tc_.is_match_exhaustive(match_stmt)) {
        return false;
    }

    for (const auto& arm : match_stmt.arms) {
        if (!definitely_returns(arm.body)) {
            return false;
        }
    }

    return true;
}

bool StatementTypeChecker::body_can_break(const std::vector<StatementPtr>& stmts) const {
    for (const auto& stmt : stmts) {
        switch (stmt->kind) {
            case StatementKind::Break:
                return true;

            case StatementKind::While:
            case StatementKind::For:
                // A break inside a nested loop binds to that loop, not this one.
                break;

            case StatementKind::If: {
                const auto& if_stmt = static_cast<const IfStatement&>(*stmt);

                if (body_can_break(if_stmt.then_body) || body_can_break(if_stmt.else_body)) {
                    return true;
                }

                break;
            }

            case StatementKind::Block: {
                const auto& block = static_cast<const BlockStatement&>(*stmt);

                if (body_can_break(block.statements)) {
                    return true;
                }

                break;
            }

            case StatementKind::Match: {
                const auto& match_stmt = static_cast<const MatchStatement&>(*stmt);

                for (const auto& arm : match_stmt.arms) {
                    if (body_can_break(arm.body)) {
                        return true;
                    }
                }

                break;
            }

            case StatementKind::Try: {
                const auto& try_stmt = static_cast<const TryStatement&>(*stmt);

                if (body_can_break(try_stmt.try_body) || body_can_break(try_stmt.catch_body) ||
                    body_can_break(try_stmt.finally_body)) {
                    return true;
                }

                break;
            }

            default:
                break;
        }

        // Stop at the first statement that definitely returns: everything after
        // it in this list is unreachable, so a `break` there cannot exit the
        // loop.  Without this, dead code such as `while true { return x  break }`
        // would be wrongly treated as able to fall through (a false-positive
        // missing-return error).  The stop is evaluated AFTER the break scan
        // above so a statement that itself definitely returns yet contains a
        // reachable break within it (e.g. a block `{ if c { break }  return v }`)
        // still has that break detected before the loop stops.
        if (definitely_returns(std::span<const StatementPtr>(&stmt, 1))) {
            break;
        }
    }

    return false;
}

} // namespace luma
