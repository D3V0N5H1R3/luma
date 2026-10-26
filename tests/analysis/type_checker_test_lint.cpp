// Type checker unit tests — linter warnings: unused, shadow, unreachable, hints.

#include "type_checker_test_helpers.hpp"

// ─── Linter warnings ───

// True if the type checker + linter report `code` for `source`. The
// variable-usage rules (W0001, W0003, W0004, W0005, W0012) live in the linter.
static bool reports(const std::string& source, DiagnosticCode code) {
    return has_code(check_warnings_with_lint(source), code);
}

static void test_shadow_variable_warns() {
    ASSERT_TRUE(reports("function void test() {\n"
                        "  integer x = 1\n"
                        "  if true {\n"
                        "    integer x = 2\n"
                        "  }\n"
                        "}\n",
                        DiagnosticCode::ShadowedVariable));
}

static void test_underscore_prefix_no_shadow_warning() {
    ASSERT_FALSE(reports("function void test() {\n"
                         "  integer _x = 1\n"
                         "  if true {\n"
                         "    integer _x = 2\n"
                         "  }\n"
                         "}\n",
                         DiagnosticCode::ShadowedVariable));
}

static void test_for_loop_variable_shadow_warning() {
    ASSERT_TRUE(reports("function void test() {\n"
                        "  integer i = 10\n"
                        "  for i in 0..3 {\n"
                        "    print(i)\n"
                        "  }\n"
                        "  print(i)\n"
                        "}\n",
                        DiagnosticCode::ShadowedVariable));
}

static void test_sibling_for_loops_do_not_shadow() {
    ASSERT_FALSE(reports("function void test() {\n"
                         "  for i in 0..3 {\n"
                         "    print(i)\n"
                         "  }\n"
                         "  for i in 0..3 {\n"
                         "    print(i)\n"
                         "  }\n"
                         "}\n",
                         DiagnosticCode::ShadowedVariable));
}

static void test_unreachable_code_warning() {
    ASSERT_TRUE(has_warnings("function integer test() {\n"
                             "  return 1\n"
                             "  integer x = 2\n"
                             "}\n"));
}

static void test_redundant_boolean_comparison_warning() {
    ASSERT_TRUE(has_warnings("boolean flag = true\n"
                             "boolean b = flag == true\n"));
}

// ─── Unused variable. ───

static void test_unused_variable_warns() {
    ASSERT_TRUE(reports("function integer test() {\n"
                        "  integer x = 42\n"
                        "  return 1\n"
                        "}\n",
                        DiagnosticCode::UnusedVariable));
}

static void test_unused_variable_no_warn_if_read() {
    ASSERT_FALSE(reports("function integer test() {\n"
                         "  integer x = 42\n"
                         "  return x\n"
                         "}\n",
                         DiagnosticCode::UnusedVariable));
}

static void test_unused_variable_underscore_suppressed() {
    ASSERT_FALSE(reports("function integer test() {\n"
                         "  integer _x = 42\n"
                         "  return 1\n"
                         "}\n",
                         DiagnosticCode::UnusedVariable));
}

// ─── Unused parameter. ───

static void test_unused_parameter_warns() {
    ASSERT_TRUE(reports("function integer add(integer a, integer b) {\n"
                        "  return a\n"
                        "}\n",
                        DiagnosticCode::UnusedParameter));
}

static void test_unused_parameter_no_warn_if_read() {
    ASSERT_FALSE(reports("function integer add(integer a, integer b) {\n"
                         "  return a + b\n"
                         "}\n",
                         DiagnosticCode::UnusedParameter));
}

static void test_unused_parameter_underscore_suppressed() {
    ASSERT_FALSE(reports("function integer add(integer a, integer _b) {\n"
                         "  return a\n"
                         "}\n",
                         DiagnosticCode::UnusedParameter));
}

// ─── Unused function (only with require_main=true). ───

static void test_unused_function_warns() {
    // Unused-function warnings only fire with require_main=true, so drive
    // check_warnings in that mode and pin the specific warning code.
    const auto warnings = check_warnings("@main\n"
                                         "function void main() { print(\"hi\") }\n"
                                         "function integer helper() { return 42 }\n",
                                         /*require_main=*/true);

    ASSERT_TRUE(has_code(warnings, DiagnosticCode::UnusedFunction));
}

static void test_unused_function_no_warn_if_called() {
    const auto warnings = check_warnings("@main\n"
                                         "function void main() {\n"
                                         "  integer x = helper()\n"
                                         "  print(x)\n"
                                         "}\n"
                                         "function integer helper() { return 42 }\n",
                                         /*require_main=*/true);

    ASSERT_TRUE(warnings.empty());
}

