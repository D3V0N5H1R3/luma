// Standard library tests: Stack.

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "stdlib_test_helpers.hpp"

using luma::test::eval;

static void test_stack_concat() {
    const auto v = eval("Stack.to_array(Stack.concat("
                        "Stack.from_array([1, 2]),"
                        "Stack.from_array([3, 4])))");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 4U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*v.as_array()->elements)[3].as_integer(), 4);
}

static void test_stack_from_array() {
    const auto result = eval("Stack.from_array([1, 2, 3]) |> Stack.length()");

    ASSERT_EQ(result.as_integer(), 3);
}

static void test_stack_is_empty() {
    const auto result = eval(R"(
        Stack.is_empty(Stack.new())
    )");

    ASSERT_TRUE(result.is_truthy());
}

static void test_stack_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Stack.new"));
    ASSERT_TRUE(env->has("Stack.from_array"));
    ASSERT_TRUE(env->has("Stack.push"));
    ASSERT_TRUE(env->has("Stack.pop"));
    ASSERT_TRUE(env->has("Stack.peek"));
    ASSERT_TRUE(env->has("Stack.length"));
    ASSERT_TRUE(env->has("Stack.is_empty"));
    ASSERT_TRUE(env->has("Stack.to_array"));
    ASSERT_TRUE(env->has("Stack.map"));
    ASSERT_TRUE(env->has("Stack.filter"));
    ASSERT_TRUE(env->has("Stack.reduce"));
    ASSERT_TRUE(env->has("Stack.each"));
    ASSERT_TRUE(env->has("Stack.partition"));
    ASSERT_TRUE(env->has("Stack.concat"));
}

