// DAP compiled breakpoint cache and watch cache tests.

#include <string>

#include "compiled_breakpoint.hpp"
#include "dap_types.hpp"
#include "debug_session_state.hpp"
#include "json/json.hpp"
#include "test_framework.hpp"

using namespace luma::dap;
using luma::json::JsonValue;

namespace {

// ─── CompiledBreakpointCache tests ─────────────────────────────────

void test_breakpoint_condition_compile_error() {
    CompiledBreakpointCache cache;
    const auto& cond = cache.compile_condition(1, "invalid syntax !!!!");
    ASSERT_FALSE(cond.is_valid);
    ASSERT_FALSE(cond.compile_error.empty());
}

void test_breakpoint_valid_condition() {
    CompiledBreakpointCache cache;
    const auto& cond = cache.compile_condition(1, "1 + 1 == 2");
    // The condition is stored regardless of compilation outcome.
    ASSERT_EQ(cond.source_expression, "1 + 1 == 2");
    ASSERT_TRUE(cond.is_valid);
    ASSERT_TRUE(cond.compile_error.empty());
    ASSERT_TRUE(cache.has_condition(1));
}

void test_breakpoint_condition_free_variable() {
    // A condition may reference program locals (free variables such as `x`).
    // The validation path bypasses the type checker, so these must compile
    // cleanly rather than being rejected as undefined-variable type errors.
    CompiledBreakpointCache cache;
    const auto& cond = cache.compile_condition(1, "x > 5");
    ASSERT_TRUE(cond.is_valid);
    ASSERT_TRUE(cond.compile_error.empty());
}

void test_breakpoint_log_message_template() {
    CompiledBreakpointCache cache;
    const auto& msg = cache.compile_log_message(1, "x = {1 + 2}");
    // Template should be stored with literal and expression segments.
    ASSERT_EQ(msg.template_text, "x = {1 + 2}");
    ASSERT_GE(msg.segments.size(), static_cast<std::size_t>(1));
}

void test_breakpoint_log_message_valid_expression() {
    // Each braced expression segment must compile; free variables (program
    // locals such as `count`) are permitted because evaluation injects them as
    // globals into the scratch environment.
    CompiledBreakpointCache cache;
    const auto& msg = cache.compile_log_message(1, "count = {count + 1}");
    ASSERT_TRUE(msg.is_valid);
    ASSERT_TRUE(msg.compile_error.empty());
}

void test_breakpoint_cache_invalidation() {
    CompiledBreakpointCache cache;
    (void)cache.compile_condition(1, "true");
    ASSERT_TRUE(cache.has_condition(1));
    cache.invalidate(1);
    ASSERT_FALSE(cache.has_condition(1));
}

void test_breakpoint_cache_invalidate_all() {
    CompiledBreakpointCache cache;
    (void)cache.compile_condition(1, "true");
    (void)cache.compile_condition(2, "false");
    ASSERT_EQ(cache.cache_size(), static_cast<std::size_t>(2));
    cache.invalidate_all();
    ASSERT_EQ(cache.cache_size(), static_cast<std::size_t>(0));
}

void test_breakpoint_cache_size_tracking() {
    CompiledBreakpointCache cache;
    ASSERT_EQ(cache.cache_size(), static_cast<std::size_t>(0));
    (void)cache.compile_condition(1, "true");
    ASSERT_EQ(cache.cache_size(), static_cast<std::size_t>(1));
    (void)cache.compile_condition(2, "false");
    ASSERT_EQ(cache.cache_size(), static_cast<std::size_t>(2));
}

void test_breakpoint_cache_overwrite() {
    CompiledBreakpointCache cache;
    (void)cache.compile_condition(1, "true");
    const auto& cond1 = cache.compile_condition(1, "false");
    // Overwrite should replace the cached entry.
    ASSERT_EQ(cond1.source_expression, "false");
    ASSERT_EQ(cache.cache_size(), static_cast<std::size_t>(1));
}

void test_breakpoint_log_message_literal_only() {
    CompiledBreakpointCache cache;
    const auto& msg = cache.compile_log_message(1, "hello world");
    ASSERT_TRUE(msg.is_valid);
    ASSERT_EQ(msg.segments.size(), static_cast<std::size_t>(1));
    ASSERT_FALSE(msg.segments[0].is_expression);
    ASSERT_EQ(msg.segments[0].literal, "hello world");
}

void test_breakpoint_log_message_expression_only() {
    CompiledBreakpointCache cache;
    const auto& msg = cache.compile_log_message(1, "{42}");
    // Template is stored; segments are parsed from the template.
    ASSERT_EQ(msg.template_text, "{42}");
    ASSERT_GE(msg.segments.size(), static_cast<std::size_t>(1));
}

// ─── WatchCache tests ──────────────────────────────────────────────

void test_watch_cache_store_and_lookup() {
    WatchCache cache;
    cache.put(0, "x", {.expression = "x", .frame_id = 0, .result = "42", .type = "integer"});
    const auto entry = cache.get(0, "x");
    ASSERT_TRUE(entry.has_value());
    ASSERT_EQ(entry->result, "42");
    ASSERT_EQ(entry->type, "integer");
}

void test_watch_cache_lookup_missing() {
    WatchCache cache;
    const auto entry = cache.get(0, "nonexistent");
    ASSERT_FALSE(entry.has_value());
}

void test_watch_cache_invalidate() {
    WatchCache cache;
    cache.put(0, "x", {.expression = "x", .result = "42"});
    cache.invalidate();
    ASSERT_FALSE(cache.get(0, "x").has_value());
}

void test_watch_cache_overwrite() {
    WatchCache cache;
    cache.put(0, "x", {.expression = "x", .result = "42"});
    cache.put(0, "x", {.expression = "x", .result = "99"});
    const auto entry = cache.get(0, "x");
    ASSERT_TRUE(entry.has_value());
    ASSERT_EQ(entry->result, "99");
}

void test_watch_cache_multiple_entries() {
    WatchCache cache;
    cache.put(0, "a", {.expression = "a", .result = "1"});
    cache.put(0, "b", {.expression = "b", .result = "2"});
    cache.put(0, "c", {.expression = "c", .result = "3"});
    ASSERT_TRUE(cache.get(0, "a").has_value());
    ASSERT_TRUE(cache.get(0, "b").has_value());
    ASSERT_TRUE(cache.get(0, "c").has_value());
    ASSERT_EQ(REQUIRE_VALUE(cache.get(0, "a")).result, "1");
    ASSERT_EQ(REQUIRE_VALUE(cache.get(0, "c")).result, "3");
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Compiled Cache Tests");

    // CompiledBreakpointCache.
    RUN(test_breakpoint_condition_compile_error);
    RUN(test_breakpoint_valid_condition);
    RUN(test_breakpoint_condition_free_variable);
    RUN(test_breakpoint_log_message_template);
    RUN(test_breakpoint_log_message_valid_expression);
    RUN(test_breakpoint_cache_invalidation);
    RUN(test_breakpoint_cache_invalidate_all);
    RUN(test_breakpoint_cache_size_tracking);
    RUN(test_breakpoint_cache_overwrite);
    RUN(test_breakpoint_log_message_literal_only);
    RUN(test_breakpoint_log_message_expression_only);

    // WatchCache.
    RUN(test_watch_cache_store_and_lookup);
    RUN(test_watch_cache_lookup_missing);
    RUN(test_watch_cache_invalidate);
    RUN(test_watch_cache_overwrite);
    RUN(test_watch_cache_multiple_entries);

    return SUMMARY();
}
