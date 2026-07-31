// VM unit tests: core arithmetic, operators, and control flow.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/errors/error.hpp"
#include "runtime/compiler/compiled_function.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_introspection.hpp"
#include "stdlib_test_helpers.hpp"

// ─── Tests ───

LUMA_TEST(vm_integer_arithmetic) {
    const auto result = eval("2 + 3");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 5);
}

LUMA_TEST(vm_integer_subtraction) {
    const auto result = eval("10 - 7");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 3);
}

LUMA_TEST(vm_integer_multiplication) {
    const auto result = eval("6 * 7");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_integer_division) {
    const auto result = eval("15 // 4");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 3);
}

LUMA_TEST(vm_modulo) {
    const auto result = eval("17 % 5");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 2);
}

LUMA_TEST(vm_boolean_true) {
    const auto result = eval("true");

    ASSERT_TRUE(result.is_bool());
    ASSERT_EQ(result.as_bool(), true);
}

LUMA_TEST(vm_boolean_false) {
    const auto result = eval("false");

    ASSERT_TRUE(result.is_bool());
    ASSERT_EQ(result.as_bool(), false);
}

LUMA_TEST(vm_string_literal) {
    const auto result = eval("\"hello\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "hello");
}

LUMA_TEST(vm_string_interpolation_concat) {
    const auto result = eval("string name = \"world\"\n\"hello ${name}\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "hello world");
}

LUMA_TEST(vm_comparison_less) {
    const auto result = eval("3 < 5");

    ASSERT_TRUE(result.is_bool());
    ASSERT_EQ(result.as_bool(), true);
}

LUMA_TEST(vm_comparison_equal) {
    const auto result = eval("42 == 42");

    ASSERT_TRUE(result.is_bool());
    ASSERT_EQ(result.as_bool(), true);
}

LUMA_TEST(vm_comparison_not_equal) {
    const auto result = eval("1 != 2");

    ASSERT_TRUE(result.is_bool());
    ASSERT_EQ(result.as_bool(), true);
}

LUMA_TEST(vm_negation) {
    const auto result = eval("-42");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), -42);
}

LUMA_TEST(vm_logical_not) {
    const auto result = eval("!true");

    ASSERT_TRUE(result.is_bool());
    ASSERT_EQ(result.as_bool(), false);
}

LUMA_TEST(vm_variable_binding) {
    const auto result = eval("integer x = 42\nx");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_mutable_assignment) {
    const auto result = eval("function integer assign_test() {\n"
                             "    mutable integer x = 1\n"
                             "    x = 5\n"
                             "    return x\n"
                             "}\n"
                             "assign_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 5);
}

LUMA_TEST(vm_increment) {
    const auto result = eval("function integer inc_test() {\n"
                             "    mutable integer x = 10\n"
                             "    x++\n"
                             "    return x\n"
                             "}\n"
                             "inc_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 11);
}

LUMA_TEST(vm_decrement) {
    const auto result = eval("function integer dec_test() {\n"
                             "    mutable integer x = 10\n"
                             "    x--\n"
                             "    return x\n"
                             "}\n"
                             "dec_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 9);
}

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

LUMA_TEST(vm_array_literal) {
    const auto result = eval("[1, 2, 3]");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), 3U);
}

LUMA_TEST(vm_array_index) {
    const auto result = eval("[10, 20, 30][1]");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 20);
}

LUMA_TEST(vm_array_index_out_of_bounds) {
    ASSERT_TRUE(throws_runtime("[1, 2, 3][10]"));
}

LUMA_TEST(vm_tuple) {
    const auto result = eval("(1, \"hello\", true)");

    ASSERT_TRUE(result.is_tuple());
}

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

LUMA_TEST(vm_constant_folding_result) {
    // Constant folding should produce same result as runtime.
    const auto result = eval("3 * 4 + 2");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 14);
}

LUMA_TEST(vm_bitwise_and) {
    const auto result = eval("0b1100 & 0b1010");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 0b1000);
}

LUMA_TEST(vm_bitwise_or) {
    const auto result = eval("0b1100 | 0b1010");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 0b1110);
}

