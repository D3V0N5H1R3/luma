// Type checker unit tests — function calls, match, interfaces, and control flow.

#include "type_checker_test_helpers.hpp"

// ─── Call-site argument checking ───

static void test_call_wrong_arg_count_too_few() {
    ASSERT_TRUE(fails_with("function integer add(integer a, integer b) {\n"
                           "    return a + b\n"
                           "}\n"
                           "integer x = add(1)\n",
                           DiagnosticCode::WrongArgCount));
}

static void test_call_wrong_arg_count_too_many() {
    ASSERT_TRUE(fails_with("function integer add(integer a, integer b) {\n"
                           "    return a + b\n"
                           "}\n"
                           "integer x = add(1, 2, 3)\n",
                           DiagnosticCode::WrongArgCount));
}

static void test_call_arg_type_mismatch() {
    ASSERT_TRUE(fails_with("function integer add(integer a, integer b) {\n"
                           "    return a + b\n"
                           "}\n"
                           "integer x = add(\"hello\", 2)\n",
                           DiagnosticCode::TypeMismatch));
}

static void test_call_arg_type_mismatch_hint() {
    // Passing a string where number expected should produce a hint.
    const auto errors = check("function number scale(number a, number b) {\n"
                              "    return a * b\n"
                              "}\n"
                              "number x = scale(\"hello\", 2.0)\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].hint.has_value());
}

static void test_call_arg_count_respects_defaults() {
    // A call omitting the optional argument should pass.
    ASSERT_TRUE(passes("function string greet(string name, string prefix = \"Hi\") {\n"
                       "    return prefix + \" \" + name\n"
                       "}\n"
                       "string s = greet(\"Alice\")\n"));
}

// ─── Named-argument checking ───

static void test_named_argument_correct_type_passes() {
    ASSERT_TRUE(passes("function integer add(integer a, integer b) {\n"
                       "    return a + b\n"
                       "}\n"
                       "integer x = add(a: 1, b: 2)\n"));
}

static void test_named_argument_type_mismatch_fails() {
    ASSERT_TRUE(fails_with("function integer add(integer a, integer b) {\n"
                           "    return a + b\n"
                           "}\n"
                           "integer x = add(a: \"hello\", b: 2)\n",
                           DiagnosticCode::TypeMismatch));
}

static void test_named_argument_reordered_type_checked() {
    // Reordering named arguments must still type-check each by name, so a
    // mismatch on the second-listed (but first-declared) parameter fails.
    ASSERT_TRUE(fails("function string label(string name, integer count) {\n"
                      "    return name\n"
                      "}\n"
                      "string s = label(count: 3, name: 7)\n"));
}

// ─── Mutable parameters ───

static void test_mutable_parameter_reassignment_passes() {
    ASSERT_TRUE(passes("function integer accumulate(mutable integer total, integer add) {\n"
                       "    total = total + add\n"
                       "    return total\n"
                       "}\n"));
}

static void test_immutable_parameter_reassignment_fails() {
    // Parameters are immutable by default; reassigning one is a type error.
    ASSERT_TRUE(fails_with("function integer accumulate(integer total, integer add) {\n"
                           "    total = total + add\n"
                           "    return total\n"
                           "}\n",
                           DiagnosticCode::ImmutableAssignment));
}

// ─── @main / @test annotation rules ───

static void test_main_with_parameters_fails() {
    ASSERT_TRUE(fails("@main\n"
                      "function void main(integer x) {\n"
                      "}\n"));
}

static void test_main_without_parameters_passes() {
    ASSERT_TRUE(passes("@main\n"
                       "function void main() {\n"
                       "}\n"));
}

static void test_test_with_parameters_fails() {
    ASSERT_TRUE(fails("@test\n"
                      "function void test_foo(integer x) {\n"
                      "}\n"));
}

static void test_multiple_main_fails() {
    ASSERT_TRUE(fails("@main\n"
                      "function void main() {\n"
                      "}\n"
                      "@main\n"
                      "function void second() {\n"
                      "}\n"));
}

// ─── Match exhaustiveness ───

static void test_match_boolean_exhaustive() {
    ASSERT_TRUE(passes("function integer foo(boolean b) {\n"
                       "    return match b {\n"
                       "        case true  { 1 }\n"
                       "        case false { 0 }\n"
                       "    }\n"
                       "}\n"));
}

static void test_match_boolean_non_exhaustive() {
    ASSERT_TRUE(fails("function integer foo(boolean b) {\n"
                      "    return match b {\n"
                      "        case true { 1 }\n"
                      "    }\n"
                      "}\n"));
}

static void test_match_integer_literal_needs_else() {
    ASSERT_TRUE(fails("function string foo(integer x) {\n"
                      "    return match x {\n"
                      "        case 1 { \"one\" }\n"
                      "        case 2 { \"two\" }\n"
                      "    }\n"
                      "}\n"));
}

static void test_match_integer_literal_with_else() {
    ASSERT_TRUE(passes("function string foo(integer x) {\n"
                       "    return match x {\n"
                       "        case 1 { \"one\" }\n"
                       "        case 2 { \"two\" }\n"
                       "        else   { \"other\" }\n"
                       "    }\n"
                       "}\n"));
}

static void test_match_range_needs_else() {
    ASSERT_TRUE(fails("function string foo(integer x) {\n"
                      "    return match x {\n"
                      "        case 0..=9   { \"low\" }\n"
                      "        case 10..=19 { \"mid\" }\n"
                      "    }\n"
                      "}\n"));
}

static void test_match_range_with_else() {
    ASSERT_TRUE(passes("function string foo(integer x) {\n"
                       "    return match x {\n"
                       "        case 0..=9   { \"low\" }\n"
                       "        case 10..20  { \"mid\" }\n"
                       "        else         { \"high\" }\n"
                       "    }\n"
                       "}\n"));
}

static void test_match_range_alternatives_pass() {
    ASSERT_TRUE(passes("function string foo(integer x) {\n"
                       "    return match x {\n"
                       "        case 0..=9 | 20..=29 { \"a\" }\n"
                       "        else                 { \"b\" }\n"
                       "    }\n"
                       "}\n"));
}

static void test_match_range_non_integer_subject_fails() {
    // Range-case subject validation runs on statement-form matches (mirroring
    // integer-case validation), so use a statement-form match here.
    ASSERT_TRUE(fails("function void foo(string s) {\n"
                      "    match s {\n"
                      "        case 0..=9 { }\n"
                      "        else       { }\n"
                      "    }\n"
                      "}\n"));
}

static void test_match_comparison_needs_else() {
    ASSERT_TRUE(fails("function string foo(integer x) {\n"
                      "    return match x {\n"
                      "        case 1 { \"one\" }\n"
                      "        case 2 { \"two\" }\n"
                      "    }\n"
                      "}\n"));
}

static void test_match_comparison_with_else() {
    ASSERT_TRUE(passes("function string foo(integer x) {\n"
                       "    return match x {\n"
                       "        case 1 { \"one\" }\n"
                       "        case 2 { \"two\" }\n"
                       "        else      { \"other\" }\n"
                       "    }\n"
                       "}\n"));
}

// ─── Enum match exhaustiveness ───

static void test_match_enum_exhaustive() {
    ASSERT_TRUE(passes("choice Color { Red  Green  Blue }\n"
                       "\n"
                       "function string color_name(Color c) {\n"
                       "    return match c {\n"
                       "        case Color.Red   { \"red\" }\n"
                       "        case Color.Green { \"green\" }\n"
                       "        case Color.Blue  { \"blue\" }\n"
                       "    }\n"
                       "}\n"));
}

static void test_match_enum_non_exhaustive() {
    ASSERT_TRUE(fails("choice Color { Red  Green  Blue }\n"
                      "\n"
                      "function string color_name(Color c) {\n"
                      "    return match c {\n"
                      "        case Color.Red   { \"red\" }\n"
                      "        case Color.Green { \"green\" }\n"
                      "    }\n"
                      "}\n"));
}

static void test_match_multi_pattern_exhaustive() {
    ASSERT_TRUE(passes("choice Color { Red  Green  Blue }\n"
                       "\n"
                       "function string color_name(Color c) {\n"
                       "    return match c {\n"
                       "        case Color.Red | Color.Blue { \"extreme\" }\n"
                       "        case Color.Green            { \"middle\" }\n"
                       "    }\n"
                       "}\n"));
}

static void test_match_multi_pattern_non_exhaustive() {
    ASSERT_TRUE(fails("choice Color { Red  Green  Blue }\n"
                      "\n"
                      "function string color_name(Color c) {\n"
                      "    return match c {\n"
                      "        case Color.Red | Color.Blue { \"extreme\" }\n"
                      "    }\n"
                      "}\n"));
}

// ─── Result / optional match exhaustiveness ───

static void test_match_result_non_exhaustive() {
    ASSERT_TRUE(fails("function string foo(result<integer> r) {\n"
                      "    return match r {\n"
                      "        success(v) { \"ok\" }\n"
                      "    }\n"
                      "}\n"));
}

static void test_match_optional_non_exhaustive() {
    ASSERT_TRUE(fails("function string foo(optional<integer> o) {\n"
                      "    return match o {\n"
                      "        case some(v) { \"some\" }\n"
                      "    }\n"
                      "}\n"));
}

// ─── Match guards ───

static void test_match_guard_must_be_boolean() {
    // A non-boolean guard (here an integer) is rejected.
    ASSERT_TRUE(fails("function string foo(integer x) {\n"
                      "    return match x {\n"
                      "        case >= 5 when x { \"big\" }\n"
                      "        else { \"small\" }\n"
                      "    }\n"
                      "}\n"));
}

static void test_match_guard_boolean_passes() {
    ASSERT_TRUE(passes("function string foo(integer x) {\n"
                       "    return match x {\n"
                       "        case >= 5 when x % 2 == 0 { \"even-big\" }\n"
                       "        case >= 5 { \"odd-big\" }\n"
                       "        else { \"small\" }\n"
                       "    }\n"
                       "}\n"));
}

static void test_match_guarded_arm_not_exhaustive() {
    // A guarded arm does not count toward exhaustiveness, so a guarded-only
    // `some` arm leaves `some` uncovered.
    ASSERT_TRUE(fails("function string foo(optional<integer> o) {\n"
                      "    return match o {\n"
                      "        case some(v) when v > 0 { \"pos\" }\n"
                      "        case none { \"none\" }\n"
                      "    }\n"
                      "}\n"));
}

static void test_match_guarded_arm_with_fallback_passes() {
    // Adding an unguarded `some` fallback restores exhaustiveness.
    ASSERT_TRUE(passes("function string foo(optional<integer> o) {\n"
                       "    return match o {\n"
                       "        case some(v) when v > 0 { \"pos\" }\n"
                       "        case some(v) { \"nonpos\" }\n"
                       "        case none { \"none\" }\n"
                       "    }\n"
                       "}\n"));
}

static void test_match_guarded_boolean_not_exhaustive() {
    // A guarded `true` arm does not cover `true`.
    ASSERT_TRUE(fails("function integer foo(boolean b) {\n"
                      "    return match b {\n"
                      "        case true when b { 1 }\n"
                      "        case false     { 0 }\n"
                      "    }\n"
                      "}\n"));
}

// ─── Type alias ───

static void test_type_alias() {
    ASSERT_TRUE(passes("type Age = integer\n"
                       "\n"
                       "Age x = 25\n"));
}

static void test_type_alias_interchangeable() {
    // Alias and underlying type are mutually assignable in both directions.
    ASSERT_TRUE(passes("type Age = integer\n"
                       "Age a = 5\n"
                       "integer b = a\n"
                       "Age c = b\n"));
}

static void test_function_type_alias() {
    ASSERT_TRUE(passes("type Predicate = function(integer) -> boolean\n"
                       "Predicate is_positive = (integer x) -> x > 0\n"
                       "boolean ok = is_positive(5)\n"));
}

static void test_collection_type_alias() {
    ASSERT_TRUE(passes("type IntList = array<integer>\n"
                       "IntList xs = [1, 2, 3]\n"
                       "integer n = Array.length(xs)\n"));
    ASSERT_TRUE(passes("type ScoreMap = dictionary<number>\n"
                       "ScoreMap m = {\"a\": 1.0}\n"));
}

static void test_chained_type_alias() {
    // An alias may refer to another alias.
    ASSERT_TRUE(passes("type A = integer\n"
                       "type B = A\n"
                       "B x = 5\n"));
}

static void test_type_alias_as_record_field() {
    ASSERT_TRUE(passes("type Score = number\n"
                       "record Player {\n"
                       "    Score score\n"
                       "}\n"
                       "Player p = Player { score = 9.0 }\n"));
}

static void test_type_alias_in_namespace_qualified() {
    ASSERT_TRUE(passes("namespace Geometry {\n"
                       "    type Distance = number\n"
                       "}\n"
                       "Geometry.Distance d = 42.0\n"));
}

static void test_recursive_type_alias() {
    ASSERT_TRUE(fails("type A = A\n"));                         // direct self-reference
    ASSERT_TRUE(fails("type B = C\ntype C = B\n"));             // mutual 2-cycle
    ASSERT_TRUE(fails("type D = E\ntype E = F\ntype F = D\n")); // mutual 3-cycle
}

static void test_unknown_type_in_alias() {
    ASSERT_TRUE(fails("type Bad = Nonexistent\n"));
}

static void test_type_alias_assignment_mismatch() {
    // A value of the wrong underlying type cannot be assigned through the alias.
    ASSERT_TRUE(fails("type Age = integer\n"
                      "Age x = \"hello\"\n"));
}

static void test_function_type_alias_arg_mismatch() {
    // Calling through a function-type alias still checks argument types.
    ASSERT_TRUE(fails("type Predicate = function(integer) -> boolean\n"
                      "Predicate p = (integer x) -> x > 0\n"
                      "boolean r = p(\"not an integer\")\n"));
}

// ─── Interface satisfaction ───

static void test_interface_satisfaction() {
    ASSERT_TRUE(passes("interface Printable {\n"
                       "    string label\n"
                       "}\n"
                       "\n"
                       "record Widget {\n"
                       "    string label,\n"
                       "    integer width\n"
                       "}\n"
                       "\n"
                       "function void show(Printable p) {\n"
                       "}\n"
                       "\n"
                       "@main\n"
                       "function void start() {\n"
                       "    Widget w = Widget { label = \"btn\", width = 100 }\n"
                       "    show(w)\n"
                       "}\n"));
}

// ─── Interface-to-interface assignability ───

static void test_interface_to_interface_assignability() {
    ASSERT_TRUE(passes("interface Named {\n"
                       "    string name\n"
                       "}\n"
                       "\n"
                       "interface FullNamed {\n"
                       "    string name,\n"
                       "    number score\n"
                       "}\n"
                       "\n"
                       "function void greet(Named n) {\n"
                       "}\n"
                       "\n"
                       "record Player { string name, number score }\n"
                       "\n"
                       "@main\n"
                       "function void start() {\n"
                       "    Player p = Player { name = \"Alice\", score = 99 }\n"
                       "    FullNamed fn = p\n"
                       "    greet(fn)\n"
                       "}\n"));
}

static void test_interface_to_interface_missing_field_fails() {
    ASSERT_TRUE(fails("interface Named {\n"
                      "    string name\n"
                      "}\n"
                      "\n"
                      "interface Scored {\n"
                      "    number score\n"
                      "}\n"
                      "\n"
                      "function void greet(Named n) {\n"
                      "}\n"
                      "\n"
                      "record Scored2 { number score }\n"
                      "\n"
                      "@main\n"
                      "function void start() {\n"
                      "    Scored2 s = Scored2 { score = 10 }\n"
                      "    Scored si = s\n"
                      "    greet(si)\n"
                      "}\n"));
}

// A record that lacks a field required by the interface does not satisfy it.
static void test_interface_not_satisfied_missing_field() {
    ASSERT_TRUE(fails("interface Named {\n"
                      "    string name\n"
                      "}\n"
                      "\n"
                      "record Dog { integer age }\n"
                      "\n"
                      "function void greet(Named n) {\n"
                      "}\n"
                      "\n"
                      "@main\n"
                      "function void start() {\n"
                      "    Dog d = Dog { age = 5 }\n"
                      "    greet(d)\n"
                      "}\n"));
}

// A record whose field has an incompatible type does not satisfy the interface
// (string is not assignable from integer).
static void test_interface_field_type_incompatible() {
    ASSERT_TRUE(fails("interface Named {\n"
                      "    string name\n"
                      "}\n"
                      "\n"
                      "record Robot { integer name }\n"
                      "\n"
                      "function void greet(Named n) {\n"
                      "}\n"
                      "\n"
                      "@main\n"
                      "function void start() {\n"
                      "    Robot r = Robot { name = 5 }\n"
                      "    greet(r)\n"
                      "}\n"));
}

// Accessing a field that the interface does not declare is a type error.
static void test_interface_undefined_field_access() {
    ASSERT_TRUE(fails("interface Named {\n"
                      "    string name\n"
                      "}\n"
                      "\n"
                      "function integer level(Named n) {\n"
                      "    return n.level\n"
                      "}\n"));
}

// A record with an integer field satisfies an interface that declares the same
// field as number, because integer is assignable to number.
static void test_interface_integer_satisfies_number_field() {
    ASSERT_TRUE(passes("interface Measurable {\n"
                       "    number value\n"
                       "}\n"
                       "\n"
                       "record Reading { integer value }\n"
                       "\n"
                       "function number read_value(Measurable m) {\n"
                       "    return m.value\n"
                       "}\n"
                       "\n"
                       "@main\n"
                       "function void start() {\n"
                       "    Reading r = Reading { value = 42 }\n"
                       "    number v = read_value(r)\n"
                       "}\n"));
}

// An interface with no fields is satisfied by any record.
static void test_interface_empty_satisfied_by_any_record() {
    ASSERT_TRUE(passes("interface Any {}\n"
                       "\n"
                       "record Thing { integer x }\n"
                       "\n"
                       "function void use_any(Any a) {\n"
                       "}\n"
                       "\n"
                       "@main\n"
                       "function void start() {\n"
                       "    Thing t = Thing { x = 1 }\n"
                       "    use_any(t)\n"
                       "}\n"));
}

// ─── Stdlib calls don't cause errors ───

static void test_stdlib_calls_accepted() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    integer len = String.length(\"hello\")\n"
                       "}\n"));
}

