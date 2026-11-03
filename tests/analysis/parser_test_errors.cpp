// Parser unit tests: syntax error detection, error recovery, diagnostics, and
// recursion depth limits.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "analysis/ast/expression.hpp"
#include "ast_test_util.hpp"
#include "common/resource_limits.hpp"
#include "parser_test_helpers.hpp"
#include "test_parse_helper.hpp"

using luma::BinaryExpression;
using luma::CallExpression;
using luma::ChoiceDeclaration;
using luma::Diagnostic;
using luma::ExpressionPtr;
using luma::FieldAccessExpression;
using luma::FunctionDeclaration;
using luma::IndexAccessExpression;
using luma::LiteralExpression;
using luma::MatchStatement;
using luma::NamedArgument;
using luma::ResourceLimits;
using luma::SourceLocation;
using luma::StatementKind;
using luma::TokenType;
using luma::UnaryExpression;
using luma::VariableExpression;
using luma::test::first_function;
using luma::test::LimitGuard;
using luma::test::parse;
using luma::test::parse_errors;

// ─── Syntax errors ───

static void test_syntax_error_reported() {
    const auto errors = parse_errors("function {");

    ASSERT_FALSE(errors.empty());
}

static void test_multiple_syntax_errors_collected() {
    // The parser should recover and collect multiple independent errors.
    const auto errors = parse_errors("integer return = 5\n"
                                     "integer if = 10\n"
                                     "integer x = 42\n");

    ASSERT_GE(errors.size(), 2U);
}

// ─── Syntax error detection ───

static void test_missing_closing_brace() {
    const auto errors = parse_errors("function void f() {\n"
                                     "    integer x = 1\n");

    ASSERT_FALSE(errors.empty());
}

