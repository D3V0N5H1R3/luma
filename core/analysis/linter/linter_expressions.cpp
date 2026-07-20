#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/linter/linter.hpp"

namespace luma {

// ─────────── Expression handlers ───────────
//
// — Arithmetic & logic checks —

void Linter::visit_binary(const BinaryExpression& bin) {
    // Note: Division-by-zero (W0014) and floating-point equality (W0009)
    // checks are now handled by plugins via the LintPluginRegistry.

    // Warn about comparison to boolean literal (x == true → x, x == false → !x).
    if (bin.op == TokenType::EqualsEquals || bin.op == TokenType::BangEquals) {
        if (is_boolean_literal(*bin.left) || is_boolean_literal(*bin.right)) {
            warn("Comparison to boolean literal", bin.location,
                 "use the boolean expression directly: 'x' instead of 'x == true', "
                 "or '!x' instead of 'x == false'",
                 DiagnosticCode::AlwaysTrueFalse);
        }
    }

    lint_expression(*bin.left);
    lint_expression(*bin.right);
}

void Linter::visit_unary(const UnaryExpression& unary) {
    // Note: Double negation (W0015) check is now handled by a plugin
    // via the LintPluginRegistry.

    lint_expression(*unary.operand);
}

// — Call & pipe expressions —

void Linter::visit_call(const CallExpression& call) {
    lint_expression(*call.callee);

    for (const auto& arg : call.arguments) {
        lint_expression(*arg);
    }

    for (const auto& named : call.named_arguments) {
        lint_expression(*named.value);
    }
}

void Linter::visit_pipe(const PipeExpression& pipe) {
    lint_expression(*pipe.left);
    lint_expression(*pipe.right);
}

void Linter::visit_error_pipe(const ErrorPipeExpression& error_pipe) {
    lint_expression(*error_pipe.left);
    lint_expression(*error_pipe.right);
}

// — Control flow expressions —

void Linter::visit_if(const IfExpression& if_expr) {
    lint_expression(*if_expr.condition);

    if (if_expr.then_expr() != nullptr) {
        lint_expression(*if_expr.then_expr());
    }

    if (if_expr.else_expr() != nullptr) {
        lint_expression(*if_expr.else_expr());
    }

    lint_scoped_block(if_expr.then_body(), /*last_is_value=*/true);
    lint_scoped_block(if_expr.else_body(), /*last_is_value=*/true);
}

void Linter::visit_match(const MatchExpression& match) {
    lint_expression(*match.subject);
    lint_match_arms(match.arms, match.location);
}

void Linter::visit_lambda(const LambdaExpression& lambda) {
    auto guard = make_scope_guard();

    for (const auto& param : lambda.parameters) {
        tracker_.track_variable(param.name, lambda.location, true);
    }

    if (lambda.is_expression_body() && (lambda.expression_body() != nullptr)) {
        lint_expression(*lambda.expression_body());
    } else {
        lint_block(lambda.statements(), /*last_is_value=*/true);
    }
}

// — Data construction expressions —

void Linter::visit_record_creation(const RecordCreationExpression& record) {
    for (const auto& field : record.fields) {
        lint_expression(*field.value);
    }
}

void Linter::visit_record_with(const RecordWithExpression& record_with) {
    lint_expression(*record_with.base);

    for (const auto& field : record_with.overrides) {
        lint_expression(*field.value);
    }
}

void Linter::visit_array_literal(const ArrayLiteralExpression& arr) {
    for (const auto& elem : arr.elements) {
        lint_expression(*elem);
    }
}

void Linter::visit_dictionary_literal(const DictionaryLiteralExpression& dict) {
    for (const auto& entry : dict.entries) {
        lint_expression(*entry.key);
        lint_expression(*entry.value);
    }
}

void Linter::visit_tuple_literal(const TupleLiteralExpression& tuple) {
    for (const auto& elem : tuple.elements) {
        lint_expression(*elem);
    }
}

void Linter::visit_string_interpolation(const StringInterpolationExpression& interp) {
    for (const auto& expr : interp.expressions) {
        lint_expression(*expr);
    }
}

// — Type & wrapping expressions —

void Linter::visit_downcast(const DowncastExpression& downcast) {
    lint_expression(*downcast.operand);
}

void Linter::visit_is(const IsExpression& is) {
    lint_expression(*is.operand);
}

void Linter::visit_success(const SuccessExpression& success) {
    lint_expression(*success.value);
}

void Linter::visit_failure(const FailureExpression& failure) {
    lint_expression(*failure.message);
}

void Linter::visit_some(const SomeExpression& some) {
    lint_expression(*some.value);
}

// — Concurrency expressions —

void Linter::visit_spawn(const SpawnExpression& spawn) {
    lint_expression(*spawn.call);
}

void Linter::visit_await(const AwaitExpression& await_expr) {
    lint_expression(*await_expr.operand);
}

void Linter::visit_task_scope(const TaskScopeExpression& task_scope) {
    lint_scoped_block(task_scope.body);
}

// — Leaf expressions (traversal only) —

void Linter::visit_variable(const VariableExpression& var) {
    tracker_.mark_used(var.name);
}

void Linter::visit_range(const RangeExpression& range) {
    lint_expression(*range.start);
    lint_expression(*range.end);
}

void Linter::visit_field_access(const FieldAccessExpression& field) {
    lint_expression(*field.object);
}

void Linter::visit_index_access(const IndexAccessExpression& idx) {
    lint_expression(*idx.object);
    lint_expression(*idx.index);
}

} // namespace luma
