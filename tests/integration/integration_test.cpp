// Full-pipeline integration tests — exercise the entire
// Lexer → Parser → TypeChecker → Linter → Compiler → VM chain.
//
// Coverage: arithmetic, functions, closures, records, choice/result/optional
// matching, pipes, named arguments, is<T>/downcast, tail calls, error
// propagation (stack overflow, syntax errors, division-by-zero, undefined
// variables), structured concurrency (task_scope/spawn/await/channel), and
// whitespace invariance.
//
// Remaining gaps (higher-value integration seams still to add):
//   - Linter warnings surfaced through the pipeline (shared_eval currently runs
//     the linter but discards its warnings).
//   - Compilation-cache reuse and invalidation across runs.
//   - Error-reporter rendering (formatted diagnostics with source spans).

#include <cstdint>
#include <exception>
#include <string>

#include "shared_eval.hpp"
#include "test_framework.hpp"

using namespace luma;

// The checked pipeline (lex → parse → type check → lint → compile → VM). Named
// explicitly to avoid confusion with luma::test::eval, which skips the analysis
// passes; both share the `eval` root but mean opposite things.
using luma::test::eval_checked;

// ─── Assertion helpers ───
// Run the checked pipeline and assert the result's type and value. Failures
// throw, and the enclosing RUN(test_name) still identifies the failing case.

static void assert_eval_int(const std::string& source, std::int64_t expected) {
    const auto val = eval_checked(source);
    ASSERT_TRUE(val.is_integer());
    ASSERT_EQ(val.as_integer(), expected);
}

static void assert_eval_str(const std::string& source, const std::string& expected) {
    const auto val = eval_checked(source);
    ASSERT_TRUE(val.is_string());
    ASSERT_EQ(val.as_string(), expected);
}

static void assert_eval_bool(const std::string& source, bool expected) {
    const auto val = eval_checked(source);
    ASSERT_TRUE(val.is_bool());
    ASSERT_EQ(val.as_bool(), expected);
}

static void assert_eval_num(const std::string& source, double expected, double epsilon = 0.001) {
    const auto val = eval_checked(source);
    ASSERT_TRUE(val.is_number());
    ASSERT_NEAR(val.as_number(), expected, epsilon);
}

// ─── Tests ───

static void test_integer_arithmetic() {
    assert_eval_int("2 + 3", 5);
}

static void test_string_interpolation() {
    assert_eval_str("integer x = 42\n"
                    "\"answer is ${x}\"\n",
                    "answer is 42");
}

static void test_function_definition_and_call() {
    assert_eval_int("function integer add(integer a, integer b) {\n"
                    "    return a + b\n"
                    "}\n"
                    "add(3, 4)\n",
                    7);
}

static void test_array_operations() {
    assert_eval_int("array<integer> arr = [1, 2, 3, 4, 5]\n"
                    "Array.length(arr)\n",
                    5);
}

static void test_match_expression() {
    assert_eval_str("function string classify(integer x) {\n"
                    "    return match x {\n"
                    "        case 0 { \"zero\" }\n"
                    "        case 42 { \"answer\" }\n"
                    "        else { \"other\" }\n"
                    "    }\n"
                    "}\n"
                    "classify(42)\n",
                    "answer");
}

static void test_record_creation() {
    assert_eval_int("record Point {\n"
                    "    integer x,\n"
                    "    integer y\n"
                    "}\n"
                    "function integer point_sum() {\n"
                    "    Point p = Point { x = 10, y = 20 }\n"
                    "    return p.x + p.y\n"
                    "}\n"
                    "point_sum()\n",
                    30);
}

static void test_for_loop() {
    assert_eval_int("function integer sum_range() {\n"
                    "    mutable integer sum = 0\n"
                    "    for i in 1..6 {\n"
                    "        sum = sum + i\n"
                    "    }\n"
                    "    return sum\n"
                    "}\n"
                    "sum_range()\n",
                    15);
}

static void test_tail_call() {
    assert_eval_int("function integer sum_to(integer n, integer acc) {\n"
                    "    if n <= 0 { return acc }\n"
                    "    return sum_to(n - 1, acc + n)\n"
                    "}\n"
                    "sum_to(200, 0)\n",
                    20100);
}

static void test_pipe_operator() {
    assert_eval_int("function integer double(integer x) {\n"
                    "    return x * 2\n"
                    "}\n"
                    "5 |> double()\n",
                    10);
}

