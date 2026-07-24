// Parser unit tests.

#include <string>

#include "ast_test_util.hpp"
#include "common/resource_limits.hpp"
#include "test_parse_helper.hpp"

using luma::test::as_binary;
using luma::test::as_unary;
using luma::test::first_initializer;

// ─── Tests ───

static void test_empty_program() {
    const auto program = parse("");

    ASSERT_TRUE(program.declarations.empty());
    ASSERT_TRUE(program.statements.empty());
}

static void test_variable_declaration() {
    const auto program = parse("integer x = 10");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::VariableDeclaration);

    const auto& var = static_cast<const VariableDeclStatement&>(*program.statements[0]);

    ASSERT_EQ(var.name, "x");
    ASSERT_EQ(var.is_mutable, false);
}

static void test_mutable_variable() {
    const auto program = parse("mutable integer x = 5");

    ASSERT_EQ(program.statements.size(), 1U);

    const auto& var = static_cast<const VariableDeclStatement&>(*program.statements[0]);

    ASSERT_EQ(var.name, "x");
    ASSERT_EQ(var.is_mutable, true);
}

static void test_function_declaration() {
    const auto program = parse("function integer add(integer a, integer b) {\n  return a + b\n}");

    ASSERT_EQ(program.declarations.size(), 1U);
    ASSERT_EQ(program.declarations[0]->kind, DeclarationKind::Function);

    const auto& func = static_cast<const FunctionDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(func.name, "add");
    ASSERT_EQ(func.parameters.size(), 2U);
    ASSERT_EQ(func.parameters[0].name, "a");
    ASSERT_EQ(func.parameters[1].name, "b");
    ASSERT_EQ(func.return_type.name(), "integer");
}

static void test_main_function() {
    const auto program = parse("@main\nfunction void main() {\n}");

    ASSERT_EQ(program.declarations.size(), 1U);

    const auto& func = static_cast<const FunctionDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(func.name, "main");
    ASSERT_TRUE(func.is_main);
}

static void test_test_function() {
    const auto program = parse("@test\nfunction void test_something() {\n}");

    ASSERT_EQ(program.declarations.size(), 1U);

    const auto& func = static_cast<const FunctionDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(func.name, "test_something");
    ASSERT_TRUE(func.is_test);
}

static void test_unknown_annotation_rejected() {
    // Only @main and @test are valid; any other annotation is a syntax error.
    const auto errors = parse_errors("@frobnicate\nfunction void f() {\n}");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("unknown annotation") != std::string::npos);
}

static void test_annotation_on_non_function_rejected() {
    // Annotations may only precede function declarations; a record after the
    // annotation must be rejected rather than silently annotated.
    const auto errors = parse_errors("@test\nrecord Point {\n    integer x\n}");

    ASSERT_FALSE(errors.empty());
}

static void test_record_declaration() {
    const auto program = parse("record Point {\n  number x,\n  number y\n}");

    ASSERT_EQ(program.declarations.size(), 1U);
    ASSERT_EQ(program.declarations[0]->kind, DeclarationKind::Record);

    const auto& rec = static_cast<const RecordDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(rec.name, "Point");
    ASSERT_EQ(rec.fields.size(), 2U);
    ASSERT_EQ(rec.fields[0].name, "x");
    ASSERT_EQ(rec.fields[1].name, "y");
}

static void test_choice_declaration() {
    const auto program = parse("choice Color {\n  Red\n  Green\n  Blue\n};");

    ASSERT_EQ(program.declarations.size(), 1U);
    ASSERT_EQ(program.declarations[0]->kind, DeclarationKind::Choice);

    const auto& e = static_cast<const ChoiceDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(e.name, "Color");
    ASSERT_EQ(e.variants.size(), 3U);
    ASSERT_EQ(e.variants[0].name, "Red");
    ASSERT_EQ(e.variants[1].name, "Green");
    ASSERT_EQ(e.variants[2].name, "Blue");
}

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

static void test_syntax_error_throws() {
    auto errors = parse_errors("function {");

    ASSERT_FALSE(errors.empty());
}

static void test_while_statement() {
    const auto program = parse("while true {\n}");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::While);
}

