// Parser unit tests: lambdas, calls, tuples, member access, task scopes, and
// operator precedence.

#include <string>

#include "ast_test_util.hpp"
#include "test_parse_helper.hpp"

using luma::BinaryExpression;
using luma::CallExpression;
using luma::ErrorPipeExpression;
using luma::Expression;
using luma::ExpressionKind;
using luma::ExpressionStatement;
using luma::FieldAccessExpression;
using luma::FunctionDeclaration;
using luma::LambdaExpression;
using luma::LiteralExpression;
using luma::PipeExpression;
using luma::Program;
using luma::StatementKind;
using luma::TaskScopeExpression;
using luma::TokenType;
using luma::try_cast;
using luma::TupleLiteralExpression;
using luma::UnaryExpression;
using luma::VariableDeclStatement;
using luma::test::as_binary;
using luma::test::as_unary;
using luma::test::first_initializer;
using luma::test::parse;
using luma::test::parse_errors;

// ─── Lambdas ───

static void test_lambda_expression() {
    const auto program = parse("(integer x) -> x * 2");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::Expression);

    const auto& expr_stmt = static_cast<const ExpressionStatement&>(*program.statements[0]);

    ASSERT_EQ(expr_stmt.expression->kind, ExpressionKind::Lambda);
}

// Returns the lambda from a single-statement program of the form `<lambda>`.
static const LambdaExpression& parse_single_lambda(const Program& program) {
    const auto& expr_stmt = static_cast<const ExpressionStatement&>(*program.statements[0]);

    return static_cast<const LambdaExpression&>(*expr_stmt.expression);
}

static void test_lambda_no_params() {
    const auto program = parse("() -> 42");

    ASSERT_EQ(program.statements.size(), 1U);

    const auto& lambda = parse_single_lambda(program);

    ASSERT_EQ(lambda.kind, ExpressionKind::Lambda);
    ASSERT_TRUE(lambda.parameters.empty());
    ASSERT_TRUE(lambda.is_expression_body());
}

static void test_lambda_multiple_params() {
    const auto program = parse("(integer a, string b) -> a");

    ASSERT_EQ(program.statements.size(), 1U);

    const auto& lambda = parse_single_lambda(program);

    ASSERT_EQ(lambda.parameters.size(), 2U);
    ASSERT_EQ(lambda.parameters[0].name, "a");
    ASSERT_EQ(lambda.parameters[0].type.name(), "integer");
    ASSERT_EQ(lambda.parameters[1].name, "b");
    ASSERT_EQ(lambda.parameters[1].type.name(), "string");
    ASSERT_TRUE(lambda.is_expression_body());
}

static void test_lambda_block_body() {
    const auto program = parse("(integer x) -> {\n  integer y = x * x\n  return y + 1\n}");

    ASSERT_EQ(program.statements.size(), 1U);

    const auto& lambda = parse_single_lambda(program);

    ASSERT_EQ(lambda.parameters.size(), 1U);
    ASSERT_FALSE(lambda.is_expression_body());
    ASSERT_EQ(lambda.statements().size(), 2U);
}

static void test_lambda_nested() {
    const auto program = parse("(integer n) -> (integer x) -> x + n");

    ASSERT_EQ(program.statements.size(), 1U);

    const auto& outer = parse_single_lambda(program);

    ASSERT_EQ(outer.parameters.size(), 1U);
    ASSERT_TRUE(outer.is_expression_body());
    ASSERT_NE(outer.expression_body(), nullptr);
    ASSERT_EQ(outer.expression_body()->kind, ExpressionKind::Lambda);

    const auto& inner = static_cast<const LambdaExpression&>(*outer.expression_body());

    ASSERT_EQ(inner.parameters.size(), 1U);
    ASSERT_EQ(inner.parameters[0].name, "x");
}

// ─── Malformed lambdas ───

static void test_lambda_missing_body_error() {
    const auto errors = parse_errors("(integer x) ->");

    ASSERT_FALSE(errors.empty());
}

static void test_lambda_double_arrow_error() {
    const auto errors = parse_errors("(integer x) -> -> x");

    ASSERT_FALSE(errors.empty());
}