LUMA_TEST(vm_short_circuit_and) {
    // Should not evaluate the right side when left is false.
    const auto result = eval("false && true");

    ASSERT_TRUE(result.is_bool());
    ASSERT_EQ(result.as_bool(), false);
}

LUMA_TEST(vm_short_circuit_or) {
    // Should not evaluate the right side when left is true.
    const auto result = eval("true || false");

    ASSERT_TRUE(result.is_bool());
    ASSERT_EQ(result.as_bool(), true);
}

LUMA_TEST(vm_none) {
    const auto result = eval("none");

    ASSERT_TRUE(result.is_null());
}

LUMA_TEST(vm_string_interpolation) {
    const auto result = eval("integer x = 42\n\"value is ${x}\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "value is 42");
}

LUMA_TEST(vm_mixed_int_number_add) {
    const auto result = eval("integer x = 3\nnumber y = 1.5\nx + y");

    ASSERT_TRUE(result.is_number());
    ASSERT_EQ(result.as_number(), 4.5);
}

LUMA_TEST(vm_mixed_int_number_multiply) {
    const auto result = eval("integer x = 4\nnumber y = 2.5\nx * y");

    ASSERT_TRUE(result.is_number());
    ASSERT_EQ(result.as_number(), 10.0);
}

LUMA_TEST(vm_division_by_zero) {
    ASSERT_TRUE(throws_runtime("integer x = 10\ninteger y = 0\nx / y"));
}

LUMA_TEST(vm_modulo_by_zero) {
    ASSERT_TRUE(throws_runtime("integer x = 10\ninteger y = 0\nx % y"));
}

LUMA_TEST(vm_comparison_greater) {
    const auto result = eval("5 > 3");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_comparison_less_equal) {
    const auto result = eval("3 <= 3");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_comparison_greater_equal) {
    const auto result = eval("2 >= 5");

    ASSERT_TRUE(result.is_bool());
    ASSERT_FALSE(result.as_bool());
}

LUMA_TEST(vm_nested_function_call) {
    auto result =
        eval("function integer add(integer a, integer b) { return a + b }\n"
             "function integer double_add(integer x, integer y) { return add(x, y) * 2 }\n"
             "double_add(3, 4)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 14);
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

LUMA_TEST(vm_dictionary_index) {
    const auto result = eval("dictionary<string> d = {\"a\": \"hello\", \"b\": \"world\"}\n"
                             "d[\"b\"]");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "world");
}

LUMA_TEST(vm_dictionary_key_not_found) {
    ASSERT_TRUE(throws_runtime("dictionary<string> d = {\"a\": \"hello\"}\n"
                               "d[\"missing\"]"));
}

LUMA_TEST(vm_string_concatenation) {
    const auto result = eval("\"hello\" + \" \" + \"world\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "hello world");
}

LUMA_TEST(vm_negative_array_index) {
    // Negative indices are out of bounds (not Python-style wrapping).
    ASSERT_THROWS(eval("array<integer> a = [10, 20, 30]\n"
                       "a[-1]"));
}

LUMA_TEST(vm_bitwise_xor) {
    const auto result = eval("5 ^ 3"); // 101 ^ 011 = 110

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 6);
}

LUMA_TEST(vm_bitwise_not) {
    const auto result = eval("~0");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), -1);
}

LUMA_TEST(vm_shift_left) {
    const auto result = eval("1 << 4");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 16);
}

LUMA_TEST(vm_shift_right) {
    const auto result = eval("16 >> 2");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 4);
}

LUMA_TEST(vm_shift_out_of_range) {
    ASSERT_TRUE(throws_runtime("integer x = 1\ninteger y = 64\nx << y"));
    ASSERT_TRUE(throws_runtime("integer x = 1\ninteger y = -1\nx << y"));
}

LUMA_TEST(vm_integer_division_truncates) {
    const auto result = eval("7 / 2");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 3);
}

LUMA_TEST(vm_number_division) {
    const auto result = eval("7.0 / 2.0");

    ASSERT_TRUE(result.is_number());
    ASSERT_EQ(result.as_number(), 3.5);
}

