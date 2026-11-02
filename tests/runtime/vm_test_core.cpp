// VM unit tests: arithmetic, operators, literals, strings, variables, and
// collections.

#include "stdlib_test_helpers.hpp"

using luma::test::eval;

// ─── Arithmetic ───

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

LUMA_TEST(vm_negation) {
    const auto result = eval("-42");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), -42);
}

LUMA_TEST(vm_constant_folding_result) {
    // Constant folding should produce same result as runtime.
    const auto result = eval("3 * 4 + 2");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 14);
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

LUMA_TEST(vm_true_division_promotes) {
    // '/' is true division and always yields a number, so 7 / 2 == 3.5.
    const auto result = eval("7 / 2");

    ASSERT_TRUE(result.is_number());
    ASSERT_EQ(result.as_number(), 3.5);
}

LUMA_TEST(vm_floor_division_rounds_toward_negative_infinity) {
    // '//' is floor division: -7 // 2 rounds toward negative infinity to -4.
    const auto result = eval("-7 // 2");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), -4);
}

LUMA_TEST(vm_number_division) {
    const auto result = eval("7.0 / 2.0");

    ASSERT_TRUE(result.is_number());
    ASSERT_EQ(result.as_number(), 3.5);
}

// ─── Literals and equality ───

// Code review (function identity): a value-semantics copy of a function — a
// `mutable` binding or a mutable array element, both deep-copied — compared
// unequal to the original while an immutable alias compared equal, so
// Array.contains depended on the container's mutability.  A copy is now the
// same function; a separately created closure of the same code is not.
LUMA_TEST(vm_function_copy_equals_original) {
    const auto result =
        eval("function integer twice(integer x) { return x * 2 }\n"
             "function string compare() {\n"
             "    mutable function(integer) -> integer copied = twice\n"
             "    function(integer) -> integer aliased = twice\n"
             "    mutable array<function(integer) -> integer> mutable_list = [twice]\n"
             "    array<function(integer) -> integer> immutable_list = [twice]\n"
             "    mutable integer base = 1\n"
             "    function(integer) -> integer adder = (integer x) -> x + base\n"
             "    mutable function(integer) -> integer adder_copy = adder\n"
             "    function(integer) -> integer other_adder = (integer x) -> x + base\n"
             "    return \"${copied == twice} ${aliased == twice} "
             "${Array.contains(mutable_list, twice)} ${Array.contains(immutable_list, twice)} "
             "${adder_copy == adder} ${other_adder == adder}\"\n"
             "}\n"
             "compare()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "true true true true true false");
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

LUMA_TEST(vm_none) {
    const auto result = eval("none");

    ASSERT_TRUE(result.is_null());
}

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

// Regression (review #12/#15): Value::equals for identity types and exact
// integer/number equality, exercised end to end through `==`.
LUMA_TEST(vm_equality_integer_number_exact_above_2_pow_53) {
    // `mutable` operands keep the comparison out of compile-time folding, so
    // the VM's runtime equality (Value::equals) is what is exercised.
    // 2^53 + 1 is not representable as a double; it must not equal 2^53.0.
    ASSERT_FALSE(eval("mutable integer a = 9007199254740993\n"
                      "mutable number b = 9007199254740992.0\n"
                      "a == b")
                     .as_bool());
    ASSERT_TRUE(eval("mutable integer a = 9007199254740992\n"
                     "mutable number b = 9007199254740992.0\n"
                     "a == b")
                    .as_bool());
    ASSERT_TRUE(eval("mutable integer a = 3\nmutable number b = 3.0\na == b").as_bool());
    ASSERT_FALSE(eval("mutable integer a = 3\nmutable number b = 3.5\na == b").as_bool());
}

// ─── Comparison and logical operators ───

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

LUMA_TEST(vm_logical_not) {
    const auto result = eval("!true");

    ASSERT_TRUE(result.is_bool());
    ASSERT_EQ(result.as_bool(), false);
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

// ─── Bitwise operators ───

LUMA_TEST(vm_bitwise_and) {
    const auto result = eval("Bits.and(0b1100, 0b1010)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 0b1000);
}

LUMA_TEST(vm_bitwise_or) {
    const auto result = eval("Bits.or(0b1100, 0b1010)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 0b1110);
}

LUMA_TEST(vm_bitwise_xor) {
    const auto result = eval("Bits.xor(5, 3)"); // 101 ^ 011 = 110

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 6);
}

LUMA_TEST(vm_bitwise_not) {
    const auto result = eval("Bits.not(0)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), -1);
}

LUMA_TEST(vm_shift_left) {
    const auto result = eval("Bits.shift_left(1, 4)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 16);
}

LUMA_TEST(vm_shift_right) {
    const auto result = eval("Bits.shift_right(16, 2)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 4);
}

// ─── Strings ───

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

LUMA_TEST(vm_string_interpolation) {
    const auto result = eval("integer x = 42\n\"value is ${x}\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "value is 42");
}

LUMA_TEST(vm_string_concatenation) {
    const auto result = eval("\"hello\" + \" \" + \"world\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "hello world");
}

LUMA_TEST(vm_concatenate_op) {
    const auto result = eval("\"abc\" + \"def\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "abcdef");
}

LUMA_TEST(vm_concatenate_chain_reuses_growing_buffer) {
    // Regression test for the in-place append fast path in
    // VM::handle_concatenate: a long chain of `+` operators repeatedly
    // appends to the same left-hand stack value across many concatenations,
    // growing well past the small-string-optimisation threshold. Correctness
    // must hold even when the underlying buffer is reused (grown in place)
    // rather than reallocated exactly to size on every `+`.
    const auto result =
        eval("\"a\" + \"bb\" + \"ccc\" + \"dddd\" + \"eeeee\" + \"ffffff\" + \"ggggggg\" + "
             "\"hhhhhhhh\" + \"iiiiiiiii\" + \"jjjjjjjjjj\" + \"kkkkkkkkkkk\" + "
             "\"llllllllllll\" + \"mmmmmmmmmmmmm\" + \"nnnnnnnnnnnnnn\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "abbcccddddeeeeeffffffggggggghhhhhhhhiiiiiiiiijjjjjjjjjj"
                                  "kkkkkkkkkkkllllllllllllmmmmmmmmmmmmmnnnnnnnnnnnnnn");
}

LUMA_TEST(vm_string_index) {
    const auto result = eval("\"hello\"[1]");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "e");
}

LUMA_TEST(vm_string_repeat) {
    const auto result = eval("\"ha\" * 3");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "hahaha");
}

// ─── Variables ───

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

// ─── Collections ───

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

LUMA_TEST(vm_tuple) {
    const auto result = eval("(1, \"hello\", true)");

    ASSERT_TRUE(result.is_tuple());

    const auto& elements = result.as_tuple()->elements;

    ASSERT_EQ(elements.size(), 3);
    ASSERT_EQ(elements[0].as_integer(), 1);
    ASSERT_EQ(elements[1].as_string(), "hello");
    ASSERT_TRUE(elements[2].as_bool());
}

LUMA_TEST(vm_dictionary_index) {
    const auto result = eval("dictionary<string> d = {\"a\": \"hello\", \"b\": \"world\"}\n"
                             "d[\"b\"]");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "world");
}

LUMA_TEST(vm_make_range) {
    const auto result = eval("[1, 2, 3, 4, 5][1..3]");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), 2U);
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

LUMA_TEST(vm_range_inclusive_slice) {
    const auto result = eval("[10, 20, 30, 40, 50][1..=3]");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), 3U);
    ASSERT_EQ((*result.as_array()->elements)[0].as_integer(), 20);
    ASSERT_EQ((*result.as_array()->elements)[2].as_integer(), 40);
}

// ─── Membership (in) ───

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

// ─── type_of ───

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