static void test_lambda_trailing_comma_param_error() {
    const auto errors = parse_errors("(integer x,) -> x");

    ASSERT_FALSE(errors.empty());
}

static void test_lambda_untyped_param_error() {
    // Lambda parameters must be typed; `(x) -> x` has no type annotation.
    const auto errors = parse_errors("(x) -> x");

    ASSERT_FALSE(errors.empty());
}

// ─── Calls ───

static void test_turbofish_call() {
    const auto program = parse("function<T> T identity(T value) {\n"
                               "    return value\n"
                               "}\n"
                               "integer x = identity::<integer>(42)\n");

    // Should have function declaration + variable statement
    ASSERT_EQ(program.declarations.size(), 1);
    ASSERT_EQ(program.statements.size(), 1);

    // The variable should have a call expression with type_arguments
    const auto& var = static_cast<const VariableDeclStatement&>(*program.statements[0]);

    ASSERT_NE(var.initializer, nullptr);
    ASSERT_EQ(var.initializer->kind, ExpressionKind::Call);

    const auto& call = static_cast<const CallExpression&>(*var.initializer);

    ASSERT_EQ(call.type_arguments.size(), 1);
    ASSERT_EQ(call.type_arguments[0].name(), "integer");
}

static void test_named_argument_call_parsing() {
    // An all-named call routes every argument into named_arguments, in source
    // order, and leaves the positional argument list empty.
    const auto program = parse("function string make(string name, integer age) {\n"
                               "    return name\n"
                               "}\n"
                               "string s = make(name: \"Alice\", age: 30)\n");

    ASSERT_EQ(program.statements.size(), 1U);

    const auto& var = static_cast<const VariableDeclStatement&>(*program.statements[0]);

    ASSERT_NE(var.initializer, nullptr);
    ASSERT_EQ(var.initializer->kind, ExpressionKind::Call);

    const auto& call = static_cast<const CallExpression&>(*var.initializer);

    ASSERT_TRUE(call.arguments.empty());
    ASSERT_EQ(call.named_arguments.size(), 2U);
    ASSERT_EQ(call.named_arguments[0].name, "name");
    ASSERT_EQ(call.named_arguments[1].name, "age");
    ASSERT_NE(call.named_arguments[0].value, nullptr);
    ASSERT_EQ(call.named_arguments[0].value->kind, ExpressionKind::Literal);
}

static void test_mixed_positional_and_named_argument_parsing() {
    // Positional arguments precede named arguments; the parser keeps each in its
    // own list while preserving the source order within each.
    const auto program = parse("function string make(string name, integer age, boolean active) {\n"
                               "    return name\n"
                               "}\n"
                               "string s = make(\"Alice\", active: true, age: 30)\n");

    ASSERT_EQ(program.statements.size(), 1U);

    const auto& var = static_cast<const VariableDeclStatement&>(*program.statements[0]);

    ASSERT_NE(var.initializer, nullptr);
    ASSERT_EQ(var.initializer->kind, ExpressionKind::Call);

    const auto& call = static_cast<const CallExpression&>(*var.initializer);

    ASSERT_EQ(call.arguments.size(), 1U);
    ASSERT_EQ(call.named_arguments.size(), 2U);
    ASSERT_EQ(call.named_arguments[0].name, "active");
    ASSERT_EQ(call.named_arguments[1].name, "age");
}

static void test_positional_argument_after_named_error() {
    // A positional argument may not follow a named argument in a call.
    const auto errors = parse_errors("function integer sub(integer a, integer b) {\n"
                                     "    return a - b\n"
                                     "}\n"
                                     "integer x = sub(a: 1, 2)\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("positional argument after named argument"),
              std::string::npos);
}

static void test_comparison_with_uppercase_name_is_not_generic() {
    // The generic-argument lookahead stops at tokens that cannot be in a type.
    const auto program = parse("boolean r = MAX < x + 1\ninteger y = 2\n");

    ASSERT_EQ(program.statements.size(), 2U);
    ASSERT_EQ(as_binary(first_initializer(program)).op, TokenType::Less);
}

