// VM unit tests: generic execution and nested type-argument checks.

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

// ─── Generic execution: records, turbofish, multi-param, constructors ───

LUMA_TEST(vm_generic_record_construct_access) {
    // Generic record construction and field access executed through the VM.
    const auto result = eval("record Box<T> { T value }\n"
                             "function integer f() {\n"
                             "    Box<integer> b = Box<integer> { value = 42 }\n"
                             "    return b.value\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_generic_function_turbofish) {
    // Turbofish explicit type argument executed end-to-end.
    const auto result = eval("function<T> T identity(T value) { return value }\n"
                             "identity::<integer>(99)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 99);
}

LUMA_TEST(vm_generic_swap_two_params) {
    // Two-type-parameter generic returning a tuple with swapped elements.
    const auto result = eval("function<T, U> (U, T) swap(T a, U b) { return (b, a) }\n"
                             "function string f() {\n"
                             "    (string, integer) p = swap(1, \"hi\")\n"
                             "    return p.0\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "hi");
}

LUMA_TEST(vm_generic_returns_generic_record) {
    // A generic function constructing and returning a generic record.
    const auto result = eval("record Box<T> { T value }\n"
                             "function<T> Box<T> wrap(T v) { return Box<T> { value = v } }\n"
                             "function integer f() {\n"
                             "    Box<integer> b = wrap(7)\n"
                             "    return b.value\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 7);
}

LUMA_TEST(vm_trusted_downcast_success) {
    // trusted_downcast from an interface-typed value to the concrete record
    // returns the value directly (no result wrapper).
    const auto result = eval("interface HasId { integer id }\n"
                             "record User { integer id, string name }\n"
                             "function integer f() {\n"
                             "    HasId thing = User { id = 5, name = \"Ada\" }\n"
                             "    User u = trusted_downcast<User>(thing)\n"
                             "    return u.id\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 5);
}

LUMA_TEST(vm_trusted_downcast_throws) {
    // trusted_downcast to an incompatible type throws a RuntimeError.
    ASSERT_THROWS(eval("function string f(integer x) { return trusted_downcast<string>(x) }\n"
                       "f(42)"));
}

LUMA_TEST(vm_downcast_interface_to_record) {
    // downcast from an interface-typed value to the concrete record succeeds.
    const auto result = eval("interface HasId { integer id }\n"
                             "record User { integer id, string name }\n"
                             "function boolean f() {\n"
                             "    HasId thing = User { id = 1, name = \"Bo\" }\n"
                             "    result<User> r = downcast<User>(thing)\n"
                             "    return Result.is_success(r)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

// ─── Nested type-argument verification for is<T> / downcast<T> ───
// Every nesting level of array / dictionary / result / reference / queue /
// stack / set / optional / tuple type arguments is checked at runtime.

LUMA_TEST(vm_is_nested_array_match) {
    const auto result = eval("function boolean f() {\n"
                             "    array<array<integer>> v = [[1, 2], [3]]\n"
                             "    return is<array<array<integer>>>(v)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_is_nested_array_mismatch) {
    // Deepest element type (string) differs from the queried element type.
    const auto result = eval("function boolean f() {\n"
                             "    array<array<string>> v = [[\"a\"], [\"b\"]]\n"
                             "    return is<array<array<integer>>>(v)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(!result.as_bool());
}

LUMA_TEST(vm_is_dictionary_value_match) {
    const auto result = eval("function boolean f() {\n"
                             "    dictionary<integer> d = { \"a\": 1, \"b\": 2 }\n"
                             "    return is<dictionary<integer>>(d)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_is_dictionary_value_mismatch) {
    // Dictionary value type must be verified: string values are not integers.
    const auto result = eval("function boolean f() {\n"
                             "    dictionary<string> d = { \"a\": \"x\" }\n"
                             "    return is<dictionary<integer>>(d)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(!result.as_bool());
}

LUMA_TEST(vm_is_dictionary_nested_array_mismatch) {
    const auto result = eval("function boolean f() {\n"
                             "    dictionary<array<string>> d = { \"a\": [\"x\"] }\n"
                             "    return is<dictionary<array<integer>>>(d)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(!result.as_bool());
}

LUMA_TEST(vm_is_result_inner_match) {
    const auto result = eval("function boolean f() {\n"
                             "    result<integer> r = success(7)\n"
                             "    return is<result<integer>>(r)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_is_result_inner_mismatch) {
    // A success result carrying a string is not a result<integer>.
    const auto result = eval("function boolean f() {\n"
                             "    result<string> r = success(\"hi\")\n"
                             "    return is<result<integer>>(r)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(!result.as_bool());
}

LUMA_TEST(vm_is_result_two_param_error_match) {
    // result<T, E>: a failure's error value is verified against E.
    const auto result = eval("function boolean f() {\n"
                             "    result<integer, string> r = failure(\"boom\")\n"
                             "    return is<result<integer, string>>(r)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_is_reference_inner_mismatch) {
    const auto result = eval("function boolean f() {\n"
                             "    reference<string> r = Reference.new(\"hi\")\n"
                             "    return is<reference<integer>>(r)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(!result.as_bool());
}

LUMA_TEST(vm_is_reference_inner_match) {
    const auto result = eval("function boolean f() {\n"
                             "    reference<integer> r = Reference.new(5)\n"
                             "    return is<reference<integer>>(r)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_is_queue_element_mismatch) {
    const auto result = eval("function boolean f() {\n"
                             "    queue<string> q0 = Queue.new()\n"
                             "    queue<string> q = Queue.enqueue(q0, \"x\")\n"
                             "    return is<queue<integer>>(q)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(!result.as_bool());
}

LUMA_TEST(vm_is_queue_element_match) {
    const auto result = eval("function boolean f() {\n"
                             "    queue<integer> q0 = Queue.new()\n"
                             "    queue<integer> q = Queue.enqueue(q0, 1)\n"
                             "    return is<queue<integer>>(q)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_is_stack_element_match) {
    const auto result = eval("function boolean f() {\n"
                             "    stack<integer> s0 = Stack.new()\n"
                             "    stack<integer> s = Stack.push(s0, 1)\n"
                             "    return is<stack<integer>>(s)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_is_stack_element_mismatch) {
    const auto result = eval("function boolean f() {\n"
                             "    stack<string> s0 = Stack.new()\n"
                             "    stack<string> s = Stack.push(s0, \"x\")\n"
                             "    return is<stack<integer>>(s)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(!result.as_bool());
}

LUMA_TEST(vm_is_set_element_match) {
    const auto result = eval("function boolean f() {\n"
                             "    set<integer> s = Set.from_array([1, 2, 3])\n"
                             "    return is<set<integer>>(s)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_is_set_element_mismatch) {
    const auto result = eval("function boolean f() {\n"
                             "    set<string> s = Set.from_array([\"a\", \"b\"])\n"
                             "    return is<set<integer>>(s)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(!result.as_bool());
}

LUMA_TEST(vm_is_optional_some_match) {
    const auto result = eval("function boolean f() {\n"
                             "    optional<integer> o = some(5)\n"
                             "    return is<optional<integer>>(o)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_is_optional_none_match) {
    // `none` satisfies optional<T> for any T.
    const auto result = eval("function boolean f() {\n"
                             "    optional<integer> o = none\n"
                             "    return is<optional<integer>>(o)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_is_optional_some_mismatch) {
    const auto result = eval("function boolean f() {\n"
                             "    optional<string> o = some(\"hi\")\n"
                             "    return is<optional<integer>>(o)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(!result.as_bool());
}

LUMA_TEST(vm_is_tuple_nested_element_mismatch) {
    // Nested generic element type inside a tuple is verified.
    const auto result = eval("function boolean f() {\n"
                             "    (integer, array<string>) t = (1, [\"x\"])\n"
                             "    return is<(integer, array<integer>)>(t)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(!result.as_bool());
}

LUMA_TEST(vm_is_tuple_nested_element_match) {
    const auto result = eval("function boolean f() {\n"
                             "    (integer, array<integer>) t = (1, [2, 3])\n"
                             "    return is<(integer, array<integer>)>(t)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_downcast_dictionary_value_mismatch_fails) {
    // downcast verifying nested value type: a string-valued dictionary cannot
    // be downcast to dictionary<integer>.
    const auto result =
        eval("function boolean f() {\n"
             "    dictionary<string> d = { \"a\": \"x\" }\n"
             "    result<dictionary<integer>> r = downcast<dictionary<integer>>(d)\n"
             "    return Result.is_success(r)\n"
             "}\n"
             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(!result.as_bool());
}

LUMA_TEST(vm_downcast_dictionary_value_match_succeeds) {
    const auto result =
        eval("function boolean f() {\n"
             "    dictionary<integer> d = { \"a\": 1 }\n"
             "    result<dictionary<integer>> r = downcast<dictionary<integer>>(d)\n"
             "    return Result.is_success(r)\n"
             "}\n"
             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

// ─── Main ───

int main() {
    LUMA_RUN_ALL();
}
