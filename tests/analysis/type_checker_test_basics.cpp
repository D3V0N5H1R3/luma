// Type checker unit tests — basic type checking fundamentals.

#include "type_checker_test_helpers.hpp"

// ─── Basic valid programs ───

static void test_empty_program() {
    ASSERT_TRUE(passes(""));
}

static void test_valid_variable_declarations() {
    ASSERT_TRUE(passes("integer x = 42"));
    ASSERT_TRUE(passes("number pi = 3.14"));
    ASSERT_TRUE(passes("boolean flag = true"));
    ASSERT_TRUE(passes("string name = \"Alice\""));
    ASSERT_TRUE(fails("any x = 42"));
    ASSERT_TRUE(fails("any s = \"hello\""));
}

static void test_valid_mutable_variables() {
    ASSERT_TRUE(passes("mutable integer x = 0\n"
                       "x = 1\n"));
}

static void test_valid_function() {
    ASSERT_TRUE(passes("function integer add(integer a, integer b) {\n"
                       "    return a + b\n"
                       "}\n"));
}

static void test_valid_function_no_return_type() {
    ASSERT_TRUE(passes("function void greet(string name) {\n"
                       "    return\n"
                       "}\n"));
}

// ─── Type mismatch errors ───

static void test_variable_type_mismatch() {
    ASSERT_TRUE(fails_with("integer x = \"hello\"", DiagnosticCode::TypeMismatch));
    ASSERT_TRUE(fails_with("string s = 42", DiagnosticCode::TypeMismatch));
    ASSERT_TRUE(fails_with("boolean b = 3.14", DiagnosticCode::TypeMismatch));
}

static void test_integer_to_number_promotion() {
    // integer → number is allowed (implicit widening).
    ASSERT_TRUE(passes("number x = 42"));
    // number → integer is NOT allowed.
    ASSERT_TRUE(fails_with("integer x = 3.14", DiagnosticCode::TypeMismatch));
}

// ─── Immutability checks ───

static void test_immutable_assignment_error() {
    ASSERT_TRUE(fails_with("integer x = 42\n"
                           "x = 10\n",
                           DiagnosticCode::ImmutableAssignment));
}

static void test_mutable_assignment_ok() {
    ASSERT_TRUE(passes("mutable integer x = 42\n"
                       "x = 10\n"));
}

static void test_immutable_increment_error() {
    ASSERT_TRUE(fails_with("integer x = 0\n"
                           "x++\n",
                           DiagnosticCode::ImmutableAssignment));
}

static void test_immutable_decrement_error() {
    ASSERT_TRUE(fails_with("integer x = 0\n"
                           "x--\n",
                           DiagnosticCode::ImmutableAssignment));
}

static void test_immutable_compound_assignment_error() {
    ASSERT_TRUE(fails_with("integer x = 0\n"
                           "x += 1\n",
                           DiagnosticCode::ImmutableAssignment));
}

static void test_immutable_tuple_destructuring_binding_assignment_error() {
    ASSERT_TRUE(fails_with("(integer a, integer b) = (1, 2)\n"
                           "a = 3\n",
                           DiagnosticCode::ImmutableAssignment));
}

static void test_mutable_tuple_destructuring_binding_assignment_ok() {
    ASSERT_TRUE(passes("mutable (integer a, integer b) = (1, 2)\n"
                       "a = 3\n"));
}

// ─── Operator type checks ───

static void test_arithmetic_requires_numeric() {
    ASSERT_TRUE(fails("integer x = \"hello\" - 1\n"));
}

static void test_logical_requires_boolean() {
    ASSERT_TRUE(fails("boolean x = 1 && 2\n"));
}

static void test_logical_not_requires_boolean() {
    ASSERT_TRUE(fails("boolean x = !42\n"));
}

static void test_string_concatenation() {
    ASSERT_TRUE(passes("string x = \"hello\" + \" world\"\n"));
}

static void test_mixed_arithmetic_returns_number() {
    // integer + number → number, and result assigned to number.
    ASSERT_TRUE(passes("number x = 5 + 3.0\n"));
}

// ─── Return type checks ───

