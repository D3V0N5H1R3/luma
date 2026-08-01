// Linter unit tests.

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "analysis/linter/linter.hpp"
#include "lex_parse_util.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Helpers ───

static std::vector<Diagnostic> lint(const std::string& source) {
    const auto program = luma::test::lex_and_parse(source);

    Linter linter;

    return linter.lint(program);
}

static bool no_warnings(const std::string& source) {
    return lint(source).empty();
}

static bool has_warning_containing(const std::string& source, const std::string& substring) {
    const auto warnings = lint(source);

    return std::ranges::any_of(warnings, [&](const Diagnostic& d) {
        return d.message.find(substring) != std::string::npos;
    });
}

// Returns the hint attached to the first warning with the given diagnostic code,
// or nullopt if no such warning was produced.
static std::optional<std::string> hint_for_code(const std::string& source, DiagnosticCode code) {
    const auto warnings = lint(source);

    const auto match =
        std::ranges::find_if(warnings, [code](const Diagnostic& d) { return d.code == code; });

    if (match == warnings.end()) {
        return std::nullopt;
    }

    return match->hint;
}

// ─── Tests ───

static void test_clean_code_no_warnings() {
    ASSERT_TRUE(no_warnings("function integer add(integer a, integer b) {\n"
                            "    return a + b\n"
                            "}\n"));
}

static void test_empty_function_body() {
    ASSERT_TRUE(has_warning_containing("function void noop() {\n"
                                       "}\n",
                                       "empty body"));
}

static void test_unreachable_code_after_return() {
    ASSERT_TRUE(has_warning_containing("function integer example() {\n"
                                       "    return 1\n"
                                       "    integer x = 2\n"
                                       "}\n",
                                       "Unreachable"));
}

static void test_self_assignment() {
    ASSERT_TRUE(has_warning_containing("mutable integer x = 10\n"
                                       "x = x\n",
                                       "Self-assignment"));
}

static void test_constant_condition_true() {
    ASSERT_TRUE(has_warning_containing("if true {\n"
                                       "    integer x = 1\n"
                                       "}\n",
                                       "always true"));
}

static void test_constant_condition_false() {
    ASSERT_TRUE(has_warning_containing("if false {\n"
                                       "    integer x = 1\n"
                                       "}\n",
                                       "always false"));
}

static void test_empty_if_body() {
    ASSERT_TRUE(has_warning_containing("integer x = 1\n"
                                       "if x == 1 {\n"
                                       "}\n",
                                       "Empty if-body"));
}

static void test_empty_for_body() {
    ASSERT_TRUE(has_warning_containing("for i in [1, 2, 3] {\n"
                                       "}\n",
                                       "Empty for-loop"));
}

static void test_empty_while_body() {
    ASSERT_TRUE(has_warning_containing("while true {\n"
                                       "}\n",
                                       "Empty while-loop"));
}

static void test_float_equality() {
    ASSERT_TRUE(has_warning_containing("integer x = 1\n"
                                       "boolean y = x == 3.14\n",
                                       "Floating-point equality"));
}

static void test_no_false_positive_on_integer_equality() {
    // Integer equality should not trigger floating-point warning.
    const auto warnings = lint("integer x = 1\n"
                               "boolean y = x == 42\n");

    bool has_float_warn = std::ranges::any_of(warnings, [](const Diagnostic& d) {
        return d.message.find("Floating-point") != std::string::npos;
    });

    ASSERT_FALSE(has_float_warn);
}

// ─── Additional warning category tests ───

static void test_unreachable_code_after_break() {
    ASSERT_TRUE(has_warning_containing("for i in [1, 2, 3] {\n"
                                       "    break\n"
                                       "    integer x = 1\n"
                                       "}\n",
                                       "Unreachable"));
}

static void test_shadowed_variable() {
    ASSERT_TRUE(has_warning_containing("integer x = 1\n"
                                       "if true {\n"
                                       "    integer x = 2\n"
                                       "}\n",
                                       "shadow"));
}

static void test_constant_condition_true_in_if_else() {
    ASSERT_TRUE(has_warning_containing("if true {\n"
                                       "    integer x = 1\n"
                                       "} else {\n"
                                       "    integer y = 2\n"
                                       "}\n",
                                       "always true"));
}

static void test_unreachable_after_continue() {
    ASSERT_TRUE(has_warning_containing("for i in [1, 2] {\n"
                                       "    continue\n"
                                       "    integer x = 1\n"
                                       "}\n",
                                       "Unreachable"));
}

