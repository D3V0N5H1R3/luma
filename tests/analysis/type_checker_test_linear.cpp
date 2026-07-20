// Type checker unit tests — linear/affine types (unique, borrow) and typed error values.

#include "type_checker_test_helpers.hpp"

// ─── Linear/Affine types ───

static void test_unique_variable_consumed() {
    // Using a unique variable should mark it consumed.
    // Accessing it again should error.
    ASSERT_TRUE(fails("function void consume(string s) { }\n"
                      "unique string x = \"hello\"\n"
                      "consume(x)\n"
                      "consume(x)\n"));
}

static void test_unique_variable_scope_exit_warning() {
    // A unique variable that is never consumed should warn.
    ASSERT_TRUE(has_warnings("function void test() {\n"
                             "  unique string x = \"hello\"\n"
                             "}\n"));
}

static void test_unique_variable_single_use_ok() {
    // Using a unique variable exactly once should pass.
    ASSERT_TRUE(passes("function void consume(string s) { }\n"
                       "unique string x = \"hello\"\n"
                       "consume(x)\n"));
}

static void test_unique_param_consumed_in_callee() {
    // A unique parameter inside a function should be tracked.
    ASSERT_TRUE(fails("function void process(unique string s) {\n"
                      "  string a = s\n"
                      "  string b = s\n"
                      "}\n"));
}

static void test_unique_param_single_use_ok() {
    ASSERT_TRUE(passes("function string process(unique string s) {\n"
                       "  return s\n"
                       "}\n"));
}

static void test_borrow_variable_readable_ok() {
    ASSERT_TRUE(passes("borrow string x = \"hello\"\n"
                       "string y = x\n"));
}

static void test_borrow_variable_not_consumed() {
    // A borrow variable should be readable multiple times
    // (it is not consumed on access).
    ASSERT_TRUE(passes("function void consume(string s) { }\n"
                       "borrow string x = \"hello\"\n"
                       "consume(x)\n"
                       "consume(x)\n"));
}

static void test_borrow_assignment_blocked() {
    // Cannot assign to a borrow variable.
    ASSERT_TRUE(fails("mutable borrow string x = \"hello\"\n"
                      "x = \"world\"\n"));
}

static void test_borrow_compound_assignment_blocked() {
    // Cannot compound-assign to a borrow variable.
    ASSERT_TRUE(fails("mutable borrow integer x = 1\n"
                      "x += 1\n"));
}

static void test_borrow_param_not_consumed() {
    // A borrow parameter can be used multiple times.
    ASSERT_TRUE(passes("function void peek(borrow string s) {\n"
                       "  string a = s\n"
                       "  string b = s\n"
                       "}\n"));
}

static void test_underscore_unique_no_warning() {
    // _-prefixed unique variables suppress unconsumed warning.
    ASSERT_FALSE(has_warnings("function void test() {\n"
                              "  unique string _ignored = \"hello\"\n"
                              "}\n"));
}

static void test_unique_consumed_in_if_both_branches() {
    // Consumed in both branches → consumed after if/else.
    ASSERT_TRUE(fails("function void consume(string s) { }\n"
                      "unique string x = \"hello\"\n"
                      "if true {\n"
                      "  consume(x)\n"
                      "} else {\n"
                      "  consume(x)\n"
                      "}\n"
                      "consume(x)\n"));
}

static void test_unique_consumed_in_if_one_branch_still_usable() {
    // Consumed in only one branch with else → conservatively consumed.
    ASSERT_TRUE(fails("function void consume(string s) { }\n"
                      "unique string x = \"hello\"\n"
                      "if true {\n"
                      "  consume(x)\n"
                      "} else {\n"
                      "  string y = \"noop\"\n"
                      "}\n"
                      "consume(x)\n"));
}

static void test_unique_consumed_in_if_no_else_restored() {
    // Consumed only in then with no else → restored (not reliably consumed).
    ASSERT_TRUE(passes("function void consume(string s) { }\n"
                       "unique string x = \"hello\"\n"
                       "if true {\n"
                       "  consume(x)\n"
                       "}\n"
                       "consume(x)\n"));
}