static void test_unused_function_no_warn_test_mode() {
    // In test mode (require_main=false), unused functions don't warn.
    ASSERT_FALSE(has_warnings("function integer helper() { return 42 }\n"));
}

// ─── Mutable but never mutated. ───

static void test_mutable_never_mutated_warns() {
    ASSERT_TRUE(reports("function integer test() {\n"
                        "  mutable integer x = 42\n"
                        "  return x\n"
                        "}\n",
                        DiagnosticCode::MutableNeverMutated));
}

static void test_mutable_mutated_no_warn() {
    ASSERT_FALSE(reports("function integer test() {\n"
                         "  mutable integer x = 0\n"
                         "  x = 42\n"
                         "  return x\n"
                         "}\n",
                         DiagnosticCode::MutableNeverMutated));
}

static void test_mutable_compound_assigned_no_warn() {
    ASSERT_FALSE(reports("function integer test() {\n"
                         "  mutable integer x = 0\n"
                         "  x += 1\n"
                         "  return x\n"
                         "}\n",
                         DiagnosticCode::MutableNeverMutated));
}

static void test_mutable_incremented_no_warn() {
    ASSERT_FALSE(reports("function integer test() {\n"
                         "  mutable integer x = 0\n"
                         "  x++\n"
                         "  return x\n"
                         "}\n",
                         DiagnosticCode::MutableNeverMutated));
}

static void test_mutable_param_never_mutated_warns() {
    ASSERT_TRUE(reports("function integer test(mutable integer x) {\n"
                        "  return x\n"
                        "}\n",
                        DiagnosticCode::MutableNeverMutated));
}

// A second declaration of the same type alias is reported, like a duplicate
// record, choice, interface, or function (the later one silently won before).
static void test_duplicate_type_alias_warns() {
    const auto warnings = check_warnings("type Twin = (integer, integer)\n"
                                         "type Twin = (string, string)\n");

    ASSERT_TRUE(std::ranges::any_of(warnings, [](const Diagnostic& d) {
        return d.message.find("type alias 'Twin' is already declared") != std::string::npos &&
               d.primary_location().line == 2;
    }));
}

// ─── Each variable-usage warning is reported exactly once. ───

static std::size_t count_code(const std::vector<Diagnostic>& warnings, DiagnosticCode code) {
    return static_cast<std::size_t>(
        std::ranges::count_if(warnings, [code](const Diagnostic& d) { return d.code == code; }));
}

static void test_variable_usage_warnings_not_duplicated() {
    const auto warnings =
        check_warnings_with_lint("function integer helper(integer unused_param) {\n"
                                 "  integer unused_local = 3\n"
                                 "  mutable integer never_mutated = 1\n"
                                 "  mutable integer same = 1\n"
                                 "  same = same\n"
                                 "  integer outer = 1\n"
                                 "  if outer > 0 {\n"
                                 "    integer outer = 2\n"
                                 "    return outer + never_mutated + same\n"
                                 "  }\n"
                                 "  return 0\n"
                                 "}\n");

    ASSERT_EQ(count_code(warnings, DiagnosticCode::UnusedVariable), std::size_t{1});
    ASSERT_EQ(count_code(warnings, DiagnosticCode::UnusedParameter), std::size_t{1});
    ASSERT_EQ(count_code(warnings, DiagnosticCode::MutableNeverMutated), std::size_t{1});
    ASSERT_EQ(count_code(warnings, DiagnosticCode::SelfAssignment), std::size_t{1});
    ASSERT_EQ(count_code(warnings, DiagnosticCode::ShadowedVariable), std::size_t{1});
}

// An unused match-arm binding is reported once, at the arm that introduced it.
static void test_unused_match_binding_reported_at_arm() {
    const auto warnings =
        check_warnings_with_lint("function integer pick(result<integer> outcome) {\n"
                                 "  match outcome {\n"
                                 "    success(value) { return 1 }\n"
                                 "    failure(_message) { return 0 }\n"
                                 "  }\n"
                                 "}\n");

    ASSERT_EQ(count_code(warnings, DiagnosticCode::UnusedVariable), std::size_t{1});

    const auto it = std::ranges::find_if(
        warnings, [](const Diagnostic& d) { return d.code == DiagnosticCode::UnusedVariable; });

    ASSERT_EQ(it->primary_location().line, 3);
}

// ─── Unreachable code after break. ───

static void test_unreachable_after_break_warns() {
    ASSERT_TRUE(has_warnings("function void test() {\n"
                             "  array<integer> a = [1, 2, 3]\n"
                             "  for item in a {\n"
                             "    break\n"
                             "    print(item)\n"
                             "  }\n"
                             "}\n"));
}

// ─── Unreachable code after continue. ───