static void test_shadowed_variable_nested() {
    ASSERT_TRUE(has_warning_containing("integer y = 1\n"
                                       "for i in [1, 2] {\n"
                                       "    integer y = 2\n"
                                       "}\n",
                                       "shadow"));
}

static void test_no_warning_on_underscore_variable() {
    // Variables prefixed with _ should not produce unused warnings.
    const auto warnings = lint("function void f() {\n"
                               "    integer _unused = 42\n"
                               "}\n");

    bool has_unused = std::ranges::any_of(warnings, [](const Diagnostic& d) {
        return d.message.find("unused") != std::string::npos &&
               d.message.find("_unused") != std::string::npos;
    });

    ASSERT_FALSE(has_unused);
}

static void test_double_negation() {
    ASSERT_TRUE(has_warning_containing("boolean x = true\n"
                                       "boolean y = !!x\n",
                                       "Double negation"));
}

static void test_redundant_else_after_return() {
    ASSERT_TRUE(has_warning_containing("function integer f(integer x) {\n"
                                       "    if x > 0 {\n"
                                       "        return 1\n"
                                       "    } else {\n"
                                       "        return -1\n"
                                       "    }\n"
                                       "}\n",
                                       "Redundant else"));
}

static void test_discarded_result_warning() {
    ASSERT_TRUE(has_warning_containing("FileSystem.read_file(\"test.txt\")\n", "discarded"));
}

static void test_discarded_result_hint_suggests_valid_luma() {
    // The hint must teach valid Luma: 'let' is not a keyword, so it must not
    // appear, and the suggestion should reference a real handling idiom.
    const auto hint =
        hint_for_code("FileSystem.read_file(\"test.txt\")\n", DiagnosticCode::DiscardedResult);

    ASSERT_TRUE(hint.has_value());
    ASSERT_FALSE(hint->empty());
    ASSERT_TRUE(hint->find("let ") == std::string::npos);
    ASSERT_TRUE(hint->find("_ = ...") != std::string::npos);
}

static void test_discarded_result_collection_functions() {
    // New collection functions that return result<T> should also warn.
    ASSERT_TRUE(
        has_warning_containing("Array.partition([1, 2, 3], (integer x) -> x > 1)\n", "discarded"));
    ASSERT_TRUE(has_warning_containing("Set.filter(Set.from_array([1, 2]), (integer x) -> x > 0)\n",
                                       "discarded"));
}

static void test_no_discarded_result_warning_in_match_arm() {
    // W0010 must NOT fire when the call is the last (return) expression of a match arm.
    ASSERT_FALSE(has_warning_containing("function result<string> f(integer x) {\n"
                                        "    return match x {\n"
                                        "        case 1 { FileSystem.read_file(\"a.txt\") }\n"
                                        "        else   { FileSystem.read_file(\"b.txt\") }\n"
                                        "    }\n"
                                        "}\n",
                                        "discarded"));
}

static void test_no_discarded_result_warning_in_if_expression_branch() {
    // W0010 must NOT fire when the call is the tail of an if-expression branch.
    ASSERT_FALSE(has_warning_containing("function result<string> f(boolean flag) {\n"
                                        "    return if flag {\n"
                                        "        FileSystem.read_file(\"a.txt\")\n"
                                        "    } else {\n"
                                        "        FileSystem.read_file(\"b.txt\")\n"
                                        "    }\n"
                                        "}\n",
                                        "discarded"));
}

static void test_no_discarded_result_warning_in_lambda_body() {
    // W0010 must NOT fire when the call is the return expression of a lambda.
    ASSERT_FALSE(
        has_warning_containing("function<() -> result<string>> make_reader(string path) {\n"
                               "    return () -> FileSystem.read_file(path)\n"
                               "}\n",
                               "discarded"));
}

static void test_empty_catch_block() {
    ASSERT_TRUE(has_warning_containing("try {\n"
                                       "    integer x = 1\n"
                                       "} catch(err) {\n"
                                       "}\n",
                                       "Empty catch"));
}

static void test_no_empty_catch_warning_with_body() {
    ASSERT_FALSE(has_warning_containing("try {\n"
                                        "    integer x = 1\n"
                                        "} catch(err) {\n"
                                        "    print(err)\n"
                                        "}\n",
                                        "Empty catch"));
}