// ─── Tuples and member access ───

static void test_tuple_literal_expression() {
    const auto program = parse("(1, 2, 3)");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::Expression);

    const auto& expr_stmt = static_cast<const ExpressionStatement&>(*program.statements[0]);

    ASSERT_EQ(expr_stmt.expression->kind, ExpressionKind::TupleLiteral);

    const auto& tuple = static_cast<const TupleLiteralExpression&>(*expr_stmt.expression);

    ASSERT_EQ(tuple.elements.size(), 3U);
}

static void test_nested_tuple_member_access() {
    // Regression: `t.0.1` produced a single field named "0.1".
    const auto program = parse("integer x = t.0.1");
    const auto& outer = static_cast<const FieldAccessExpression&>(first_initializer(program));

    ASSERT_EQ(outer.field_name, std::string{"1"});
    ASSERT_EQ(outer.object->kind, ExpressionKind::FieldAccess);
    ASSERT_EQ(static_cast<const FieldAccessExpression&>(*outer.object).field_name,
              std::string{"0"});
}

static void test_field_name_must_be_name_token() {
    // Regression: the check used the lexeme, so a string or annotation was accepted.
    ASSERT_FALSE(parse_errors("string s = obj.\"name\"").empty());
    ASSERT_FALSE(parse_errors("string s = obj.@main").empty());
    ASSERT_TRUE(parse_errors("integer n = obj.with").empty());
}

// ─── Task scopes ───

static void test_task_scope_expression() {
    const auto program = parse("task_scope {\n}");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::Expression);

    const auto& expr_stmt = static_cast<const ExpressionStatement&>(*program.statements[0]);

    ASSERT_EQ(expr_stmt.expression->kind, ExpressionKind::TaskScope);
}

static void test_task_scope_with_body() {
    const auto program = parse("function integer compute() { return 1 }\n"
                               "@main\n"
                               "function void main() {\n"
                               "    array<integer> r = task_scope {\n"
                               "        spawn compute()\n"
                               "        spawn compute()\n"
                               "    }\n"
                               "}\n");

    ASSERT_EQ(program.declarations.size(), 2U);

    const auto& func = static_cast<const FunctionDeclaration&>(*program.declarations[1]);

    ASSERT_EQ(func.body.size(), 1U);
    ASSERT_EQ(func.body[0]->kind, StatementKind::VariableDeclaration);
}

static void test_task_scope_nested_parse() {
    const auto program = parse("task_scope {\n"
                               "    task_scope {\n"
                               "    }\n"
                               "}\n");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::Expression);

    const auto& outer_expr = static_cast<const ExpressionStatement&>(*program.statements[0]);

    ASSERT_EQ(outer_expr.expression->kind, ExpressionKind::TaskScope);

    const auto& outer_scope = static_cast<const TaskScopeExpression&>(*outer_expr.expression);

    ASSERT_EQ(outer_scope.body.size(), 1U);
}

// ─── Operator precedence and associativity ───

// These tests verify that the precedence-climbing parser builds the correct
// tree shape, independent of later evaluation. The lower-precedence operator
// must appear at the root with the higher-precedence operation nested beneath
// it. Left-associative operators nest their left operand.
//
// first_initializer(), as_binary(), and as_unary() come from ast_test_util.hpp.

// try_cast narrows a base Expression to a concrete node when the kind matches,
// aliasing the same object, and yields nullptr for any other target type — the
// checked replacement for the hand-written kind-check + static_cast guard.
static void test_try_cast_narrows_by_kind() {
    const auto program = parse("integer x = 1 + 2");
    const auto& expr = first_initializer(program);

    const auto* bin = try_cast<BinaryExpression>(expr);
    ASSERT_NE(bin, nullptr);
    ASSERT_EQ(static_cast<const Expression*>(bin), &expr);
    ASSERT_NE(bin->left, nullptr);
    ASSERT_NE(bin->right, nullptr);

    ASSERT_EQ(try_cast<UnaryExpression>(expr), nullptr);
    ASSERT_EQ(try_cast<LiteralExpression>(expr), nullptr);
}

