// Parser unit tests: declarations, namespaces, type aliases, and keyword
// rejection in declaration names.

#include <string>

#include "ast_test_util.hpp"
#include "test_parse_helper.hpp"

using luma::ChoiceDeclaration;
using luma::DeclarationKind;
using luma::ExpressionKind;
using luma::ExpressionStatement;
using luma::FunctionDeclaration;
using luma::IncludeDeclaration;
using luma::InterfaceDeclaration;
using luma::MatchArm;
using luma::MatchStatement;
using luma::NamespaceDeclaration;
using luma::RecordCreationExpression;
using luma::RecordDeclaration;
using luma::StatementKind;
using luma::TypeAliasDeclaration;
using luma::VariableDeclStatement;
using luma::test::parse;
using luma::test::parse_errors;

// ─── Declarations ───

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
    ASSERT_NE(errors[0].message.find("unknown annotation"), std::string::npos);
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

static void test_interface_declaration() {
    const auto program = parse("interface Printable {\n  string name\n}");

    ASSERT_EQ(program.declarations.size(), 1U);
    ASSERT_EQ(program.declarations[0]->kind, DeclarationKind::Interface);

    const auto& iface = static_cast<const InterfaceDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(iface.name, "Printable");
    ASSERT_EQ(iface.fields.size(), 1U);
    ASSERT_EQ(iface.fields[0].name, "name");
}

static void test_include_declaration() {
    const auto program = parse("include \"helper.luma\"");

    ASSERT_EQ(program.declarations.size(), 1U);
    ASSERT_EQ(program.declarations[0]->kind, DeclarationKind::Include);

    const auto& inc = static_cast<const IncludeDeclaration&>(*program.declarations[0]);

    ASSERT_EQ(inc.path, "helper.luma");
}

static void test_bounded_generic_declaration() {
    const auto program = parse("interface Printable {\n"
                               "    string name\n"
                               "}\n"
                               "function<T: Printable> string print_value(T value) {\n"
                               "    \"hello\"\n"
                               "}\n");

    ASSERT_EQ(program.declarations.size(), 2);

    const auto& func = static_cast<const FunctionDeclaration&>(*program.declarations[1]);

    ASSERT_EQ(func.type_params.size(), 1);
    ASSERT_EQ(func.type_params[0].name, "T");
    ASSERT_EQ(func.type_params[0].bounds.size(), 1);
    ASSERT_EQ(func.type_params[0].bounds[0], "Printable");
}

// ─── Namespaces ───

static void test_namespace_declaration() {
    const auto program =
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
    ASSERT_EQ(match.arms[0].kind(), MatchArm::Kind::VariantCase);
    ASSERT_EQ(match.arms[0].enum_type(), "Traffic.Light");
    ASSERT_EQ(match.arms[0].enum_variant(), "Red");
}

static void test_namespace_internal_unsupported_decl_is_syntax_error() {
    // 'internal' may only precede function/record/choice/interface/type.
    // Preceding a plain variable declaration must be a syntax error.
    const auto errors = parse_errors("namespace Util {\n"
                                     "    internal integer x = 5\n"
                                     "}\n");

    ASSERT_FALSE(errors.empty());
}

static void test_namespace_internal_before_closing_brace_is_syntax_error() {
    // 'internal' not followed by any declaration keyword (here the namespace's
    // closing brace) must be a syntax error.
    const auto errors = parse_errors("namespace Util {\n"
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

// ─── Type aliases ───

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
    ASSERT_NE(alias.target_type.return_type_ptr(), nullptr);
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

// ─── Keyword-as-identifier rejection ───

static void test_keyword_as_variable_name() {
    const auto errors = parse_errors("integer return = 5");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("reserved keyword"), std::string::npos);
    ASSERT_NE(errors[0].message.find("'return'"), std::string::npos);
}

static void test_keyword_as_function_name() {
    const auto errors = parse_errors("function void if() { }");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("reserved keyword"), std::string::npos);
    ASSERT_NE(errors[0].message.find("'if'"), std::string::npos);
}

static void test_keyword_as_parameter_name() {
    const auto errors = parse_errors("function void foo(integer while) { }");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("reserved keyword"), std::string::npos);
    ASSERT_NE(errors[0].message.find("'while'"), std::string::npos);
}

static void test_keyword_as_record_name() {
    const auto errors = parse_errors("record match {\n  integer x\n}");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("reserved keyword"), std::string::npos);
    ASSERT_NE(errors[0].message.find("'match'"), std::string::npos);
}

static void test_keyword_as_record_field_name() {
    const auto errors = parse_errors("record Foo {\n  integer break\n}");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("reserved keyword"), std::string::npos);
    ASSERT_NE(errors[0].message.find("'break'"), std::string::npos);
}

static void test_keyword_as_choice_name() {
    const auto errors = parse_errors("choice for {\n  A\n  B\n}");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("reserved keyword"), std::string::npos);
    ASSERT_NE(errors[0].message.find("'for'"), std::string::npos);
}

static void test_type_keyword_as_variable_name() {
    const auto errors = parse_errors("integer boolean = 5");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("reserved keyword"), std::string::npos);
    ASSERT_NE(errors[0].message.find("'boolean'"), std::string::npos);
}

static void test_literal_keyword_as_variable_name() {
    const auto errors = parse_errors("integer true = 5");

    ASSERT_FALSE(errors.empty());
    ASSERT_NE(errors[0].message.find("reserved keyword"), std::string::npos);
    ASSERT_NE(errors[0].message.find("'true'"), std::string::npos);
}

int main() {
    // Declarations.
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
    RUN(test_interface_declaration);
    RUN(test_include_declaration);
    RUN(test_bounded_generic_declaration);

    // Namespaces.
    RUN(test_namespace_declaration);
    RUN(test_namespace_qualified_type_annotation);
    RUN(test_namespace_qualified_record_creation);
    RUN(test_namespace_qualified_match_arm);
    RUN(test_namespace_internal_unsupported_decl_is_syntax_error);
    RUN(test_namespace_internal_before_closing_brace_is_syntax_error);
    RUN(test_namespace_internal_members_set_flag);

    // Type aliases.
    RUN(test_type_alias_declaration);
    RUN(test_generic_type_alias_declaration);
    RUN(test_function_type_alias_declaration);
    RUN(test_collection_type_alias_declaration);

    // Keyword-as-identifier rejection.
    RUN(test_keyword_as_variable_name);
    RUN(test_keyword_as_function_name);
    RUN(test_keyword_as_parameter_name);
    RUN(test_keyword_as_record_name);
    RUN(test_keyword_as_record_field_name);
    RUN(test_keyword_as_choice_name);
    RUN(test_type_keyword_as_variable_name);
    RUN(test_literal_keyword_as_variable_name);

    return SUMMARY();
}