LUMA_TEST(vm_try_catch) {
    const auto result = eval("function string try_test() {\n"
                             "    try {\n"
                             "        integer x = 1 / 0\n"
                             "        return \"no error\"\n"
                             "    } catch(e) {\n"
                             "        return \"caught\"\n"
                             "    }\n"
                             "}\n"
                             "try_test()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "caught");
}

LUMA_TEST(vm_make_range) {
    const auto result = eval("[1, 2, 3, 4, 5][1..3]");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), 2U);
}

LUMA_TEST(vm_concatenate_op) {
    const auto result = eval("\"abc\" + \"def\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "abcdef");
}

LUMA_TEST(vm_tuple_index) {
    const auto result = eval("(10, \"hello\", true)[0]");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 10);
}

LUMA_TEST(vm_tuple_field_access) {
    // `.N` numeric field access is a distinct opcode path from `[N]` indexing.
    const auto result = eval("(10, 20, 30).1");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 20);
}

LUMA_TEST(vm_tuple_equality) {
    // Tuple `==` is structural: same arity and element-wise equality.
    const auto result = eval("(1, 2, 3) == (1, 2, 3)");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_tuple_inequality) {
    const auto result = eval("(1, 2) != (1, 3)");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_tuple_destructuring) {
    const auto result = eval("(integer a, integer b) = (7, 9)\n"
                             "a * b");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 63);
}

LUMA_TEST(vm_nested_tuple_index) {
    const auto result = eval("((1, 2), 3)[0][1]");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 2);
}

LUMA_TEST(vm_array_slice) {
    const auto result = eval("[1, 2, 3, 4, 5][1..3]");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), 2U);
    ASSERT_EQ((*result.as_array()->elements)[0].as_integer(), 2);
    ASSERT_EQ((*result.as_array()->elements)[1].as_integer(), 3);
}

LUMA_TEST(vm_string_index) {
    const auto result = eval("\"hello\"[1]");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "e");
}