// ─── Multiple errors collected ───

static void test_multiple_errors_collected() {
    auto errors = check("integer x = \"hello\"\n"
                        "string y = 42\n");

    ASSERT_TRUE(errors.size() >= 2);
}

// ─── Result type ───

static void test_ok_and_fail_expressions() {
    ASSERT_TRUE(passes("result<integer> r = success(42)\n"
                       "result<integer> f = failure(\"error\")\n"));
}

// ─── Range type ───

static void test_range_requires_numeric() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    for i in \"a\"..\"z\" {\n"
                      "    }\n"
                      "}\n"));
}

// ─── While loop ───

static void test_while_condition_must_be_boolean() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    while 42 {\n"
                      "    }\n"
                      "}\n"));
}

static void test_while_condition_boolean_ok() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    while true {\n"
                       "        break\n"
                       "    }\n"
                       "}\n"));
}

// ─── Break / continue placement ───

static void test_break_outside_loop_fails() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    break\n"
                      "}\n"));
}

static void test_continue_outside_loop_fails() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    continue\n"
                      "}\n"));
}

static void test_break_inside_for_loop_ok() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    for i in 0 .. 10 {\n"
                       "        if i == 3 { break }\n"
                       "    }\n"
                       "}\n"));
}

static void test_continue_inside_for_loop_ok() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    for i in 0 .. 10 {\n"
                       "        if i == 3 { continue }\n"
                       "    }\n"
                       "}\n"));
}