static void test_unreachable_after_continue_warns() {
    ASSERT_TRUE(has_warnings("function void test() {\n"
                             "  array<integer> a = [1, 2, 3]\n"
                             "  for item in a {\n"
                             "    continue\n"
                             "    print(item)\n"
                             "  }\n"
                             "}\n"));
}

// ─── Unreachable code after a terminator inside a catch body. ───

static void test_unreachable_after_return_in_catch_warns() {
    // A catch body is linted like any other statement list: a terminator that
    // is not the last statement leaves the following statement unreachable.
    const auto warnings = check_warnings("function integer test() {\n"
                                         "  try {\n"
                                         "    return 1\n"
                                         "  } catch(err) {\n"
                                         "    print(err)\n"
                                         "    return 2\n"
                                         "    print(\"unreachable\")\n"
                                         "  }\n"
                                         "}\n");

    const bool warns_unreachable = std::ranges::any_of(warnings, [](const Diagnostic& d) {
        return d.message.find("unreachable code after return") != std::string::npos;
    });

    ASSERT_TRUE(warns_unreachable);
}

// ─── While-false dead code. ───

static void test_while_false_dead_code_warns() {
    ASSERT_TRUE(has_warnings("function void test() {\n"
                             "  while false {\n"
                             "    print(\"dead\")\n"
                             "  }\n"
                             "}\n"));
}

static void test_while_true_no_warn() {
    // while true is a common infinite loop pattern — no warning.
    ASSERT_FALSE(has_warnings("function void test() {\n"
                              "  while true {\n"
                              "    break\n"
                              "  }\n"
                              "}\n"));
}

// ─── Error hints ───

static void test_type_mismatch_hint_string_to_number() {
    auto errors = check("number x = \"hello\"\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].hint.has_value());
}

static void test_immutable_assignment_hint() {
    auto errors = check("integer x = 1\n"
                        "x = 2\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].hint.has_value());
}

static void test_reassignment_type_mismatch_hint() {
    // Reassigning a mutable variable with the wrong type should carry a hint.
    auto errors = check("mutable number x = 1.0\n"
                        "x = \"hello\"\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].hint.has_value());
}

static void test_compound_assignment_type_mismatch_hint() {
    // Compound assignment with incompatible types should carry a hint.
    auto errors = check("mutable number x = 1.0\n"
                        "x += \"hello\"\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].hint.has_value());
}

static void test_return_type_mismatch_hint() {
    auto errors = check("function number test() {\n"
                        "  return \"hello\"\n"
                        "}\n");

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(errors[0].hint.has_value());
}

int main() {
    // ─── Linter warnings ───

    RUN(test_shadow_variable_warns);
    RUN(test_underscore_prefix_no_shadow_warning);
    RUN(test_for_loop_variable_shadow_warning);
    RUN(test_sibling_for_loops_do_not_shadow);
    RUN(test_unreachable_code_warning);
    RUN(test_redundant_boolean_comparison_warning);

    // ─── Unused variable ───

    RUN(test_unused_variable_warns);
    RUN(test_unused_variable_no_warn_if_read);
    RUN(test_unused_variable_underscore_suppressed);

    // ─── Unused parameter ───

    RUN(test_unused_parameter_warns);
    RUN(test_unused_parameter_no_warn_if_read);
    RUN(test_unused_parameter_underscore_suppressed);

    // ─── Unused function (only with require_main=true) ───

    RUN(test_unused_function_warns);
    RUN(test_unused_function_no_warn_if_called);
    RUN(test_unused_function_no_warn_test_mode);

    // ─── Mutable but never mutated ───

    RUN(test_mutable_never_mutated_warns);
    RUN(test_mutable_mutated_no_warn);
    RUN(test_mutable_compound_assigned_no_warn);
    RUN(test_mutable_incremented_no_warn);
    RUN(test_mutable_param_never_mutated_warns);
    RUN(test_variable_usage_warnings_not_duplicated);
    RUN(test_duplicate_type_alias_warns);
    RUN(test_unused_match_binding_reported_at_arm);

    // ─── Unreachable code after break ───

    RUN(test_unreachable_after_break_warns);

    // ─── Unreachable code after continue ───

    RUN(test_unreachable_after_continue_warns);

    // ─── Unreachable code after a terminator inside a catch body ───

    RUN(test_unreachable_after_return_in_catch_warns);

    // ─── While-false dead code ───

    RUN(test_while_false_dead_code_warns);
    RUN(test_while_true_no_warn);

    // ─── Error hints ───

    RUN(test_type_mismatch_hint_string_to_number);
    RUN(test_immutable_assignment_hint);
    RUN(test_reassignment_type_mismatch_hint);
    RUN(test_compound_assignment_type_mismatch_hint);
    RUN(test_return_type_mismatch_hint);
    return SUMMARY();
}
