// DAP breakpoint manager tests — exception breakpoints, data breakpoints.

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

#include "breakpoint_manager.hpp"
#include "dap_types.hpp"
#include "test_framework.hpp"

using namespace luma::dap;

namespace {

// ─── Exception breakpoint configuration ───────────────────────────

void test_exception_breakpoints_default_state() {
    BreakpointManager mgr;
    ASSERT_FALSE(mgr.break_on_caught());
    ASSERT_FALSE(mgr.break_on_uncaught());
}

void test_exception_breakpoints_set_caught_only() {
    BreakpointManager mgr;
    mgr.set_exception_breakpoints({"caught"});
    ASSERT_TRUE(mgr.break_on_caught());
    ASSERT_FALSE(mgr.break_on_uncaught());
}

void test_exception_breakpoints_set_uncaught_only() {
    BreakpointManager mgr;
    mgr.set_exception_breakpoints({"uncaught"});
    ASSERT_FALSE(mgr.break_on_caught());
    ASSERT_TRUE(mgr.break_on_uncaught());
}

void test_exception_breakpoints_set_both() {
    BreakpointManager mgr;
    mgr.set_exception_breakpoints({"caught", "uncaught"});
    ASSERT_TRUE(mgr.break_on_caught());
    ASSERT_TRUE(mgr.break_on_uncaught());
}

void test_exception_breakpoints_clear_all() {
    BreakpointManager mgr;
    mgr.set_exception_breakpoints({"caught", "uncaught"});
    ASSERT_TRUE(mgr.break_on_caught());
    ASSERT_TRUE(mgr.break_on_uncaught());

    // Empty vector clears both filters.
    mgr.set_exception_breakpoints({});
    ASSERT_FALSE(mgr.break_on_caught());
    ASSERT_FALSE(mgr.break_on_uncaught());
}

void test_exception_breakpoints_unknown_filter_ignored() {
    BreakpointManager mgr;
    mgr.set_exception_breakpoints({"caught", "unknown_filter", "bogus"});
    ASSERT_TRUE(mgr.break_on_caught());
    ASSERT_FALSE(mgr.break_on_uncaught());
}

void test_exception_breakpoints_replace_filters() {
    BreakpointManager mgr;
    mgr.set_exception_breakpoints({"caught", "uncaught"});
    ASSERT_TRUE(mgr.break_on_caught());
    ASSERT_TRUE(mgr.break_on_uncaught());

    // Second call replaces — only uncaught now.
    mgr.set_exception_breakpoints({"uncaught"});
    ASSERT_FALSE(mgr.break_on_caught());
    ASSERT_TRUE(mgr.break_on_uncaught());
}

// ─── Data breakpoint setup ────────────────────────────────────────

void test_data_breakpoint_set_and_check() {
    BreakpointManager mgr;
    mgr.set_data_breakpoint("my_var", "write", "");

    // Unconditional data breakpoint should fire.
    bool fired = mgr.check_data_breakpoint("my_var", {});
    ASSERT_TRUE(fired);
}

void test_data_breakpoint_check_unregistered() {
    BreakpointManager mgr;
    mgr.set_data_breakpoint("x", "write", "");

    // Checking a different variable should not fire.
    bool fired = mgr.check_data_breakpoint("y", {});
    ASSERT_FALSE(fired);
}

void test_data_breakpoint_clear() {
    BreakpointManager mgr;
    mgr.set_data_breakpoint("x", "write", "");
    ASSERT_TRUE(mgr.check_data_breakpoint("x", {}));

    mgr.clear_data_breakpoints();
    ASSERT_FALSE(mgr.check_data_breakpoint("x", {}));
}

void test_data_breakpoint_condition_true() {
    BreakpointManager mgr;
    mgr.set_data_breakpoint("x", "write", "x > 5");

    auto eval_true = [](const std::string&) -> std::string {
        return "true";
    };
    ASSERT_TRUE(mgr.check_data_breakpoint("x", eval_true));
}

void test_data_breakpoint_condition_false() {
    BreakpointManager mgr;
    mgr.set_data_breakpoint("x", "write", "x > 5");

    auto eval_false = [](const std::string&) -> std::string {
        return "false";
    };
    ASSERT_FALSE(mgr.check_data_breakpoint("x", eval_false));
}

void test_data_breakpoint_condition_receives_expression() {
    BreakpointManager mgr;
    mgr.set_data_breakpoint("x", "write", "x > 5");

    std::string received_expr;
    auto eval_capture = [&received_expr](const std::string& expr) -> std::string {
        received_expr = expr;
        return "true";
    };
    (void)mgr.check_data_breakpoint("x", eval_capture);
    ASSERT_EQ(received_expr, std::string("x > 5"));
}

void test_data_breakpoint_overwrite() {
    BreakpointManager mgr;
    mgr.set_data_breakpoint("x", "write", "old_cond");

    // Overwrite with a new condition.
    mgr.set_data_breakpoint("x", "readWrite", "new_cond");

    std::string received_expr;
    auto eval_capture = [&received_expr](const std::string& expr) -> std::string {
        received_expr = expr;
        return "true";
    };
    (void)mgr.check_data_breakpoint("x", eval_capture);
    ASSERT_EQ(received_expr, std::string("new_cond"));
}

void test_data_breakpoint_multiple_variables() {
    BreakpointManager mgr;
    mgr.set_data_breakpoint("x", "write", "");
    mgr.set_data_breakpoint("y", "write", "");
    mgr.set_data_breakpoint("z", "readWrite", "");

    ASSERT_TRUE(mgr.check_data_breakpoint("x", {}));
    ASSERT_TRUE(mgr.check_data_breakpoint("y", {}));
    ASSERT_TRUE(mgr.check_data_breakpoint("z", {}));
    ASSERT_FALSE(mgr.check_data_breakpoint("w", {}));
}

void test_data_breakpoint_no_condition_null_evaluator() {
    BreakpointManager mgr;
    mgr.set_data_breakpoint("x", "write", "");

    // Null evaluator with empty condition should still fire.
    BreakpointManager::ConditionEvaluatorFn null_eval;
    ASSERT_TRUE(mgr.check_data_breakpoint("x", null_eval));
}

void test_data_breakpoint_has_active_flag() {
    BreakpointManager mgr;
    ASSERT_FALSE(mgr.has_active_breakpoints());

    mgr.set_data_breakpoint("x", "write", "");
    ASSERT_TRUE(mgr.has_active_breakpoints());

    mgr.clear_data_breakpoints();
    ASSERT_FALSE(mgr.has_active_breakpoints());
}

// ─── Cross-thread visibility of breakpoints_active_ (B03) ─────────

// Regression: has_active_breakpoints() must observe a concurrent
// set_data_breakpoint() from another thread promptly. Previously both the
// store (update_breakpoints_active_flag) and the load (has_active_breakpoints)
// used memory_order_relaxed, which on weakly-ordered architectures (ARM)
// provides no cross-thread visibility guarantee and could leave the reader
// spinning past a newly set breakpoint indefinitely. The store/load are now
// release/acquire, so this test — a reader thread spinning on
// has_active_breakpoints() until it observes the writer's update, bounded by
// a wall-clock deadline rather than an iteration count (a tight atomic-load
// loop can run millions of iterations faster than a fixed sleep on the
// writer thread) — must observe the update well within the deadline.
void test_has_active_breakpoints_visible_across_threads() {
    BreakpointManager mgr;
    std::atomic<bool> observed{false};

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    std::thread reader([&] {
        while (std::chrono::steady_clock::now() < deadline) {
            if (mgr.has_active_breakpoints()) {
                observed.store(true);
                return;
            }
        }
    });

    std::thread writer([&] { mgr.set_data_breakpoint("x", "write", ""); });

    writer.join();
    reader.join();

    ASSERT_TRUE(observed.load());
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Breakpoint Manager Tests");

    // Exception breakpoint configuration.
    RUN(test_exception_breakpoints_default_state);
    RUN(test_exception_breakpoints_set_caught_only);
    RUN(test_exception_breakpoints_set_uncaught_only);
    RUN(test_exception_breakpoints_set_both);
    RUN(test_exception_breakpoints_clear_all);
    RUN(test_exception_breakpoints_unknown_filter_ignored);
    RUN(test_exception_breakpoints_replace_filters);

    // Data breakpoint setup.
    RUN(test_data_breakpoint_set_and_check);
    RUN(test_data_breakpoint_check_unregistered);
    RUN(test_data_breakpoint_clear);
    RUN(test_data_breakpoint_condition_true);
    RUN(test_data_breakpoint_condition_false);
    RUN(test_data_breakpoint_condition_receives_expression);
    RUN(test_data_breakpoint_overwrite);
    RUN(test_data_breakpoint_multiple_variables);
    RUN(test_data_breakpoint_no_condition_null_evaluator);
    RUN(test_data_breakpoint_has_active_flag);
    RUN(test_has_active_breakpoints_visible_across_threads);

    return SUMMARY();
}