static void test_empty_catch_hint_suggests_valid_luma() {
    // The hint must reference the global 'print' builtin, not the
    // non-existent 'Console.print'.
    const auto hint = hint_for_code("try {\n"
                                    "    integer x = 1\n"
                                    "} catch(err) {\n"
                                    "}\n",
                                    DiagnosticCode::EmptyCatch);

    ASSERT_TRUE(hint.has_value());
    ASSERT_TRUE(hint->find("Console.print") == std::string::npos);
    ASSERT_TRUE(hint->find("print(err)") != std::string::npos);
}

static void test_division_by_literal_zero() {
    ASSERT_TRUE(has_warning_containing("integer x = 10\n"
                                       "integer y = x / 0\n",
                                       "Division by zero"));
}

static void test_modulo_by_literal_zero() {
    ASSERT_TRUE(has_warning_containing("integer x = 10\n"
                                       "integer y = x % 0\n",
                                       "Division by zero"));
}

static void test_integer_division_by_literal_zero() {
    ASSERT_TRUE(has_warning_containing("integer x = 10\n"
                                       "integer y = x // 0\n",
                                       "Division by zero"));
}

static void test_no_division_by_zero_warning_for_nonzero() {
    ASSERT_FALSE(has_warning_containing("integer x = 10\n"
                                        "integer y = x / 2\n",
                                        "Division by zero"));
}

static void test_no_division_by_zero_warning_for_variable_divisor() {
    ASSERT_FALSE(has_warning_containing("integer x = 10\n"
                                        "integer y = 0\n"
                                        "integer z = x / y\n",
                                        "Division by zero"));
}

// ─── Bug regression tests: expression-level scope and block handling ───

static void test_unreachable_code_in_lambda_body() {
    // Lambda bodies should detect unreachable code after return.
    ASSERT_TRUE(has_warning_containing("function void example() {\n"
                                       "    mutable (integer) -> integer fn = (integer x) -> {\n"
                                       "        return x\n"
                                       "        integer y = 2\n"
                                       "    }\n"
                                       "}\n",
                                       "Unreachable"));
}

static void test_shadowed_variable_in_lambda() {
    // Variables declared inside a lambda body should detect shadowing
    // against variables in outer scopes.
    ASSERT_TRUE(has_warning_containing("integer x = 1\n"
                                       "mutable (integer) -> integer fn = (integer a) -> {\n"
                                       "    integer x = a\n"
                                       "    return x\n"
                                       "}\n",
                                       "shadow"));
}

static void test_unreachable_code_in_if_expression_body() {
    // If-expression bodies should detect unreachable code.
    ASSERT_TRUE(has_warning_containing("function integer example() {\n"
                                       "    integer x = 1\n"
                                       "    if x > 0 {\n"
                                       "        return 1\n"
                                       "        integer dead = 2\n"
                                       "    }\n"
                                       "    return 0\n"
                                       "}\n",
                                       "Unreachable"));
}

static void test_unused_include() {
    // An include declaration with no symbols referenced should warn.
    ASSERT_TRUE(has_warning_containing("include \"some_file.luma\"\n", "unused"));
}

// ─── Mutable-but-never-mutated rule ───

static void test_mutable_never_mutated_warns() {
    ASSERT_TRUE(has_warning_containing("function integer f() {\n"
                                       "    mutable integer x = 42\n"
                                       "    return x\n"
                                       "}\n",
                                       "never mutated"));
}

static void test_mutable_variable_mutated_no_never_mutated_warning() {
    ASSERT_FALSE(has_warning_containing("function integer f() {\n"
                                        "    mutable integer x = 0\n"
                                        "    x += 5\n"
                                        "    return x\n"
                                        "}\n",
                                        "never mutated"));
}

static void test_mutable_field_compound_assignment_not_never_mutated() {
    // Regression: 'p.x += 1' mutates the root variable 'p', so the linter must
    // not report 'p' as never mutated.
    ASSERT_FALSE(has_warning_containing("record Point { integer x, integer y }\n"
                                        "function integer f() {\n"
                                        "    mutable Point p = Point { x = 1, y = 2 }\n"
                                        "    p.x += 1\n"
                                        "    return p.x\n"
                                        "}\n",
                                        "never mutated"));
}

static void test_mutable_element_compound_assignment_not_never_mutated() {
    // Regression: 'a[0] += 1' mutates the root variable 'a'.
    ASSERT_FALSE(has_warning_containing("function integer f() {\n"
                                        "    mutable array<integer> a = [1, 2, 3]\n"
                                        "    a[0] += 1\n"
                                        "    return a[0]\n"
                                        "}\n",
                                        "never mutated"));
}