static void test_interface_declaration() {
    const auto program = parse("interface Printable {\n  string name\n}");

    ASSERT_EQ(program.declarations.size(), 1U);
    ASSERT_EQ(program.declarations[0]->kind, DeclarationKind::Interface);

    const auto& iface = static_cast<const InterfaceDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(iface.name, "Printable");
    ASSERT_EQ(iface.fields.size(), 1U);
    ASSERT_EQ(iface.fields[0].name, "name");
}

static void test_namespace_declaration() {
    auto program =
        parse("namespace Geometry {\n  function number area() {\n    return 0.0\n  }\n}");

    ASSERT_EQ(program.declarations.size(), 1U);
    ASSERT_EQ(program.declarations[0]->kind, DeclarationKind::Namespace);

    const auto& ns = static_cast<const NamespaceDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(ns.name, "Geometry");
    ASSERT_FALSE(ns.declarations.empty());
}

static void test_namespace_qualified_type_annotation() {
    // "Geometry.Point p = Geometry.Point { x = 1.0, y = 2.0 }" should parse
    // with the type annotation name set to "Geometry.Point".
    const auto program = parse("namespace Geometry {\n"
                               "    record Point {\n"
                               "        number x,\n"
                               "        number y\n"
                               "    }\n"
                               "}\n"
                               "Geometry.Point p = Geometry.Point { x = 1.0, y = 2.0 }\n");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::VariableDeclaration);

    const auto& var = static_cast<const VariableDeclStatement&>(*program.statements[0]);

    ASSERT_EQ(var.type.name(), "Geometry.Point");
    ASSERT_EQ(var.name, "p");
}

static void test_namespace_qualified_record_creation() {
    // "Geometry.Point { x = 1.0, y = 2.0 }" should parse as a
    // RecordCreationExpression with type_name == "Geometry.Point".
    const auto program = parse("namespace Geometry {\n"
                               "    record Point {\n"
                               "        number x,\n"
                               "        number y\n"
                               "    }\n"
                               "}\n"
                               "Geometry.Point { x = 1.0, y = 2.0 }\n");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::Expression);

    const auto& expr_stmt = static_cast<const ExpressionStatement&>(*program.statements[0]);

    ASSERT_EQ(expr_stmt.expression->kind, ExpressionKind::RecordCreation);

    const auto& rec = static_cast<const RecordCreationExpression&>(*expr_stmt.expression);

    ASSERT_EQ(rec.type_name, "Geometry.Point");
}

static void test_namespace_qualified_match_arm() {
    // "case Traffic.Light.Red" should parse with enum_type == "Traffic.Light"
    // (the qualified type name) and enum_variant == "Red".
    const auto program = parse("namespace Traffic {\n"
                               "    choice Light {\n"
                               "        Red\n"
                               "        Green\n"
                               "    }\n"
                               "}\n"
                               "Traffic.Light x = Traffic.Light.Red\n"
                               "match x {\n"
                               "    case Traffic.Light.Red { }\n"
                               "    case Traffic.Light.Green { }\n"
                               "}\n");

    ASSERT_EQ(program.statements.size(), 2U);
    ASSERT_EQ(program.statements[1]->kind, StatementKind::Match);

    const auto& match = static_cast<const MatchStatement&>(*program.statements[1]);

    ASSERT_EQ(match.arms.size(), 2U);
    ASSERT_TRUE(match.arms[0].kind() == MatchArm::Kind::VariantCase);
    ASSERT_EQ(match.arms[0].enum_type(), "Traffic.Light");
    ASSERT_EQ(match.arms[0].enum_variant(), "Red");
}

