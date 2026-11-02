// Resource limit enforcement unit tests.
//
// Verifies that the runtime correctly rejects operations that would
// exceed configured resource limits.  Each test temporarily lowers a
// limit, exercises the guarded operation, and restores the original
// value via RAII.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "stdlib_test_helpers.hpp"

using luma::ResourceLimits;
using luma::test::CapturedStream;
using luma::test::eval;
using luma::test::LimitGuard;
using luma::test::ScopedEnv;

namespace {

// Assert that `result` is a failure result whose error message contains
// `expected` (so the failure is the limit under test, not an unrelated one).
void assert_failure_mentions(const luma::Value& result, std::string_view expected) {
    ASSERT_RESULT_FAILURE(result);
    const auto message = result.as_result()->owned_inner->to_string();
    ASSERT_NE(message.find(expected), std::string::npos);
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Call depth limit
// ═══════════════════════════════════════════════════════════

static void test_call_depth_limit_enforced() {
    const LimitGuard guard{ResourceLimits::max_call_depth, 5};

    // Recursive function that exceeds depth 5.
    ASSERT_EVAL_THROWS(R"(
        function integer recurse(integer n) {
            return 1 + recurse(n + 1)
        }
        recurse(0)
    )",
                       "maximum recursion depth (5) exceeded");
}

static void test_call_depth_within_limit_succeeds() {
    const LimitGuard guard{ResourceLimits::max_call_depth, 20};

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
    const LimitGuard guard{ResourceLimits::max_while_iterations, static_cast<std::int64_t>(10)};

    // Infinite loop should be stopped at 10 iterations.
    ASSERT_EVAL_THROWS(R"(
        mutable integer i = 0
        while true {
            i += 1
        }
    )",
                       "loop exceeded maximum iteration count (10)");
}

static void test_while_loop_within_limit_succeeds() {
    const LimitGuard guard{ResourceLimits::max_while_iterations, static_cast<std::int64_t>(100)};

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
// Total loop iteration budget
// ═══════════════════════════════════════════════════════════

static void test_nested_for_loops_hit_total_budget() {
    // Each inner `for` resets the shared while budget on entry, so only the
    // total budget can bound the 10,000 iterations of this nest.
    const LimitGuard guard{ResourceLimits::max_total_loop_iterations,
                           static_cast<std::int64_t>(1000)};

    ASSERT_EVAL_THROWS(R"(
        mutable integer n = 0
        for a in 0..100 {
            for b in 0..100 {
                n += 1
            }
        }
    )",
                       "total iteration budget");
}

static void test_sequential_for_loops_accumulate_total_budget() {
    const LimitGuard guard{ResourceLimits::max_total_loop_iterations,
                           static_cast<std::int64_t>(1000)};

    // 600 + 600 back-edges: neither loop alone exceeds the budget.
    ASSERT_EVAL_THROWS(R"(
        mutable integer n = 0
        for a in 0..600 {
            n += 1
        }
        for b in 0..600 {
            n += 1
        }
    )",
                       "total iteration budget");
}

static void test_total_budget_survives_a_caught_error() {
    const LimitGuard guard{ResourceLimits::max_total_loop_iterations,
                           static_cast<std::int64_t>(50)};

    // Catching the error must not hand the program a fresh budget.
    ASSERT_EVAL_THROWS(R"(
        mutable integer n = 0
        try {
            for a in 0..100 {
                n += 1
            }
        } catch(_err) {
            n = 0
        }
        for b in 0..2 {
            n += 1
        }
    )",
                       "total iteration budget");
}

static void test_loops_within_total_budget_succeed() {
    const LimitGuard guard{ResourceLimits::max_total_loop_iterations,
                           static_cast<std::int64_t>(1000)};

    const auto result = eval(R"(
        mutable integer n = 0
        for a in 0..10 {
            for b in 0..10 {
                n += 1
            }
        }
        n
    )");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 100);
}

// ═══════════════════════════════════════════════════════════
// Array size limit
// ═══════════════════════════════════════════════════════════

static void test_array_push_limit_enforced() {
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(3)};

    // Build an array of 3, then try to push a 4th — should throw.
    ASSERT_EVAL_THROWS(R"(
        mutable array<integer> arr = [1, 2, 3]
        arr = Array.push(arr, 4)
    )",
                       "Array.push: exceeds maximum size");
}

static void test_array_within_limit_succeeds() {
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(10)};

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
    const LimitGuard guard{ResourceLimits::max_string_size, static_cast<std::size_t>(10)};

    // Concatenating strings that exceed 10 bytes should throw.
    ASSERT_EVAL_THROWS(R"(
        string a = "hello "
        string b = "world!!"
        a + b
    )",
                       "string concatenation exceeds maximum string size");
}

static void test_string_concat_within_limit_succeeds() {
    const LimitGuard guard{ResourceLimits::max_string_size, static_cast<std::size_t>(100)};

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
    const LimitGuard guard{ResourceLimits::max_string_repeat, static_cast<std::int64_t>(5)};

    // Repeating 6 times should produce a failure result.
    assert_failure_mentions(eval(R"(String.repeat("ab", 6))"),
                            "String.repeat: count exceeds maximum");
}