// `2 + 3 * 4`  ⇒  `2 + (3 * 4)` — multiplicative binds tighter than additive.
static void test_precedence_multiplicative_over_additive() {
    const auto program = parse("integer x = 2 + 3 * 4");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::Plus);
    ASSERT_EQ(root.right->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*root.right).op, TokenType::Star);
    ASSERT_EQ(root.left->kind, ExpressionKind::Literal);
}

// `10 - 3 - 2`  ⇒  `(10 - 3) - 2` — additive is left-associative.
static void test_precedence_additive_left_associative() {
    const auto program = parse("integer x = 10 - 3 - 2");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::Minus);
    ASSERT_EQ(root.left->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*root.left).op, TokenType::Minus);
    ASSERT_EQ(root.right->kind, ExpressionKind::Literal);
}

// `12 % 5 * 2`  ⇒  `(12 % 5) * 2` — `% / // *` share one left-associative level.
static void test_precedence_multiplicative_same_level_left_associative() {
    const auto program = parse("integer x = 12 % 5 * 2");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::Star);
    ASSERT_EQ(root.left->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*root.left).op, TokenType::Percent);
}

// `1 < 2 == 3`  ⇒  `(1 < 2) == 3` — equality is below comparison.
static void test_precedence_equality_below_comparison() {
    const auto program = parse("boolean x = 1 < 2 == 3");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::EqualsEquals);
    ASSERT_EQ(root.left->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*root.left).op, TokenType::Less);
}

// `1 == 2 && 3`  ⇒  `(1 == 2) && 3` — logical AND is below equality.
static void test_precedence_logical_and_below_equality() {
    const auto program = parse("boolean x = 1 == 2 && 3");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::AmpersandAmpersand);
    ASSERT_EQ(root.left->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*root.left).op, TokenType::EqualsEquals);
}

// `1 && 2 || 3`  ⇒  `(1 && 2) || 3` — logical OR is below logical AND.
static void test_precedence_logical_or_below_and() {
    const auto program = parse("boolean x = 1 && 2 || 3");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::PipePipe);
    ASSERT_EQ(root.left->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*root.left).op, TokenType::AmpersandAmpersand);
}

// `1 || 2 ?? 3`  ⇒  `(1 || 2) ?? 3` — null-coalescing is below logical OR.
static void test_precedence_null_coalescing_below_or() {
    const auto program = parse("integer x = 1 || 2 ?? 3");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::QuestionQuestion);
    ASSERT_EQ(root.left->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*root.left).op, TokenType::PipePipe);
}

// `-2 * 3`  ⇒  `(-2) * 3` — unary negation binds tighter than multiplication.
static void test_precedence_unary_binds_tighter_than_multiplicative() {
    const auto program = parse("integer x = -2 * 3");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::Star);
    ASSERT_EQ(root.left->kind, ExpressionKind::Unary);
    ASSERT_EQ(as_unary(*root.left).op, TokenType::Minus);
}

// `1 + 2 |> f()`  ⇒  `(1 + 2) |> f()` — the pipe is the lowest-precedence binary.
static void test_precedence_pipe_below_arithmetic() {
    const auto program = parse("integer x = 1 + 2 |> double_value()");
    const auto& root = first_initializer(program);

    ASSERT_EQ(root.kind, ExpressionKind::Pipe);

    const auto& pipe = static_cast<const PipeExpression&>(root);
    ASSERT_EQ(pipe.left->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*pipe.left).op, TokenType::Plus);
}

// `5 |> double_value()`  ⇒  Pipe(left = 5, right = call) — basic pipe structure.
static void test_pipe_builds_pipe_expression() {
    const auto program = parse("integer x = 5 |> double_value()");
    const auto& root = first_initializer(program);

    ASSERT_EQ(root.kind, ExpressionKind::Pipe);

    const auto& pipe = static_cast<const PipeExpression&>(root);
    ASSERT_EQ(pipe.left->kind, ExpressionKind::Literal);
    ASSERT_EQ(pipe.right->kind, ExpressionKind::Call);
}

