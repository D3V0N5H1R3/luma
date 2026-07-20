#include "analysis/types/statement_type_checker.hpp"

#include <format>
#include <string_view>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "analysis/types/type_info.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════
// Extracted helpers (CA-30)
// ═══════════════════════════════════════════════════════════

void StatementTypeChecker::check_unique_consumption_in_loop(
    const TypeScope::OwnershipSnapshot& before, const SourceLocation& loc) {
    for (const auto& [name, was_consumed] : before) {
        if (was_consumed) {
            continue;
        }

        const auto* sym = tc_.lookup_variable(name);

        if ((sym != nullptr) && sym->is_unique && sym->is_consumed) {
            tc_.error(std::format("cannot consume unique variable '{}' inside a loop "
                                  "— it would be consumed on every iteration",
                                  name),
                      loc, "move the unique variable outside the loop, or clone it before use");
        }
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
        tc_.warn("unused result: the result<T> value is silently discarded — "
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
}

void StatementTypeChecker::visit_continue(const ContinueStatement& stmt) {
    if (tc_.context().loop_depth == 0) {
        tc_.error("'continue' is only allowed inside a loop", stmt.location,
                  "move this 'continue' inside a 'for' or 'while' loop");
    }
}

} // namespace luma
