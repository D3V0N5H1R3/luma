// VM unit tests: concurrency (tasks, channels, scopes).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/errors/error.hpp"
#include "common/resource_limits.hpp"
#include "runtime/compiler/compiled_function.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/environment.hpp"
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

// ─── Review regressions: cancellation, fail-fast, pool, scope unwinding ───

// Review #3/#6: every task owns its cancellation token.  Task.cancel(a) used
// to cancel the whole scope's shared token — so a sibling `b` could be
// cancelled too — and the cancelled task surfaced from the scope as a plain
// "task cancelled" failure.  Now only `a` is cancelled and the scope ends
// cleanly.
LUMA_TEST(vm_task_cancel_affects_only_that_task) {
    const auto result = eval("function integer spin(integer n) {\n"
                             "    mutable integer i = 0\n"
                             "    while i < n { i += 1 }\n"
                             "    return n\n"
                             "}\n"
                             "function string f() {\n"
                             "    mutable string outcome = \"\"\n"
                             "    try {\n"
                             "        task_scope {\n"
                             "            task<integer> a = spawn spin(5000000)\n"
                             "            task<integer> b = spawn spin(10)\n"
                             "            boolean _c = Task.cancel(a)\n"
                             "            integer first = await b\n"
                             "            integer second = await b\n"
                             "            boolean a_cancelled = Task.is_cancelled(a)\n"
                             "            boolean b_cancelled = Task.is_cancelled(b)\n"
                             "            outcome = \"${first} ${second} ${a_cancelled} "
                             "${b_cancelled}\"\n"
                             "        }\n"
                             "    } catch(err) {\n"
                             "        outcome = \"caught: ${err}\"\n"
                             "    }\n"
                             "    return outcome\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "10 10 true false");
}

// Review #8: cancellation is observed at loop back-edges, so cancelling a task
// stuck in an endless loop stops it.  Before, the loop only ended when it hit
// the iteration safety limit, failing the whole scope with that error.  The
// task signals that it is running before it is cancelled — cancelling a task
// that has not started yet ends it before its first instruction — and reports
// from its `finally` where the loop stopped: a task that ends while cancelled
// counts as cancelled however it ends, so only the stopping point shows that
// the back-edge check, not the iteration limit, ended the loop.
LUMA_TEST(vm_cancelled_task_stops_at_loop_back_edge) {
    const auto iteration_limit = std::to_string(ResourceLimits::max_while_iterations);
    const auto result =
        eval("function integer forever(channel<integer> started, "
             "channel<integer> stopped_at) {\n"
             "    mutable integer i = 0\n"
             "    try {\n"
             "        boolean _sent = Channel.send(started, 1)\n"
             "        while true { i += 1 }\n"
             "    } finally {\n"
             "        boolean _reported = Channel.send(stopped_at, i)\n"
             "    }\n"
             "    return i\n"
             "}\n"
             "function string f() {\n"
             "    channel<integer> started = Channel.new()\n"
             "    channel<integer> stopped_at = Channel.new()\n"
             "    mutable string outcome = \"ok\"\n"
             "    try {\n"
             "        task_scope {\n"
             "            task<integer> t = spawn forever(started, stopped_at)\n"
             "            integer _running = Channel.receive(started) |> "
             "Result.unwrap()\n"
             "            boolean _c = Task.cancel(t)\n"
             "        }\n"
             "        result<integer> stopped = Channel.try_receive(stopped_at)\n"
             "        if !(Result.is_success(stopped) && Result.unwrap(stopped) < " +
             iteration_limit +
             ") { outcome = \"loop did not stop at a back-edge: ${stopped}\" }\n"
             "    } catch(err) {\n"
             "        outcome = \"caught: ${err}\"\n"
             "    }\n"
             "    return outcome\n"
             "}\n"
             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "ok");
}

// ─── Code review: cancellation through try/catch/finally ───
//
// A `finally` is compiled as a handler that ends in Op::Rethrow.  It used to
// catch a task's CancelledException and Rethrow re-raised it as a plain
// RuntimeError, so task_scope counted the cancelled task as a failure and the
// whole scope failed with "task cancelled".

// The reported repro: a running task inside try/finally is cancelled.
LUMA_TEST(vm_cancelled_task_in_try_finally_does_not_fail_scope) {
    const auto result =
        eval("function integer count_to(channel<integer> started, integer limit) {\n"
             "    mutable integer counter = 0\n"
             "    try {\n"
             "        boolean _sent = Channel.send(started, 1)\n"
             "        while counter < limit { counter += 1 }\n"
             "    } finally {\n"
             "        counter += 0\n"
             "    }\n"
             "    return counter\n"
             "}\n"
             "function string f() {\n"
             "    channel<integer> started = Channel.new()\n"
             "    mutable string outcome = \"\"\n"
             "    try {\n"
             "        array<integer> results = task_scope {\n"
             "            task<integer> a = spawn count_to(started, 500000000)\n"
             "            task<integer> b = spawn count_to(started, 1000)\n"
             "            integer _first = Channel.receive(started) |> "
             "Result.unwrap()\n"
             "            integer _second = Channel.receive(started) |> "
             "Result.unwrap()\n"
             "            integer _b = await b\n"
             "            boolean _c = Task.cancel(a)\n"
             "        }\n"
             "        outcome = \"${Array.length(results)}\"\n"
             "    } catch(err) {\n"
             "        outcome = \"caught: ${err}\"\n"
             "    }\n"
             "    return outcome\n"
             "}\n"
             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "2");
}

// Cancellation is not an ordinary error: a cancelled task's own `catch` does
// not intercept it (the cancellation propagates, so code after the try never
// runs), but its `finally` runs — to completion, even though the body calls
// functions and loops, which are cancellation points.
LUMA_TEST(vm_cancellation_skips_catch_but_runs_finally) {
    const auto result = eval("function integer worker(channel<integer> started, channel<string> "
                             "log) {\n"
                             "    mutable integer counter = 0\n"
                             "    try {\n"
                             "        boolean _sent = Channel.send(started, 1)\n"
                             "        while true { counter += 1 }\n"
                             "    } catch(err) {\n"
                             "        boolean _c = Channel.send(log, \"catch: ${err}\")\n"
                             "    } finally {\n"
                             "        mutable integer steps = 0\n"
                             "        while steps < 100 { steps += 1 }\n"
                             "        boolean _f = Channel.send(log, \"finally ${steps}\")\n"
                             "    }\n"
                             "    boolean _after = Channel.send(log, \"after try\")\n"
                             "    return counter\n"
                             "}\n"
                             "function string f() {\n"
                             "    channel<integer> started = Channel.new()\n"
                             "    channel<string> log = Channel.new()\n"
                             "    array<integer> results = task_scope {\n"
                             "        task<integer> t = spawn worker(started, log)\n"
                             "        integer _running = Channel.receive(started) |> "
                             "Result.unwrap()\n"
                             "        boolean _c = Task.cancel(t)\n"
                             "    }\n"
                             "    mutable string entries = \"${results}\"\n"
                             "    mutable result<string> entry = Channel.try_receive(log)\n"
                             "    while Result.is_success(entry) {\n"
                             "        entries = \"${entries} | ${Result.unwrap(entry)}\"\n"
                             "        entry = Channel.try_receive(log)\n"
                             "    }\n"
                             "    return entries\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "[none] | finally 100");
}

// Only a task's *own* cancellation bypasses `catch`: awaiting a task that was
// cancelled raises a catchable "task cancelled" error, in a task and on the
// main thread alike.
LUMA_TEST(vm_awaiting_cancelled_task_stays_catchable) {
    const auto result = eval("function integer spin(channel<integer> started) {\n"
                             "    boolean _sent = Channel.send(started, 1)\n"
                             "    mutable integer i = 0\n"
                             "    while true { i += 1 }\n"
                             "    return i\n"
                             "}\n"
                             "function string waits_for(task<integer> t) {\n"
                             "    try {\n"
                             "        integer _v = await t\n"
                             "        return \"not cancelled\"\n"
                             "    } catch(err) {\n"
                             "        return \"task caught: ${err}\"\n"
                             "    }\n"
                             "}\n"
                             "function string f() {\n"
                             "    channel<integer> started = Channel.new()\n"
                             "    mutable string outcome = \"\"\n"
                             "    task_scope {\n"
                             "        task<integer> t = spawn spin(started)\n"
                             "        integer _running = Channel.receive(started) |> "
                             "Result.unwrap()\n"
                             "        boolean _c = Task.cancel(t)\n"
                             "        task<string> w = spawn waits_for(t)\n"
                             "        outcome = await w\n"
                             "        try {\n"
                             "            integer _v = await t\n"
                             "        } catch(err) {\n"
                             "            outcome = \"${outcome}; main caught: ${err}\"\n"
                             "        }\n"
                             "    }\n"
                             "    return outcome\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "task caught: task cancelled; main caught: task cancelled");
}

// Safety net: a task that ends with an ordinary error while it is being
// cancelled — here an error raised by its own `finally` during the unwind — is
// reported as cancelled, not as a failure of the scope.
LUMA_TEST(vm_error_during_cancellation_counts_as_cancelled) {
    const auto result = eval("function integer fragile(channel<integer> started) {\n"
                             "    mutable integer i = 0\n"
                             "    try {\n"
                             "        boolean _sent = Channel.send(started, 1)\n"
                             "        while true { i += 1 }\n"
                             "    } finally {\n"
                             "        integer _bad = [1, 2, 3][10]\n"
                             "    }\n"
                             "    return i\n"
                             "}\n"
                             "function string f() {\n"
                             "    channel<integer> started = Channel.new()\n"
                             "    mutable string outcome = \"\"\n"
                             "    try {\n"
                             "        array<integer> results = task_scope {\n"
                             "            task<integer> t = spawn fragile(started)\n"
                             "            integer _running = Channel.receive(started) |> "
                             "Result.unwrap()\n"
                             "            boolean _c = Task.cancel(t)\n"
                             "        }\n"
                             "        outcome = \"${results}\"\n"
                             "    } catch(err) {\n"
                             "        outcome = \"caught: ${err}\"\n"
                             "    }\n"
                             "    return outcome\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "[none]");
}

// Review #7/#8: join_all is fail-fast in completion order, and blocking channel
// waits are cancellable.  Child 0 blocks forever on an empty channel; child 1
// fails at once.  The old in-spawn-order join waited on child 0 first and,
// since a channel wait never observed cancellation, hung forever.
LUMA_TEST(vm_task_scope_fail_fast_cancels_blocked_sibling) {
    const auto result = eval("function integer wait_forever(channel<integer> ch) {\n"
                             "    return Channel.receive(ch) |> Result.unwrap()\n"
                             "}\n"
                             "function integer boom() { return [1, 2, 3][10] }\n"
                             "function string f() {\n"
                             "    channel<integer> ch = Channel.new()\n"
                             "    mutable string outcome = \"\"\n"
                             "    try {\n"
                             "        task_scope {\n"
                             "            task<integer> _a = spawn wait_forever(ch)\n"
                             "            task<integer> _b = spawn boom()\n"
                             "        }\n"
                             "    } catch(err) {\n"
                             "        outcome = err\n"
                             "    }\n"
                             "    return outcome\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_TRUE(result.as_string().find("out of bounds") != std::string::npos);
}

// Review #1: tasks that open their own task_scope block a pool worker while
// joining.  With more such tasks than workers, every worker used to block on
// children still queued behind them — a deadlock.  The pool now starts a
// compensating worker when all workers are blocked.
LUMA_TEST(vm_nested_task_scopes_do_not_deadlock_the_pool) {
    const auto result = eval("function integer leaf(integer x) { return x }\n"
                             "function integer middle(integer x) {\n"
                             "    array<integer> inner = task_scope {\n"
                             "        spawn leaf(x)\n"
                             "    }\n"
                             "    return inner[0]\n"
                             "}\n"
                             "function integer f() {\n"
                             "    array<integer> results = task_scope {\n"
                             "        for i in 0 .. 128 {\n"
                             "            spawn middle(i)\n"
                             "        }\n"
                             "    }\n"
                             "    return Array.length(results)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 128);
}

// Review #2: a task_scope opened inside a native callback (Array.map) that
// fails is now retired when the error escapes the callback.  It used to stay
// the current scope, so the outer scope's later spawn attached to it and the
// outer TaskScopeEnd joined the wrong scope.
LUMA_TEST(vm_task_scope_left_open_by_failed_callback_is_unwound) {
    const auto result = eval("function integer work(integer x) { return x * 2 }\n"
                             "function integer leaky(integer x) {\n"
                             "    task_scope {\n"
                             "        task<integer> _t = spawn work(x)\n"
                             "        integer _bad = [1, 2, 3][10]\n"
                             "    }\n"
                             "    return x\n"
                             "}\n"
                             "function integer f() {\n"
                             "    array<integer> outer = task_scope {\n"
                             "        spawn work(1)\n"
                             "        result<array<integer>> _mapped = Array.map([1], leaky)\n"
                             "        spawn work(3)\n"
                             "    }\n"
                             "    return outer[0] + outer[1] + Array.length(outer)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 2 + 6 + 2);
}

// Review #13: spawned tasks read a snapshot of the globals and may not write
// them (the live Environment is unsynchronised, so a write raced every reader).
LUMA_TEST(vm_task_reads_global_snapshot) {
    const auto result = eval("integer base = 40\n"
                             "function integer add(integer x) { return base + x }\n"
                             "function integer f() {\n"
                             "    mutable integer total = 0\n"
                             "    task_scope {\n"
                             "        task<integer> t = spawn add(2)\n"
                             "        total = await t\n"
                             "    }\n"
                             "    return total\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_task_global_write_is_rejected) {
    const auto result = eval("mutable integer counter = 0\n"
                             "function integer bump() {\n"
                             "    counter = counter + 1\n"
                             "    return counter\n"
                             "}\n"
                             "function string f() {\n"
                             "    mutable string outcome = \"\"\n"
                             "    try {\n"
                             "        task_scope {\n"
                             "            task<integer> t = spawn bump()\n"
                             "            integer _v = await t\n"
                             "        }\n"
                             "    } catch(err) {\n"
                             "        outcome = err\n"
                             "    }\n"
                             "    return outcome\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_TRUE(result.as_string().find("cannot assign to global variable 'counter' from a "
                                        "spawned task") != std::string::npos);
}

// Code review (task global snapshot): re-snapshotting after a global write
// re-copies only the bindings written since the previous snapshot.  Tasks must
// still see every write made before they were spawned — by assignment
// (SetGlobal) and by in-place element writes (UniqueGlobal).
LUMA_TEST(vm_task_snapshot_sees_latest_global_writes) {
    const auto result = eval("mutable integer n = 0\n"
                             "mutable array<integer> cells = [0]\n"
                             "function integer read_both() { return n * 100 + cells[0] }\n"
                             "function integer f() {\n"
                             "    array<integer> seen = task_scope {\n"
                             "        for i in 1 .. 4 {\n"
                             "            n += 1\n"
                             "            cells[0] = i * 2\n"
                             "            spawn read_both()\n"
                             "        }\n"
                             "    }\n"
                             "    return seen[0] * 1000000 + seen[1] * 1000 + seen[2]\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), (102 * 1000000) + (204 * 1000) + 306);
}

// The copy cache behind the snapshot: an unchanged binding shares its earlier
// copy, a binding with a new write_version is re-copied, and a binding written
// outside the VM (k_unversioned) is always re-copied.
LUMA_TEST(vm_task_snapshot_reuses_unchanged_binding_copies) {
    auto env = std::make_shared<Environment>(nullptr);
    auto big = std::make_shared<ArrayValue>();
    big->elements->assign(1000, Value{std::int64_t{1}});
    env->define("big", Value{big}, true);
    env->define("n", Value{std::int64_t{0}}, true);
    env->define("Math.pi", Value{3.14}, false);

    BindingCopyCache cache;
    const auto first = env->copy_bindings(nullptr, /*mutable_bindings=*/true, cache);

    auto* n = env->find_binding("n");
    n->value = Value{std::int64_t{1}};
    n->write_version = 1;
    const auto second = env->copy_bindings(nullptr, /*mutable_bindings=*/true, cache);

    const auto* big_first = first->find_binding("big")->value.as_array().get();
    const auto* big_second = second->find_binding("big")->value.as_array().get();
    ASSERT_TRUE(big_first != big.get());  // Still an isolated deep copy...
    ASSERT_TRUE(big_first == big_second); // ...made once and then shared.
    ASSERT_EQ(second->find_binding("n")->value.as_integer(), 1);
    ASSERT_FALSE(second->has_local("Math.pi")); // Immutable bindings live in the base layer.

    env->set("big", Value{std::make_shared<ArrayValue>()}, {}); // Written outside the VM.
    const auto third = env->copy_bindings(nullptr, /*mutable_bindings=*/true, cache);
    ASSERT_EQ(third->find_binding("big")->value.as_array()->elements->size(),
              static_cast<std::size_t>(0));
}

// Perf guard for the same review finding: one global write per spawn used to
// re-deep-copy every user global (here a 200,000-element array) on each spawn,
// making the loop ~30x slower than the identical loop without the write.  Only
// the written binding is re-copied now, so the two loops cost about the same.
// The bound compares the two runs, so it holds across build types.
LUMA_TEST(vm_task_snapshot_after_global_write_does_not_recopy_unchanged_globals) {
    const std::string program = "array<integer> big = Array.range(0, 200000) ?? []\n"
                                "mutable integer n = 0\n"
                                "function integer work(integer i) { return big[i] }\n"
                                "function integer spawn_all(boolean write_global) {\n"
                                "    array<integer> results = task_scope {\n"
                                "        for i in 0 .. 300 {\n"
                                "            if write_global { n += 1 }\n"
                                "            spawn work(i)\n"
                                "        }\n"
                                "    }\n"
                                "    return Array.length(results)\n"
                                "}\n";

    const auto time_run = [&](std::string_view call) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = eval(program + std::string{call});
        const auto elapsed = std::chrono::steady_clock::now() - start;

        ASSERT_TRUE(result.is_integer());
        ASSERT_EQ(result.as_integer(), 300);

        return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    };

    const auto without_writes = time_run("spawn_all(false)");
    const auto with_writes = time_run("spawn_all(true)");

    ASSERT_TRUE(with_writes < (without_writes * 4) + std::chrono::milliseconds{250});
}

// Review #4: ~VM used to null the thread's current task_scope unconditionally,
// even when the scope belonged to another VM on the same thread (the
// debugger's scratch VM evaluating a conditional breakpoint inside a scope).
LUMA_TEST(vm_destructor_preserves_foreign_task_scope) {
    TaskScope scope;
    VMTaskManager::current_scope = &scope;

    {
        const auto env = luma::test::make_std_env();
        const VM scratch{env};
    }

    const bool preserved = VMTaskManager::current_scope == &scope;
    VMTaskManager::current_scope = nullptr;

    ASSERT_TRUE(preserved);
}

// ─── Main ───

int main() {
    LUMA_RUN_ALL();
}