static void test_stack_partition() {
    const auto v = eval("Stack.partition("
                        "Stack.from_array([1, 2, 3, 4, 5]),"
                        "(integer x) -> x % 2 == 0)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& tup = v.as_result()->owned_inner->as_tuple()->elements;

    ASSERT_EQ(tup.size(), 2U);
    ASSERT_EQ(tup[0].as_stack()->elements.size(), 2U);
    ASSERT_EQ(tup[1].as_stack()->elements.size(), 3U);
}

static void test_stack_peek() {
    const auto result = eval("Stack.from_array([10, 20, 30]) |> Stack.peek() |> Result.unwrap()");

    ASSERT_EQ(result.as_integer(), 30);
}

static void test_stack_push_pop() {
    const auto result = eval("Stack.push(Stack.new(), 42) |> Stack.pop() |> Result.unwrap()");

    ASSERT_TRUE(result.is_tuple());

    const auto& elems = result.as_tuple()->elements;

    ASSERT_EQ(elems[0].as_integer(), 42);
}

static void test_stack_new_length_zero() {
    ASSERT_EQ(eval("Stack.new() |> Stack.length()").as_integer(), 0);
}

static void test_stack_push_lifo_order() {
    // Push appends to the top; to_array lists elements bottom-to-top, so the
    // last value pushed is the final array element.
    const auto v = eval("Stack.new()"
                        " |> Stack.push(1)"
                        " |> Stack.push(2)"
                        " |> Stack.push(3)"
                        " |> Stack.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*v.as_array()->elements)[2].as_integer(), 3);
}

static void test_stack_pop_returns_top() {
    // Pop removes the most recently pushed element (LIFO) and returns
    // (top, remaining); the remainder keeps its bottom-to-top order.
    const auto v = eval("Stack.from_array([1, 2, 3])"
                        " |> Stack.pop()"
                        " |> Result.unwrap()");

    ASSERT_TRUE(v.is_tuple());

    const auto& elems = v.as_tuple()->elements;

    ASSERT_EQ(elems[0].as_integer(), 3);

    const auto& rest = elems[1].as_stack()->elements;

    ASSERT_EQ(rest.size(), 2U);
    ASSERT_EQ(rest[0].as_integer(), 1);
    ASSERT_EQ(rest[1].as_integer(), 2);
}

static void test_stack_push_is_immutable() {
    // Push is persistent: the source stack is left unchanged.
    const auto v = eval(R"(
        stack<integer> original = Stack.from_array([1, 2])
        stack<integer> extended = Stack.push(original, 3)
        Stack.length(original) + 100 * Stack.length(extended)
    )");

    ASSERT_EQ(v.as_integer(), 302);
}

static void test_stack_peek_does_not_remove() {
    // Peek inspects the top without consuming it.
    const auto v = eval(R"(
        stack<integer> s = Stack.from_array([10, 20])
        integer top = Stack.peek(s) |> Result.unwrap()
        top + 100 * Stack.length(s)
    )");

    ASSERT_EQ(v.as_integer(), 220);
}

static void test_stack_map() {
    const auto v = eval("Stack.from_array([1, 2, 3])"
                        " |> Stack.map((integer x) -> x * 10)"
                        " |> Result.unwrap()"
                        " |> Stack.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 10);
    ASSERT_EQ((*v.as_array()->elements)[2].as_integer(), 30);
}

static void test_stack_filter() {
    const auto v = eval("Stack.from_array([1, 2, 3, 4])"
                        " |> Stack.filter((integer x) -> x % 2 == 0)"
                        " |> Result.unwrap()"
                        " |> Stack.length()");

    ASSERT_EQ(v.as_integer(), 2);
}

static void test_stack_reduce() {
    const auto v = eval("Stack.from_array([1, 2, 3, 4])"
                        " |> Stack.reduce(0, (integer acc, integer x) -> acc + x)"
                        " |> Result.unwrap()");

    ASSERT_EQ(v.as_integer(), 10);
}

static void test_stack_each() {
    const auto v = eval("Stack.from_array([1, 2, 3]) |> Stack.each((integer x) -> x)");

    ASSERT_RESULT_SUCCESS(v);

    // each visits every element for its side effects.
    ASSERT_EQ(eval("reference<integer> total = Reference.new(0)\n"
                   "result<none> _r = Stack.each(Stack.from_array([10, 20, 30]),"
                   "(integer x) -> { Reference.set(total, Reference.get(total) + x) })\n"
                   "Reference.get(total)")
                  .as_integer(),
              60);
}

static void test_stack_to_array() {
    const auto v = eval("Stack.from_array([1, 2, 3]) |> Stack.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
}

// ─── Negative: empty-container failures ───────────────────────────

static void test_stack_pop_empty_fails() {
    ASSERT_RESULT_FAILURE(eval("Stack.new() |> Stack.pop()"));
}

// ─── Predicate queries, membership, pop_while, reverse ────────────

static void test_stack_any_all_count() {
    ASSERT_EQ(eval("Stack.from_array([1,2,3,4]) |> Stack.any((integer x) -> x > 3) "
                   "|> Result.unwrap()")
                  .as_bool(),
              true);
    ASSERT_EQ(eval("Stack.from_array([1,2,3,4]) |> Stack.all((integer x) -> x > 3) "
                   "|> Result.unwrap()")
                  .as_bool(),
              false);
    ASSERT_EQ(eval("Stack.from_array([1,2,3,4]) |> Stack.count((integer x) -> x % 2 == 0) "
                   "|> Result.unwrap()")
                  .as_integer(),
              2);
}

static void test_stack_contains_find() {
    ASSERT_EQ(eval("Stack.from_array([1,2,3]) |> Stack.contains(2)").as_bool(), true);
    ASSERT_EQ(eval("Stack.from_array([1,2,3]) |> Stack.contains(9)").as_bool(), false);

    // find returns the first match from the top (back).
    ASSERT_EQ(eval("Stack.from_array([1,2,3,4]) |> Stack.find((integer x) -> x % 2 == 0) "
                   "|> Result.unwrap()")
                  .as_integer(),
              4);
    ASSERT_RESULT_FAILURE(eval("Stack.from_array([1,3]) |> Stack.find((integer x) -> x > 9)"));
}

static void test_stack_pop_while() {
    // Pop the top run of even numbers (top-first: 4, then 2 stops at odd 3).
    const auto v = eval("Stack.from_array([1, 3, 2, 4]) "
                        "|> Stack.pop_while((integer x) -> x % 2 == 0) |> Result.unwrap()");

    // v is a tuple (array, stack). The popped array is [4, 2] (top-first).
    ASSERT_TRUE(v.is_tuple());
    const auto& popped = v.as_tuple()->elements.at(0);

    ASSERT_TRUE(popped.is_array());
    ASSERT_EQ(popped.as_array()->elements->size(), 2U);
    ASSERT_EQ((*popped.as_array()->elements)[0].as_integer(), 4);
    ASSERT_EQ((*popped.as_array()->elements)[1].as_integer(), 2);

    // Remaining stack keeps [1, 3].
    const auto& rest = v.as_tuple()->elements.at(1);

    ASSERT_EQ(rest.as_stack()->elements.size(), 2U);
}

static void test_stack_reverse() {
    // reverse then to_array: [1,2,3] -> [3,2,1].
    const auto v = eval("Stack.from_array([1,2,3]) |> Stack.reverse() |> Stack.to_array()");

    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 3);
    ASSERT_EQ((*v.as_array()->elements)[2].as_integer(), 1);
}

static void test_stack_peek_empty_fails() {
    ASSERT_RESULT_FAILURE(eval("Stack.new() |> Stack.peek()"));
}

// ─── Negative: wrong-argument-type errors ─────────────────────────

// (source, expected error message) for each operation given a wrong-typed
// receiver or argument.
constexpr std::array<std::pair<std::string_view, std::string_view>, 6> wrong_type_cases{{
    {"Stack.length(42)", "Stack.length: expected stack, got 'integer'"},
    {"Stack.push(42, 1)", "Stack.push: expected stack, got 'integer'"},
    {"Stack.pop(42)", "Stack.pop: expected stack, got 'integer'"},
    {R"(Stack.peek("x"))", "Stack.peek: expected stack, got 'string'"},
    {"Stack.from_array(42)", "Stack.from_array: expected array, got 'integer'"},
    {"Stack.concat(Stack.new(), 42)", "Stack.concat: expected stack, got 'integer'"},
}};

static void test_stack_wrong_type_throws(std::string_view source, std::string_view message) {
    ASSERT_EVAL_THROWS(std::string{source}, message);
}

int main() {
    RUN(test_stack_concat);
    RUN(test_stack_from_array);
    RUN(test_stack_is_empty);
    RUN(test_stack_module);
    RUN(test_stack_partition);
    RUN(test_stack_peek);
    RUN(test_stack_push_pop);
    RUN(test_stack_new_length_zero);
    RUN(test_stack_push_lifo_order);
    RUN(test_stack_pop_returns_top);
    RUN(test_stack_push_is_immutable);
    RUN(test_stack_peek_does_not_remove);
    RUN(test_stack_map);
    RUN(test_stack_filter);
    RUN(test_stack_reduce);
    RUN(test_stack_each);
    RUN(test_stack_to_array);
    RUN(test_stack_pop_empty_fails);
    RUN(test_stack_peek_empty_fails);
    RUN(test_stack_any_all_count);
    RUN(test_stack_contains_find);
    RUN(test_stack_pop_while);
    RUN(test_stack_reverse);
    RUN_PARAMETERIZED(test_stack_wrong_type_throws, wrong_type_cases);
    return SUMMARY();
}
