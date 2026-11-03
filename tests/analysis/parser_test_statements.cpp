// Parser unit tests: statements, match, destructuring, and the newline rule.

#include <cstddef>
#include <string>

#include "ast_test_util.hpp"
#include "parser_test_helpers.hpp"
#include "test_parse_helper.hpp"

using luma::ExpressionKind;
using luma::FunctionDeclaration;
using luma::MatchArm;
using luma::MatchStatement;
using luma::RecordDestructuringStatement;
using luma::StatementKind;
using luma::TupleDestructuringStatement;
using luma::VariableDeclStatement;
using luma::test::first_function;
using luma::test::first_initializer;
using luma::test::parse;
using luma::test::parse_errors;

// ─── Statements ───

static void test_if_statement() {
    const auto program = parse("if true {\n}");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::If);
}

static void test_for_statement() {
    const auto program = parse("for item in [1, 2, 3] {\n}");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::For);
}

static void test_expression_statement() {
    const auto program = parse("1 + 2");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::Expression);
}

static void test_while_statement() {
    const auto program = parse("while true {\n}");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::While);
}

// ─── Match statements ───

static void test_match_statement() {
    const auto program = parse("integer x = 1\n"
                               "match x {\n"
                               "    case 1 { }\n"
                               "    else { }\n"
                               "}\n");

    ASSERT_EQ(program.statements.size(), 2U);
    ASSERT_EQ(program.statements[1]->kind, StatementKind::Match);
}