static void test_return_type_mismatch() {
    ASSERT_TRUE(fails_with("function integer foo() {\n"
                           "    return \"hello\"\n"
                           "}\n",
                           DiagnosticCode::TypeMismatch));
}

static void test_return_type_correct() {
    ASSERT_TRUE(passes("function string foo() {\n"
                       "    return \"hello\"\n"
                       "}\n"));
}

// ─── Record checks ───

static void test_record_creation_valid() {
    ASSERT_TRUE(passes("record Point {\n"
                       "    number x,\n"
                       "    number y\n"
                       "}\n"
                       "\n"
                       "Point p = Point { x = 1.0, y = 2.0 }\n"));
}

static void test_record_missing_field() {
    ASSERT_TRUE(fails("record Point {\n"
                      "    number x,\n"
                      "    number y\n"
                      "}\n"
                      "\n"
                      "Point p = Point { x = 1.0 }\n"));
}

static void test_record_unknown_field() {
    ASSERT_TRUE(fails("record Point {\n"
                      "    number x,\n"
                      "    number y\n"
                      "}\n"
                      "\n"
                      "Point p = Point { x = 1.0, y = 2.0, z = 3.0 }\n"));
}

static void test_record_field_type_mismatch() {
    ASSERT_TRUE(fails("record Point {\n"
                      "    number x,\n"
                      "    number y\n"
                      "}\n"
                      "\n"
                      "Point p = Point { x = \"hello\", y = 2.0 }\n"));
}

// ─── Record field access ───

static void test_record_field_access_valid() {
    ASSERT_TRUE(passes("record Point {\n"
                       "    number x,\n"
                       "    number y\n"
                       "}\n"
                       "\n"
                       "Point p = Point { x = 1.0, y = 2.0 }\n"
                       "number val = p.x\n"));
}

static void test_record_field_access_unknown_field() {
    ASSERT_TRUE(fails("record Point {\n"
                      "    number x,\n"
                      "    number y\n"
                      "}\n"
                      "\n"
                      "Point p = Point { x = 1.0, y = 2.0 }\n"
                      "number val = p.z\n"));
}

// ─── Record `with` expression checks ───

static void test_record_with_unknown_field() {
    ASSERT_TRUE(fails("record Point {\n"
                      "    number x,\n"
                      "    number y\n"
                      "}\n"
                      "\n"
                      "Point p = Point { x = 1.0, y = 2.0 }\n"
                      "Point q = p with { z = 3.0 }\n")); // z is not a field
}

static void test_record_with_type_mismatch() {
    ASSERT_TRUE(fails("record Point {\n"
                      "    number x,\n"
                      "    number y\n"
                      "}\n"
                      "\n"
                      "Point p = Point { x = 1.0, y = 2.0 }\n"
                      "Point q = p with { x = \"oops\" }\n")); // string override on number field
}

static void test_record_with_non_record_base() {
    ASSERT_TRUE(fails("integer n = 5\n"
                      "integer m = n with { x = 1 }\n")); // `with` requires a record value
}

static void test_mutable_param_allows_field_assignment() {
    ASSERT_TRUE(passes("record Point {\n"
                       "    number x,\n"
                       "    number y\n"
                       "}\n"
                       "\n"
                       "function void zero(mutable Point p) {\n"
                       "    p.x = 0.0\n"
                       "    p.y = 0.0\n"
                       "}\n"));
}

// ─── Enum checks ───

static void test_enum_valid() {
    ASSERT_TRUE(passes("choice Color { Red  Green  Blue }\n"));
}

static void test_enum_variant_access_invalid() {
    ASSERT_TRUE(fails("choice Color { Red  Green  Blue }\n"
                      "Color c = Color.Yellow\n"));
}

// ─── Array checks ───

static void test_array_homogeneous() {
    ASSERT_TRUE(passes("array<integer> nums = [1, 2, 3]\n"));
}

static void test_array_heterogeneous_error() {
    ASSERT_TRUE(fails("array<integer> nums = [1, \"two\", 3]\n"));
}

// ─── Tuple checks ───

