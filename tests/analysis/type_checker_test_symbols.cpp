// Type checker unit tests — exported symbol table (SymbolExporter).
//
// Exercises TypeChecker::export_symbols(), which delegates to SymbolExporter to
// build the SymbolTable the language server consumes for hover, completion, and
// go-to-definition.  The exporter is otherwise untested: these tests pin down
// which declarations reach the table, how they are keyed, and which fields are
// resolved versus intentionally left Unknown.

#include "type_checker_test_helpers.hpp"

// ─── Harness ───
// The shared check() helper owns its TypeChecker internally, so it cannot be
// reused here: export_symbols() must be called on the *same* checker instance
// that ran check().  This local harness keeps the checker alive across both.

static SymbolTable exported(const std::string& source) {
    const auto program = parse(source);

    TypeChecker checker;
    (void)checker.check(program, /*require_main=*/false);

    return checker.export_symbols();
}

// ─── Functions ───

static void test_exports_user_function_with_return_and_params() {
    const auto table = exported("function integer add(integer a, integer b) {\n"
                                "    return a + b\n"
                                "}\n");

    ASSERT_EQ(table.functions.count("add"), 1u);

    const auto& add = table.functions.at("add");
    ASSERT_EQ(add.name, std::string("add"));
    ASSERT_FALSE(add.is_test);

    // The return type is resolved from the inferred Func signature.
    ASSERT_EQ(add.return_type.kind, TypeInfo::Kind::Integer);

    // Parameter *names* are exported in declaration order; parameter *types*
    // are intentionally left Unknown (full resolution needs the checking-time
    // scope, which the exporter does not retain).
    ASSERT_EQ(add.parameters.size(), 2u);
    ASSERT_EQ(add.parameters[0].first, std::string("a"));
    ASSERT_EQ(add.parameters[1].first, std::string("b"));
    ASSERT_EQ(add.parameters[0].second.kind, TypeInfo::Kind::Unknown);
    ASSERT_EQ(add.parameters[1].second.kind, TypeInfo::Kind::Unknown);
}

static void test_exports_void_function() {
    const auto table = exported("function void greet(string name) {\n"
                                "    return\n"
                                "}\n");

    ASSERT_EQ(table.functions.count("greet"), 1u);

    const auto& greet = table.functions.at("greet");
    ASSERT_EQ(greet.return_type.kind, TypeInfo::Kind::Void);
    ASSERT_EQ(greet.parameters.size(), 1u);
    ASSERT_EQ(greet.parameters[0].first, std::string("name"));
}

static void test_marks_test_functions() {
    const auto table = exported("@test\n"
                                "function void checks_true() {\n"
                                "    assert(true)\n"
                                "}\n");

    ASSERT_EQ(table.functions.count("checks_true"), 1u);
    ASSERT_TRUE(table.functions.at("checks_true").is_test);
}

static void test_exports_multiple_functions() {
    const auto table = exported("function integer one() { return 1 }\n"
                                "function integer two() { return 2 }\n"
                                "function integer three() { return 3 }\n");

    ASSERT_EQ(table.functions.count("one"), 1u);
    ASSERT_EQ(table.functions.count("two"), 1u);
    ASSERT_EQ(table.functions.count("three"), 1u);
    ASSERT_EQ(table.functions.size(), 3u);
}

// ─── Records ───

static void test_exports_record_with_field_names() {
    const auto table = exported("record Point { integer x, integer y }\n");

    ASSERT_EQ(table.records.count("Point"), 1u);

    const auto& point = table.records.at("Point");
    ASSERT_EQ(point.name, std::string("Point"));
    ASSERT_EQ(point.fields.size(), 2u);
    ASSERT_EQ(point.fields[0].first, std::string("x"));
    ASSERT_EQ(point.fields[1].first, std::string("y"));
}

