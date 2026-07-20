// Resource limit enforcement unit tests.
//
// Verifies that the runtime correctly rejects operations that would
// exceed configured resource limits.  Each test temporarily lowers a
// limit, exercises the guarded operation, and restores the original
// value via RAII.

#include <cstdint>
#include <stdexcept>
#include <string>

#include "analysis/errors/error.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "stdlib_test_helpers.hpp"

using namespace luma;

// ═══════════════════════════════════════════════════════════
// Call depth limit
// ═══════════════════════════════════════════════════════════

static void test_call_depth_limit_enforced() {
    LimitGuard guard{ResourceLimits::max_call_depth, 5};

    // Recursive function that exceeds depth 5.
    ASSERT_TRUE(throws_runtime(R"(
        function integer recurse(integer n) {
            return 1 + recurse(n + 1)
        }
        recurse(0)
    )"));
}

static void test_call_depth_within_limit_succeeds() {
    LimitGuard guard{ResourceLimits::max_call_depth, 20};

    // Only 3 levels of recursion — well within the limit.
    const auto result = eval(R"(
        function integer count_down(integer n) {
            if n <= 0 { return 0 }
            return 1 + count_down(n - 1)
        }
        count_down(3)
    )");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 3);
}

// ═══════════════════════════════════════════════════════════
// While loop iteration limit
// ═══════════════════════════════════════════════════════════

static void test_while_loop_limit_enforced() {
    LimitGuard guard{ResourceLimits::max_while_iterations, static_cast<std::int64_t>(10)};

    // Infinite loop should be stopped at 10 iterations.
    ASSERT_TRUE(throws_runtime(R"(
        mutable integer i = 0
        while true {
            i += 1
        }
    )"));
}

static void test_while_loop_within_limit_succeeds() {
    LimitGuard guard{ResourceLimits::max_while_iterations, static_cast<std::int64_t>(100)};

    const auto result = eval(R"(
        mutable integer i = 0
        while i < 5 {
            i += 1
        }
        i
    )");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 5);
}

// ═══════════════════════════════════════════════════════════
// Array size limit
// ═══════════════════════════════════════════════════════════

static void test_array_push_limit_enforced() {
    LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(3)};

    // Build an array of 3, then try to push a 4th — should throw.
    ASSERT_TRUE(throws_runtime(R"(
        mutable array<integer> arr = [1, 2, 3]
        arr = Array.push(arr, 4)
    )"));
}

static void test_array_within_limit_succeeds() {
    LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(10)};

    const auto result = eval(R"(
        array<integer> arr = Array.push([1, 2, 3], 4)
        Array.length(arr)
    )");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 4);
}

// ═══════════════════════════════════════════════════════════
// String size limit
// ═══════════════════════════════════════════════════════════

static void test_string_concat_limit_enforced() {
    LimitGuard guard{ResourceLimits::max_string_size, static_cast<std::size_t>(10)};

    // Concatenating strings that exceed 10 bytes should throw.
    ASSERT_TRUE(throws_runtime(R"(
        string a = "hello "
        string b = "world!!"
        a + b
    )"));
}

static void test_string_concat_within_limit_succeeds() {
    LimitGuard guard{ResourceLimits::max_string_size, static_cast<std::size_t>(100)};

    const auto result = eval(R"(
        "hello" + " world"
    )");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "hello world");
}

// ═══════════════════════════════════════════════════════════
// String repeat limit
// ═══════════════════════════════════════════════════════════

static void test_string_repeat_limit_enforced() {
    LimitGuard guard{ResourceLimits::max_string_repeat, static_cast<std::int64_t>(5)};

    // Repeating 6 times should produce a failure result.
    const auto result = eval(R"(
        result<string> r = String.repeat("ab", 6)
        Result.is_success(r)
    )");

    ASSERT_TRUE(result.is_bool());
    ASSERT_FALSE(result.as_bool());
}

static void test_string_repeat_within_limit_succeeds() {
    LimitGuard guard{ResourceLimits::max_string_repeat, static_cast<std::int64_t>(10)};

    const auto result = eval(R"(
        Result.unwrap(String.repeat("ab", 3))
    )");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "ababab");
}

// ═══════════════════════════════════════════════════════════
// Dictionary size limit
// ═══════════════════════════════════════════════════════════

static void test_dictionary_limit_enforced() {
    LimitGuard guard{ResourceLimits::max_dictionary_size, static_cast<std::size_t>(2)};

    // Creating a dictionary with 3 entries should throw.
    ASSERT_TRUE(throws_runtime(R"(
        mutable dictionary<integer> d = {"a": 1, "b": 2}
        d = Dictionary.set(d, "c", 3)
    )"));
}

static void test_dictionary_within_limit_succeeds() {
    LimitGuard guard{ResourceLimits::max_dictionary_size, static_cast<std::size_t>(10)};

    const auto result = eval(R"(
        dictionary<integer> d = Dictionary.set({"a": 1}, "b", 2)
        Dictionary.length(d)
    )");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 2);
}

// ═══════════════════════════════════════════════════════════
// Queue size limit
// ═══════════════════════════════════════════════════════════

static void test_queue_limit_enforced() {
    LimitGuard guard{ResourceLimits::max_queue_size, static_cast<std::size_t>(2)};

    // Enqueuing past the limit should throw.
    ASSERT_TRUE(throws_runtime(R"(
        mutable queue<integer> q = Queue.from_array([1, 2])
        q = Queue.enqueue(q, 3)
    )"));
}

static void test_queue_within_limit_succeeds() {
    LimitGuard guard{ResourceLimits::max_queue_size, static_cast<std::size_t>(10)};

    const auto result = eval(R"(
        queue<integer> q = Queue.enqueue(Queue.new(), 42)
        Queue.length(q)
    )");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 1);
}

// ═══════════════════════════════════════════════════════════
// Stack size limit
// ═══════════════════════════════════════════════════════════

static void test_stack_limit_enforced() {
    LimitGuard guard{ResourceLimits::max_stack_size, static_cast<std::size_t>(2)};

    ASSERT_TRUE(throws_runtime(R"(
        mutable stack<integer> s = Stack.from_array([1, 2])
        s = Stack.push(s, 3)
    )"));
}

static void test_stack_within_limit_succeeds() {
    LimitGuard guard{ResourceLimits::max_stack_size, static_cast<std::size_t>(10)};

    const auto result = eval(R"(
        stack<integer> s = Stack.push(Stack.new(), 99)
        Stack.length(s)
    )");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 1);
}

// ═══════════════════════════════════════════════════════════
// Array.range limit
// ═══════════════════════════════════════════════════════════

static void test_array_range_limit_enforced() {
    LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(5)};

    // Array.range(0, 10) produces 10 elements — exceeds limit of 5.
    const auto result = eval(R"(
        result<array<integer>> r = Array.range(0, 10)
        Result.is_success(r)
    )");

    ASSERT_TRUE(result.is_bool());
    ASSERT_FALSE(result.as_bool());
}

// ═══════════════════════════════════════════════════════════
// Array.repeat limit
// ═══════════════════════════════════════════════════════════

static void test_array_repeat_limit_enforced() {
    LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(3)};

    const auto result = eval(R"(
        result<array<integer>> r = Array.repeat(0, 10)
        Result.is_success(r)
    )");

    ASSERT_TRUE(result.is_bool());
    ASSERT_FALSE(result.as_bool());
}

// ═══════════════════════════════════════════════════════════
// String.pad_left / pad_right limit
// ═══════════════════════════════════════════════════════════

static void test_string_pad_limit_enforced() {
    LimitGuard guard{ResourceLimits::max_pad_width, static_cast<std::size_t>(5)};

    // Padding to width 10 exceeds limit of 5.
    const auto result = eval(R"(
        result<string> r = String.pad_left("hi", 10, " ")
        Result.is_success(r)
    )");

    ASSERT_TRUE(result.is_bool());
    ASSERT_FALSE(result.as_bool());
}

// ─── main ───

int main() {
    // Call depth.
    RUN(test_call_depth_limit_enforced);
    RUN(test_call_depth_within_limit_succeeds);

    // While loop iterations.
    RUN(test_while_loop_limit_enforced);
    RUN(test_while_loop_within_limit_succeeds);

    // Array size.
    RUN(test_array_push_limit_enforced);
    RUN(test_array_within_limit_succeeds);
    RUN(test_array_range_limit_enforced);
    RUN(test_array_repeat_limit_enforced);

    // String size.
    RUN(test_string_concat_limit_enforced);
    RUN(test_string_concat_within_limit_succeeds);

    // String repeat.
    RUN(test_string_repeat_limit_enforced);
    RUN(test_string_repeat_within_limit_succeeds);

    // String pad.
    RUN(test_string_pad_limit_enforced);

    // Dictionary size.
    RUN(test_dictionary_limit_enforced);
    RUN(test_dictionary_within_limit_succeeds);

    // Queue size.
    RUN(test_queue_limit_enforced);
    RUN(test_queue_within_limit_succeeds);

    // Stack size.
    RUN(test_stack_limit_enforced);
    RUN(test_stack_within_limit_succeeds);

    return SUMMARY();
}