// ─── For iterable type ───

static void test_for_over_integer_fails() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    for x in 5 {\n"
                      "    }\n"
                      "}\n"));
}

// ─── If expression branch typing ───

static void test_if_expression_branches_same_type_ok() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    string label = if true { \"yes\" } else { \"no\" }\n"
                       "}\n"));
}

static void test_if_expression_branch_type_mismatch_fails() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    integer x = if true { 1 } else { \"two\" }\n"
                      "}\n"));
}

// ─── Match arm result typing ───

static void test_match_arm_type_mismatch_fails() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    integer y = 3\n"
                      "    integer r = match y {\n"
                      "        case 1 { 10 }\n"
                      "        else { \"no\" }\n"
                      "    }\n"
                      "}\n"));
}

// ─── Lambda type ───

static void test_lambda_valid() {
    ASSERT_TRUE(passes("function(integer) -> integer f = (integer x) -> x * 2\n"));
}

static void test_lambda_no_params_valid() {
    ASSERT_TRUE(passes("function() -> integer f = () -> 42\n"));
}

static void test_lambda_multi_param_valid() {
    ASSERT_TRUE(passes("function(integer, integer) -> integer f = "
                       "(integer a, integer b) -> a + b\n"));
}

static void test_lambda_captures_outer_variable() {
    ASSERT_TRUE(passes("function void f() {\n"
                       "    integer threshold = 60\n"
                       "    function(integer) -> boolean p = (integer s) -> s >= threshold\n"
                       "}\n"));
}