static void test_type_error_detected() {
    ASSERT_THROWS(eval_checked("integer x = \"hello\"\n"));
}

static void test_boolean_logic() {
    assert_eval_bool("boolean a = true\n"
                     "boolean b = false\n"
                     "a && !b\n",
                     true);
}

static void test_string_operations() {
    assert_eval_int("String.length(\"hello\")\n", 5);
}

// ─── Additional integration tests ───

static void test_closure_captures() {
    assert_eval_int("function function(integer) -> integer make_adder(integer n) {\n"
                    "    return (integer x) -> n + x\n"
                    "}\n"
                    "function(integer) -> integer add5 = make_adder(5)\n"
                    "add5(3)\n",
                    8);
}

static void test_choice_type_match() {
    // Test basic choice type with simple variant matching.
    assert_eval_str("choice Color {\n"
                    "    Red,\n"
                    "    Green,\n"
                    "    Blue\n"
                    "}\n"
                    "function string name(Color c) {\n"
                    "    return match c {\n"
                    "        case Color.Red { \"red\" }\n"
                    "        case Color.Green { \"green\" }\n"
                    "        case Color.Blue { \"blue\" }\n"
                    "    }\n"
                    "}\n"
                    "name(Color.Red)\n",
                    "red");
}

static void test_result_success_pipeline() {
    assert_eval_str("function result<integer> parse(string s) {\n"
                    "    return success(42)\n"
                    "}\n"
                    "function string describe(result<integer> r) {\n"
                    "    return match r {\n"
                    "        success(v) { \"got ${v}\" }\n"
                    "        failure(e) { \"error\" }\n"
                    "    }\n"
                    "}\n"
                    "describe(parse(\"42\"))\n",
                    "got 42");
}

static void test_result_failure_pipeline() {
    assert_eval_str("function result<integer> parse(string s) {\n"
                    "    return failure(\"bad input\")\n"
                    "}\n"
                    "function string describe(result<integer> r) {\n"
                    "    return match r {\n"
                    "        success(v) { \"got ${v}\" }\n"
                    "        failure(e) { \"error: ${e}\" }\n"
                    "    }\n"
                    "}\n"
                    "describe(parse(\"abc\"))\n",
                    "error: bad input");
}

// Regression: a match used as the tail value of an enclosing match arm must
// yield the inner match's value, not none. Previously compile_body_as_expression
// compiled a trailing match statement as a value-less statement, silently
// producing none.
static void test_nested_match_arm_value() {
    assert_eval_str("choice MathError { DivByZero Overflow }\n"
                    "function string classify(result<integer, MathError> r) {\n"
                    "    return match r {\n"
                    "        success(v) { \"ok ${v}\" }\n"
                    "        failure(e) {\n"
                    "            match e {\n"
                    "            case MathError.DivByZero { \"div0\" }\n"
                    "            case MathError.Overflow  { \"overflow\" }\n"
                    "            }\n"
                    "        }\n"
                    "    }\n"
                    "}\n"
                    "classify(failure(MathError.DivByZero))\n",
                    "div0");
}

static void test_optional_some_none() {
    assert_eval_int("function optional<integer> find(integer x) {\n"
                    "    if x > 0 { return some(x) }\n"
                    "    return none\n"
                    "}\n"
                    "function integer get_or_default(optional<integer> o) {\n"
                    "    return o ?? -1\n"
                    "}\n"
                    "get_or_default(find(0))\n",
                    -1);
}

static void test_record_with_methods() {
    assert_eval_int("record Point { integer x, integer y }\n"
                    "function integer abs(integer n) {\n"
                    "    if n < 0 { return -n }\n"
                    "    return n\n"
                    "}\n"
                    "function integer manhattan(Point a, Point b) {\n"
                    "    return abs(a.x - b.x) + abs(a.y - b.y)\n"
                    "}\n"
                    "function integer f() {\n"
                    "    Point p1 = Point { x = 1, y = 2 }\n"
                    "    Point p2 = Point { x = 4, y = 6 }\n"
                    "    return manhattan(p1, p2)\n"
                    "}\n"
                    "f()\n",
                    7);
}

static void test_record_with_expression() {
    assert_eval_int("record Point { integer x, integer y }\n"
                    "function integer f() {\n"
                    "    Point p1 = Point { x = 1, y = 2 }\n"
                    "    Point p2 = p1 with { x = 10 }\n"
                    "    return p2.x + p2.y\n"
                    "}\n"
                    "f()\n",
                    12);
}