static void test_namespace_internal_unsupported_decl_is_syntax_error() {
    // 'internal' may only precede function/record/choice/interface/type.
    // Preceding a plain variable declaration must be a syntax error.
    auto errors = parse_errors("namespace Util {\n"
                               "    internal integer x = 5\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_namespace_internal_before_closing_brace_is_syntax_error() {
    // 'internal' not followed by any declaration keyword (here the namespace's
    // closing brace) must be a syntax error.
    auto errors = parse_errors("namespace Util {\n"
                               "    internal\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_namespace_internal_members_set_flag() {
    // Every declaration kind that 'internal' may precede must carry the
    // is_internal_to_namespace flag, while a public member must not.
    const auto program = parse("namespace Util {\n"
                               "    function integer pub() {\n"
                               "        return 0\n"
                               "    }\n"
                               "    internal function integer sec() {\n"
                               "        return 1\n"
                               "    }\n"
                               "    internal record Rec {\n"
                               "        integer v\n"
                               "    }\n"
                               "    internal choice Ch {\n"
                               "        A\n"
                               "        B\n"
                               "    }\n"
                               "    internal interface If {\n"
                               "        number area\n"
                               "    }\n"
                               "    internal type Alias = integer\n"
                               "}\n");

    ASSERT_EQ(program.declarations.size(), 1U);
    ASSERT_EQ(program.declarations[0]->kind, DeclarationKind::Namespace);

    const auto& ns = static_cast<const NamespaceDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(ns.declarations.size(), 6U);
    ASSERT_FALSE(ns.declarations[0]->is_internal_to_namespace); // public function
    ASSERT_TRUE(ns.declarations[1]->is_internal_to_namespace);  // internal function
    ASSERT_TRUE(ns.declarations[2]->is_internal_to_namespace);  // internal record
    ASSERT_TRUE(ns.declarations[3]->is_internal_to_namespace);  // internal choice
    ASSERT_TRUE(ns.declarations[4]->is_internal_to_namespace);  // internal interface
    ASSERT_TRUE(ns.declarations[5]->is_internal_to_namespace);  // internal type alias
}

static void test_type_alias_declaration() {
    const auto program = parse("type Age = integer");

    ASSERT_EQ(program.declarations.size(), 1U);
    ASSERT_EQ(program.declarations[0]->kind, DeclarationKind::TypeAlias);

    const auto& alias = static_cast<const TypeAliasDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(alias.name, "Age");
    ASSERT_TRUE(alias.type_params.empty());
    ASSERT_TRUE(alias.target_type.is_plain());
    ASSERT_EQ(alias.target_type.name(), "integer");
}

static void test_generic_type_alias_declaration() {
    const auto program = parse("type Pair<T> = (T, T)");

    ASSERT_EQ(program.declarations.size(), 1U);
    ASSERT_EQ(program.declarations[0]->kind, DeclarationKind::TypeAlias);

    const auto& alias = static_cast<const TypeAliasDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(alias.name, "Pair");
    ASSERT_EQ(alias.type_params.size(), 1U);
    ASSERT_EQ(alias.type_params[0].name, "T");
    ASSERT_TRUE(alias.target_type.is_tuple());
    ASSERT_EQ(alias.target_type.tuple_elements().size(), 2U);
    ASSERT_EQ(alias.target_type.tuple_elements()[0].name(), "T");
    ASSERT_EQ(alias.target_type.tuple_elements()[1].name(), "T");
}

static void test_function_type_alias_declaration() {
    const auto program = parse("type Predicate = function(integer) -> boolean");

    ASSERT_EQ(program.declarations.size(), 1U);

    const auto& alias = static_cast<const TypeAliasDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(alias.name, "Predicate");
    ASSERT_TRUE(alias.target_type.is_func());
    ASSERT_EQ(alias.target_type.type_params().size(), 1U);
    ASSERT_EQ(alias.target_type.type_params()[0].name(), "integer");
    ASSERT_TRUE(alias.target_type.return_type_ptr() != nullptr);
    ASSERT_EQ(alias.target_type.return_type_ptr()->name(), "boolean");
}

static void test_collection_type_alias_declaration() {
    const auto program = parse("type ScoreMap = dictionary<number>");

    ASSERT_EQ(program.declarations.size(), 1U);

    const auto& alias = static_cast<const TypeAliasDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(alias.name, "ScoreMap");
    ASSERT_TRUE(alias.target_type.is_plain());
    ASSERT_EQ(alias.target_type.name(), "dictionary");
    ASSERT_EQ(alias.target_type.type_params().size(), 1U);
    ASSERT_EQ(alias.target_type.type_params()[0].name(), "number");
}

static void test_match_statement() {
    const auto program = parse("integer x = 1\n"
                               "match x {\n"
                               "    case == 1 { }\n"
                               "    else { }\n"
                               "}\n");

    ASSERT_EQ(program.statements.size(), 2U);
    ASSERT_EQ(program.statements[1]->kind, StatementKind::Match);
}

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

static void test_include_declaration() {
    const auto program = parse("include \"helper.luma\"");

    ASSERT_EQ(program.declarations.size(), 1U);
    ASSERT_EQ(program.declarations[0]->kind, DeclarationKind::Include);

    const auto& inc = static_cast<const IncludeDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(inc.path, "helper.luma");
}

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

static void test_tuple_literal_expression() {
    const auto program = parse("(1, 2, 3)");

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::Expression);

    const auto& expr_stmt = static_cast<const ExpressionStatement&>(*program.statements[0]);

    ASSERT_EQ(expr_stmt.expression->kind, ExpressionKind::TupleLiteral);

    const auto& tuple = static_cast<const TupleLiteralExpression&>(*expr_stmt.expression);

    ASSERT_EQ(tuple.elements.size(), 3U);
}

// ─── Keyword-as-identifier rejection ───

static void test_keyword_as_variable_name() {
    auto errors = parse_errors("integer return = 5");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("reserved keyword") != std::string::npos);
    ASSERT_TRUE(errors[0].message.find("'return'") != std::string::npos);
}

