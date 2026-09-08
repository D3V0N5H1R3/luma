// DAP breakpoint manager tests — exception breakpoints.

#include <string>

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

    return SUMMARY();
}