static void test_lambda_as_higher_order_argument_valid() {
    ASSERT_TRUE(passes("function integer apply_fn(function(integer) -> integer fn, integer x) {\n"
                       "    return fn(x)\n"
                       "}\n"
                       "function void g() {\n"
                       "    integer r = apply_fn((integer n) -> n + 1, 5)\n"
                       "}\n"));
}

static void test_lambda_returned_from_function_valid() {
    ASSERT_TRUE(passes("function function(integer) -> integer make_adder(integer base) {\n"
                       "    return (integer x) -> base + x\n"
                       "}\n"));
}

static void test_lambda_body_type_mismatch_fails() {
    // Body infers integer, but the variable is declared as a string-returning function.
    ASSERT_TRUE(fails("function(integer) -> string f = (integer x) -> x * 2\n"));
}

static void test_lambda_param_signature_mismatch_fails() {
    // Parameter type differs: declared takes string, lambda takes integer.
    ASSERT_TRUE(fails("function(string) -> integer f = (integer x) -> x * 2\n"));
}

static void test_lambda_call_wrong_arg_count_fails() {
    ASSERT_TRUE(fails_with("function(integer) -> integer f = (integer x) -> x * 2\n"
                           "integer y = f(1, 2)\n",
                           DiagnosticCode::WrongArgCount));
}