static void test_nested_function_calls() {
    assert_eval_int("function integer square(integer x) { return x * x }\n"
                    "function integer add(integer a, integer b) { return a + b }\n"
                    "add(square(3), square(4))\n",
                    25);
}

static void test_pipe_chain() {
    assert_eval_int("function integer double(integer x) { return x * 2 }\n"
                    "function integer increment(integer x) { return x + 1 }\n"
                    "5 |> double() |> increment()\n",
                    11);
}

static void test_dictionary_operations() {
    assert_eval_int("function integer f() {\n"
                    "    dictionary<integer> d = {\"a\": 1, \"b\": 2, \"c\": 3}\n"
                    "    return d[\"a\"] + d[\"c\"]\n"
                    "}\n"
                    "f()\n",
                    4);
}

static void test_namespace_function_call() {
    assert_eval_int("namespace Utils {\n"
                    "    function integer add(integer a, integer b) {\n"
                    "        return a + b\n"
                    "    }\n"
                    "}\n"
                    "Utils.add(10, 20)\n",
                    30);
}

static void test_try_catch_execution() {
    assert_eval_int("function integer safe_div(integer a, integer b) {\n"
                    "    try {\n"
                    "        return a // b\n"
                    "    } catch(e) {\n"
                    "        return -1\n"
                    "    }\n"
                    "}\n"
                    "safe_div(10, 0)\n",
                    -1);
}

static void test_for_in_with_stdlib() {
    assert_eval_int("function integer f() {\n"
                    "    mutable integer sum = 0\n"
                    "    array<integer> nums = [10, 20, 30]\n"
                    "    for n in nums {\n"
                    "        sum = sum + n\n"
                    "    }\n"
                    "    return sum\n"
                    "}\n"
                    "f()\n",
                    60);
}

static void test_recursive_fibonacci() {
    assert_eval_int("function integer fib(integer n) {\n"
                    "    if n <= 1 { return n }\n"
                    "    return fib(n - 1) + fib(n - 2)\n"
                    "}\n"
                    "fib(10)\n",
                    55);
}

static void test_higher_order_function() {
    assert_eval_int("function integer apply(function(integer) -> integer f, integer x) {\n"
                    "    return f(x)\n"
                    "}\n"
                    "function integer triple(integer x) { return x * 3 }\n"
                    "apply(triple, 7)\n",
                    21);
}

static void test_contains_operator() {
    assert_eval_bool("3 in [1, 2, 3, 4, 5]\n", true);
}

static void test_contains_range_operator() {
    assert_eval_bool("50 in 1..=100\n", true);
    assert_eval_bool("100 in 1..100\n", false);
    assert_eval_bool("0 in 1..=100\n", false);
}

// ═══════════════════════════════════════════════════════════
// Named arguments (runtime)
// ═══════════════════════════════════════════════════════════

static void test_named_arguments_all() {
    assert_eval_int("function integer add(integer a, integer b) { return a + b }\n"
                    "add(a: 10, b: 20)\n",
                    30);
}

static void test_named_arguments_mixed() {
    assert_eval_int(
        "function integer calc(integer x, integer y, integer z) { return x * 100 + y * 10 + z }\n"
        "calc(5, z: 3, y: 7)\n",
        573);
}

static void test_named_arguments_reorder() {
    assert_eval_int("function integer f(integer a, integer b) { return a - b }\n"
                    "f(b: 10, a: 30)\n",
                    20);
}

// ═══════════════════════════════════════════════════════════
// Downcast / IsType (runtime)
// ═══════════════════════════════════════════════════════════

static void test_is_type_true() {
    assert_eval_bool("is<integer>(42)\n", true);
}

static void test_is_type_false() {
    assert_eval_bool("is<string>(42)\n", false);
}

static void test_is_type_number_widens_integer() {
    assert_eval_bool("is<number>(42)\n", true);
}

static void test_downcast_success() {
    assert_eval_bool("result<integer> r = downcast<integer>(42)\n"
                     "Result.is_success(r)\n",
                     true);
}

static void test_downcast_failure() {
    assert_eval_bool("result<string> r = downcast<string>(42)\n"
                     "Result.is_success(r)\n",
                     false);
}

// ═══════════════════════════════════════════════════════════
// Clone (mutable value isolation)
// ═══════════════════════════════════════════════════════════