static void test_unique_consumed_in_match_arm() {
    // Consumed in a match arm → conservatively consumed after.
    ASSERT_TRUE(fails("function void consume(string s) { }\n"
                      "unique string x = \"hello\"\n"
                      "boolean flag = true\n"
                      "match flag {\n"
                      "  case true { consume(x) }\n"
                      "  case false { }\n"
                      "}\n"
                      "consume(x)\n"));
}

static void test_borrow_passed_to_unique_param_blocked() {
    // Cannot pass a borrow variable to a unique parameter.
    ASSERT_TRUE(fails("function void consume(unique string s) { }\n"
                      "borrow string x = \"hello\"\n"
                      "consume(x)\n"));
}

static void test_borrow_piped_to_unique_param_blocked() {
    // Piping a borrowed variable into a unique parameter is blocked too —
    // the pipe target still takes ownership of its first argument.
    ASSERT_TRUE(fails("function void consume(unique string s) { }\n"
                      "borrow string x = \"hello\"\n"
                      "x |> consume()\n"));
}

static void test_unique_passed_to_borrow_param_not_consumed() {
    // Passing a unique variable to a borrow parameter should NOT
    // consume it — the callee only borrows.
    ASSERT_TRUE(passes("function void peek(borrow string s) { }\n"
                       "unique string x = \"hello\"\n"
                       "peek(x)\n"
                       "peek(x)\n"));
}

static void test_unique_consumed_in_while_loop_blocked() {
    // Consuming a unique variable inside a loop is an error.
    ASSERT_TRUE(fails("function void consume(string s) { }\n"
                      "unique string x = \"hello\"\n"
                      "while true {\n"
                      "  consume(x)\n"
                      "}\n"));
}

static void test_unique_consumed_in_for_loop_blocked() {
    // Consuming a unique variable inside a for loop is an error.
    ASSERT_TRUE(fails("function void consume(string s) { }\n"
                      "unique string x = \"hello\"\n"
                      "array<integer> arr = [1, 2, 3]\n"
                      "for item in arr {\n"
                      "  consume(x)\n"
                      "}\n"));
}

static void test_unique_not_consumed_after_loop() {
    // A unique variable NOT consumed in a loop remains available.
    ASSERT_TRUE(passes("function void consume(string s) { }\n"
                       "unique string x = \"hello\"\n"
                       "while false {\n"
                       "  integer y = 1\n"
                       "}\n"
                       "consume(x)\n"));
}

static void test_borrow_increment_blocked() {
    // Cannot increment a borrow variable.
    ASSERT_TRUE(fails("mutable borrow integer x = 1\n"
                      "x++\n"));
}

static void test_borrow_decrement_blocked() {
    // Cannot decrement a borrow variable.
    ASSERT_TRUE(fails("mutable borrow integer x = 1\n"
                      "x--\n"));
}

static void test_borrow_field_assignment_blocked() {
    // Cannot assign to a field of a borrowed variable.
    ASSERT_TRUE(fails("record Point { integer x, integer y }\n"
                      "borrow Point p = Point { x = 1, y = 2 }\n"
                      "p.x = 42\n"));
}

static void test_borrow_field_compound_assignment_blocked() {
    // Cannot compound-assign to a field of a borrowed variable.
    ASSERT_TRUE(fails("record Point { integer x, integer y }\n"
                      "borrow Point p = Point { x = 1, y = 2 }\n"
                      "p.x += 1\n"));
}

static void test_borrow_index_assignment_blocked() {
    // Cannot assign to an element of a borrowed array.
    ASSERT_TRUE(fails("mutable array<integer> a = [1, 2, 3]\n"
                      "borrow array<integer> b = a\n"
                      "b[0] = 42\n"));
}

static void test_borrow_index_compound_assignment_blocked() {
    // Cannot compound-assign to an element of a borrowed array.
    ASSERT_TRUE(fails("mutable array<integer> a = [1, 2, 3]\n"
                      "borrow array<integer> b = a\n"
                      "b[0] += 1\n"));
}