static void test_lambda_call_wrong_arg_type_fails() {
    ASSERT_TRUE(fails("function(integer) -> integer f = (integer x) -> x * 2\n"
                      "integer y = f(\"hi\")\n"));
}

static void test_lambda_undefined_variable_in_body_fails() {
    ASSERT_TRUE(fails("function() -> integer f = () -> nope + 1\n"));
}

// ─── Pipe operator ───

static void test_pipe_valid() {
    ASSERT_TRUE(passes("function integer double_it(integer x) { return x * 2 }\n"
                       "function void foo() {\n"
                       "    5 |> double_it()\n"
                       "}\n"));
}

static void test_pipe_user_function_typed_extra_args() {
    // Regression: in a pipe the piped value fills parameter 0 and explicit
    // arguments fill the remaining parameters.  When the parameters have
    // different types, the explicit argument must be matched against
    // parameter 1 (not parameter 0, which the piped value already fills).
    ASSERT_TRUE(passes("function string tag(integer count, string label) {\n"
                       "    return label\n"
                       "}\n"
                       "function void foo() {\n"
                       "    5 |> tag(\"items\")\n"
                       "}\n"));
}

static void test_pipe_user_function_typed_extra_arg_mismatch() {
    // The explicit argument must still be checked against parameter 1, so a
    // wrong-typed explicit argument is reported (as argument 2).
    ASSERT_TRUE(fails_with("function string tag(integer count, string label) {\n"
                           "    return label\n"
                           "}\n"
                           "function void foo() {\n"
                           "    5 |> tag(99)\n"
                           "}\n",
                           DiagnosticCode::TypeMismatch));
}