static void test_mutable_clone_isolation() {
    assert_eval_int("function integer f() {\n"
                    "    array<integer> original = [1, 2, 3]\n"
                    "    mutable array<integer> copy = original\n"
                    "    copy[0] = 99\n"
                    "    return original[0]\n"
                    "}\n"
                    "f()\n",
                    1);
}

// ═══════════════════════════════════════════════════════════
// IntToNumber (implicit widening)
// ═══════════════════════════════════════════════════════════

static void test_int_to_number_widening() {
    assert_eval_num("number x = 42\nx\n", 42.0);
}

// ═══════════════════════════════════════════════════════════
// Rethrow (catch + re-throw)
// ═══════════════════════════════════════════════════════════

static void test_rethrow_propagates() {
    // try-finally: error in try body is rethrown after finally runs.
    assert_eval_str("function string f() {\n"
                    "    mutable string log = \"before\"\n"
                    "    try {\n"
                    "        try {\n"
                    "            integer n = 0\n"
                    "            integer x = 1 / n\n"
                    "        } finally {\n"
                    "            log = \"finally ran\"\n"
                    "        }\n"
                    "    } catch(e) {\n"
                    "        return log\n"
                    "    }\n"
                    "    return \"no error\"\n"
                    "}\n"
                    "f()\n",
                    "finally ran");
}

// ═══════════════════════════════════════════════════════════
// Tail call optimization (runtime)
// ═══════════════════════════════════════════════════════════

static void test_tail_call_deep_recursion() {
    // This would overflow the stack without tail call optimization.
    assert_eval_int("function integer loop(integer n, integer acc) {\n"
                    "    if n <= 0 { return acc }\n"
                    "    return loop(n - 1, acc + 1)\n"
                    "}\n"
                    "loop(1000, 0)\n",
                    1000);
}

// ═══════════════════════════════════════════════════════════
// Mutable parameters (value semantics)
// ═══════════════════════════════════════════════════════════

static void test_mutable_parameter_value_semantics() {
    // A `mutable` parameter may be reassigned inside the function, but the
    // argument is passed by value: the caller's variable stays unchanged.
    // Encodes both values as original*1000 + bumped → 5*1000 + 105.
    assert_eval_int("function integer bump(mutable integer n) {\n"
                    "    n = n + 100\n"
                    "    return n\n"
                    "}\n"
                    "integer original = 5\n"
                    "integer bumped = bump(original)\n"
                    "original * 1000 + bumped\n",
                    5105);
}

// ═══════════════════════════════════════════════════════════
// Structured concurrency (task_scope / spawn / await / channel)
// ═══════════════════════════════════════════════════════════

static void test_concurrency_task_scope_collects_results() {
    // A task_scope block collects the results of its spawned tasks into an
    // array, in spawn order, joining before the block yields.
    assert_eval_int("function integer square(integer x) { return x * x }\n"
                    "function integer f() {\n"
                    "    array<integer> results = task_scope {\n"
                    "        spawn square(3)\n"
                    "        spawn square(4)\n"
                    "        spawn square(5)\n"
                    "    }\n"
                    "    return results[0] + results[1] + results[2]\n"
                    "}\n"
                    "f()\n",
                    50);
}

static void test_concurrency_task_await() {
    // spawn schedules a task; await blocks for and returns its result.
    assert_eval_int("function integer f() {\n"
                    "    task<integer> t = spawn String.length(\"hello\")\n"
                    "    return await t\n"
                    "}\n"
                    "f()\n",
                    5);
}

static void test_concurrency_channel_send_receive() {
    // A channel carries values FIFO; receive returns a result<T> that unwraps
    // to the sent value.
    assert_eval_int("function integer f() {\n"
                    "    channel<integer> ch = Channel.new()\n"
                    "    boolean _s1 = Channel.send(ch, 10)\n"
                    "    boolean _s2 = Channel.send(ch, 20)\n"
                    "    result<integer> a = Channel.receive(ch)\n"
                    "    result<integer> b = Channel.receive(ch)\n"
                    "    return Result.unwrap(a) + Result.unwrap(b)\n"
                    "}\n"
                    "f()\n",
                    30);
}

// ═══════════════════════════════════════════════════════════
// Whitespace invariance (lexer/parser robustness to formatting)
// ═══════════════════════════════════════════════════════════