static void test_tuple_valid() {
    ASSERT_TRUE(passes("function (integer, string) get_pair() {\n"
                       "    return (1, \"hello\")\n"
                       "}\n"));
}

// A tuple literal with more than four elements violates the 2–4 element rule.
static void test_tuple_too_many_elements() {
    ASSERT_TRUE(fails("(integer, integer, integer, integer, integer) t = (1, 2, 3, 4, 5)\n"));
}

// Destructuring a tuple into the wrong number of bindings is a type error.
static void test_tuple_destructuring_count_mismatch() {
    ASSERT_TRUE(fails("(integer a, integer b, integer c) = (1, 2)\n"));
}

// A binding whose declared type does not match the element type is a type error.
static void test_tuple_destructuring_type_mismatch() {
    ASSERT_TRUE(fails("(string a, integer b) = (1, 2)\n"));
}

// Numeric field access (.N) past the last element is caught at compile time.
static void test_tuple_field_access_out_of_bounds() {
    ASSERT_TRUE(fails("integer x = (1, 2, 3).5\n"));
}

// ─── If condition checks ───

static void test_if_condition_must_be_boolean() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    if 42 {\n"
                      "        return\n"
                      "    }\n"
                      "}\n"));
}

static void test_if_condition_boolean_ok() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    if true {\n"
                       "        return\n"
                       "    }\n"
                       "}\n"));
}

// ─── Increment/decrement type checks ───

static void test_increment_requires_numeric() {
    ASSERT_TRUE(fails("mutable string s = \"hi\"\n"
                      "s++\n"));
}

static void test_decrement_requires_numeric() {
    ASSERT_TRUE(fails("mutable string s = \"hi\"\n"
                      "s--\n"));
}

// ─── Compound assignment checks ───

static void test_compound_assignment_numeric() {
    ASSERT_TRUE(passes("mutable integer x = 10\n"
                       "x += 5\n"
                       "x -= 2\n"
                       "x *= 3\n"));
}

static void test_compound_assignment_string_concat() {
    ASSERT_TRUE(passes("mutable string s = \"hello\"\n"
                       "s += \" world\"\n"));
}

static void test_compound_assignment_type_error() {
    ASSERT_TRUE(fails("mutable boolean b = true\n"
                      "b += 1\n"));
}

// ─── @main annotation ───

static void test_main_multiple_error() {
    ASSERT_TRUE(fails("@main\n"
                      "function void start() {\n"
                      "}\n"
                      "\n"
                      "@main\n"
                      "function void start2() {\n"
                      "}\n"));
}

static void test_main_with_params_error() {
    ASSERT_TRUE(fails("@main\n"
                      "function void start(integer x) {\n"
                      "}\n"));
}

static void test_missing_main_is_error() {
    // A program with no @main should be a type error (require_main = true).
    const auto errors = check("function integer add(integer a, integer b) { return a + b }\n",
                              /*require_main=*/true);

    ASSERT_FALSE(errors.empty());
}

static void test_missing_main_ok_for_tests() {
    // Files with no @main must pass when require_main is false (--test mode).
    ASSERT_TRUE(passes("@test\n"
                       "function void test_something() {\n"
                       "    assert(1 + 1 == 2)\n"
                       "}\n"));
}

static void test_test_with_params_error() {
    // @test functions are invoked by the test runner with no arguments, so
    // they must not declare parameters.
    ASSERT_TRUE(fails("@test\n"
                      "function void test_bad(integer x) {\n"
                      "    assert(x == x)\n"
                      "}\n"));
}

// ─── Undefined variable ───

static void test_undefined_variable() {
    ASSERT_TRUE(fails("integer x = y\n"));
}

static void test_defined_variable() {
    ASSERT_TRUE(passes("integer y = 10\n"
                       "integer x = y\n"));
}

// ─── For loop ───

static void test_for_loop_with_range() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    for i in 0..10 {\n"
                       "        integer x = i\n"
                       "    }\n"
                       "}\n"));
}

static void test_for_loop_with_string() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    for ch in \"hello\" {\n"
                       "        string s = ch\n"
                       "    }\n"
                       "}\n"));
}