static void test_string_repeat_within_limit_succeeds() {
    const LimitGuard guard{ResourceLimits::max_string_repeat, static_cast<std::int64_t>(10)};

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
    const LimitGuard guard{ResourceLimits::max_dictionary_size, static_cast<std::size_t>(2)};

    // Creating a dictionary with 3 entries should throw.
    ASSERT_EVAL_THROWS(R"(
        mutable dictionary<integer> d = {"a": 1, "b": 2}
        d = Dictionary.set(d, "c", 3)
    )",
                       "Dictionary.set: result exceeds maximum dictionary size");
}

static void test_dictionary_within_limit_succeeds() {
    const LimitGuard guard{ResourceLimits::max_dictionary_size, static_cast<std::size_t>(10)};

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
    const LimitGuard guard{ResourceLimits::max_queue_size, static_cast<std::size_t>(2)};

    // Enqueuing past the limit should throw.
    ASSERT_EVAL_THROWS(R"(
        mutable queue<integer> q = Queue.from_array([1, 2])
        q = Queue.enqueue(q, 3)
    )",
                       "Queue.enqueue: exceeds maximum size");
}

static void test_queue_within_limit_succeeds() {
    const LimitGuard guard{ResourceLimits::max_queue_size, static_cast<std::size_t>(10)};

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
    const LimitGuard guard{ResourceLimits::max_stack_size, static_cast<std::size_t>(2)};

    ASSERT_EVAL_THROWS(R"(
        mutable stack<integer> s = Stack.from_array([1, 2])
        s = Stack.push(s, 3)
    )",
                       "Stack.push: exceeds maximum size");
}

static void test_stack_within_limit_succeeds() {
    const LimitGuard guard{ResourceLimits::max_stack_size, static_cast<std::size_t>(10)};

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
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(5)};

    // Array.range(0, 10) produces 10 elements — exceeds limit of 5.
    assert_failure_mentions(eval("Array.range(0, 10)"),
                            "Array.range: range exceeds maximum array size");
}

// ═══════════════════════════════════════════════════════════
// Array.repeat limit
// ═══════════════════════════════════════════════════════════

static void test_array_repeat_limit_enforced() {
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(3)};

    assert_failure_mentions(eval("Array.repeat(0, 10)"),
                            "Array.repeat: result exceeds maximum array size");
}

// ═══════════════════════════════════════════════════════════
// String.pad_left / pad_right limit
// ═══════════════════════════════════════════════════════════

static void test_string_pad_limit_enforced() {
    const LimitGuard guard{ResourceLimits::max_pad_width, static_cast<std::size_t>(5)};

    // Padding to width 10 exceeds limit of 5.
    assert_failure_mentions(eval(R"(String.pad_left("hi", 10, " "))"),
                            "String.pad_left: width exceeds maximum");
}

// ═══════════════════════════════════════════════════════════
// LUMA_LIMIT_* environment overrides are range-checked
// (regression: 0 and arbitrarily large values were accepted)
// ═══════════════════════════════════════════════════════════

static void test_env_override_zero_is_ignored() {
    const LimitGuard guard{ResourceLimits::max_parse_depth, 128};
    const ScopedEnv env{"LUMA_LIMIT_MAX_PARSE_DEPTH", "0"};
    const CapturedStream captured_err{std::cerr};

    ResourceLimits::init_from_env();

    // 0 would make every program fail to parse; the default is kept.
    ASSERT_EQ(ResourceLimits::max_parse_depth, 128);
    ASSERT_NE(captured_err.str().find("LUMA_LIMIT_MAX_PARSE_DEPTH"), std::string::npos);
}

static void test_env_override_above_ceiling_is_clamped() {
    const LimitGuard guard{ResourceLimits::max_parse_depth, 128};
    const ScopedEnv env{"LUMA_LIMIT_MAX_PARSE_DEPTH", "100000000"};
    const CapturedStream captured_err{std::cerr};

    ResourceLimits::init_from_env();

    // A huge parse depth would defeat the native stack-overflow guard.
    ASSERT_EQ(ResourceLimits::max_parse_depth, 512);
    ASSERT_NE(captured_err.str().find("clamping"), std::string::npos);
}

static void test_env_override_in_range_is_applied() {
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(10'000'000)};
    const ScopedEnv env{"LUMA_LIMIT_MAX_ARRAY_SIZE", "1234"};

    ResourceLimits::init_from_env();

    ASSERT_EQ(ResourceLimits::max_array_size, 1234);
}

// ─── main ───

int main() {
    // Environment overrides.
    RUN(test_env_override_zero_is_ignored);
    RUN(test_env_override_above_ceiling_is_clamped);
    RUN(test_env_override_in_range_is_applied);

    // Call depth.
    RUN(test_call_depth_limit_enforced);
    RUN(test_call_depth_within_limit_succeeds);

    // While loop iterations.
    RUN(test_while_loop_limit_enforced);
    RUN(test_while_loop_within_limit_succeeds);
    RUN(test_nested_for_loops_hit_total_budget);
    RUN(test_sequential_for_loops_accumulate_total_budget);
    RUN(test_total_budget_survives_a_caught_error);
    RUN(test_loops_within_total_budget_succeed);

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