static void test_type_names_surface_in_variables_but_not_functions() {
    // Characterization: a record type name binds a non-Func symbol in the top
    // scope, so the exporter — which only filters out Func-typed symbols —
    // surfaces it in `variables` in addition to `records`.  It is never
    // exported as a function.  Pinned so any future change is reviewed.
    const auto table = exported("record Point { integer x, integer y }\n");

    ASSERT_EQ(table.records.count("Point"), 1u);
    ASSERT_EQ(table.functions.count("Point"), 0u);
    ASSERT_EQ(table.variables.count("Point"), 1u);
    ASSERT_NE(table.variables.at("Point").type.kind, TypeInfo::Kind::Func);
}

// ─── Choices ───

static void test_exports_choice_with_variants() {
    const auto table = exported("choice Color { Red, Green, Blue }\n");

    ASSERT_EQ(table.choices.count("Color"), 1u);

    const auto& color = table.choices.at("Color");
    ASSERT_EQ(color.name, std::string("Color"));
    ASSERT_EQ(color.variants.size(), 3u);
    ASSERT_EQ(color.variants[0], std::string("Red"));
    ASSERT_EQ(color.variants[1], std::string("Green"));
    ASSERT_EQ(color.variants[2], std::string("Blue"));
}

// ─── Top-level variables ───

static void test_exports_top_level_variable_with_resolved_type() {
    const auto table = exported("integer answer = 42\n");

    ASSERT_EQ(table.variables.count("answer"), 1u);

    const auto& answer = table.variables.at("answer");
    ASSERT_EQ(answer.type.kind, TypeInfo::Kind::Integer);
    ASSERT_FALSE(answer.is_mutable);
}

static void test_exports_mutable_flag() {
    const auto table = exported("mutable integer counter = 0\n");

    ASSERT_EQ(table.variables.count("counter"), 1u);
    ASSERT_TRUE(table.variables.at("counter").is_mutable);
}

static void test_variables_map_excludes_functions() {
    // Functions live in the functions map only — never duplicated as variables,
    // even though a function name binds a Func-typed symbol in the top scope.
    const auto table = exported("function integer f() { return 0 }\n"
                                "integer g = 1\n");

    ASSERT_EQ(table.variables.count("f"), 0u);
    ASSERT_EQ(table.variables.count("g"), 1u);
}

// ─── Stdlib signatures ───

static void test_exports_stdlib_signatures() {
    // The stdlib signature map is always populated, independent of user code,
    // so the LSP can offer stdlib return types in an otherwise empty file.
    const auto table = exported("");

    ASSERT_FALSE(table.stdlib_signatures.empty());
}

// ─── Combined program ───

static void test_exports_all_declaration_kinds_together() {
    const auto table = exported("record User { string name, integer age }\n"
                                "choice Status { Active, Inactive }\n"
                                "function boolean is_active(Status s) {\n"
                                "    return true\n"
                                "}\n"
                                "integer threshold = 18\n");

    ASSERT_EQ(table.records.count("User"), 1u);
    ASSERT_EQ(table.choices.count("Status"), 1u);
    ASSERT_EQ(table.functions.count("is_active"), 1u);
    ASSERT_EQ(table.variables.count("threshold"), 1u);

    ASSERT_EQ(table.functions.at("is_active").return_type.kind, TypeInfo::Kind::Boolean);
    ASSERT_EQ(table.variables.at("threshold").type.kind, TypeInfo::Kind::Integer);
}

int main() {
    luma::test::print_suite_header("Type Checker — Exported Symbol Table");

    RUN(test_exports_user_function_with_return_and_params);
    RUN(test_exports_void_function);
    RUN(test_marks_test_functions);
    RUN(test_exports_multiple_functions);

    RUN(test_exports_record_with_field_names);
    RUN(test_type_names_surface_in_variables_but_not_functions);

    RUN(test_exports_choice_with_variants);

    RUN(test_exports_top_level_variable_with_resolved_type);
    RUN(test_exports_mutable_flag);
    RUN(test_variables_map_excludes_functions);

    RUN(test_exports_stdlib_signatures);

    RUN(test_exports_all_declaration_kinds_together);

    return SUMMARY();
}