LUMA_TEST(vm_swap_via_locals) {
    // Tests Swap opcode indirectly through variable manipulation.
    const auto result = eval("function integer swap_test() {\n"
                             "    mutable integer a = 1\n"
                             "    mutable integer b = 2\n"
                             "    integer temp = a\n"
                             "    a = b\n"
                             "    b = temp\n"
                             "    return a * 10 + b\n"
                             "}\n"
                             "swap_test()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 21); // a=2, b=1
}

LUMA_TEST(vm_contains_array) {
    const auto result = eval("3 in [1, 2, 3, 4]");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_contains_string) {
    const auto result = eval("\"lo\" in \"hello\"");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_not_contains) {
    const auto result = eval("5 in [1, 2, 3]");

    ASSERT_TRUE(result.is_bool());
    ASSERT_FALSE(result.as_bool());
}

LUMA_TEST(vm_contains_range_inclusive) {
    ASSERT_TRUE(eval("1 in 1..=100").as_bool());
    ASSERT_TRUE(eval("100 in 1..=100").as_bool());
    ASSERT_TRUE(eval("50 in 1..=100").as_bool());
    ASSERT_FALSE(eval("0 in 1..=100").as_bool());
    ASSERT_FALSE(eval("101 in 1..=100").as_bool());
}

LUMA_TEST(vm_contains_range_exclusive) {
    ASSERT_TRUE(eval("1 in 1..100").as_bool());
    ASSERT_TRUE(eval("99 in 1..100").as_bool());
    ASSERT_FALSE(eval("100 in 1..100").as_bool());
    ASSERT_FALSE(eval("0 in 1..100").as_bool());
}

LUMA_TEST(vm_contains_range_empty) {
    // start == end exclusive, and start > end, are both empty.
    ASSERT_FALSE(eval("5 in 5..5").as_bool());
    ASSERT_FALSE(eval("5 in 10..1").as_bool());
    ASSERT_TRUE(eval("5 in 5..=5").as_bool());
}

// The type checker rejects a non-integer left operand of `in <range>`, but a
// mistyped value can still reach the VM through dynamic paths (e.g. an
// `any`-typed value). The unchecked eval() path reproduces that: the VM must
// surface a clean RuntimeError rather than crash on an unchecked as_integer().
LUMA_TEST(vm_contains_range_non_integer_throws) {
    ASSERT_THROWS_WITH_MESSAGE(eval("\"x\" in 1..=100"), "'in' on a range requires an integer");
    ASSERT_THROWS_WITH_MESSAGE(eval("1.5 in 1..100"), "'in' on a range requires an integer");
}

// ─── Runtime error tests ───

LUMA_TEST(vm_recursion_depth_limit) {
    // Recursion beyond 256 frames should throw a RuntimeError.
    // Use `1 + infinite(...)` to prevent tail-call optimisation.
    ASSERT_TRUE(throws_runtime("function integer infinite(integer n) {\n"
                               "    return 1 + infinite(n + 1)\n"
                               "}\n"
                               "infinite(0)"));
}

LUMA_TEST(vm_string_index_out_of_bounds) {
    ASSERT_TRUE(throws_runtime("\"hi\"[10]"));
}

// ─── Index type-mismatch guards ───
// The type checker normally rejects these, but a mistyped value can reach an
// index operation at runtime through dynamic paths (e.g. a GraphicalUi update
// receiving an unexpected type). The VM must surface a clean RuntimeError
// rather than crash on an unchecked std::variant access (bad_variant_access).
// The unchecked eval() path reproduces that dynamic situation.

LUMA_TEST(vm_string_index_non_integer_throws) {
    ASSERT_TRUE(throws_runtime("\"hello\"[\"x\"]"));
}

LUMA_TEST(vm_array_index_non_integer_throws) {
    ASSERT_TRUE(throws_runtime("[1, 2, 3][\"x\"]"));
}

LUMA_TEST(vm_dict_index_non_string_throws) {
    ASSERT_TRUE(throws_runtime("dictionary<integer> d = {\"a\": 1}\n"
                               "d[0]"));
}

LUMA_TEST(vm_array_index_set_non_integer_throws) {
    ASSERT_TRUE(throws_runtime("mutable array<integer> a = [1, 2, 3]\n"
                               "a[\"x\"] = 9"));
}

LUMA_TEST(vm_dict_index_set_non_string_throws) {
    ASSERT_TRUE(throws_runtime("mutable dictionary<integer> d = {\"a\": 1}\n"
                               "d[0] = 9"));
}

LUMA_TEST(vm_integer_division_by_zero) {
    ASSERT_TRUE(throws_runtime("10 // 0"));
}

LUMA_TEST(vm_tuple_index_out_of_bounds) {
    ASSERT_TRUE(throws_runtime("(1, 2, 3)[5]"));
}

LUMA_TEST(vm_string_repeat_negative) {
    // Repeating a string with a negative count should throw or return empty.
    // Either outcome is acceptable.
    try {
        const auto result = eval("\"ha\" * -1");

        ASSERT_TRUE(result.is_string());
        ASSERT_EQ(result.as_string(), "");
    } catch (...) { // NOLINT(bugprone-empty-catch)
        // Expected.
    }
}

LUMA_TEST(vm_try_finally_always_runs) {
    const auto result = eval("function string finally_test() {\n"
                             "    mutable string log = \"\"\n"
                             "    try {\n"
                             "        log = log + \"try\"\n"
                             "    } finally {\n"
                             "        log = log + \"-finally\"\n"
                             "    }\n"
                             "    return log\n"
                             "}\n"
                             "finally_test()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "try-finally");
}

// ─── Closure tests ───

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

// ─── For-in loop tests ───

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

// ─── Pipe operator tests ───

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
    ASSERT_THROWS(eval("5 |> String.uppercase()"));
}

// ─── Type-of test ───

LUMA_TEST(vm_typeof) {
    const auto result = eval("type_of(42)");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "integer");
}

LUMA_TEST(vm_typeof_string) {
    const auto result = eval("type_of(\"hello\")");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "string");
}