static void test_missing_opening_brace() {
    const auto errors = parse_errors("function void f()\n"
                                     "    integer x = 1\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_missing_closing_parenthesis() {
    const auto errors = parse_errors("integer x = (1 + 2\n");

    ASSERT_FALSE(errors.empty());
}

static void test_missing_function_name() {
    // Function declaration without a name should produce a parse error.
    const auto errors = parse_errors("function void () {\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_invalid_function_parameter() {
    const auto errors = parse_errors("function void f(42) {\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_duplicate_else_clause() {
    const auto errors = parse_errors("if true {\n"
                                     "} else {\n"
                                     "} else {\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_for_missing_iterator_variable() {
    const auto errors = parse_errors("for in [1, 2] {\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_record_missing_field_type() {
    const auto errors = parse_errors("record Broken {\n"
                                     "    x\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_for_missing_in_keyword() {
    const auto errors = parse_errors("for x [1, 2, 3] {\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_try_without_catch_or_finally() {
    const auto errors = parse_errors("function void foo() {\n"
                                     "    try {\n"
                                     "        integer x = 1\n"
                                     "    }\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_catch_without_variable() {
    // The catch clause must bind an error variable: `catch(err)`. A bare
    // `catch { }` with no parenthesised identifier is a syntax error.
    const auto errors = parse_errors("function void foo() {\n"
                                     "    try {\n"
                                     "        integer x = 1\n"
                                     "    } catch {\n"
                                     "        integer y = 2\n"
                                     "    }\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_catch_empty_parentheses() {
    // The catch clause requires an identifier inside the parentheses.
    const auto errors = parse_errors("function void foo() {\n"
                                     "    try {\n"
                                     "        integer x = 1\n"
                                     "    } catch() {\n"
                                     "        integer y = 2\n"
                                     "    }\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_invalid_operator_sequence() {
    // There is no unary '+', so the second '+' cannot start an operand.
    const auto errors = parse_errors("integer x = 1 + + 2\n");

    ASSERT_EQ(errors.size(), 1U);
    ASSERT_EQ(errors[0].message, std::string{"unexpected token '+'"});
    ASSERT_EQ(errors[0].primary_location().column, 17);
}

// ─── Error recovery ───

static void test_synchronize_recovers_at_function() {
    // After the first error, the parser should synchronize at 'function' and
    // successfully parse the second declaration.
    const auto errors = parse_errors("integer = 5\n"
                                     "function void greet() {\n"
                                     "    integer x = 1\n"
                                     "}\n");

    ASSERT_GE(errors.size(), 1U);
}

static void test_synchronize_recovers_at_task_scope() {
    // Parser should recognize task_scope as a synchronization point.
    const auto errors = parse_errors("integer = 5\n"
                                     "task_scope {\n"
                                     "    integer x = 1\n"
                                     "}\n");

    ASSERT_GE(errors.size(), 1U);
}

static void test_foreign_keyword_var_hint() {
    // Using 'var' (from other languages) should produce a helpful hint.
    const auto errors = parse_errors("function void f() {\n"
                                     "    var x = 5\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());

    bool has_var_hint = false;

    for (const auto& err : errors) {
        if (err.hint.has_value() &&
            err.hint->find("Luma does not use 'var'") != std::string::npos) {
            has_var_hint = true;
        }
    }

    ASSERT_TRUE(has_var_hint);
}

static void test_foreign_keyword_let_hint() {
    const auto errors = parse_errors("function void f() {\n"
                                     "    let x = 5\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());

    bool has_let_hint = false;

    for (const auto& err : errors) {
        if (err.hint.has_value() &&
            err.hint->find("Luma does not use 'let'") != std::string::npos) {
            has_let_hint = true;
        }
    }

    ASSERT_TRUE(has_let_hint);
}

static void test_foreign_keyword_fn_hint() {
    const auto errors = parse_errors("fn greet() {\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());

    bool has_fn_hint = false;

    for (const auto& err : errors) {
        if (err.hint.has_value() &&
            err.hint->find("did you mean 'function'") != std::string::npos) {
            has_fn_hint = true;
        }
    }

    ASSERT_TRUE(has_fn_hint);
}

static void test_foreign_keyword_class_hint() {
    const auto errors = parse_errors("class Point {\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());

    bool has_class_hint = false;

    for (const auto& err : errors) {
        if (err.hint.has_value() && err.hint->find("did you mean 'record'") != std::string::npos) {
            has_class_hint = true;
        }
    }

    ASSERT_TRUE(has_class_hint);
}

static void test_recovery_continues_after_errors() {
    // The parser should recover from multiple errors and collect them all.
    const auto errors = parse_errors("integer = 5\n"
                                     "integer y = 10\n"
                                     "string = 20\n"
                                     "integer z = 30\n");

    ASSERT_GE(errors.size(), 2U);
}

// ─── Malformed input ───

static void test_unterminated_expression() {
    // An incomplete variable initializer should produce a parse error.
    const auto errors = parse_errors("integer x =");

    ASSERT_FALSE(errors.empty());
}

static void test_missing_closing_paren_in_call() {
    // A function call missing its closing parenthesis should produce a parse error.
    const auto errors = parse_errors("function void f(integer a, integer b) { }\n"
                                     "f(1, 2\n");

    ASSERT_FALSE(errors.empty());
}

static void test_invalid_token_in_expression() {
    // A stray token where an expression is expected should produce a parse error.
    const auto errors = parse_errors("integer x = }");

    ASSERT_FALSE(errors.empty());
}

// ─── Edge cases (CA-26) ───

static void test_recovery_after_garbage_tokens() {
    // Multiple garbage tokens followed by a valid statement — the parser
    // should recover and parse the valid statement.
    const auto errors = parse_errors("+ + + integer x = 1");

    ASSERT_FALSE(errors.empty());
}

static void test_recovery_missing_function_body() {
    // A function declaration missing its body should produce errors but
    // not crash the parser.
    const auto errors = parse_errors("function integer add(integer a, integer b)");

    ASSERT_FALSE(errors.empty());
}

static void test_recovery_nested_braces_malformed() {
    // Deeply nested mismatched braces should produce errors.
    const auto errors = parse_errors("if true { if true { if true { } }");

    ASSERT_FALSE(errors.empty());
}

static void test_recovery_multiple_consecutive_errors() {
    // Several malformed statements in a row — parser should collect
    // multiple diagnostics.
    const auto errors = parse_errors("integer = \n string = \n boolean = \n");

    ASSERT_GE(errors.size(), 2U);
}

// ─── Block-local recovery ───

static void test_recovery_stays_inside_function_body() {
    // Regression: an error in a body unwound to top level, after which the
    // function's closing '}' produced a cascading "unexpected token" error.
    const auto source = std::string{"function void f() {\n"
                                    "    integer x = = 5\n"
                                    "    integer y = 2\n"
                                    "}\n"
                                    "function void g() {\n"
                                    "}\n"};

    ASSERT_EQ(parse_errors(source).size(), 1U);

    const auto program = parse(source);

    ASSERT_EQ(program.declarations.size(), 2U);
    ASSERT_EQ(first_function(program).body.size(), 1U);
}

static void test_recovery_stays_inside_namespace() {
    // Regression: the remaining `internal` members raised "'internal' keyword is
    // only allowed inside a namespace".
    const auto errors = parse_errors("namespace N {\n"
                                     "    function void a() {\n"
                                     "        integer x = = 1\n"
                                     "    }\n"
                                     "    internal function integer = 2\n"
                                     "    internal function void b() {\n"
                                     "    }\n"
                                     "}\n");

    ASSERT_EQ(errors.size(), 2U);
    ASSERT_TRUE(std::ranges::none_of(errors, [](const Diagnostic& error) {
        return error.message.find("only allowed inside a namespace") != std::string::npos;
    }));
}

static void test_recovery_inside_match_arms() {
    const auto source = std::string{"match x {\n"
                                    "    case + { }\n"
                                    "    case 2 { }\n"
                                    "    else { }\n"
                                    "}\n"};

    ASSERT_EQ(parse_errors(source).size(), 1U);

    const auto program = parse(source);
    const auto& match = static_cast<const MatchStatement&>(*program.statements.at(0));

    ASSERT_EQ(match.arms.size(), 2U);
}

// The (start) lines of the syntax errors in `source`, comma-separated in
// emission order, e.g. "7,11".
static std::string error_lines(const std::string& source) {
    std::string lines;

    for (const auto& error : parse_errors(source)) {
        if (!lines.empty()) {
            lines += ',';
        }

        lines += std::to_string(error.primary_location().line);
    }

    return lines;
}

static void test_recovery_skips_rest_of_multiline_call() {
    // Regression: block recovery started at depth 0 although the failing
    // statement had already opened `add(`, so `4)` was re-parsed as new
    // statements ("unexpected ')'").  Only the real errors remain: the `+*`
    // and the incomplete `"x" +` (reported at the closing '}').
    const auto source = std::string{"function integer add(integer x, integer y, integer z) {\n"
                                    "    return x + y + z\n"
                                    "}\n"
                                    "@main\n"
                                    "function void main() {\n"
                                    "    integer a = add(1,\n"
                                    "        2 +* 3,\n"
                                    "        4)\n"
                                    "    print(\"after\")\n"
                                    "    integer b = \"x\" +\n"
                                    "}\n"};

    ASSERT_EQ(error_lines(source), std::string{"7,11"});

    const auto program = parse(source);
    const auto& main_body =
        static_cast<const FunctionDeclaration&>(*program.declarations.at(1)).body;

    ASSERT_EQ(program.declarations.size(), 2U);
    ASSERT_EQ(main_body.size(), 1U);
    ASSERT_EQ(main_body[0]->kind, StatementKind::Expression);
}

static void test_recovery_skips_rest_of_record_literal() {
    // Regression: the record literal's '}' was taken as the end of the
    // function body, so the remaining statements and the real closing '}'
    // cascaded into "unexpected token '}'".
    const auto source = std::string{"record Point { integer x, integer y }\n"
                                    "@main\n"
                                    "function void main() {\n"
                                    "    Point p = Point {\n"
                                    "        x = 1 +* 2,\n"
                                    "        y = 3\n"
                                    "    }\n"
                                    "    print(p)\n"
                                    "    integer q = 5\n"
                                    "    print(q)\n"
                                    "}\n"};

    ASSERT_EQ(error_lines(source), std::string{"5"});

    const auto program = parse(source);

    ASSERT_EQ(program.declarations.size(), 2U);
    ASSERT_TRUE(program.statements.empty());
    ASSERT_EQ(static_cast<const FunctionDeclaration&>(*program.declarations.at(1)).body.size(), 3U);
}

static void test_recovery_skips_rest_of_array_literal() {
    // Regression: `3,` and the closing ']' of the array literal raised errors.
    const auto source = std::string{"@main\n"
                                    "function void main() {\n"
                                    "    array<integer> xs = [\n"
                                    "        1 +* 2,\n"
                                    "        3,\n"
                                    "        4\n"
                                    "    ]\n"
                                    "    print(xs)\n"
                                    "}\n"};

    ASSERT_EQ(error_lines(source), std::string{"4"});
    ASSERT_EQ(first_function(parse(source)).body.size(), 1U);
}

static void test_recovery_skips_rest_of_nested_open_constructs() {
    // An optional index `?[`, a call inside an array and a record literal
    // inside a call are all skipped to their closers.  A closer that ends an
    // inner construct the parser had already recovered (the missing ')' of
    // `g(1` closed by ']') does not leave that construct counted as open.
    const auto source = std::string{"function void f() {\n"
                                    "    integer a = xs?[\n"
                                    "        1 +* 2\n"
                                    "    ]\n"
                                    "    integer b = h([g(1, 2],\n"
                                    "        Point {\n"
                                    "            x = +*,\n"
                                    "        })\n"
                                    "    integer c = [g(1] +* 2\n"
                                    "    print(c)\n"
                                    "}\n"};

    // Line 5 reports the missing ')' of `g(1, 2` and line 9 both the missing
    // ')' of `g(1` and the `+*`; nothing is reported for a closer.
    ASSERT_EQ(error_lines(source), std::string{"3,5,7,9,9"});

    const auto program = parse(source);
    const auto& body = first_function(program).body;

    ASSERT_EQ(body.size(), 1U);
    ASSERT_EQ(body[0]->kind, StatementKind::Expression);
}

static void test_recovery_unclosed_construct_stops_at_block_end() {
    // A construct that is never closed must not swallow the block's '}' (or
    // the following function), and recovery still terminates at end of file.
    const auto source = std::string{"function void f() {\n"
                                    "    integer a = add(1 +* 2\n"
                                    "    print(1)\n"
                                    "}\n"
                                    "function void g() {\n"
                                    "}\n"};

    ASSERT_EQ(error_lines(source), std::string{"2"});
    ASSERT_EQ(parse(source).declarations.size(), 2U);

    ASSERT_FALSE(
        parse_errors("function void f() {\n    integer a = add(1,\n        2 +* 3,\n").empty());
    ASSERT_FALSE(parse_errors("function void f() {\n    integer a = [\n        1 +* 2,\n").empty());
}

static void test_recovery_inside_choice_variants() {
    const auto source = std::string{"choice Shape {\n"
                                    "    Circle(number)\n"
                                    "    Square(number side)\n"
                                    "}\n"};

    ASSERT_EQ(parse_errors(source).size(), 1U);

    const auto program = parse(source);
    const auto& choice = static_cast<const ChoiceDeclaration&>(*program.declarations.at(0));

    ASSERT_EQ(choice.variants.size(), 1U);
    ASSERT_EQ(choice.variants[0].name, std::string{"Square"});
}

// ─── Diagnostics ───

static void test_annotation_before_non_function_message() {
    // Regression: this check sat after parse_function_decl() and could never fire.
    const auto errors = parse_errors("@main\nrecord X {\n    integer x\n}\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("expected a function declaration after annotation"),
              std::string::npos);
}

static void test_try_without_handler_hint_uses_real_syntax() {
    const auto errors = parse_errors("try {\n}\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].hint.has_value());
    ASSERT_NE(errors[0].hint->find("catch(err)"), std::string::npos);
}

static void test_syntax_error_span_starts_at_token() {
    // Token locations mark one past the token; the diagnostic must cover the
    // offending token itself so the caret points at its first character.
    const auto errors = parse_errors("integer x = )");

    ASSERT_FALSE(errors.empty());
    ASSERT_EQ(errors[0].primary_location().column, 13);
    ASSERT_EQ(errors[0].spans.at(0).end.column, 14);
}

static void test_binary_operator_at_end_of_input_is_a_syntax_error() {
    // A binary operator with no right operand at end of input must be a
    // recoverable syntax error.  The parser raises ParseFailure and recovers;
    // fuzz_parser reaches exactly this shape constantly.
    for (const auto* source : {"a>", "a<", "a+", "a*", "a==", "a>=", "1 -", "x &&"}) {
        ASSERT_FALSE(parse_errors(source).empty());
    }
}

// ─── Recursion depth limit (stack-overflow guard) ───

// Parse `source` with max_parse_depth temporarily lowered so the test
// exercises the parser's depth guard without itself recursing deeply enough to
// risk a real stack overflow.  The guard restores the limit even on a throw.
[[nodiscard]] static std::vector<Diagnostic> parse_errors_capped(const std::string& source,
                                                                 int limit) {
    const LimitGuard guard{ResourceLimits::max_parse_depth, limit};

    return parse_errors(source);
}

[[nodiscard]] static bool has_depth_error(const std::vector<Diagnostic>& errors) {
    return std::ranges::any_of(errors, [](const Diagnostic& error) {
        return error.message.find("nesting depth") != std::string::npos;
    });
}

static void test_deep_expression_nesting_rejected() {
    // Deeply nested grouping must yield a recoverable "maximum nesting depth
    // exceeded" diagnostic rather than overflowing the stack.  The input nests
    // far beyond the (lowered) limit, but the parser only recurses as deep as
    // the limit before bailing out.
    const auto errors = parse_errors_capped(std::string(64, '('), 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_deep_paren_type_lookahead_rejected() {
    // Regression: skip_type_at() — the statement-start lookahead that decides
    // whether a leading '(' opens a tuple-type variable declaration — recursed
    // once per nested '(' with no depth bound.  Because it runs *before* the
    // depth-tracked parse path, deeply nested parens overflowed the native
    // stack inside the lookahead instead of yielding a recoverable diagnostic.
    // Unlike the sibling tests, shallow input cannot expose this: the fallback
    // expression parser re-parses the parens and trips its own guard, masking
    // the missing bound.  Only genuinely deep input reaches the unguarded
    // recursion, so the limit is lowered (keeping the *post-fix* recursion
    // shallow and safe) while the input nests far enough to overflow the
    // pre-fix code.
    const auto errors = parse_errors_capped(std::string(500000, '('), 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_deep_index_nesting_rejected() {
    // Postfix index chains (x[0[0[0…) recurse through parse_expression for the
    // index, so they are bounded by the same guard.
    std::string source = "x";

    for (int i = 0; i < 64; ++i) {
        source += "[0";
    }

    const auto errors = parse_errors_capped(source, 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_deep_range_nesting_rejected() {
    // Chained range expressions (`1..1..1..…`) parse the range end via
    // parse_addition(), which re-descends into parse_postfix() and greedily
    // consumes the next `..`.  Each `..` therefore adds a level of native
    // recursion that the iterative chain counter (ensure_chain_within_limit)
    // never sees, so without an explicit guard on that path deep enough input
    // overflows the stack.  The input nests far beyond the lowered limit.
    std::string source = "1";

    for (int i = 0; i < 64; ++i) {
        source += "..1";
    }

    const auto errors = parse_errors_capped(source, 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_deep_type_annotation_nesting_rejected() {
    // Nested generic type arguments (array<array<…>>) recurse through the
    // TypeAnnotationParser, which shares the parser's depth budget.  Without a
    // guard this path would overflow the stack on deep enough input.
    std::string type;

    for (int i = 0; i < 64; ++i) {
        type += "array<";
    }

    type += "integer";

    for (int i = 0; i < 64; ++i) {
        type += ">";
    }

    const auto errors = parse_errors_capped("function " + type + " f() { return 0 }", 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_moderate_nesting_still_parses() {
    // Nesting that stays within the limit must parse cleanly — the guard must
    // not reject ordinary, reasonably-nested expressions.
    const LimitGuard guard{ResourceLimits::max_parse_depth, 16};
    const auto source = "integer x = " + std::string(8, '(') + "1" + std::string(8, ')');

    ASSERT_TRUE(parse_errors(source).empty());

    const auto program = parse(source);

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::VariableDeclaration);
}

static void test_deep_unary_nesting_rejected() {
    // Stacked prefix unary operators (!!!…, ---…) recurse through parse_unary()
    // without re-entering parse_expression().  Without a guard on that path this
    // overflows the stack on deep enough input (a defect first surfaced by the
    // fuzzers).  The input nests far beyond the lowered limit.
    const auto errors = parse_errors_capped(std::string(64, '!') + "1", 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_deep_await_nesting_rejected() {
    // `await await await … x` recurses into parse_unary() from parse_primary()
    // without re-entering parse_expression(), so it shares the same depth guard.
    std::string source;

    for (int i = 0; i < 64; ++i) {
        source += "await ";
    }

    source += "x";

    const auto errors = parse_errors_capped(source, 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_deep_spawn_nesting_rejected() {
    // `spawn spawn spawn … f()` recurses into parse_postfix() from
    // parse_primary() without re-entering parse_expression(), so it shares the
    // same depth guard.
    std::string source;

    for (int i = 0; i < 64; ++i) {
        source += "spawn ";
    }

    source += "f()";

    const auto errors = parse_errors_capped(source, 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_moderate_unary_nesting_still_parses() {
    // Stacked unary operators that stay within the limit must parse cleanly —
    // guarding the recursive path must not reject ordinary unary expressions.
    const LimitGuard guard{ResourceLimits::max_parse_depth, 16};
    const auto source = "boolean x = " + std::string(8, '!') + "true";

    ASSERT_TRUE(parse_errors(source).empty());

    const auto program = parse(source);

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::VariableDeclaration);
}

static void test_deep_else_if_statement_nesting_rejected() {
    // `if … else if … else if …` statement chains recurse through
    // parse_if_statement() without re-entering the guarded parse_statement(),
    // so they share the same depth guard.
    std::string source = "if false { }";

    for (int i = 0; i < 64; ++i) {
        source += " else if false { }";
    }

    const auto errors = parse_errors_capped(source, 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_deep_else_if_expression_nesting_rejected() {
    // `if … else if … else …` expression chains recurse through
    // parse_if_expression() without re-entering the guarded parse_expression(),
    // so they share the same depth guard.
    std::string source = "integer x = if false { 1 }";

    for (int i = 0; i < 64; ++i) {
        source += " else if false { 1 }";
    }

    source += " else { 0 }";

    const auto errors = parse_errors_capped(source, 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

// Parse `source` with max_expression_depth temporarily lowered to `limit`.
// Flat operator/pipe/postfix chains are built iteratively (so max_parse_depth
// never trips); they are instead bounded by max_expression_depth, which guards
// the *recursive* AST walk/teardown.  Mirrors parse_errors_capped.
[[nodiscard]] static std::vector<Diagnostic> parse_errors_capped_expr(const std::string& source,
                                                                      int limit) {
    const LimitGuard guard{ResourceLimits::max_expression_depth, limit};

    return parse_errors(source);
}

static void test_flat_binary_chain_rejected() {
    // `1 + 1 + 1 + …` is built iteratively, so max_parse_depth never trips; the
    // left-leaning AST spine is instead walked recursively by later passes and
    // its destructor, which would overflow the native stack on a long enough
    // chain.  max_expression_depth must bound the chain at build time.
    std::string source = "integer x = 1";

    for (int i = 0; i < 64; ++i) {
        source += " + 1";
    }

    const auto errors = parse_errors_capped_expr(source, 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_flat_pipe_chain_rejected() {
    // Pipe chains (`x |> f() |> f() |> …`) are likewise built iteratively and
    // share the max_expression_depth bound.
    std::string source = "integer x = a";

    for (int i = 0; i < 64; ++i) {
        source += " |> f()";
    }

    const auto errors = parse_errors_capped_expr(source, 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_flat_postfix_chain_rejected() {
    // Postfix field-access chains (`a.b.b.b…`) are built iteratively in
    // parse_postfix and share the max_expression_depth bound.
    std::string source = "integer x = a";

    for (int i = 0; i < 64; ++i) {
        source += ".b";
    }

    const auto errors = parse_errors_capped_expr(source, 16);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_depth_error(errors));
}

static void test_moderate_flat_chain_still_parses() {
    // A flat chain that stays within the limit must parse cleanly — the bound
    // must not reject ordinary, reasonably-long expressions.
    const LimitGuard guard{ResourceLimits::max_expression_depth, 16};
    std::string source = "integer x = 1";

    for (int i = 0; i < 8; ++i) {
        source += " + 1";
    }

    ASSERT_TRUE(parse_errors(source).empty());

    const auto program = parse(source);

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::VariableDeclaration);
}

static void test_postfix_chain_limit_spans_parentheses() {
    // Regression: the postfix chain counter restarted inside every parenthesis,
    // so `((a.b…b).b…b).b…b` built max_parse_depth × max_expression_depth
    // nested field accesses.  A parenthesised receiver's chain now counts
    // towards the chain it continues.
    const std::string chain = [] {
        std::string links;

        for (int i = 0; i < 12; ++i) {
            links += ".b";
        }

        return links;
    }();

    std::string nested = "a";

    for (int i = 0; i < 3; ++i) {
        nested = "(" + nested + chain + ")";
    }

    ASSERT_TRUE(has_depth_error(parse_errors_capped_expr("integer x = " + nested, 16)));

    // Short chains through parentheses, and an operator between them, still parse.
    const LimitGuard guard{ResourceLimits::max_expression_depth, 16};

    ASSERT_TRUE(parse_errors("integer x = (a.b.c).d(1)[0]?.e").empty());
    ASSERT_TRUE(parse_errors("integer x = (a" + chain + " + c)" + chain).empty());
}

static void test_deep_postfix_spine_teardown_is_iterative() {
    // Regression: only binary/pipe chains were destroyed iteratively.  A
    // postfix spine (field access, index, call, '?') that alternates with
    // binary operators — as nested parentheses produce — was freed recursively
    // and could overflow the native stack.  Build one far deeper than any
    // native stack could unwind recursively and destroy it.
    const SourceLocation loc{};
    ExpressionPtr spine = std::make_unique<VariableExpression>(loc, "a");

    for (int i = 0; i < 200'000; ++i) {
        switch (i % 5) {
            case 0:
                spine = std::make_unique<FieldAccessExpression>(loc, std::move(spine), "b");
                break;
            case 1:
                spine = std::make_unique<IndexAccessExpression>(
                    loc, std::move(spine),
                    std::make_unique<LiteralExpression>(loc, std::int64_t{0}));
                break;
            case 2:
                spine = std::make_unique<CallExpression>(loc, std::move(spine),
                                                         std::vector<ExpressionPtr>{},
                                                         std::vector<NamedArgument>{});
                break;
            case 3:
                spine = std::make_unique<UnaryExpression>(loc, TokenType::QuestionMark,
                                                          std::move(spine));
                break;
            default:
                spine = std::make_unique<BinaryExpression>(
                    loc, std::move(spine), TokenType::Plus,
                    std::make_unique<LiteralExpression>(loc, std::int64_t{1}));
                break;
        }
    }

    spine.reset();

    ASSERT_TRUE(spine == nullptr);
}

// Regression: the generic-argument lookahead (skip_type_at at every statement
// start, is_generic_record_creation at every `Name<`) scanned to end-of-file
// over type-like tokens, so many lines of `X<X` took quadratic time.  It now
// stops at a line break outside parentheses and after a bounded number of
// tokens.  The input is large enough that a quadratic scan (~10^9 token
// visits) takes many seconds, while the linear one parses in milliseconds.
static void test_generic_lookahead_is_bounded() {
    std::string many_lines;

    for (int i = 0; i < 40'000; ++i) {
        many_lines += "X<X\n";
    }

    const auto start = std::chrono::steady_clock::now();

    (void)parse_errors(many_lines);

    std::string one_line = "boolean b = X";

    for (int i = 0; i < 20'000; ++i) {
        one_line += "<X>X";
    }

    (void)parse_errors(one_line);

    const auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 3000);

    // Generic types and generic record creation still parse.
    ASSERT_TRUE(parse_errors("dictionary<array<integer>> d = {}\n"
                             "function(integer) -> array<string> f = (integer n) -> [\"x\"]\n"
                             "Box<integer> b = Box<integer> { value = 1 }\n"
                             "boolean lt = n < limit\n")
                    .empty());

    // So do type argument lists split across lines: the bound stops only at a
    // name that starts a line right after a token that ends a type.
    ASSERT_TRUE(parse_errors("array<\n"
                             "    integer\n"
                             "> a = [1, 2]\n"
                             "dictionary<array<\n"
                             "    string>> names = {\"x\": [\"y\"]}\n"
                             "Pair<integer, string> p = Pair<\n"
                             "    integer,\n"
                             "    string\n"
                             "> { first = 1, second = \"x\" }\n")
                    .empty());
}

int main() {
    // Syntax errors.
    RUN(test_syntax_error_reported);
    RUN(test_multiple_syntax_errors_collected);

    // Syntax error detection.
    RUN(test_missing_closing_brace);
    RUN(test_missing_opening_brace);
    RUN(test_missing_closing_parenthesis);
    RUN(test_missing_function_name);
    RUN(test_invalid_function_parameter);
    RUN(test_duplicate_else_clause);
    RUN(test_for_missing_iterator_variable);
    RUN(test_record_missing_field_type);
    RUN(test_for_missing_in_keyword);
    RUN(test_try_without_catch_or_finally);
    RUN(test_catch_without_variable);
    RUN(test_catch_empty_parentheses);
    RUN(test_invalid_operator_sequence);

    // Error recovery.
    RUN(test_synchronize_recovers_at_function);
    RUN(test_synchronize_recovers_at_task_scope);
    RUN(test_foreign_keyword_var_hint);
    RUN(test_foreign_keyword_let_hint);
    RUN(test_foreign_keyword_fn_hint);
    RUN(test_foreign_keyword_class_hint);
    RUN(test_recovery_continues_after_errors);

    // Malformed input.
    RUN(test_unterminated_expression);
    RUN(test_missing_closing_paren_in_call);
    RUN(test_invalid_token_in_expression);

    // Edge cases (CA-26).
    RUN(test_recovery_after_garbage_tokens);
    RUN(test_recovery_missing_function_body);
    RUN(test_recovery_nested_braces_malformed);
    RUN(test_recovery_multiple_consecutive_errors);

    // Block-local recovery.
    RUN(test_recovery_stays_inside_function_body);
    RUN(test_recovery_stays_inside_namespace);
    RUN(test_recovery_inside_match_arms);
    RUN(test_recovery_skips_rest_of_multiline_call);
    RUN(test_recovery_skips_rest_of_record_literal);
    RUN(test_recovery_skips_rest_of_array_literal);
    RUN(test_recovery_skips_rest_of_nested_open_constructs);
    RUN(test_recovery_unclosed_construct_stops_at_block_end);
    RUN(test_recovery_inside_choice_variants);

    // Diagnostics.
    RUN(test_annotation_before_non_function_message);
    RUN(test_try_without_handler_hint_uses_real_syntax);
    RUN(test_syntax_error_span_starts_at_token);
    RUN(test_binary_operator_at_end_of_input_is_a_syntax_error);

    // Recursion depth limit (stack-overflow guard).
    RUN(test_deep_expression_nesting_rejected);
    RUN(test_deep_paren_type_lookahead_rejected);
    RUN(test_deep_index_nesting_rejected);
    RUN(test_deep_range_nesting_rejected);
    RUN(test_deep_type_annotation_nesting_rejected);
    RUN(test_deep_unary_nesting_rejected);
    RUN(test_deep_await_nesting_rejected);
    RUN(test_deep_spawn_nesting_rejected);
    RUN(test_deep_else_if_statement_nesting_rejected);
    RUN(test_deep_else_if_expression_nesting_rejected);
    RUN(test_moderate_nesting_still_parses);
    RUN(test_moderate_unary_nesting_still_parses);
    RUN(test_flat_binary_chain_rejected);
    RUN(test_flat_pipe_chain_rejected);
    RUN(test_flat_postfix_chain_rejected);
    RUN(test_moderate_flat_chain_still_parses);
    RUN(test_postfix_chain_limit_spans_parentheses);
    RUN(test_deep_postfix_spine_teardown_is_iterative);
    RUN(test_generic_lookahead_is_bounded);

    return SUMMARY();
}