static void test_match_equals_literal_rejected() {
    // The redundant `case == <literal>` form is a syntax error: literals must be
    // matched by value with the bare form (`case 1`). The `==` operator remains
    // valid against a non-literal expression.
    const auto errors = parse_errors("integer x = 1\n"
                                     "match x {\n"
                                     "    case == 1 { }\n"
                                     "    else { }\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("== <literal>"), std::string::npos);
}

static void test_match_equals_string_literal_rejected() {
    const auto errors = parse_errors("string s = \"hi\"\n"
                                     "match s {\n"
                                     "    case == \"hi\" { }\n"
                                     "    else { }\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_match_equals_nonliteral_allowed() {
    // `case == <non-literal>` has no bare form and must still parse.
    const auto program = parse("integer x = 1\n"
                               "integer target = 2\n"
                               "match x {\n"
                               "    case == target { }\n"
                               "    else { }\n"
                               "}\n");

    ASSERT_EQ(program.statements.back()->kind, StatementKind::Match);
}

static void test_record_match_pattern() {
    const auto program = parse("match p {\n case Point { x, y } { print(x) }\n }");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::Match);

    const auto& match = static_cast<const MatchStatement&>(*program.statements[0]);

    ASSERT_EQ(match.arms.size(), 1U);
    ASSERT_EQ(match.arms[0].kind(), MatchArm::Kind::RecordCase);
    ASSERT_EQ(match.arms[0].record_type(), "Point");
    ASSERT_EQ(match.arms[0].record_field_bindings().size(), 2U);
    ASSERT_EQ(match.arms[0].record_field_bindings()[0], "x");
}

// ─── Multi-pattern match ───

static void test_match_integer_literal() {
    const auto program = parse("integer x = 1\n"
                               "match x {\n"
                               "    case 1 { }\n"
                               "    case 2 { }\n"
                               "    else { }\n"
                               "}\n");

    ASSERT_EQ(program.statements.size(), 2U);
    ASSERT_EQ(program.statements[1]->kind, StatementKind::Match);

    const auto& match_stmt = static_cast<const MatchStatement&>(*program.statements[1]);

    ASSERT_EQ(match_stmt.arms.size(), 3U);
    ASSERT_EQ(match_stmt.arms[0].kind(), MatchArm::Kind::IntegerCase);
    ASSERT_EQ(match_stmt.arms[0].integer_value(), 1);
    ASSERT_EQ(match_stmt.arms[1].kind(), MatchArm::Kind::IntegerCase);
    ASSERT_EQ(match_stmt.arms[1].integer_value(), 2);
    ASSERT_EQ(match_stmt.arms[2].kind(), MatchArm::Kind::Else);
}

static void test_match_range_pattern() {
    const auto program = parse("integer x = 1\n"
                               "match x {\n"
                               "    case 0..=9 { }\n"
                               "    case 10..20 { }\n"
                               "    case 30..=39 | 40..=49 { }\n"
                               "    else { }\n"
                               "}\n");

    ASSERT_EQ(program.statements.size(), 2U);
    ASSERT_EQ(program.statements[1]->kind, StatementKind::Match);

    const auto& match_stmt = static_cast<const MatchStatement&>(*program.statements[1]);

    ASSERT_EQ(match_stmt.arms.size(), 4U);

    // case 0..=9 — inclusive
    ASSERT_EQ(match_stmt.arms[0].kind(), MatchArm::Kind::IntegerRangeCase);
    ASSERT_EQ(match_stmt.arms[0].range_lo(), 0);
    ASSERT_EQ(match_stmt.arms[0].range_hi(), 9);
    ASSERT_TRUE(match_stmt.arms[0].range_inclusive());

    // case 10..20 — half-open
    ASSERT_EQ(match_stmt.arms[1].kind(), MatchArm::Kind::IntegerRangeCase);
    ASSERT_EQ(match_stmt.arms[1].range_lo(), 10);
    ASSERT_EQ(match_stmt.arms[1].range_hi(), 20);
    ASSERT_FALSE(match_stmt.arms[1].range_inclusive());

    // case 30..=39 | 40..=49 — range in both primary and alternative
    ASSERT_EQ(match_stmt.arms[2].kind(), MatchArm::Kind::IntegerRangeCase);
    ASSERT_EQ(match_stmt.arms[2].range_lo(), 30);
    ASSERT_EQ(match_stmt.arms[2].range_hi(), 39);
    ASSERT_EQ(match_stmt.arms[2].alternatives.size(), 1U);
    ASSERT_EQ(match_stmt.arms[2].alternatives[0].kind(), MatchArm::Kind::IntegerRangeCase);
    ASSERT_EQ(match_stmt.arms[2].alternatives[0].range_lo(), 40);
    ASSERT_EQ(match_stmt.arms[2].alternatives[0].range_hi(), 49);
    ASSERT_TRUE(match_stmt.arms[2].alternatives[0].range_inclusive());

    ASSERT_EQ(match_stmt.arms[3].kind(), MatchArm::Kind::Else);
}

static void test_match_multi_pattern() {
    const auto program = parse("choice Color { Red, Green, Blue }\n"
                               "@main\n"
                               "function void main() {\n"
                               "    Color c = Color.Red\n"
                               "    match c {\n"
                               "        case Color.Red | Color.Blue { }\n"
                               "        case Color.Green            { }\n"
                               "    }\n"
                               "}\n");

    // Program should parse without error and contain the match statement.
    ASSERT_EQ(program.declarations.size(), 2U);

    const auto& func = static_cast<const FunctionDeclaration&>(*program.declarations[1]);

    // match is the second statement (after the variable declaration).
    ASSERT_EQ(func.body.size(), 2U);
    ASSERT_EQ(func.body[1]->kind, StatementKind::Match);
}

// ─── Match guards and alternatives ───

static void test_match_guard_parsed() {
    const auto program = parse("integer x = 1\n"
                               "match x {\n"
                               "    case >= 5 when x % 2 == 1 { }\n"
                               "    else { }\n"
                               "}\n");

    ASSERT_EQ(program.statements.size(), 2U);

    const auto& match_stmt = static_cast<const MatchStatement&>(*program.statements[1]);

    ASSERT_EQ(match_stmt.arms.size(), 2U);
    ASSERT_TRUE(match_stmt.arms[0].has_guard());
    ASSERT_FALSE(match_stmt.arms[1].has_guard());
}

static void test_match_guard_on_some_binding() {
    const auto program = parse("optional<integer> o = some(1)\n"
                               "match o {\n"
                               "    case some(v) when v > 0 { }\n"
                               "    case some(v) { }\n"
                               "    case none { }\n"
                               "}\n");

    const auto& match_stmt = static_cast<const MatchStatement&>(*program.statements[1]);

    ASSERT_EQ(match_stmt.arms.size(), 3U);
    ASSERT_TRUE(match_stmt.arms[0].has_guard());
    ASSERT_EQ(match_stmt.arms[0].kind(), MatchArm::Kind::SomeCase);
    ASSERT_FALSE(match_stmt.arms[1].has_guard());
}

static void test_match_guard_on_success_arm() {
    // Guards on success/failure arms parse (parity with `case` arms).
    const auto program = parse("result<integer> r = success(1)\n"
                               "match r {\n"
                               "    success(v) when v > 0 { }\n"
                               "    success(v) { }\n"
                               "    failure(e) { }\n"
                               "}\n");

    const auto& match_stmt = static_cast<const MatchStatement&>(*program.statements[1]);

    ASSERT_EQ(match_stmt.arms.size(), 3U);
    ASSERT_TRUE(match_stmt.arms[0].has_guard());
    ASSERT_EQ(match_stmt.arms[0].kind(), MatchArm::Kind::SuccessResult);
}

static void test_match_alternatives_parsed() {
    const auto program = parse("integer x = 1\n"
                               "match x {\n"
                               "    case 1 | 2 | 3 { }\n"
                               "    else { }\n"
                               "}\n");

    const auto& match_stmt = static_cast<const MatchStatement&>(*program.statements[1]);

    ASSERT_EQ(match_stmt.arms[0].integer_value(), 1);
    ASSERT_EQ(match_stmt.arms[0].alternatives.size(), 2U);
}

static void test_match_guard_with_alternatives() {
    const auto program = parse("integer x = 1\n"
                               "match x {\n"
                               "    case 1 | 2 when x > 0 { }\n"
                               "    else { }\n"
                               "}\n");

    const auto& match_stmt = static_cast<const MatchStatement&>(*program.statements[1]);

    ASSERT_EQ(match_stmt.arms[0].alternatives.size(), 1U);
    ASSERT_TRUE(match_stmt.arms[0].has_guard());
}

static void test_match_alternative_with_binding_error() {
    const auto errors = parse_errors("optional<integer> o = some(1)\n"
                                     "match o {\n"
                                     "    case some(x) | none { }\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("alternatives cannot be used"), std::string::npos);
}

static void test_match_invalid_arm_error() {
    const auto errors = parse_errors("integer x = 1\n"
                                     "match x {\n"
                                     "    case { }\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("invalid match arm"), std::string::npos);
}

// ─── Destructuring ───

static void test_tuple_destructuring() {
    const auto program = parse("(integer a, string b) = (1, \"hello\")");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::TupleDestructuring);
}

static void test_mutable_tuple_destructuring() {
    const auto program = parse("mutable (integer a, integer b) = (1, 2)");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::TupleDestructuring);

    const auto& destr = static_cast<const TupleDestructuringStatement&>(*program.statements[0]);

    ASSERT_TRUE(destr.is_mutable);
    ASSERT_EQ(destr.bindings.size(), 2U);
}

static void test_record_destructuring() {
    const auto program = parse("Point { x, y } = p");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::RecordDestructuring);

    const auto& destr = static_cast<const RecordDestructuringStatement&>(*program.statements[0]);

    ASSERT_FALSE(destr.is_mutable);
    ASSERT_EQ(destr.type_name, "Point");
    ASSERT_EQ(destr.fields.size(), 2U);
    ASSERT_EQ(destr.fields[0], "x");
    ASSERT_EQ(destr.fields[1], "y");
}

