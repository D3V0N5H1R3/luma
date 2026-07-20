// VM unit tests: concurrency (tasks, channels, scopes).

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

// ─── Concurrency tests ───

LUMA_TEST(vm_spawn_await) {
    const auto result = eval("function integer work(integer x) { return x * 2 }\n"
                             "function integer f() {\n"
                             "    task<integer> t = spawn work(21)\n"
                             "    return await t\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_task_scope) {
    const auto result = eval("function integer work(integer x) { return x * 2 }\n"
                             "function integer f() {\n"
                             "    mutable integer val = 0\n"
                             "    task_scope {\n"
                             "        task<integer> t = spawn work(21)\n"
                             "        val = await t\n"
                             "    }\n"
                             "    return val\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_task_scope_multiple_spawns) {
    const auto result = eval("function integer square(integer x) { return x * x }\n"
                             "function integer f() {\n"
                             "    mutable integer total = 0\n"
                             "    task_scope {\n"
                             "        task<integer> t1 = spawn square(2)\n"
                             "        task<integer> t2 = spawn square(3)\n"
                             "        task<integer> t3 = spawn square(4)\n"
                             "        total = await t1 + await t2 + await t3\n"
                             "    }\n"
                             "    return total\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 29); // 4+9+16
}

LUMA_TEST(vm_task_scope_nested) {
    const auto result = eval("function integer square(integer x) { return x * x }\n"
                             "function integer f() {\n"
                             "    mutable integer outer_val = 0\n"
                             "    task_scope {\n"
                             "        task<integer> t1 = spawn square(3)\n"
                             "        mutable integer inner_val = 0\n"
                             "        task_scope {\n"
                             "            task<integer> t2 = spawn square(5)\n"
                             "            inner_val = await t2\n"
                             "        }\n"
                             "        outer_val = await t1 + inner_val\n"
                             "    }\n"
                             "    return outer_val\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 34); // 9+25
}

LUMA_TEST(vm_task_cancel_and_is_cancelled) {
    const auto result = eval("function integer work(integer x) { return x * 2 }\n"
                             "function boolean f() {\n"
                             "    mutable boolean cancelled = false\n"
                             "    task_scope {\n"
                             "        task<integer> t = spawn work(5)\n"
                             "        integer _r = await t\n"
                             "        boolean _c = Task.cancel(t)\n"
                             "        cancelled = Task.is_cancelled(t)\n"
                             "    }\n"
                             "    return cancelled\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_task_is_cancelled_initially_false) {
    const auto result = eval("function integer work(integer x) { return x * 2 }\n"
                             "function boolean f() {\n"
                             "    mutable boolean cancelled = true\n"
                             "    task_scope {\n"
                             "        task<integer> t = spawn work(5)\n"
                             "        integer _r = await t\n"
                             "        cancelled = Task.is_cancelled(t)\n"
                             "    }\n"
                             "    return cancelled\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_FALSE(result.as_bool());
}

// ─── Exceptional task-scope teardown (regression for retire_task_scope) ───
//
// Both exit paths from a task_scope share one cancel → join → restore → pop
// teardown sequence (VM::retire_task_scope). These two tests drive that sequence
// through its two callers and assert the error surfaces cleanly — no crash,
// deadlock, or double-pop — rather than exercising only the happy path above.

// An error raised inside the scope body unwinds past the still-open scope, so
// exception dispatch retires it via unwind_task_scopes_to (cancelling the
// spawned child on the way out).
LUMA_TEST(vm_task_scope_body_error_unwinds_scope) {
    ASSERT_TRUE(throws_runtime("function integer work(integer x) { return x * 2 }\n"
                               "function integer f() {\n"
                               "    task_scope {\n"
                               "        task<integer> _t = spawn work(5)\n"
                               "        integer _bad = [1, 2, 3][10]\n"
                               "    }\n"
                               "    return 0\n"
                               "}\n"
                               "f()"));
}

// A spawned child that fails but is never awaited surfaces its error from
// join_all() at scope end, driving handle_task_scope_end's catch-path teardown.
LUMA_TEST(vm_task_scope_unawaited_child_error_propagates) {
    ASSERT_TRUE(throws_runtime("function integer boom() { return [1, 2, 3][10] }\n"
                               "function integer f() {\n"
                               "    task_scope {\n"
                               "        task<integer> _t = spawn boom()\n"
                               "    }\n"
                               "    return 0\n"
                               "}\n"
                               "f()"));
}

// ─── Main ───

int main() {
    LUMA_RUN_ALL();
}
