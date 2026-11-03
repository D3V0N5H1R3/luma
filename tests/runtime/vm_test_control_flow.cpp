// VM unit tests: conditionals, loops, match, functions, named arguments,
// closures, and the pipe operator.

#include "stdlib_test_helpers.hpp"

using luma::test::eval;

// ─── Conditionals ───

LUMA_TEST(vm_if_true_branch) {
    const auto result = eval("function integer if_test() {\n"
                             "    if true { return 42 }\n"
                             "    return 0\n"
                             "}\n"
                             "if_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_if_false_branch) {
    const auto result = eval("function integer if_test() {\n"
                             "    if false { return 42 }\n"
                             "    return 0\n"
                             "}\n"
                             "if_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 0);
}

LUMA_TEST(vm_if_else_expression) {
    const auto result = eval("integer x = if true { 42 } else { 0 }\nx");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_if_else_if_chain) {
    const auto result = eval("function string grade(integer score) {\n"
                             "    if score >= 90 {\n"
                             "        return \"A\"\n"
                             "    } else if score >= 80 {\n"
                             "        return \"B\"\n"
                             "    } else {\n"
                             "        return \"F\"\n"
                             "    }\n"
                             "}\n"
                             "grade(85)");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "B");
}

// ─── Loops ───

LUMA_TEST(vm_while_loop) {
    const auto result = eval("function integer while_test() {\n"
                             "    mutable integer x = 0\n"
                             "    while x < 5 { x += 1 }\n"
                             "    return x\n"
                             "}\n"
                             "while_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 5);
}

LUMA_TEST(vm_for_loop_sum) {
    const auto result = eval("function integer loop_sum() {\n"
                             "    mutable integer total = 0\n"
                             "    mutable integer i = 1\n"
                             "    while i < 4 { total += i\n i += 1 }\n"
                             "    return total\n"
                             "}\n"
                             "loop_sum()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 6); // 1+2+3
}

LUMA_TEST(vm_for_in_array) {
    const auto result = eval("function integer sum_array() {\n"
                             "    mutable integer total = 0\n"
                             "    for x in [10, 20, 30] {\n"
                             "        total += x\n"
                             "    }\n"
                             "    return total\n"
                             "}\n"
                             "sum_array()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 60);
}

LUMA_TEST(vm_for_in_range) {
    const auto result = eval("function integer range_sum() {\n"
                             "    mutable integer total = 0\n"
                             "    for i in 0 .. 5 {\n"
                             "        total += i\n"
                             "    }\n"
                             "    return total\n"
                             "}\n"
                             "range_sum()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 10); // 0+1+2+3+4
}

LUMA_TEST(vm_for_in_range_inclusive) {
    const auto result = eval("function integer range_sum() {\n"
                             "    mutable integer total = 0\n"
                             "    for i in 1 ..= 5 {\n"
                             "        total += i\n"
                             "    }\n"
                             "    return total\n"
                             "}\n"
                             "range_sum()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 15); // 1+2+3+4+5 (endpoint included)
}

LUMA_TEST(vm_for_in_string) {
    const auto result = eval("function integer count_chars() {\n"
                             "    mutable integer count = 0\n"
                             "    for _ch in \"hello\" {\n"
                             "        count += 1\n"
                             "    }\n"
                             "    return count\n"
                             "}\n"
                             "count_chars()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 5);
}

LUMA_TEST(vm_for_dict_kv) {
    const auto result = eval("function integer f() {\n"
                             "    dictionary<integer> d = {\"a\": 1, \"b\": 2, \"c\": 3}\n"
                             "    mutable integer total = 0\n"
                             "    for k, v in d {\n"
                             "        total += v\n"
                             "    }\n"
                             "    return total\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 6); // 1+2+3
}

// ─── Break and continue ───

LUMA_TEST(vm_break_in_while) {
    const auto result = eval("function integer break_test() {\n"
                             "    mutable integer x = 0\n"
                             "    while true {\n"
                             "        x += 1\n"
                             "        if x == 5 { break }\n"
                             "    }\n"
                             "    return x\n"
                             "}\n"
                             "break_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 5);
}

LUMA_TEST(vm_continue_in_for) {
    const auto result = eval("function integer continue_test() {\n"
                             "    mutable integer total = 0\n"
                             "    for x in [1, 2, 3, 4, 5] {\n"
                             "        if x == 3 { continue }\n"
                             "        total += x\n"
                             "    }\n"
                             "    return total\n"
                             "}\n"
                             "continue_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 12); // 1+2+4+5
}

LUMA_TEST(vm_break_in_for) {
    const auto result = eval("function integer break_test() {\n"
                             "    mutable integer last = 0\n"
                             "    for i in 0 .. 100 {\n"
                             "        if i == 5 { break }\n"
                             "        last = i\n"
                             "    }\n"
                             "    return last\n"
                             "}\n"
                             "break_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 4);
}

LUMA_TEST(vm_continue_in_while) {
    const auto result = eval("function integer continue_test() {\n"
                             "    mutable integer sum = 0\n"
                             "    mutable integer i = 0\n"
                             "    while i < 10 {\n"
                             "        i += 1\n"
                             "        if i % 2 == 0 { continue }\n"
                             "        sum += i\n"
                             "    }\n"
                             "    return sum\n"
                             "}\n"
                             "continue_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 25); // 1+3+5+7+9
}

LUMA_TEST(vm_nested_loop_break_inner_only) {
    const auto result = eval("function integer nested_test() {\n"
                             "    mutable integer count = 0\n"
                             "    for _outer in 0 .. 3 {\n"
                             "        for inner in 0 .. 5 {\n"
                             "            if inner == 2 { break }\n"
                             "            count += 1\n"
                             "        }\n"
                             "    }\n"
                             "    return count\n"
                             "}\n"
                             "nested_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 6); // outer 3 * inner 2
}

// ─── Match ───

LUMA_TEST(vm_match_boolean) {
    const auto result = eval("function string describe(boolean flag) {\n"
                             "    return match flag {\n"
                             "        case true { \"on\" }\n"
                             "        case false { \"off\" }\n"
                             "    }\n"
                             "}\n"
                             "describe(false)");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "off");
}

LUMA_TEST(vm_match_optional_some) {
    const auto result = eval("function integer unwrap_or_zero(optional<integer> opt) {\n"
                             "    return match opt {\n"
                             "        case some(v) { v }\n"
                             "        case none { 0 }\n"
                             "    }\n"
                             "}\n"
                             "unwrap_or_zero(some(42))");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_match_optional_none) {
    const auto result = eval("function integer unwrap_or_zero(optional<integer> opt) {\n"
                             "    return match opt {\n"
                             "        case some(v) { v }\n"
                             "        case none { 0 }\n"
                             "    }\n"
                             "}\n"
                             "unwrap_or_zero(none)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 0);
}

// ─── Functions ───

LUMA_TEST(vm_function_call) {
    const auto result = eval("function integer double(integer x) { return x * 2 }\n"
                             "double(21)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_recursive_function) {
    const auto result = eval("function integer factorial(integer n) {\n"
                             "    if n <= 1 { return 1 }\n"
                             "    return n * factorial(n - 1)\n"
                             "}\n"
                             "factorial(5)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 120);
}

LUMA_TEST(vm_nested_function_call) {
    const auto result =
        eval("function integer add(integer a, integer b) { return a + b }\n"
             "function integer double_add(integer x, integer y) { return add(x, y) * 2 }\n"
             "double_add(3, 4)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 14);
}

// ─── Named arguments ───

LUMA_TEST(vm_named_arguments) {
    const auto result = eval("function integer subtract(integer a, integer b) { return a - b }\n"
                             "subtract(a: 10, b: 3)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 7);
}

LUMA_TEST(vm_named_arguments_reordered) {
    const auto result = eval("function integer subtract(integer a, integer b) { return a - b }\n"
                             "subtract(b: 3, a: 10)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 7);
}

LUMA_TEST(vm_named_argument_with_default) {
    // A named argument may omit a trailing parameter that has a default value.
    const auto result = eval("function string greet(string name, string prefix = \"Hi\") {\n"
                             "    return prefix + \", \" + name\n"
                             "}\n"
                             "greet(name: \"Bob\")");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "Hi, Bob");
}

LUMA_TEST(vm_unknown_named_argument_throws) {
    // Supplying a named argument that matches no parameter is a runtime error.
    ASSERT_THROWS_WITH_MESSAGE(
        eval("function integer subtract(integer a, integer b) { return a - b }\n"
             "subtract(a: 10, c: 3)"),
        "unknown named argument");
}

// ─── Closures ───

LUMA_TEST(vm_closure_captures_local) {
    const auto result = eval("function integer make_adder() {\n"
                             "    integer x = 10\n"
                             "    function(integer) -> integer add = (integer y) -> x + y\n"
                             "    return add(5)\n"
                             "}\n"
                             "make_adder()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 15);
}

LUMA_TEST(vm_closure_factory_returns_closure) {
    const auto result = eval("function function(integer) -> integer make_adder(integer base) {\n"
                             "    return (integer x) -> base + x\n"
                             "}\n"
                             "function integer run() {\n"
                             "    function(integer) -> integer add5 = make_adder(5)\n"
                             "    return add5(10)\n"
                             "}\n"
                             "run()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 15);
}

LUMA_TEST(vm_closure_multiple_captures) {
    const auto result = eval("function integer run() {\n"
                             "    integer a = 10\n"
                             "    integer b = 20\n"
                             "    function() -> integer f = () -> a + b\n"
                             "    return f()\n"
                             "}\n"
                             "run()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 30);
}

LUMA_TEST(vm_block_body_lambda) {
    const auto result = eval("function integer run() {\n"
                             "    integer offset = 100\n"
                             "    function(integer) -> integer t = (integer x) -> {\n"
                             "        integer doubled = x * 2\n"
                             "        return doubled + offset\n"
                             "    }\n"
                             "    return t(5)\n"
                             "}\n"
                             "run()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 110);
}

LUMA_TEST(vm_curried_lambda) {
    const auto result = eval("function integer run() {\n"
                             "    function(integer) -> function(integer) -> integer make = "
                             "(integer n) -> (integer x) -> x + n\n"
                             "    function(integer) -> integer add3 = make(3)\n"
                             "    return add3(4)\n"
                             "}\n"
                             "run()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 7);
}

LUMA_TEST(vm_array_of_lambdas) {
    const auto result = eval("function integer run() {\n"
                             "    array<function(integer) -> integer> fns = "
                             "[(integer x) -> x + 1, (integer x) -> x * 2]\n"
                             "    function(integer) -> integer second = fns[1]\n"
                             "    return second(10)\n"
                             "}\n"
                             "run()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 20);
}

LUMA_TEST(vm_lambda_as_higher_order_argument) {
    const auto result =
        eval("function integer apply_fn(function(integer) -> integer f, integer x) {\n"
             "    return f(x)\n"
             "}\n"
             "function integer run() {\n"
             "    return apply_fn((integer n) -> n * n, 7)\n"
             "}\n"
             "run()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 49);
}

// ─── Pipe operator ───

LUMA_TEST(vm_pipe_operator) {
    const auto result = eval("function integer double(integer x) { return x * 2 }\n"
                             "5 |> double()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 10);
}

LUMA_TEST(vm_pipe_with_extra_argument) {
    // The piped value is the first argument; explicit args follow it.
    const auto result = eval("function integer add(integer a, integer b) { return a + b }\n"
                             "10 |> add(5)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 15);
}

LUMA_TEST(vm_pipe_namespace_function) {
    const auto result = eval("namespace M {\n"
                             "    function integer twice(integer n) { return n * 2 }\n"
                             "}\n"
                             "21 |> M.twice()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_pipe_stdlib_arg_type_mismatch_throws) {
    // Piping an integer into String.uppercase() is a stdlib argument-type
    // mismatch — stdlib parameters are untyped to the checker, so this is
    // caught at runtime rather than at compile time.
    ASSERT_EVAL_THROWS("5 |> String.uppercase()",
                       "String.uppercase: expected string, got 'integer'");
}

// ─── Main ───

int main() {
    LUMA_RUN_ALL();
}