static void test_assign_to_consumed_unique_blocked() {
    // Re-assigning a unique variable after it has been consumed is an error —
    // the value was already moved away.
    ASSERT_TRUE(fails("function void consume(string s) { }\n"
                      "mutable unique string x = \"hello\"\n"
                      "consume(x)\n"
                      "x = \"world\"\n"));
}

static void test_unique_piped_to_unique_param_consumes() {
    // Piping a unique variable into a consuming parameter moves it; piping it
    // again must fail because the value is already consumed.
    ASSERT_TRUE(fails("function string take(unique string s) { return s }\n"
                      "unique string x = \"hello\"\n"
                      "string a = x |> take()\n"
                      "string b = x |> take()\n"));
}

static void test_function_returns_unique_consumed_once() {
    // A function may return a unique value; consuming the result exactly once
    // is valid.
    ASSERT_TRUE(passes("function unique string acquire() { return \"res\" }\n"
                       "function void consume(unique string s) { }\n"
                       "unique string x = acquire()\n"
                       "consume(x)\n"));
}

static void test_unique_piped_to_borrow_param_not_consumed() {
    // Piping a unique variable into a borrow parameter only lends it, so the
    // same variable can be piped again.
    ASSERT_TRUE(passes("function void peek(borrow string s) { string r = s }\n"
                       "unique string x = \"hello\"\n"
                       "x |> peek()\n"
                       "x |> peek()\n"));
}

static void test_borrow_field_read_ok() {
    // Reading a field of a borrowed record is allowed (borrow is read-only,
    // not unreadable).
    ASSERT_TRUE(passes("record Point { integer x, integer y }\n"
                       "borrow Point p = Point { x = 1, y = 2 }\n"
                       "integer a = p.x\n"
                       "integer b = p.y\n"));
}

// ─── Typed error values: result<T, E> ───

static void test_result_two_type_params_valid() {
    ASSERT_TRUE(passes("result<integer, string> x = success(42)\n"));
}

static void test_result_two_type_params_fail() {
    ASSERT_TRUE(passes("result<integer, string> x = failure(\"oops\")\n"));
}

int main() {
    // ─── Linear/Affine types ───

    RUN(test_unique_variable_consumed);
    RUN(test_unique_variable_scope_exit_warning);
    RUN(test_unique_variable_single_use_ok);
    RUN(test_unique_param_consumed_in_callee);
    RUN(test_unique_param_single_use_ok);
    RUN(test_borrow_variable_readable_ok);
    RUN(test_borrow_variable_not_consumed);
    RUN(test_borrow_assignment_blocked);
    RUN(test_borrow_compound_assignment_blocked);
    RUN(test_borrow_param_not_consumed);
    RUN(test_underscore_unique_no_warning);
    RUN(test_unique_consumed_in_if_both_branches);
    RUN(test_unique_consumed_in_if_one_branch_still_usable);
    RUN(test_unique_consumed_in_if_no_else_restored);
    RUN(test_unique_consumed_in_match_arm);
    RUN(test_borrow_passed_to_unique_param_blocked);
    RUN(test_borrow_piped_to_unique_param_blocked);
    RUN(test_unique_passed_to_borrow_param_not_consumed);
    RUN(test_unique_consumed_in_while_loop_blocked);
    RUN(test_unique_consumed_in_for_loop_blocked);
    RUN(test_unique_not_consumed_after_loop);
    RUN(test_borrow_increment_blocked);
    RUN(test_borrow_decrement_blocked);
    RUN(test_borrow_field_assignment_blocked);
    RUN(test_borrow_field_compound_assignment_blocked);
    RUN(test_borrow_index_assignment_blocked);
    RUN(test_borrow_index_compound_assignment_blocked);
    RUN(test_assign_to_consumed_unique_blocked);
    RUN(test_unique_piped_to_unique_param_consumes);
    RUN(test_function_returns_unique_consumed_once);
    RUN(test_unique_piped_to_borrow_param_not_consumed);
    RUN(test_borrow_field_read_ok);

    // ─── Typed error values: result<T, E> ───

    RUN(test_result_two_type_params_valid);
    RUN(test_result_two_type_params_fail);
    return SUMMARY();
}