// `value !> step()`  ⇒  ErrorPipe(left = value, right = call) — basic error-pipe structure.
static void test_error_pipe_builds_error_pipe_expression() {
    const auto program = parse("result<integer> r = value !> step()");
    const auto& root = first_initializer(program);

    ASSERT_EQ(root.kind, ExpressionKind::ErrorPipe);

    const auto& pipe = static_cast<const ErrorPipeExpression&>(root);
    ASSERT_EQ(pipe.left->kind, ExpressionKind::Variable);
    ASSERT_EQ(pipe.right->kind, ExpressionKind::Call);
}

// `5 |> f() |> g()`  ⇒  Pipe(Pipe(5, f()), g()) — the pipe is left-associative.
static void test_pipe_chain_left_associative() {
    const auto program = parse("integer x = 5 |> f() |> g()");
    const auto& root = first_initializer(program);

    ASSERT_EQ(root.kind, ExpressionKind::Pipe);

    const auto& outer = static_cast<const PipeExpression&>(root);
    ASSERT_EQ(outer.left->kind, ExpressionKind::Pipe);

    const auto& inner = static_cast<const PipeExpression&>(*outer.left);
    ASSERT_EQ(inner.left->kind, ExpressionKind::Literal);
}

// `5 |> f() !> g()`  ⇒  ErrorPipe(Pipe(5, f()), g()) — `|>` and `!>` share one
// left-associative precedence level, so the trailing `!>` is the outermost node.
static void test_mixed_pipe_error_pipe_left_associative() {
    const auto program = parse("result<integer> r = 5 |> f() !> g()");
    const auto& root = first_initializer(program);

    ASSERT_EQ(root.kind, ExpressionKind::ErrorPipe);

    const auto& outer = static_cast<const ErrorPipeExpression&>(root);
    ASSERT_EQ(outer.left->kind, ExpressionKind::Pipe);
}

// `(2 + 3) * 4`  ⇒  `(2 + 3) * 4` — parentheses override precedence.
static void test_precedence_parentheses_override() {
    const auto program = parse("integer x = (2 + 3) * 4");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::Star);
    ASSERT_EQ(root.left->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*root.left).op, TokenType::Plus);
}

int main() {
    // Lambdas.
    RUN(test_lambda_expression);
    RUN(test_lambda_no_params);
    RUN(test_lambda_multiple_params);
    RUN(test_lambda_block_body);
    RUN(test_lambda_nested);

    // Malformed lambdas.
    RUN(test_lambda_missing_body_error);
    RUN(test_lambda_double_arrow_error);
    RUN(test_lambda_trailing_comma_param_error);
    RUN(test_lambda_untyped_param_error);

    // Calls.
    RUN(test_turbofish_call);
    RUN(test_named_argument_call_parsing);
    RUN(test_mixed_positional_and_named_argument_parsing);
    RUN(test_positional_argument_after_named_error);
    RUN(test_comparison_with_uppercase_name_is_not_generic);

    // Tuples and member access.
    RUN(test_tuple_literal_expression);
    RUN(test_nested_tuple_member_access);
    RUN(test_field_name_must_be_name_token);

    // Task scopes.
    RUN(test_task_scope_expression);
    RUN(test_task_scope_with_body);
    RUN(test_task_scope_nested_parse);

    // Operator precedence and associativity.
    RUN(test_try_cast_narrows_by_kind);
    RUN(test_precedence_multiplicative_over_additive);
    RUN(test_precedence_additive_left_associative);
    RUN(test_precedence_multiplicative_same_level_left_associative);
    RUN(test_precedence_equality_below_comparison);
    RUN(test_precedence_logical_and_below_equality);
    RUN(test_precedence_logical_or_below_and);
    RUN(test_precedence_null_coalescing_below_or);
    RUN(test_precedence_unary_binds_tighter_than_multiplicative);
    RUN(test_precedence_pipe_below_arithmetic);
    RUN(test_pipe_builds_pipe_expression);
    RUN(test_error_pipe_builds_error_pipe_expression);
    RUN(test_pipe_chain_left_associative);
    RUN(test_mixed_pipe_error_pipe_left_associative);
    RUN(test_precedence_parentheses_override);

    return SUMMARY();
}