static void test_whitespace_variations_preserve_semantics() {
    // The same program written with minimal and with conventional whitespace
    // must lex, parse, type-check, compile, and run to the same value — the
    // pipeline is insensitive to source formatting.
    const std::string compact = "function integer add(integer a,integer b){return a+b}\n"
                                "add(3,4)\n";
    const std::string spaced = "function integer add(integer a, integer b) {\n"
                               "    return a + b\n"
                               "}\n"
                               "add(3, 4)\n";

    const auto compact_value = eval_checked(compact);
    const auto spaced_value = eval_checked(spaced);
    ASSERT_TRUE(compact_value.is_integer());
    ASSERT_TRUE(spaced_value.is_integer());
    ASSERT_EQ(compact_value.as_integer(), spaced_value.as_integer());
    ASSERT_EQ(compact_value.as_integer(), 7);
}

// ─── Error-path integration tests ───

static void test_stack_overflow_reports_error() {
    // A deeply recursive non-tail-call function should raise a stack overflow
    // or runtime error rather than crashing the process.
    ASSERT_THROWS(eval_checked("function integer recurse(integer n) {\n"
                               "    return recurse(n + 1) + 1\n"
                               "}\n"
                               "recurse(0)\n"));
}

static void test_syntax_error_reports_diagnostic() {
    // A syntax error should be caught during parsing/type-checking,
    // producing a meaningful exception.
    bool caught = false;

    try {
        (void)eval_checked("function integer broken( {\n"
                           "    return 42\n"
                           "}\n");
    } catch (const std::exception& e) {
        caught = true;
        // The error message should not be empty.
        ASSERT_TRUE(std::string{e.what()}.size() > 0);
    }

    ASSERT_TRUE(caught);
}

static void test_division_by_zero_caught() {
    // Division by zero at runtime should throw, not crash.
    ASSERT_THROWS(eval_checked("1 / 0\n"));
}

static void test_undefined_variable_error() {
    // Reference to an undefined variable should produce a type error.
    ASSERT_THROWS(eval_checked("integer x = undefined_variable\n"));
}

// ─── Main ───

int main() {
    RUN(test_integer_arithmetic);
    RUN(test_string_interpolation);
    RUN(test_function_definition_and_call);
    RUN(test_array_operations);
    RUN(test_match_expression);
    RUN(test_record_creation);
    RUN(test_for_loop);
    RUN(test_tail_call);
    RUN(test_pipe_operator);
    RUN(test_type_error_detected);
    RUN(test_boolean_logic);
    RUN(test_string_operations);

    // Additional integration tests.
    RUN(test_closure_captures);
    RUN(test_choice_type_match);
    RUN(test_result_success_pipeline);
    RUN(test_result_failure_pipeline);
    RUN(test_nested_match_arm_value);
    RUN(test_optional_some_none);
    RUN(test_record_with_methods);
    RUN(test_record_with_expression);
    RUN(test_nested_function_calls);
    RUN(test_pipe_chain);
    RUN(test_dictionary_operations);
    RUN(test_namespace_function_call);
    RUN(test_try_catch_execution);
    RUN(test_for_in_with_stdlib);
    RUN(test_recursive_fibonacci);
    RUN(test_higher_order_function);
    RUN(test_contains_operator);
    RUN(test_contains_range_operator);

    // Named arguments.
    RUN(test_named_arguments_all);
    RUN(test_named_arguments_mixed);
    RUN(test_named_arguments_reorder);

    // Downcast / IsType.
    RUN(test_is_type_true);
    RUN(test_is_type_false);
    RUN(test_is_type_number_widens_integer);
    RUN(test_downcast_success);
    RUN(test_downcast_failure);

    // Clone.
    RUN(test_mutable_clone_isolation);

    // IntToNumber.
    RUN(test_int_to_number_widening);

    // Rethrow.
    RUN(test_rethrow_propagates);

    // Tail call.
    RUN(test_tail_call_deep_recursion);

    // Mutable parameters.
    RUN(test_mutable_parameter_value_semantics);

    // Structured concurrency.
    RUN(test_concurrency_task_scope_collects_results);
    RUN(test_concurrency_task_await);
    RUN(test_concurrency_channel_send_receive);

    // Whitespace invariance.
    RUN(test_whitespace_variations_preserve_semantics);

    // Error-path integration tests.
    RUN(test_stack_overflow_reports_error);
    RUN(test_syntax_error_reports_diagnostic);
    RUN(test_division_by_zero_caught);
    RUN(test_undefined_variable_error);

    return SUMMARY();
}
