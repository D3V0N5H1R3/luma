// DAP breakpoint manager tests — exception breakpoints.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "breakpoint_manager.hpp"
#include "dap_types.hpp"
#include "i_source_locator.hpp"
#include "runtime/compiler/compiled_function.hpp"
#include "test_framework.hpp"

using luma::dap::BreakpointManager;
using luma::dap::BreakpointRequest;
using luma::dap::ISourceLocator;

namespace {

// A source locator that reports every path as one fixed file id, so
// set_breakpoints resolves without a real SourceManager.
class SingleFileLocator : public ISourceLocator {
public:
    explicit SingleFileLocator(luma::FileId id) : id_(id) {}

    [[nodiscard]] std::optional<luma::FileId>
    find_file_id(std::string_view /*path*/) const override {
        return id_;
    }

    [[nodiscard]] const luma::SourceFile* get_file(luma::FileId /*file_id*/) const override {
        return nullptr;
    }

    void for_each_file(
        std::function<void(luma::FileId, const luma::SourceFile*)> /*fn*/) const override {}

private:
    luma::FileId id_;
};

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

// ─── Line breakpoints: one-to-many per executable line ─────────────

// Regression: two breakpoints on distinct (non-executable) lines that both snap
// forward to the same executable line must each retain their own condition.
// Previously the second overwrote the first in a one-entry-per-line map, so the
// first breakpoint's condition was silently lost.
void test_two_breakpoints_snapping_to_one_line_both_retained() {
    constexpr luma::FileId k_file_id = 1;

    // Compiled program with executable lines {10, 20}; lines 8 and 9 are not
    // executable and snap forward to line 10.
    auto top_level = std::make_shared<luma::CompiledFunction>();
    top_level->mutable_chunk().source_map.append(
        0, luma::SourceLocation{.file_id = k_file_id, .line = 10});
    top_level->mutable_chunk().source_map.append(
        1, luma::SourceLocation{.file_id = k_file_id, .line = 20});

    SingleFileLocator locator{k_file_id};

    BreakpointManager mgr;
    mgr.set_source_locator(&locator);
    mgr.set_compiled_program(std::make_shared<std::vector<luma::CompiledFunction>>(), top_level);

    BreakpointRequest req_a;
    req_a.line = 8;
    req_a.condition = "cond_a";

    BreakpointRequest req_b;
    req_b.line = 9;
    req_b.condition = "cond_b";

    const auto responses = mgr.set_breakpoints("test.luma", {req_a, req_b});

    // Both requests get a response, each bound to the snapped line 10 with a
    // distinct id.
    ASSERT_EQ(responses.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(responses[0].line, 10);
    ASSERT_EQ(responses[1].line, 10);
    ASSERT_NE(responses[0].id, responses[1].id);

    const int id_a = responses[0].id;
    const int id_b = responses[1].id;

    // A condition evaluator that reports only the given condition as true.
    auto only_true = [](const std::string& which) {
        return [which](const std::string& expr) -> std::string {
            return expr == which ? "true" : "false";
        };
    };

    // When only cond_a holds, breakpoint A fires — proving A's condition was not
    // dropped by the collision with B.
    const auto hit_a = mgr.check_breakpoint(k_file_id, 10, only_true("cond_a"));
    ASSERT_TRUE(hit_a.should_break);
    ASSERT_EQ(hit_a.hit_breakpoint_id, id_a);

    // When only cond_b holds, breakpoint B fires.
    const auto hit_b = mgr.check_breakpoint(k_file_id, 10, only_true("cond_b"));
    ASSERT_TRUE(hit_b.should_break);
    ASSERT_EQ(hit_b.hit_breakpoint_id, id_b);

    // When neither condition holds, nothing fires.
    const auto no_hit = mgr.check_breakpoint(k_file_id, 10, only_true("nothing"));
    ASSERT_FALSE(no_hit.should_break);
}

// Compiled program for k_test_file_id with executable lines {10, 20}.
constexpr luma::FileId k_test_file_id = 1;

[[nodiscard]] std::shared_ptr<luma::CompiledFunction> make_two_line_program() {
    auto top_level = std::make_shared<luma::CompiledFunction>();
    top_level->mutable_chunk().source_map.append(
        0, luma::SourceLocation{.file_id = k_test_file_id, .line = 10});
    top_level->mutable_chunk().source_map.append(
        1, luma::SourceLocation{.file_id = k_test_file_id, .line = 20});
    return top_level;
}

// Regression: every breakpoint bound to a line is evaluated.  Returning at the
// first qualifying one let a logpoint hide a real breakpoint on the same line
// (the program never stopped) and left later logpoints silent.
void test_logpoint_does_not_hide_breakpoint_on_same_line() {
    SingleFileLocator locator{k_test_file_id};

    BreakpointManager mgr;
    mgr.set_source_locator(&locator);
    mgr.set_compiled_program(std::make_shared<std::vector<luma::CompiledFunction>>(),
                             make_two_line_program());

    BreakpointRequest log_a;
    log_a.line = 10;
    log_a.log_message = "first {x}";

    BreakpointRequest stop;
    stop.line = 10;

    BreakpointRequest log_b;
    log_b.line = 10;
    log_b.log_message = "second";

    const auto responses = mgr.set_breakpoints("test.luma", {log_a, stop, log_b});
    ASSERT_EQ(responses.size(), static_cast<std::size_t>(3));

    const auto result = mgr.check_breakpoint(k_test_file_id, 10, {});

    ASSERT_TRUE(result.should_break);
    ASSERT_EQ(result.hit_breakpoint_id, responses[1].id);
    ASSERT_EQ(result.log_messages.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(result.log_messages[0], "first {x}");
    ASSERT_EQ(result.log_messages[1], "second");
}

// Regression: a breakpoint set before the program was compiled keeps the ID the
// client was given, and resolution reports it so the adapter can send a
// `breakpoint` event marking it verified.  Resolution used to allocate a new ID
// and report nothing, leaving the editor's breakpoint unverified forever.
void test_pending_breakpoint_keeps_id_when_resolved() {
    BreakpointManager mgr;

    BreakpointRequest pending;
    pending.line = 15;
    pending.id = -7;

    const auto before = mgr.set_breakpoints("test.luma", {pending});
    ASSERT_EQ(before.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(before[0].id, -7);
    ASSERT_FALSE(before[0].verified);

    SingleFileLocator locator{k_test_file_id};
    mgr.set_source_locator(&locator);
    mgr.set_compiled_program(std::make_shared<std::vector<luma::CompiledFunction>>(),
                             make_two_line_program());

    const auto resolved = mgr.resolve_pending_breakpoints();
    ASSERT_EQ(resolved.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(resolved[0].id, -7);
    ASSERT_TRUE(resolved[0].verified);
    ASSERT_EQ(resolved[0].line, 20);

    // The bound breakpoint fires under the same ID.
    const auto hit = mgr.check_breakpoint(k_test_file_id, 20, {});
    ASSERT_TRUE(hit.should_break);
    ASSERT_EQ(hit.hit_breakpoint_id, -7);

    // A second resolution binds nothing new.
    ASSERT_TRUE(mgr.resolve_pending_breakpoints().empty());
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

    // Line breakpoints.
    RUN(test_two_breakpoints_snapping_to_one_line_both_retained);
    RUN(test_logpoint_does_not_hide_breakpoint_on_same_line);
    RUN(test_pending_breakpoint_keeps_id_when_resolved);

    return SUMMARY();
}