int main() {
    // ─── Call-site argument checking ───

    RUN(test_call_wrong_arg_count_too_few);
    RUN(test_call_wrong_arg_count_too_many);
    RUN(test_call_arg_type_mismatch);
    RUN(test_call_arg_type_mismatch_hint);
    RUN(test_call_arg_count_respects_defaults);

    // ─── Named-argument checking ───

    RUN(test_named_argument_correct_type_passes);
    RUN(test_named_argument_type_mismatch_fails);
    RUN(test_named_argument_reordered_type_checked);

    // ─── Mutable parameters ───

    RUN(test_mutable_parameter_reassignment_passes);
    RUN(test_immutable_parameter_reassignment_fails);

    // ─── @main / @test annotation rules ───

    RUN(test_main_with_parameters_fails);
    RUN(test_main_without_parameters_passes);
    RUN(test_test_with_parameters_fails);
    RUN(test_multiple_main_fails);

    // ─── Match exhaustiveness ───

    RUN(test_match_boolean_exhaustive);
    RUN(test_match_boolean_non_exhaustive);
    RUN(test_match_integer_literal_needs_else);
    RUN(test_match_integer_literal_with_else);
    RUN(test_match_range_needs_else);
    RUN(test_match_range_with_else);
    RUN(test_match_range_alternatives_pass);
    RUN(test_match_range_non_integer_subject_fails);
    RUN(test_match_comparison_needs_else);
    RUN(test_match_comparison_with_else);
    RUN(test_match_enum_exhaustive);
    RUN(test_match_enum_non_exhaustive);
    RUN(test_match_multi_pattern_exhaustive);
    RUN(test_match_multi_pattern_non_exhaustive);
    RUN(test_match_result_non_exhaustive);
    RUN(test_match_optional_non_exhaustive);
    RUN(test_match_guard_must_be_boolean);
    RUN(test_match_guard_boolean_passes);
    RUN(test_match_guarded_arm_not_exhaustive);
    RUN(test_match_guarded_arm_with_fallback_passes);
    RUN(test_match_guarded_boolean_not_exhaustive);

    // ─── Type alias ───

    RUN(test_type_alias);
    RUN(test_type_alias_interchangeable);
    RUN(test_function_type_alias);
    RUN(test_collection_type_alias);
    RUN(test_chained_type_alias);
    RUN(test_type_alias_as_record_field);
    RUN(test_type_alias_in_namespace_qualified);
    RUN(test_recursive_type_alias);
    RUN(test_unknown_type_in_alias);
    RUN(test_type_alias_assignment_mismatch);
    RUN(test_function_type_alias_arg_mismatch);

    // ─── Interface satisfaction ───

    RUN(test_interface_satisfaction);
    RUN(test_interface_not_satisfied_missing_field);
    RUN(test_interface_field_type_incompatible);
    RUN(test_interface_undefined_field_access);
    RUN(test_interface_integer_satisfies_number_field);
    RUN(test_interface_empty_satisfied_by_any_record);

    // ─── Interface-to-interface assignability ───

    RUN(test_interface_to_interface_assignability);
    RUN(test_interface_to_interface_missing_field_fails);

    // ─── Stdlib ───

    RUN(test_stdlib_calls_accepted);

    // ─── Multiple errors ───

    RUN(test_multiple_errors_collected);

    // ─── Result type ───

    RUN(test_ok_and_fail_expressions);

    // ─── Range type ───

    RUN(test_range_requires_numeric);

    // ─── While loop ───

    RUN(test_while_condition_must_be_boolean);
    RUN(test_while_condition_boolean_ok);

    // ─── Break / continue placement ───

    RUN(test_break_outside_loop_fails);
    RUN(test_continue_outside_loop_fails);
    RUN(test_break_inside_for_loop_ok);
    RUN(test_continue_inside_for_loop_ok);

    // ─── For iterable type ───

    RUN(test_for_over_integer_fails);

    // ─── If expression branch typing ───

    RUN(test_if_expression_branches_same_type_ok);
    RUN(test_if_expression_branch_type_mismatch_fails);

    // ─── Match arm result typing ───

    RUN(test_match_arm_type_mismatch_fails);

    // ─── Lambda ───

    RUN(test_lambda_valid);
    RUN(test_lambda_no_params_valid);
    RUN(test_lambda_multi_param_valid);
    RUN(test_lambda_captures_outer_variable);
    RUN(test_lambda_as_higher_order_argument_valid);
    RUN(test_lambda_returned_from_function_valid);
    RUN(test_lambda_body_type_mismatch_fails);
    RUN(test_lambda_param_signature_mismatch_fails);
    RUN(test_lambda_call_wrong_arg_count_fails);
    RUN(test_lambda_call_wrong_arg_type_fails);
    RUN(test_lambda_undefined_variable_in_body_fails);

    // ─── Pipe operator ───

    RUN(test_pipe_valid);
    RUN(test_pipe_user_function_typed_extra_args);
    RUN(test_pipe_user_function_typed_extra_arg_mismatch);
    return SUMMARY();
}
