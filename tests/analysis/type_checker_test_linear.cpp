// Type checker unit tests — linear/affine types (unique, borrow) and typed error values.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <utility>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
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
    // A unique variable that is never consumed is a hard error (ownership is
    // enforced, not advisory).
    ASSERT_TRUE(fails("function void test() {\n"
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
    // Consumed only in then with no else → consumed afterwards: the implicit
    // empty else does not undo the move on the then path (as for if/else).
    ASSERT_TRUE(fails("function void consume(string s) { }\n"
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
    // consume it — the callee only borrows, so the value can still be
    // consumed afterwards.
    ASSERT_TRUE(passes("function void peek(borrow string s) { }\n"
                       "function void consume(string s) { }\n"
                       "unique string x = \"hello\"\n"
                       "peek(x)\n"
                       "peek(x)\n"
                       "consume(x)\n"));
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
    // same variable can be piped again and still consumed afterwards.
    ASSERT_TRUE(passes("function void peek(borrow string s) { string r = s }\n"
                       "function void consume(string s) { }\n"
                       "unique string x = \"hello\"\n"
                       "x |> peek()\n"
                       "x |> peek()\n"
                       "consume(x)\n"));
}

static void test_unique_captured_in_lambda_then_reused_blocked() {
    // Capturing a unique variable inside a closure consumes it (the closure
    // takes ownership of the captured value). Using the variable again after
    // the capture is a use-after-move error.
    ASSERT_TRUE(fails("function void consume(string s) { }\n"
                      "unique string x = \"hello\"\n"
                      "function() -> string grab = () -> x\n"
                      "consume(x)\n"));
}

static void test_unique_captured_in_lambda_single_use_ok() {
    // Capturing a unique variable exactly once (in a closure that is then
    // invoked) satisfies the consume-once rule.
    ASSERT_TRUE(passes("function void consume(string s) { }\n"
                       "unique string x = \"hello\"\n"
                       "function() -> void grab = () -> consume(x)\n"
                       "grab()\n"));
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

// ─── Regression: shadowed unique variables keep separate ownership ───

static void test_unique_shadowing_ownership_restored_per_variable() {
    // The inner x shadows the outer one; consuming the outer x before the
    // block must not make the inner x look consumed after the if merges
    // ownership (the shadowed outer entry used to overwrite the inner one).
    ASSERT_TRUE(passes("function void consume(unique string s) { }\n"
                       "function void run() {\n"
                       "    unique string x = \"outer\"\n"
                       "    consume(x)\n"
                       "    if true {\n"
                       "        unique string x = \"inner\"\n"
                       "        if true {\n"
                       "            print(\"branch\")\n"
                       "        }\n"
                       "        consume(x)\n"
                       "    }\n"
                       "}\n"));
}

static void test_unique_consumed_in_if_expression_branch_then_reused_fails() {
    ASSERT_TRUE(fails("function integer consume(unique string s) { return 1 }\n"
                      "unique string x = \"hello\"\n"
                      "integer n = if true { consume(x) } else { 0 }\n"
                      "consume(x)\n"));
}

// ─── Regression: spawn inside task_scope is checked once ───

static void test_spawn_unique_argument_in_task_scope_consumed_once() {
    // The spawned call used to be inferred twice, so its unique argument was
    // reported as used after being consumed.
    ASSERT_TRUE(passes("function integer work(unique string s) { return 1 }\n"
                       "unique string token = \"t\"\n"
                       "array<integer> results = task_scope {\n"
                       "    spawn work(token)\n"
                       "}\n"));
}

static void test_spawn_error_in_task_scope_reported_once() {
    const auto diagnostics = check("function integer work(integer n) { return n }\n"
                                   "array<integer> results = task_scope {\n"
                                   "    spawn work(\"text\")\n"
                                   "}\n");
    ASSERT_EQ(diagnostics.size(), std::size_t{1});
}

// ─── Regression: ownership snapshots scale with unique names only ───

namespace {

// A program with `functions` top-level functions and one function holding
// `ifs` if/else statements.
std::string make_branchy_program(int functions, int ifs) {
    std::string source;
    source.reserve(static_cast<std::size_t>(functions + ifs) * 72);

    for (int i{0}; i < functions; ++i) {
        source += std::format("function integer f{}(integer x) {{ return x + {} }}\n", i, i);
    }

    source += "function integer run(integer seed) {\n"
              "    mutable integer total = seed\n";

    for (int i{0}; i < ifs; ++i) {
        source +=
            std::format("    if total > {} {{ total = f{}(total) }} else {{ total = total + 1 }}\n",
                        i, i % functions);
    }

    source += "    return total\n"
              "}\n";

    return source;
}

// Fastest of a few type checks of `program`, in microseconds.
std::int64_t fastest_check_us(const Program& program) {
    std::int64_t best{std::numeric_limits<std::int64_t>::max()};

    for (int run{0}; run < 3; ++run) {
        TypeChecker checker;

        const auto start = std::chrono::steady_clock::now();
        const auto errors = checker.check(program, false);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        ASSERT_TRUE(errors.empty());
        best =
            std::min(best, std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
    }

    return best;
}

} // namespace

static void test_ownership_snapshots_do_not_scale_with_visible_names() {
    // Every if/else snapshots ownership a few times.  A snapshot used to copy
    // every visible name (all top-level functions, stdlib namespaces) into a
    // set, so checking N ifs with M functions in scope cost O(N × M).  Keep the
    // ifs fixed and add 3000 functions: a linear checker slows by roughly the
    // cost of checking those functions (about 2×), the quadratic one by well
    // over 10×.  A ratio, unlike a wall-clock bound, holds in Debug and
    // sanitizer builds too.
    constexpr int k_ifs{3000};

    const auto few_names = parse(make_branchy_program(100, k_ifs));
    const auto many_names = parse(make_branchy_program(3100, k_ifs));

    const auto few_us = fastest_check_us(few_names);
    const auto many_us = fastest_check_us(many_names);

    ASSERT_LT(many_us, few_us * 6 + 20'000);
}

// ─── Regression: branches that never fall through do not feed the merge ───

static void test_unique_consumed_in_returning_branch_usable_after_if() {
    ASSERT_TRUE(passes("function void process(unique string s) { print(s) }\n"
                       "function void handle(unique string token, boolean bad) {\n"
                       "    if bad {\n"
                       "        process(token)\n"
                       "        return\n"
                       "    }\n"
                       "    process(token)\n"
                       "}\n"));
}

static void test_unique_consumed_in_falling_through_branch_still_consumed() {
    // The then-branch falls through, so the value may have been moved.
    ASSERT_TRUE(fails("function void process(unique string s) { print(s) }\n"
                      "function void handle(unique string token, boolean bad) {\n"
                      "    if bad {\n"
                      "        process(token)\n"
                      "    }\n"
                      "    process(token)\n"
                      "}\n"));
}

static void test_unique_returning_else_branch_ignored_in_merge() {
    // Only the then-branch reaches the code after the if; it did not consume.
    ASSERT_TRUE(passes("function void process(unique string s) { print(s) }\n"
                       "function void handle(unique string token, boolean ok) {\n"
                       "    if ok {\n"
                       "        print(\"ok\")\n"
                       "    } else {\n"
                       "        process(token)\n"
                       "        return\n"
                       "    }\n"
                       "    process(token)\n"
                       "}\n"));
    // ...but when the falling-through branch consumed, the value is gone.
    ASSERT_TRUE(fails("function void process(unique string s) { print(s) }\n"
                      "function void handle(unique string token, boolean ok) {\n"
                      "    if ok {\n"
                      "        process(token)\n"
                      "    } else {\n"
                      "        return\n"
                      "    }\n"
                      "    process(token)\n"
                      "}\n"));
}

static void test_unique_consumed_in_returning_match_arm_usable_after_match() {
    ASSERT_TRUE(passes("function void process(unique string s) { print(s) }\n"
                       "function void handle(unique string token, integer kind) {\n"
                       "    match kind {\n"
                       "        case 1 {\n"
                       "            process(token)\n"
                       "            return\n"
                       "        }\n"
                       "        else {\n"
                       "            print(\"other\")\n"
                       "        }\n"
                       "    }\n"
                       "    process(token)\n"
                       "}\n"));
}

static void test_unique_consumed_in_returning_if_expression_branch_usable_after() {
    ASSERT_TRUE(passes("function integer process(unique string s) { return 1 }\n"
                       "function integer handle(unique string token, boolean bad) {\n"
                       "    integer n = if bad {\n"
                       "        integer ignored = process(token)\n"
                       "        return ignored\n"
                       "    } else {\n"
                       "        2\n"
                       "    }\n"
                       "    return process(token) + n\n"
                       "}\n"));
}

static void test_unique_consumed_before_break_leaves_loop_consumed() {
    // Consuming on the way out of the loop is not a per-iteration consumption...
    ASSERT_TRUE(passes("function void process(unique string s) { print(s) }\n"
                       "function void handle(unique string token, array<integer> xs) {\n"
                       "    for x in xs {\n"
                       "        if x > 2 {\n"
                       "            process(token)\n"
                       "            break\n"
                       "        }\n"
                       "    }\n"
                       "}\n"));
    // ...but the value may be gone after the loop.
    ASSERT_TRUE(fails("function void process(unique string s) { print(s) }\n"
                      "function void handle(unique string token, array<integer> xs) {\n"
                      "    for x in xs {\n"
                      "        if x > 2 {\n"
                      "            process(token)\n"
                      "            break\n"
                      "        }\n"
                      "    }\n"
                      "    process(token)\n"
                      "}\n"));
}

static void test_unique_consumed_before_continue_is_consumed_in_loop() {
    // `continue` carries the consumed state into the next iteration.
    ASSERT_TRUE(fails_with_message("function void process(unique string s) { print(s) }\n"
                                   "function void handle(unique string token, boolean b) {\n"
                                   "    mutable integer i = 0\n"
                                   "    while i < 3 {\n"
                                   "        i = i + 1\n"
                                   "        if b {\n"
                                   "            process(token)\n"
                                   "            continue\n"
                                   "        }\n"
                                   "    }\n"
                                   "}\n",
                                   "inside a loop"));
}

// ─── Regression: recorded spawn types never outlive a pass ───

static void test_spawn_types_not_reused_across_checks() {
    // The stray spawn (an error: outside any task_scope) used to leave its
    // node and result type recorded, and each task_scope searched every
    // recorded entry.  Swapping the two spawn nodes (keeping each call where
    // it was) makes the stale entry's node the task_scope's spawn in the next
    // check, which then saw the stale string type instead of integer.
    auto program = parse("function integer work_int() { return 1 }\n"
                         "function string work_str() { return \"s\" }\n"
                         "task<string> stray = spawn work_str()\n"
                         "array<integer> results = task_scope {\n"
                         "    spawn work_int()\n"
                         "}\n");

    auto& stray_init = static_cast<VariableDeclStatement&>(*program.statements[0]).initializer;
    auto& scope = static_cast<TaskScopeExpression&>(
        *static_cast<VariableDeclStatement&>(*program.statements[1]).initializer);
    auto& scoped_spawn = static_cast<ExpressionStatement&>(*scope.body[0]).expression;

    const auto swap_spawn_nodes = [&] {
        std::swap(stray_init, scoped_spawn);
        std::swap(static_cast<SpawnExpression&>(*stray_init).call,
                  static_cast<SpawnExpression&>(*scoped_spawn).call);
    };

    TypeChecker checker;
    const auto first = checker.check(program, false);
    swap_spawn_nodes();
    const auto second = checker.check(program, false);

    ASSERT_EQ(second.size(), first.size());

    TypeChecker incremental;
    const auto first_input = incremental.check_incremental(program);
    swap_spawn_nodes();
    const auto second_input = incremental.check_incremental(program);

    ASSERT_EQ(second_input.size(), first_input.size());
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
    RUN(test_unique_captured_in_lambda_then_reused_blocked);
    RUN(test_unique_captured_in_lambda_single_use_ok);
    RUN(test_borrow_field_read_ok);

    // ─── Typed error values: result<T, E> ───

    RUN(test_result_two_type_params_valid);
    RUN(test_result_two_type_params_fail);

    // ─── Regression: ownership snapshots, spawn checked once ───

    RUN(test_unique_shadowing_ownership_restored_per_variable);
    RUN(test_unique_consumed_in_if_expression_branch_then_reused_fails);
    RUN(test_spawn_unique_argument_in_task_scope_consumed_once);
    RUN(test_spawn_error_in_task_scope_reported_once);

    // ─── Regression: snapshot cost, non-falling-through branches, spawn reset ───

    RUN(test_ownership_snapshots_do_not_scale_with_visible_names);
    RUN(test_unique_consumed_in_returning_branch_usable_after_if);
    RUN(test_unique_consumed_in_falling_through_branch_still_consumed);
    RUN(test_unique_returning_else_branch_ignored_in_merge);
    RUN(test_unique_consumed_in_returning_match_arm_usable_after_match);
    RUN(test_unique_consumed_in_returning_if_expression_branch_usable_after);
    RUN(test_unique_consumed_before_break_leaves_loop_consumed);
    RUN(test_unique_consumed_before_continue_is_consumed_in_loop);
    RUN(test_spawn_types_not_reused_across_checks);
    return SUMMARY();
}