static void test_mutable_field_plain_assignment_not_never_mutated() {
    // 'p.x = 1' already worked; guard against regressing the plain-assignment path.
    ASSERT_FALSE(has_warning_containing("record Point { integer x, integer y }\n"
                                        "function integer f() {\n"
                                        "    mutable Point p = Point { x = 1, y = 2 }\n"
                                        "    p.x = 9\n"
                                        "    return p.x\n"
                                        "}\n",
                                        "never mutated"));
}

static void test_mutable_field_increment_not_never_mutated() {
    // Regression: 'p.x++' mutates the root variable 'p', so the linter must
    // not report 'p' as never mutated.
    ASSERT_FALSE(has_warning_containing("record Point { integer x, integer y }\n"
                                        "function integer f() {\n"
                                        "    mutable Point p = Point { x = 1, y = 2 }\n"
                                        "    p.x++\n"
                                        "    return p.x\n"
                                        "}\n",
                                        "never mutated"));
}

static void test_mutable_element_increment_not_never_mutated() {
    // Regression: 'a[0]++' mutates the root variable 'a'.
    ASSERT_FALSE(has_warning_containing("function integer f() {\n"
                                        "    mutable array<integer> a = [1, 2, 3]\n"
                                        "    a[0]++\n"
                                        "    return a[0]\n"
                                        "}\n",
                                        "never mutated"));
}

static void test_mutable_field_decrement_not_never_mutated() {
    // Regression: 'p.x--' mutates the root variable 'p', mirroring the increment path.
    ASSERT_FALSE(has_warning_containing("record Point { integer x, integer y }\n"
                                        "function integer f() {\n"
                                        "    mutable Point p = Point { x = 1, y = 2 }\n"
                                        "    p.x--\n"
                                        "    return p.x\n"
                                        "}\n",
                                        "never mutated"));
}

// ─── main ───

int main() {
    RUN(test_clean_code_no_warnings);
    RUN(test_empty_function_body);
    RUN(test_unreachable_code_after_return);
    RUN(test_self_assignment);
    RUN(test_constant_condition_true);
    RUN(test_constant_condition_false);
    RUN(test_empty_if_body);
    RUN(test_empty_for_body);
    RUN(test_empty_while_body);
    RUN(test_float_equality);
    RUN(test_no_false_positive_on_integer_equality);

    // Additional warning categories.
    RUN(test_unreachable_code_after_break);
    RUN(test_shadowed_variable);
    RUN(test_constant_condition_true_in_if_else);
    RUN(test_unreachable_after_continue);
    RUN(test_shadowed_variable_nested);
    RUN(test_no_warning_on_underscore_variable);

    // Additional warning categories.
    RUN(test_double_negation);
    RUN(test_redundant_else_after_return);
    RUN(test_discarded_result_warning);
    RUN(test_discarded_result_hint_suggests_valid_luma);
    RUN(test_discarded_result_collection_functions);
    RUN(test_no_discarded_result_warning_in_match_arm);
    RUN(test_no_discarded_result_warning_in_if_expression_branch);
    RUN(test_no_discarded_result_warning_in_lambda_body);
    RUN(test_empty_catch_block);
    RUN(test_no_empty_catch_warning_with_body);
    RUN(test_empty_catch_hint_suggests_valid_luma);
    RUN(test_division_by_literal_zero);
    RUN(test_modulo_by_literal_zero);
    RUN(test_integer_division_by_literal_zero);
    RUN(test_no_division_by_zero_warning_for_nonzero);
    RUN(test_no_division_by_zero_warning_for_variable_divisor);

    // Bug regression tests.
    RUN(test_unreachable_code_in_lambda_body);
    RUN(test_shadowed_variable_in_lambda);
    RUN(test_unreachable_code_in_if_expression_body);

    // Unused include detection.
    RUN(test_unused_include);

    // Mutable-but-never-mutated rule (incl. compound/increment/decrement field/element regression).
    RUN(test_mutable_never_mutated_warns);
    RUN(test_mutable_variable_mutated_no_never_mutated_warning);
    RUN(test_mutable_field_compound_assignment_not_never_mutated);
    RUN(test_mutable_element_compound_assignment_not_never_mutated);
    RUN(test_mutable_field_plain_assignment_not_never_mutated);
    RUN(test_mutable_field_increment_not_never_mutated);
    RUN(test_mutable_element_increment_not_never_mutated);
    RUN(test_mutable_field_decrement_not_never_mutated);

    return SUMMARY();
}
