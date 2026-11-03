#include "analysis/types/statement_type_checker.hpp"

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/types/match_arm_binding.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "analysis/types/type_info.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════
// Extracted helpers (CA-30)
// ═══════════════════════════════════════════════════════════

void StatementTypeChecker::check_unique_consumption_in_loop(
    const TypeScope::OwnershipSnapshot& before,
    const std::vector<TypeScope::OwnershipSnapshot>& next_iteration, const SourceLocation& loc) {
    for (const auto& [name, was_consumed] : before) {
        if (was_consumed) {
            continue;
        }

        const auto consumed_in = [&name](const TypeScope::OwnershipSnapshot& snapshot) {
            const auto found = std::ranges::find(snapshot, name, &TypeScope::OwnershipEntry::first);
            return found != snapshot.end() && found->second;
        };

        if (std::ranges::any_of(next_iteration, consumed_in)) {
            tc_.error(std::format("cannot consume unique variable '{}' inside a loop "
                                  "— it would be consumed on every iteration",
                                  name),
                      loc, "move the unique variable outside the loop, or clone it before use");
        }
    }
}

void StatementTypeChecker::begin_loop_ownership_flow() {
    tc_.context().loop_flows.push_back(
        {.outer_scope = tc_.context().current_scope, .break_states = {}, .continue_states = {}});
}

void StatementTypeChecker::end_loop_ownership_flow(const TypeScope::OwnershipSnapshot& before,
                                                   bool body_falls_through,
                                                   const SourceLocation& loc) {
    auto& flows = tc_.context().loop_flows;
    auto flow = std::move(flows.back());
    flows.pop_back();

    auto& scope = *tc_.context().current_scope;

    std::vector<TypeScope::OwnershipSnapshot> next_iteration = std::move(flow.continue_states);

    if (body_falls_through) {
        next_iteration.push_back(scope.snapshot_ownership());
    }

    check_unique_consumption_in_loop(before, next_iteration, loc);

    scope.restore_ownership(before);

    if (!flow.break_states.empty()) {
        // Leaving the loop: either it ended normally (state as before, given
        // the restore above) or a `break` left with its own state.
        flow.break_states.push_back(before);
        match_arm_binding::merge_branch_ownership(tc_, before, flow.break_states);
    }
}

StatementTypeChecker::StatementTypeChecker(TypeCheckingServices& tc) : tc_{tc} {}

void StatementTypeChecker::check_statement_list(const std::vector<StatementPtr>& stmts) {
    for (std::size_t i{0}; i < stmts.size(); ++i) {
        const Statement& stmt = *stmts[i];
        check_statement(stmt);

        // Warn about unreachable code after terminal statements.
        if (i + 1 < stmts.size() && is_terminator_statement(stmt)) {
            const std::string_view label = stmt.kind == StatementKind::Return  ? "return"
                                           : stmt.kind == StatementKind::Break ? "break"
                                                                               : "continue";
            tc_.warn(std::format("unreachable code after {} statement", label),
                     stmts[i + 1]->location);
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Statement checking
// ═══════════════════════════════════════════════════════════

void StatementTypeChecker::check_statement(const Statement& stmt) {
    dispatch_stmt(stmt);
}

// ─── Handlers for statement kinds with inline logic ───

void StatementTypeChecker::visit_expression_statement(const ExpressionStatement& stmt) {
    const auto result_type = tc_.infer_expression_type(*stmt.expression);

    if (result_type.kind == TypeInfo::Kind::Result) {
        tc_.error("unused result: the result<T> value is silently discarded — "
                  "handle it with 'match', 'Result.unwrap', 'Result.unwrap_or', "
                  "or suppress with '_ = ...'",
                  stmt.expression->location, "", DiagnosticCode::DiscardedResult);
    } else if (result_type.kind != TypeInfo::Kind::Void &&
               result_type.kind != TypeInfo::Kind::None &&
               result_type.kind != TypeInfo::Kind::Unknown &&
               result_type.kind != TypeInfo::Kind::StdlibAny &&
               result_type.kind != TypeInfo::Kind::Namespace &&
               stmt.expression->kind == ExpressionKind::Call) {
        tc_.warn(std::format("discarded value: the '{}' return value is unused — "
                             "assign it to a variable or suppress with '_ = ...'",
                             result_type.to_string()),
                 stmt.expression->location, "", DiagnosticCode::DiscardedResult);
    }
}

void StatementTypeChecker::visit_break(const BreakStatement& stmt) {
    if (tc_.context().loop_depth == 0) {
        tc_.error("'break' is only allowed inside a loop", stmt.location,
                  "move this 'break' inside a 'for' or 'while' loop");
    }

    if (auto& flows = tc_.context().loop_flows; !flows.empty()) {
        flows.back().break_states.push_back(flows.back().outer_scope->snapshot_ownership());
    }
}

void StatementTypeChecker::visit_continue(const ContinueStatement& stmt) {
    if (tc_.context().loop_depth == 0) {
        tc_.error("'continue' is only allowed inside a loop", stmt.location,
                  "move this 'continue' inside a 'for' or 'while' loop");
    }

    if (auto& flows = tc_.context().loop_flows; !flows.empty()) {
        flows.back().continue_states.push_back(flows.back().outer_scope->snapshot_ownership());
    }
}

} // namespace luma