static void test_mutable_record_destructuring() {
    const auto program = parse("mutable Point { x, y, z } = p");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::RecordDestructuring);

    const auto& destr = static_cast<const RecordDestructuringStatement&>(*program.statements[0]);

    ASSERT_TRUE(destr.is_mutable);
    ASSERT_EQ(destr.fields.size(), 3U);
}

static void test_qualified_record_destructuring() {
    const auto program = parse("Geometry.Point { x, y } = p");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::RecordDestructuring);

    const auto& destr = static_cast<const RecordDestructuringStatement&>(*program.statements[0]);

    ASSERT_EQ(destr.type_name, "Geometry.Point");
}

static void test_record_creation_is_not_destructuring() {
    // A bare record-creation expression statement must not be mistaken for a
    // destructuring binding (its fields carry '=' and there is no trailing '=').
    const auto program = parse("Point { x = 1, y = 2 }");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::Expression);
}

// ─── Newline rule ───

static void test_newline_paren_starts_new_statement() {
    // Regression: `cfg.total` followed by a line starting with '(' was parsed as
    // the call `cfg.total(string k, ...)`.
    const auto source = std::string{"function void f() {\n"
                                    "    integer t = cfg.total\n"
                                    "    (string k, integer v) = pair\n"
                                    "}\n"};

    ASSERT_TRUE(parse_errors(source).empty());

    const auto program = parse(source);
    const auto& body = first_function(program).body;

    ASSERT_EQ(body.size(), 2U);
    ASSERT_EQ(body[1]->kind, StatementKind::TupleDestructuring);
}