static void test_keyword_as_function_name() {
    auto errors = parse_errors("function void if() { }");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("reserved keyword") != std::string::npos);
    ASSERT_TRUE(errors[0].message.find("'if'") != std::string::npos);
}

static void test_keyword_as_parameter_name() {
    auto errors = parse_errors("function void foo(integer while) { }");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("reserved keyword") != std::string::npos);
    ASSERT_TRUE(errors[0].message.find("'while'") != std::string::npos);
}

static void test_keyword_as_record_name() {
    auto errors = parse_errors("record match {\n  integer x\n}");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("reserved keyword") != std::string::npos);
    ASSERT_TRUE(errors[0].message.find("'match'") != std::string::npos);
}

static void test_keyword_as_record_field_name() {
    auto errors = parse_errors("record Foo {\n  integer break\n}");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("reserved keyword") != std::string::npos);
    ASSERT_TRUE(errors[0].message.find("'break'") != std::string::npos);
}

static void test_keyword_as_choice_name() {
    auto errors = parse_errors("choice for {\n  A\n  B\n}");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("reserved keyword") != std::string::npos);
    ASSERT_TRUE(errors[0].message.find("'for'") != std::string::npos);
}

static void test_type_keyword_as_variable_name() {
    auto errors = parse_errors("integer boolean = 5");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("reserved keyword") != std::string::npos);
    ASSERT_TRUE(errors[0].message.find("'boolean'") != std::string::npos);
}

static void test_literal_keyword_as_variable_name() {
    auto errors = parse_errors("integer true = 5");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("reserved keyword") != std::string::npos);
    ASSERT_TRUE(errors[0].message.find("'true'") != std::string::npos);
}

static void test_turbofish_call() {
    const auto program = parse("function<T> T identity(T value) {\n"
                               "    return value\n"
                               "}\n"
                               "integer x = identity::<integer>(42)\n");

    // Should have function declaration + variable statement
    ASSERT_TRUE(program.declarations.size() == 1);
    ASSERT_TRUE(program.statements.size() == 1);

    // The variable should have a call expression with type_arguments
    const auto& var = static_cast<const VariableDeclStatement&>(*program.statements[0]);

    ASSERT_TRUE(var.initializer != nullptr);
    ASSERT_TRUE(var.initializer->kind == ExpressionKind::Call);

    const auto& call = static_cast<const CallExpression&>(*var.initializer);

    ASSERT_TRUE(call.type_arguments.size() == 1);
    ASSERT_TRUE(call.type_arguments[0].name() == "integer");
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

    ASSERT_TRUE(var.initializer != nullptr);
    ASSERT_TRUE(var.initializer->kind == ExpressionKind::Call);

    const auto& call = static_cast<const CallExpression&>(*var.initializer);

    ASSERT_TRUE(call.arguments.empty());
    ASSERT_EQ(call.named_arguments.size(), 2U);
    ASSERT_EQ(call.named_arguments[0].name, "name");
    ASSERT_EQ(call.named_arguments[1].name, "age");
    ASSERT_TRUE(call.named_arguments[0].value != nullptr);
    ASSERT_TRUE(call.named_arguments[0].value->kind == ExpressionKind::Literal);
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

    ASSERT_TRUE(var.initializer != nullptr);
    ASSERT_TRUE(var.initializer->kind == ExpressionKind::Call);

    const auto& call = static_cast<const CallExpression&>(*var.initializer);

    ASSERT_EQ(call.arguments.size(), 1U);
    ASSERT_EQ(call.named_arguments.size(), 2U);
    ASSERT_EQ(call.named_arguments[0].name, "active");
    ASSERT_EQ(call.named_arguments[1].name, "age");
}

