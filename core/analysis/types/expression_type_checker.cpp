#include "analysis/types/expression_type_checker.hpp"

#include <format>
#include <string_view>
#include <utility>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/diagnostics/diagnostic_builders.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "common/resource_limits.hpp"
#include "common/scope_guard.hpp"

namespace luma {

ExpressionTypeChecker::ExpressionTypeChecker(TypeCheckingServices& tc) : tc_{tc} {}

void ExpressionTypeChecker::emit_err(std::string_view message, const SourceLocation& loc,
                                     std::string_view hint, DiagnosticCode code) {
    tc_.error(message, loc, hint, code);
}

// Main dispatch — delegates to CRTP ExpressionDispatcher::dispatch_expr().
//
// Guards against native stack overflow: the parser builds flat operator,
// postfix, and pipe chains iteratively, so an arbitrarily long `a + b + c +
// ...` chain produces a deep left-leaning AST without ever tripping the
// parser's max_parse_depth guard.  This recursive walk would then exhaust the
// native stack.  Emit a diagnostic and stop descending once the limit is hit.
TypeInfo ExpressionTypeChecker::infer_expression_type(const Expression& expr) {
    if (++expression_depth_ > ResourceLimits::max_expression_depth) {
        --expression_depth_;
        tc_.error("maximum expression nesting depth exceeded", expr.location,
                  "simplify the expression or split it into smaller parts");
        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    const ScopeGuard guard{[this] {
        --expression_depth_;
    }};

    return dispatch_expr(expr);
}

// ─── Handlers for expression kinds with inline logic ───

TypeInfo ExpressionTypeChecker::visit_field_access(const FieldAccessExpression& expr) {
    const auto result_type = infer_field_access_inner(expr);

    if (expr.is_optional) {
        // Auto-flatten: if the field type is already optional<T>,
        // return optional<T> instead of optional<optional<T>>.
        if (result_type.kind == TypeInfo::Kind::Optional) {
            return result_type;
        }

        return TypeInfo::make_optional(result_type);
    }

    return result_type;
}

TypeInfo
ExpressionTypeChecker::visit_string_interpolation(const StringInterpolationExpression& node) {
    for (const auto& e : node.expressions) {
        const auto t = infer_expression_type(*e);

        if (t.kind == TypeInfo::Kind::Func) {
            tc_.warn("interpolating a function value will produce "
                     "'<function ...>' — did you mean to call it?",
                     e->location, "add parentheses to call the function: ${func()}");
        } else if (t.kind == TypeInfo::Kind::Namespace) {
            tc_.warn("interpolating a namespace value is unlikely to be "
                     "useful",
                     e->location, "access a specific member of the namespace instead");
        }
    }

    return TypeInfo::make(TypeInfo::Kind::String);
}

TypeInfo ExpressionTypeChecker::visit_spawn(const SpawnExpression& node) {
    if (node.call->kind != ExpressionKind::Call) {
        tc_.error("spawn requires a function call expression", node.location,
                  "use the syntax: spawn function_name(args)");
    }

    if (tc_.context().task_scope_depth == 0) {
        tc_.warn("spawn outside task_scope — task runs unstructured (fire-and-forget)",
                 node.location,
                 "wrap spawn calls in a task_scope { } block for structured concurrency");
    }

    const auto inner = infer_expression_type(*node.call);

    return TypeInfo::make_task(inner);
}

TypeInfo ExpressionTypeChecker::visit_task_scope(const TaskScopeExpression& node) {
    ++tc_.context().task_scope_depth;

    // Balance the task-scope-depth counter on every exit path.
    const ScopeGuard task_scope_depth_guard{[this] {
        --tc_.context().task_scope_depth;
    }};

    TypeInfo element_type = TypeInfo::make(TypeInfo::Kind::Unknown);

    {
        auto scope = tc_.make_scope_guard();

        for (const auto& stmt : node.body) {
            tc_.check_statement(*stmt);
        }
    }

    // Infer the element type from spawn calls in the body. Every spawned task
    // contributes its result to the array the task_scope evaluates to, so — as
    // with an array literal — all spawns must share a compatible result type.
    // Without this check a value of the wrong type could be bound into the
    // result array with no diagnostic (e.g. a string result in an
    // array<integer>), which the runtime would then surface as a type error.
    bool have_element_type = false;

    for (const auto& stmt : node.body) {
        if (stmt->kind != StatementKind::Expression) {
            continue;
        }

        const auto& expr_stmt = static_cast<const ExpressionStatement&>(*stmt);

        if (expr_stmt.expression->kind != ExpressionKind::Spawn) {
            continue;
        }

        const auto& spawn = static_cast<const SpawnExpression&>(*expr_stmt.expression);
        const auto inner = infer_expression_type(*spawn.call);

        if (!have_element_type) {
            element_type = inner;
            have_element_type = true;
        } else if (!tc_.is_assignable(element_type, inner)) {
            tc_.error(std::format("all spawned tasks in a task_scope must have the same result "
                                  "type: first is '{}', this one is '{}'",
                                  element_type.to_string(), inner.to_string()),
                      spawn.location,
                      "task_scope collects every spawn result into one array — convert the results "
                      "to a common type or await tasks individually",
                      DiagnosticCode::IncompatibleTypes);
        }
    }

    if (element_type.kind == TypeInfo::Kind::Unknown) {
        element_type = TypeInfo::make(TypeInfo::Kind::StdlibAny);
    }

    return TypeInfo::make_array(element_type);
}

TypeInfo ExpressionTypeChecker::visit_await(const AwaitExpression& node) {
    const auto task_type = type_check_helpers::infer_and_require_kind(
        tc_, *node.operand, TypeInfo::Kind::Task, node.location, "await requires a task value",
        "wrap the expression in a task using Task.run() or spawn");

    if (task_type.kind == TypeInfo::Kind::Task && !task_type.inner_types.empty()) {
        return task_type.element_type();
    }

    return TypeInfo::make(TypeInfo::Kind::StdlibAny);
}

TypeInfo ExpressionTypeChecker::visit_expression_unhandled(const Expression& /*unused*/) {
    return TypeInfo::make(TypeInfo::Kind::Unknown);
}

// ─── Assignment target inference (uses raw inner helpers) ───

TypeInfo ExpressionTypeChecker::infer_assignment_target(const Expression& expr) {
    if (expr.kind == ExpressionKind::Variable) {
        return visit_variable(static_cast<const VariableExpression&>(expr));
    }

    if (expr.kind == ExpressionKind::FieldAccess) {
        return infer_field_access_inner(static_cast<const FieldAccessExpression&>(expr));
    }

    if (expr.kind == ExpressionKind::IndexAccess) {
        return visit_index_access(static_cast<const IndexAccessExpression&>(expr));
    }

    return TypeInfo::make(TypeInfo::Kind::StdlibAny);
}

TypeInfo
ExpressionTypeChecker::infer_block_result(const std::vector<std::unique_ptr<Statement>>& body) {
    if (body.empty()) {
        return TypeInfo::make(TypeInfo::Kind::Void);
    }

    // Check all statements. The last one may be an expression statement
    // whose value is the block's result (used by match/if expressions).
    for (std::size_t i{0}; i + 1 < body.size(); ++i) {
        tc_.check_statement(*body[i]);
    }

    const auto& last = *body.back();

    if (last.kind == StatementKind::Expression) {
        return infer_expression_type(*static_cast<const ExpressionStatement&>(last).expression);
    }

    if (last.kind == StatementKind::Match) {
        // A match statement in value position (e.g. the tail of a match arm or
        // if-expression branch) yields the match's result type.
        const auto& match_stmt = static_cast<const MatchStatement&>(last);

        return infer_match_result(*match_stmt.subject, match_stmt.arms, match_stmt.location);
    }

    if (last.kind == StatementKind::If) {
        // An if statement in value position yields its unified branch type.
        return infer_if_statement_result(static_cast<const IfStatement&>(last));
    }

    tc_.check_statement(last);

    return TypeInfo::make(TypeInfo::Kind::Void);
}

std::string ExpressionTypeChecker::type_mismatch_hint(const TypeInfo& expected,
                                                      const TypeInfo& actual) {
    return diag_builders::type_mismatch_hint(expected, actual);
}

// Flow-sensitive type narrowing helpers

void ExpressionTypeChecker::push_refinement(const std::string& var, TypeInfo narrowed) {
    refinements_.push(var, std::move(narrowed));
}

void ExpressionTypeChecker::pop_refinements(std::size_t mark) {
    refinements_.pop_to(mark);
}

std::size_t ExpressionTypeChecker::refinement_mark() const {
    return refinements_.mark();
}

const TypeInfo* ExpressionTypeChecker::find_refinement(const std::string& var) const {
    return refinements_.find(var);
}

bool ExpressionTypeChecker::try_extract_is_refinement(const Expression& condition,
                                                      std::string& var_name,
                                                      TypeInfo& narrowed_type) {
    if (condition.kind != ExpressionKind::Is) {
        return false;
    }

    const auto& is_expr = static_cast<const IsExpression&>(condition);

    // Only narrow for simple variable operands.
    if (!is_expr.operand || is_expr.operand->kind != ExpressionKind::Variable) {
        return false;
    }

    const auto& var_expr = static_cast<const VariableExpression&>(*is_expr.operand);

    // Don't narrow mutable variables — they could be reassigned in the scope.
    const auto* sym = tc_.lookup_variable(var_expr.name);

    if ((sym == nullptr) || sym->is_mutable) {
        return false;
    }

    // Resolve the target type from the is<T> annotation.
    const auto resolved = tc_.resolve_type(is_expr.target_type);

    if (resolved.kind == TypeInfo::Kind::Unknown) {
        return false;
    }

    var_name = var_expr.name;
    narrowed_type = resolved;

    return true;
}

} // namespace luma