static void test_newline_paren_after_call_or_index_starts_new_statement() {
    // A '(' that starts a line begins a new statement after any callable
    // expression kind: a plain variable (formerly swallowed as the call
    // `b(integer c, ...)`), a call result, an index and a parenthesised
    // lambda.  Field access is covered by test_newline_paren_starts_new_statement.
    const auto program = parse("integer a = b\n(integer c, integer d) = pair\n"
                               "integer e = f(1)\n(integer g, integer h) = pair\n"
                               "integer i = items[0]\n(integer j, integer k) = pair\n"
                               "function(integer) -> integer l = ((integer x) -> x + 1)\n"
                               "(integer m, integer n) = pair\n");

    ASSERT_TRUE(parse_errors("integer a = b\n(integer c, integer d) = pair\n").empty());
    ASSERT_EQ(program.statements.size(), 8U);

    for (std::size_t index = 0; index < program.statements.size(); index += 2) {
        ASSERT_EQ(program.statements[index]->kind, StatementKind::VariableDeclaration);
        ASSERT_EQ(program.statements[index + 1]->kind, StatementKind::TupleDestructuring);
    }

    const auto initializer_kind = [&](std::size_t index) {
        return static_cast<const VariableDeclStatement&>(*program.statements[index])
            .initializer->kind;
    };

    ASSERT_EQ(initializer_kind(0), ExpressionKind::Variable);
    ASSERT_EQ(initializer_kind(2), ExpressionKind::Call);
    ASSERT_EQ(initializer_kind(4), ExpressionKind::IndexAccess);
    ASSERT_EQ(initializer_kind(6), ExpressionKind::Lambda);
}

static void test_newline_bracket_starts_new_statement() {
    // A '[' on a new line is an array literal, not an index into `b`.
    const auto program = parse("integer a = b\n[1, 2] |> print()\n");

    ASSERT_EQ(program.statements.size(), 2U);
    ASSERT_EQ(first_initializer(program).kind, ExpressionKind::Variable);
}

static void test_same_line_call_and_multiline_binary_still_continue() {
    const auto program = parse("integer a = f(1)(2)[0]\n"
                               "integer b = 1\n"
                               "    + 2\n"
                               "integer c = 3\n"
                               "    |> g()\n");

    ASSERT_EQ(program.statements.size(), 3U);
    ASSERT_EQ(first_initializer(program).kind, ExpressionKind::IndexAccess);

    const auto& b = static_cast<const VariableDeclStatement&>(*program.statements[1]);
    ASSERT_EQ(b.initializer->kind, ExpressionKind::Binary);

    const auto& c = static_cast<const VariableDeclStatement&>(*program.statements[2]);
    ASSERT_EQ(c.initializer->kind, ExpressionKind::Pipe);
}

int main() {
    // Statements.
    RUN(test_if_statement);
    RUN(test_for_statement);
    RUN(test_expression_statement);
    RUN(test_while_statement);

    // Match statements.
    RUN(test_match_statement);
    RUN(test_match_equals_literal_rejected);
    RUN(test_match_equals_string_literal_rejected);
    RUN(test_match_equals_nonliteral_allowed);
    RUN(test_record_match_pattern);

    // Multi-pattern match.
    RUN(test_match_integer_literal);
    RUN(test_match_range_pattern);
    RUN(test_match_multi_pattern);

    // Match guards and alternatives.
    RUN(test_match_guard_parsed);
    RUN(test_match_guard_on_some_binding);
    RUN(test_match_guard_on_success_arm);
    RUN(test_match_alternatives_parsed);
    RUN(test_match_guard_with_alternatives);
    RUN(test_match_alternative_with_binding_error);
    RUN(test_match_invalid_arm_error);

    // Destructuring.
    RUN(test_tuple_destructuring);
    RUN(test_mutable_tuple_destructuring);
    RUN(test_record_destructuring);
    RUN(test_mutable_record_destructuring);
    RUN(test_qualified_record_destructuring);
    RUN(test_record_creation_is_not_destructuring);

    // Newline rule.
    RUN(test_newline_paren_starts_new_statement);
    RUN(test_newline_paren_after_call_or_index_starts_new_statement);
    RUN(test_newline_bracket_starts_new_statement);
    RUN(test_same_line_call_and_multiline_binary_still_continue);

    return SUMMARY();
}