static void test_for_loop_with_array() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    array<string> names = [\"a\", \"b\"]\n"
                       "    for name in names {\n"
                       "        string s = name\n"
                       "    }\n"
                       "}\n"));
}

static void test_for_single_var_over_dict_is_error() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    dictionary<integer> d = {\"a\": 1}\n"
                      "    for k in d {\n"
                      "    }\n"
                      "}\n"));
}

// ─── Default parameter value ───

static void test_default_param_type_mismatch() {
    ASSERT_TRUE(fails("function void greet(string name, integer count = \"oops\") {\n"
                      "}\n"));
}

static void test_default_param_type_ok() {
    ASSERT_TRUE(passes("function string greet(string name, string prefix = \"Hello\") {\n"
                       "    return prefix + \", \" + name\n"
                       "}\n"));
}

int main() {
    // ─── Basic valid programs ───

    RUN(test_empty_program);
    RUN(test_valid_variable_declarations);
    RUN(test_valid_mutable_variables);
    RUN(test_valid_function);
    RUN(test_valid_function_no_return_type);

    // ─── Type mismatch ───

    RUN(test_variable_type_mismatch);
    RUN(test_integer_to_number_promotion);

    // ─── Immutability ───

    RUN(test_immutable_assignment_error);
    RUN(test_mutable_assignment_ok);
    RUN(test_immutable_increment_error);
    RUN(test_immutable_decrement_error);
    RUN(test_immutable_compound_assignment_error);
    RUN(test_immutable_tuple_destructuring_binding_assignment_error);
    RUN(test_mutable_tuple_destructuring_binding_assignment_ok);

    // ─── Operators ───

    RUN(test_arithmetic_requires_numeric);
    RUN(test_logical_requires_boolean);
    RUN(test_logical_not_requires_boolean);
    RUN(test_string_concatenation);
    RUN(test_mixed_arithmetic_returns_number);

    // ─── Return type ───

    RUN(test_return_type_mismatch);
    RUN(test_return_type_correct);

    // ─── Records ───

    RUN(test_record_creation_valid);
    RUN(test_record_missing_field);
    RUN(test_record_unknown_field);
    RUN(test_record_field_type_mismatch);
    RUN(test_record_field_access_valid);
    RUN(test_record_field_access_unknown_field);
    RUN(test_record_with_unknown_field);
    RUN(test_record_with_type_mismatch);
    RUN(test_record_with_non_record_base);
    RUN(test_mutable_param_allows_field_assignment);

    // ─── Enums ───

    RUN(test_enum_valid);
    RUN(test_enum_variant_access_invalid);

    // ─── Arrays ───

    RUN(test_array_homogeneous);
    RUN(test_array_heterogeneous_error);

    // ─── Tuples ───

    RUN(test_tuple_valid);
    RUN(test_tuple_too_many_elements);
    RUN(test_tuple_destructuring_count_mismatch);
    RUN(test_tuple_destructuring_type_mismatch);
    RUN(test_tuple_field_access_out_of_bounds);

    // ─── If condition ───

    RUN(test_if_condition_must_be_boolean);
    RUN(test_if_condition_boolean_ok);

    // ─── Increment/decrement ───

    RUN(test_increment_requires_numeric);
    RUN(test_decrement_requires_numeric);

    // ─── Compound assignment ───

    RUN(test_compound_assignment_numeric);
    RUN(test_compound_assignment_string_concat);
    RUN(test_compound_assignment_type_error);

    // ─── @main annotation ───

    RUN(test_main_multiple_error);
    RUN(test_main_with_params_error);
    RUN(test_missing_main_is_error);
    RUN(test_missing_main_ok_for_tests);
    RUN(test_test_with_params_error);

    // ─── Undefined variable ───

    RUN(test_undefined_variable);
    RUN(test_defined_variable);

    // ─── For loop ───

    RUN(test_for_loop_with_range);
    RUN(test_for_loop_with_string);
    RUN(test_for_loop_with_array);
    RUN(test_for_single_var_over_dict_is_error);

    // ─── Default parameters ───

    RUN(test_default_param_type_mismatch);
    RUN(test_default_param_type_ok);
    return SUMMARY();
}