// ─── Break and continue tests ───

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

// ─── String repeat test ───

LUMA_TEST(vm_string_repeat) {
    const auto result = eval("\"ha\" * 3");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "hahaha");
}

// ─── Range inclusive test ───

LUMA_TEST(vm_range_inclusive_slice) {
    const auto result = eval("[10, 20, 30, 40, 50][1..=3]");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), 3U);
    ASSERT_EQ((*result.as_array()->elements)[0].as_integer(), 20);
    ASSERT_EQ((*result.as_array()->elements)[2].as_integer(), 40);
}

// ─── If-else expression test ───

LUMA_TEST(vm_if_else_expression) {
    const auto result = eval("integer x = if true { 42 } else { 0 }\nx");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

// ─── Additional control-flow coverage ───

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

// ─── Named arguments tests ───

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

// ─── For-in dictionary key-value iteration ───

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

// ─── Try-catch-finally combined ───

LUMA_TEST(vm_try_catch_finally) {
    const auto result = eval("function string f() {\n"
                             "    mutable string log = \"\"\n"
                             "    try {\n"
                             "        log = log + \"try\"\n"
                             "        integer x = 1 / 0\n"
                             "    } catch(e) {\n"
                             "        log = log + \"-catch\"\n"
                             "    } finally {\n"
                             "        log = log + \"-finally\"\n"
                             "    }\n"
                             "    return log\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "try-catch-finally");
}

// Regression: finally must run even when the catch body itself raises a new
// error. The original lowering left the catch body unprotected, so finally was
// skipped whenever the catch threw. The new error still propagates to the
// enclosing handler — but only AFTER finally has run. Expected order: catch
// runs ("C"), finally runs ("F"), then the outer handler catches ("O").
LUMA_TEST(vm_finally_runs_when_catch_throws) {
    const auto result = eval("function string f() {\n"
                             "    mutable string log = \"\"\n"
                             "    try {\n"
                             "        try {\n"
                             "            integer a = 1 / 0\n"
                             "        } catch(e) {\n"
                             "            log = log + \"C\"\n"
                             "            integer b = 1 / 0\n"
                             "        } finally {\n"
                             "            log = log + \"F\"\n"
                             "        }\n"
                             "    } catch(outer) {\n"
                             "        log = log + \"O\"\n"
                             "    }\n"
                             "    return log\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "CFO");
}

// Regression: finally must run when control leaves the catch body via break.
LUMA_TEST(vm_finally_runs_on_break_from_catch) {
    const auto result = eval("function integer f() {\n"
                             "    mutable integer ran = 0\n"
                             "    for i in 0 .. 5 {\n"
                             "        try {\n"
                             "            integer a = 1 / 0\n"
                             "        } catch(e) {\n"
                             "            break\n"
                             "        } finally {\n"
                             "            ran = ran + 1\n"
                             "        }\n"
                             "    }\n"
                             "    return ran\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 1);
}

// finally must run when the catch body returns from the function. A Reference
// makes the side effect observable after the early return: the catch appends
// "C" and the finally appends "F" before control leaves, so the shared log is
// "CF" and the returned value is "ret".
LUMA_TEST(vm_finally_runs_on_return_from_catch) {
    const auto result = eval("function string body(reference<string> log) {\n"
                             "    try {\n"
                             "        integer a = 1 / 0\n"
                             "    } catch(e) {\n"
                             "        Reference.set(log, Reference.get(log) + \"C\")\n"
                             "        return \"ret\"\n"
                             "    } finally {\n"
                             "        Reference.set(log, Reference.get(log) + \"F\")\n"
                             "    }\n"
                             "    return \"end\"\n"
                             "}\n"
                             "function string f() {\n"
                             "    reference<string> log = Reference.new(\"\")\n"
                             "    string r = body(log)\n"
                             "    return Reference.get(log) + \"|\" + r\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "CF|ret");
}

// Regression: a non-local exit (return) inside an INNER finally must still run
// every ENCLOSING finally. The inner `return 1` triggers the inner finally,
// which appends "Fi" and itself does `return 2`; that nested return has to
// unwind the outer try and run its finally ("Fo") before leaving. A prior
// emit_try_unwind that detached all unwound handlers up front left no active
// handler while the inner finally compiled, silently skipping "Fo" (log became
// "Fi" instead of "FiFo"). Expected: outer finally runs, return 2 wins.
LUMA_TEST(vm_inner_finally_return_runs_outer_finally) {
    const auto result = eval("function integer body(reference<string> log) {\n"
                             "    try {\n"
                             "        try {\n"
                             "            return 1\n"
                             "        } finally {\n"
                             "            Reference.set(log, Reference.get(log) + \"Fi\")\n"
                             "            return 2\n"
                             "        }\n"
                             "    } finally {\n"
                             "        Reference.set(log, Reference.get(log) + \"Fo\")\n"
                             "    }\n"
                             "}\n"
                             "function string f() {\n"
                             "    reference<string> log = Reference.new(\"\")\n"
                             "    integer r = body(log)\n"
                             "    return Reference.get(log) + \"|\" + \"${r}\"\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "FiFo|2");
}

// The catch variable binds the runtime error message string, which the catch
// body can read and return.
LUMA_TEST(vm_catch_binds_error_message) {
    const auto result = eval("function string f() {\n"
                             "    try {\n"
                             "        integer a = 1 / 0\n"
                             "        return \"unreachable\"\n"
                             "    } catch(err) {\n"
                             "        return err\n"
                             "    }\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_TRUE(result.as_string().find("division by zero") != std::string::npos);
}

// The manual's nested example: an inner try/finally with no catch runs its
// finally body and then re-raises, so the outer catch still observes the error.
// Expected order: try ("t"), inner finally ("-f"), outer catch ("-c").
LUMA_TEST(vm_nested_finally_runs_before_propagation) {
    const auto result = eval("function string f() {\n"
                             "    mutable string log = \"\"\n"
                             "    try {\n"
                             "        try {\n"
                             "            log = log + \"t\"\n"
                             "            integer a = 1 / 0\n"
                             "            log = log + \"X\"\n"
                             "        } finally {\n"
                             "            log = log + \"-f\"\n"
                             "        }\n"
                             "    } catch(e) {\n"
                             "        log = log + \"-c\"\n"
                             "    }\n"
                             "    return log\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "t-f-c");
}

// ─── Compound assignment operators ───

LUMA_TEST(vm_compound_assignment) {
    const auto result = eval("function integer f() {\n"
                             "    mutable integer x = 10\n"
                             "    x += 5\n"
                             "    x -= 3\n"
                             "    x *= 2\n"
                             "    return x\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 24); // (10+5-3)*2
}

// ─── Numeric literals, overflow, and type_of (data types) ───

LUMA_TEST(vm_hex_literal) {
    const auto result = eval("0xFF");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 255);
}

LUMA_TEST(vm_binary_literal) {
    const auto result = eval("0b1010");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 10);
}

LUMA_TEST(vm_integer_overflow_promotes_to_number) {
    // Overflowing integer arithmetic widens to number instead of wrapping.
    const auto result = eval("integer x = 9223372036854775807\nx + 1");

    ASSERT_TRUE(result.is_number());
    ASSERT_EQ(result.as_number(), 9223372036854775808.0);
}

LUMA_TEST(vm_number_division_by_zero) {
    ASSERT_TRUE(throws_runtime("number x = 10.0\nnumber y = 0.0\nx / y"));
}

LUMA_TEST(vm_number_overflow_throws) {
    ASSERT_TRUE(throws_runtime("number x = 1.0e308\nx * 10.0"));
}

LUMA_TEST(vm_typeof_number) {
    const auto result = eval("type_of(3.14)");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "number");
}

LUMA_TEST(vm_typeof_boolean) {
    const auto result = eval("type_of(true)");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "boolean");
}

LUMA_TEST(vm_typeof_none) {
    const auto result = eval("type_of(none)");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "none");
}

// ─── Main ───

int main() {
    LUMA_RUN_ALL();
}