static void test_positional_argument_after_named_error() {
    // A positional argument may not follow a named argument in a call.
    auto errors = parse_errors("function integer sub(integer a, integer b) {\n"
                               "    return a - b\n"
                               "}\n"
                               "integer x = sub(a: 1, 2)\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("positional argument after named argument") !=
                std::string::npos);
}

static void test_bounded_generic_declaration() {
    const auto program = parse("interface Printable {\n"
                               "    string name\n"
                               "}\n"
                               "function<T: Printable> string print_value(T value) {\n"
                               "    \"hello\"\n"
                               "}\n");

    ASSERT_TRUE(program.declarations.size() == 2);

    const auto& func = static_cast<const FunctionDeclaration&>(*program.declarations[1]);

    ASSERT_TRUE(func.type_params.size() == 1);
    ASSERT_TRUE(func.type_params[0].name == "T");
    ASSERT_TRUE(func.type_params[0].bounds.size() == 1);
    ASSERT_TRUE(func.type_params[0].bounds[0] == "Printable");
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

// ─── Match guards & alternatives ───

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
    ASSERT_TRUE(match_stmt.arms[0].kind() == MatchArm::Kind::SomeCase);
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
    ASSERT_TRUE(match_stmt.arms[0].kind() == MatchArm::Kind::SuccessResult);
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
    auto errors = parse_errors("optional<integer> o = some(1)\n"
                               "match o {\n"
                               "    case some(x) | none { }\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("alternatives cannot be used") != std::string::npos);
}

static void test_match_invalid_arm_error() {
    auto errors = parse_errors("integer x = 1\n"
                               "match x {\n"
                               "    case { }\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].message.find("invalid match arm") != std::string::npos);
}

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

static void test_multiple_syntax_errors_collected() {
    // The parser should recover and collect multiple independent errors.
    auto errors = parse_errors("integer return = 5\n"
                               "integer if = 10\n"
                               "integer x = 42\n");

    ASSERT_TRUE(errors.size() >= 2U);
}

// ─── Syntax error detection tests ───

static void test_missing_closing_brace() {
    auto errors = parse_errors("function void f() {\n"
                               "    integer x = 1\n");

    ASSERT_FALSE(errors.empty());
}

static void test_missing_opening_brace() {
    auto errors = parse_errors("function void f()\n"
                               "    integer x = 1\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_missing_closing_parenthesis() {
    auto errors = parse_errors("integer x = (1 + 2\n");

    ASSERT_FALSE(errors.empty());
}

static void test_missing_function_name() {
    // Function declaration without a name should produce a parse error.
    auto errors = parse_errors("function void () {\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_invalid_function_parameter() {
    auto errors = parse_errors("function void f(42) {\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_duplicate_else_clause() {
    auto errors = parse_errors("if true {\n"
                               "} else {\n"
                               "} else {\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_for_missing_iterator_variable() {
    auto errors = parse_errors("for in [1, 2] {\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_record_missing_field_type() {
    auto errors = parse_errors("record Broken {\n"
                               "    x\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_for_missing_in_keyword() {
    auto errors = parse_errors("for x [1, 2, 3] {\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_try_without_catch_or_finally() {
    auto errors = parse_errors("function void foo() {\n"
                               "    try {\n"
                               "        integer x = 1\n"
                               "    }\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_catch_without_variable() {
    // The catch clause must bind an error variable: `catch(err)`. A bare
    // `catch { }` with no parenthesised identifier is a syntax error.
    auto errors = parse_errors("function void foo() {\n"
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
    auto errors = parse_errors("function void foo() {\n"
                               "    try {\n"
                               "        integer x = 1\n"
                               "    } catch() {\n"
                               "        integer y = 2\n"
                               "    }\n"
                               "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_invalid_operator_sequence() {
    auto errors = parse_errors("integer x = 1 + + 2\n");

    // Either error or parsed differently — just shouldn't crash.
    (void)errors;
}

// ─── Error recovery tests ───

static void test_synchronize_recovers_at_function() {
    // After the first error, the parser should synchronize at 'function' and
    // successfully parse the second declaration.
    auto errors = parse_errors("integer = 5\n"
                               "function void greet() {\n"
                               "    integer x = 1\n"
                               "}\n");

    ASSERT_TRUE(errors.size() >= 1U);
}

static void test_synchronize_recovers_at_task_scope() {
    // Parser should recognize task_scope as a synchronization point.
    auto errors = parse_errors("integer = 5\n"
                               "task_scope {\n"
                               "    integer x = 1\n"
                               "}\n");

    ASSERT_TRUE(errors.size() >= 1U);
}

static void test_foreign_keyword_var_hint() {
    // Using 'var' (from other languages) should produce a helpful hint.
    auto errors = parse_errors("function void f() {\n"
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
    auto errors = parse_errors("function void f() {\n"
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
    auto errors = parse_errors("fn greet() {\n"
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
    auto errors = parse_errors("class Point {\n"
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
    auto errors = parse_errors("integer = 5\n"
                               "integer y = 10\n"
                               "string = 20\n"
                               "integer z = 30\n");

    ASSERT_TRUE(errors.size() >= 2U);
}

// ─── Malformed input tests ───

static void test_unterminated_expression() {
    // An incomplete variable initializer should produce a parse error.
    auto errors = parse_errors("integer x =");

    ASSERT_FALSE(errors.empty());
}

static void test_missing_closing_paren_in_call() {
    // A function call missing its closing parenthesis should produce a parse error.
    auto errors = parse_errors("function void f(integer a, integer b) { }\n"
                               "f(1, 2\n");

    ASSERT_FALSE(errors.empty());
}

static void test_invalid_token_in_expression() {
    // A stray token where an expression is expected should produce a parse error.
    auto errors = parse_errors("integer x = }");

    ASSERT_FALSE(errors.empty());
}

// ─── Edge case tests (CA-26) ───

static void test_recovery_after_garbage_tokens() {
    // Multiple garbage tokens followed by a valid statement — the parser
    // should recover and parse the valid statement.
    auto errors = parse_errors("+ + + integer x = 1");

    ASSERT_FALSE(errors.empty());
}

static void test_recovery_missing_function_body() {
    // A function declaration missing its body should produce errors but
    // not crash the parser.
    auto errors = parse_errors("function integer add(integer a, integer b)");

    ASSERT_FALSE(errors.empty());
}

static void test_recovery_nested_braces_malformed() {
    // Deeply nested mismatched braces should produce errors.
    auto errors = parse_errors("if true { if true { if true { } }");

    ASSERT_FALSE(errors.empty());
}

static void test_recovery_multiple_consecutive_errors() {
    // Several malformed statements in a row — parser should collect
    // multiple diagnostics.
    auto errors = parse_errors("integer = \n string = \n boolean = \n");

    ASSERT_GE(errors.size(), 2U);
}

// ─── Recursion depth limit (stack-overflow guard) ───

// Parse `source` with max_parse_depth temporarily lowered so the test
// exercises the parser's depth guard without itself recursing deeply enough to
// risk a real stack overflow.  The limit is always restored before returning.
static std::vector<Diagnostic> parse_errors_capped(const std::string& source, int limit) {
    const int saved = ResourceLimits::max_parse_depth;
    ResourceLimits::max_parse_depth = limit;

    std::vector<Diagnostic> errors;

    try {
        errors = parse_errors(source);
    } catch (...) {
        ResourceLimits::max_parse_depth = saved;
        throw;
    }

    ResourceLimits::max_parse_depth = saved;

    return errors;
}

static bool has_depth_error(const std::vector<Diagnostic>& errors) {
    for (const auto& err : errors) {
        if (err.message.find("nesting depth") != std::string::npos) {
            return true;
        }
    }

    return false;
}

// Parse `source` with max_expression_depth temporarily lowered to `limit`.
// Flat operator/pipe/postfix chains are built iteratively (so max_parse_depth
// never trips); they are instead bounded by max_expression_depth, which guards
// the *recursive* AST walk/teardown.  Mirrors parse_errors_capped.
static std::vector<Diagnostic> parse_errors_capped_expr(const std::string& source, int limit) {
    const int saved = ResourceLimits::max_expression_depth;
    ResourceLimits::max_expression_depth = limit;

    std::vector<Diagnostic> errors;

    try {
        errors = parse_errors(source);
    } catch (...) {
        ResourceLimits::max_expression_depth = saved;
        throw;
    }

    ResourceLimits::max_expression_depth = saved;

    return errors;
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
    const int saved = ResourceLimits::max_parse_depth;
    ResourceLimits::max_parse_depth = 16;

    const auto program = parse("integer x = " + std::string(8, '(') + "1" + std::string(8, ')'));

    ResourceLimits::max_parse_depth = saved;

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::VariableDeclaration);
}

static void test_deep_unary_nesting_rejected() {
    // Stacked prefix unary operators (~~~…, !!!…, ---…) recurse through
    // parse_unary() without re-entering parse_expression().  Without a guard on
    // that path this overflows the stack on deep enough input (a defect first
    // surfaced by the fuzzers).  The input nests far beyond the lowered limit.
    const auto errors = parse_errors_capped(std::string(64, '~') + "1", 16);

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
    const int saved = ResourceLimits::max_parse_depth;
    ResourceLimits::max_parse_depth = 16;

    const auto program = parse("integer x = " + std::string(8, '~') + "1");

    ResourceLimits::max_parse_depth = saved;

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
    const int saved = ResourceLimits::max_expression_depth;
    ResourceLimits::max_expression_depth = 16;

    std::string source = "integer x = 1";

    for (int i = 0; i < 8; ++i) {
        source += " + 1";
    }

    const auto program = parse(source);

    ResourceLimits::max_expression_depth = saved;

    ASSERT_EQ(program.statements.size(), 1U);
    ASSERT_EQ(program.statements[0]->kind, StatementKind::VariableDeclaration);
}

//
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
    ASSERT_TRUE(bin != nullptr);
    ASSERT_TRUE(static_cast<const Expression*>(bin) == &expr);
    ASSERT_TRUE(bin->left != nullptr);
    ASSERT_TRUE(bin->right != nullptr);

    ASSERT_TRUE(try_cast<UnaryExpression>(expr) == nullptr);
    ASSERT_TRUE(try_cast<LiteralExpression>(expr) == nullptr);
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

// `1 + 2 << 3`  ⇒  `(1 + 2) << 3` — shift is below additive.
static void test_precedence_shift_below_additive() {
    const auto program = parse("integer x = 1 + 2 << 3");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::LessLess);
    ASSERT_EQ(root.left->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*root.left).op, TokenType::Plus);
}

// `4 & 1 << 2`  ⇒  `4 & (1 << 2)` — bitwise AND is below shift.
static void test_precedence_bitwise_and_below_shift() {
    const auto program = parse("integer x = 4 & 1 << 2");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::Ampersand);
    ASSERT_EQ(root.right->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*root.right).op, TokenType::LessLess);
}

// `1 | 2 ^ 3 & 4`  ⇒  `1 | (2 ^ (3 & 4))` — precedence `&` > `^` > `|`.
static void test_precedence_bitwise_or_xor_and_ordering() {
    const auto program = parse("integer x = 1 | 2 ^ 3 & 4");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::Pipe);

    const auto& xor_node = as_binary(*root.right);
    ASSERT_EQ(xor_node.op, TokenType::Caret);
    ASSERT_EQ(xor_node.right->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*xor_node.right).op, TokenType::Ampersand);
}

// `1 | 2 < 3`  ⇒  `(1 | 2) < 3` — bitwise binds tighter than comparison.
static void test_precedence_comparison_below_bitwise_or() {
    const auto program = parse("boolean x = 1 | 2 < 3");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::Less);
    ASSERT_EQ(root.left->kind, ExpressionKind::Binary);
    ASSERT_EQ(as_binary(*root.left).op, TokenType::Pipe);
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

// `~1 & 2`  ⇒  `(~1) & 2` — bitwise NOT binds tighter than bitwise AND.
static void test_precedence_bitwise_not_binds_tighter_than_and() {
    const auto program = parse("integer x = ~1 & 2");
    const auto& root = as_binary(first_initializer(program));

    ASSERT_EQ(root.op, TokenType::Ampersand);
    ASSERT_EQ(root.left->kind, ExpressionKind::Unary);
    ASSERT_EQ(as_unary(*root.left).op, TokenType::Tilde);
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
    RUN(test_empty_program);
    RUN(test_variable_declaration);
    RUN(test_mutable_variable);
    RUN(test_function_declaration);
    RUN(test_main_function);
    RUN(test_test_function);
    RUN(test_unknown_annotation_rejected);
    RUN(test_annotation_on_non_function_rejected);
    RUN(test_record_declaration);
    RUN(test_choice_declaration);
    RUN(test_if_statement);
    RUN(test_for_statement);
    RUN(test_expression_statement);
    RUN(test_syntax_error_throws);
    RUN(test_while_statement);
    RUN(test_interface_declaration);
    RUN(test_namespace_declaration);
    RUN(test_namespace_qualified_type_annotation);
    RUN(test_namespace_qualified_record_creation);
    RUN(test_namespace_qualified_match_arm);
    RUN(test_namespace_internal_unsupported_decl_is_syntax_error);
    RUN(test_namespace_internal_before_closing_brace_is_syntax_error);
    RUN(test_namespace_internal_members_set_flag);
    RUN(test_type_alias_declaration);
    RUN(test_generic_type_alias_declaration);
    RUN(test_function_type_alias_declaration);
    RUN(test_collection_type_alias_declaration);
    RUN(test_match_statement);
    RUN(test_lambda_expression);
    RUN(test_lambda_no_params);
    RUN(test_lambda_multiple_params);
    RUN(test_lambda_block_body);
    RUN(test_lambda_nested);
    RUN(test_lambda_missing_body_error);
    RUN(test_lambda_double_arrow_error);
    RUN(test_lambda_trailing_comma_param_error);
    RUN(test_lambda_untyped_param_error);
    RUN(test_include_declaration);
    RUN(test_tuple_destructuring);
    RUN(test_mutable_tuple_destructuring);
    RUN(test_tuple_literal_expression);
    RUN(test_keyword_as_variable_name);
    RUN(test_keyword_as_function_name);
    RUN(test_keyword_as_parameter_name);
    RUN(test_keyword_as_record_name);
    RUN(test_keyword_as_record_field_name);
    RUN(test_keyword_as_choice_name);
    RUN(test_type_keyword_as_variable_name);
    RUN(test_literal_keyword_as_variable_name);
    RUN(test_turbofish_call);
    RUN(test_named_argument_call_parsing);
    RUN(test_mixed_positional_and_named_argument_parsing);
    RUN(test_positional_argument_after_named_error);
    RUN(test_bounded_generic_declaration);
    RUN(test_match_integer_literal);
    RUN(test_match_multi_pattern);
    RUN(test_match_guard_parsed);
    RUN(test_match_guard_on_some_binding);
    RUN(test_match_guard_on_success_arm);
    RUN(test_match_alternatives_parsed);
    RUN(test_match_guard_with_alternatives);
    RUN(test_match_alternative_with_binding_error);
    RUN(test_match_invalid_arm_error);
    RUN(test_task_scope_expression);
    RUN(test_task_scope_with_body);
    RUN(test_task_scope_nested_parse);
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

    // Operator precedence and associativity.
    RUN(test_try_cast_narrows_by_kind);
    RUN(test_precedence_multiplicative_over_additive);
    RUN(test_precedence_additive_left_associative);
    RUN(test_precedence_multiplicative_same_level_left_associative);
    RUN(test_precedence_shift_below_additive);
    RUN(test_precedence_bitwise_and_below_shift);
    RUN(test_precedence_bitwise_or_xor_and_ordering);
    RUN(test_precedence_comparison_below_bitwise_or);
    RUN(test_precedence_equality_below_comparison);
    RUN(test_precedence_logical_and_below_equality);
    RUN(test_precedence_logical_or_below_and);
    RUN(test_precedence_null_coalescing_below_or);
    RUN(test_precedence_unary_binds_tighter_than_multiplicative);
    RUN(test_precedence_bitwise_not_binds_tighter_than_and);
    RUN(test_precedence_pipe_below_arithmetic);
    RUN(test_pipe_builds_pipe_expression);
    RUN(test_error_pipe_builds_error_pipe_expression);
    RUN(test_pipe_chain_left_associative);
    RUN(test_mixed_pipe_error_pipe_left_associative);
    RUN(test_precedence_parentheses_override);

    return SUMMARY();
}
