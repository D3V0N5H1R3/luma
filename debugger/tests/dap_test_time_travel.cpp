// DAP time-travel characterization tests.
//
// Two layers are covered:
//   1. DebugSession::step_back / reverse_continue error-path semantics
//      (recorder check, then snapshot availability).  A bare DebugSession with
//      empty callbacks is sufficient: the guard logic short-circuits before any
//      VM, thread, or callback is touched.
//   2. TimeTravelRecorder + ReplayEngine happy paths — snapshot capture,
//      interval-based recording, ring-buffer eviction, reverse navigation, and
//      value-stack restore.  A VM with a seeded value stack (via restore_stack)
//      gives snapshots observable contents without executing any bytecode.

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "debug_session.hpp"
#include "runtime/interpreter/environment.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"
#include "test_framework.hpp"
#include "time_travel.hpp"

using namespace luma::dap;
using luma::Environment;
using luma::Value;
using luma::VM;

namespace {

// Build a value stack of integers so snapshots have inspectable contents.
[[nodiscard]] std::vector<Value> int_stack(std::initializer_list<int> xs) {
    std::vector<Value> stack;
    stack.reserve(xs.size());
    for (const int x : xs) {
        stack.emplace_back(x);
    }
    return stack;
}

void test_step_back_without_time_travel_errors() {
    DebugSession session{EventCallback{}, OutputCallback{}};

    const auto result = session.step_back(1);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_EQ(result.error_message, std::string("Time-travel debugging is not enabled"));
}

void test_reverse_continue_without_time_travel_errors() {
    DebugSession session{EventCallback{}, OutputCallback{}};

    const auto result = session.reverse_continue(1);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_EQ(result.error_message, std::string("Time-travel debugging is not enabled"));
}

void test_step_back_enabled_but_no_snapshots_errors() {
    DebugSession session{EventCallback{}, OutputCallback{}};
    session.enable_time_travel();

    const auto result = session.step_back(1);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_EQ(result.error_message, std::string("No previous state available"));
}

void test_reverse_continue_enabled_but_no_snapshots_errors() {
    DebugSession session{EventCallback{}, OutputCallback{}};
    session.enable_time_travel();

    const auto result = session.reverse_continue(1);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_EQ(result.error_message, std::string("No previous state available"));
}

// ─── TimeTravelRecorder: capture ──────────────────────────────────

void test_recorder_take_snapshot_captures_stack() {
    const auto env = Environment::create();
    VM vm{env};
    vm.restore_stack(int_stack({1, 2, 3}));

    TimeTravelRecorder recorder;
    recorder.take_snapshot(vm, /*file_id=*/1, /*line=*/10, /*frame_depth=*/0);

    ASSERT_EQ(recorder.snapshot_count(), static_cast<std::size_t>(1));

    const auto* snap = recorder.latest();
    ASSERT_TRUE(snap != nullptr);
    ASSERT_EQ(snap->file_id, 1);
    ASSERT_EQ(snap->line, 10);
    ASSERT_EQ(snap->stack.size(), static_cast<std::size_t>(3));
    ASSERT_EQ(snap->stack[0].as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(snap->stack[2].as_integer(), static_cast<std::int64_t>(3));
}

void test_recorder_tracks_memory() {
    const auto env = Environment::create();
    VM vm{env};
    vm.restore_stack(int_stack({1, 2, 3}));

    TimeTravelRecorder recorder;
    ASSERT_EQ(recorder.memory_used(), static_cast<std::size_t>(0));

    recorder.take_snapshot(vm, 1, 10, 0);
    ASSERT_GT(recorder.memory_used(), static_cast<std::size_t>(0));
}

// ─── TimeTravelRecorder: interval recording ───────────────────────

void test_on_line_snapshots_at_interval() {
    TimeTravelConfig config;
    config.snapshot_interval = 3;
    TimeTravelRecorder recorder{config};

    const auto env = Environment::create();
    VM vm{env};
    vm.restore_stack(int_stack({7}));

    recorder.on_line(vm, 1, 1, 0);
    ASSERT_EQ(recorder.snapshot_count(), static_cast<std::size_t>(0));
    recorder.on_line(vm, 1, 2, 0);
    ASSERT_EQ(recorder.snapshot_count(), static_cast<std::size_t>(0));
    recorder.on_line(vm, 1, 3, 0);
    // Third line reaches the interval — one snapshot recorded.
    ASSERT_EQ(recorder.snapshot_count(), static_cast<std::size_t>(1));
    ASSERT_EQ(recorder.total_instructions(), static_cast<std::uint64_t>(3));
}

void test_on_line_disabled_records_no_snapshot() {
    TimeTravelConfig config;
    config.enabled = false;
    config.snapshot_interval = 1;
    TimeTravelRecorder recorder{config};

    const auto env = Environment::create();
    VM vm{env};

    recorder.on_line(vm, 1, 1, 0);
    // Disabled: no snapshot, but the instruction counter still advances.
    ASSERT_EQ(recorder.snapshot_count(), static_cast<std::size_t>(0));
    ASSERT_EQ(recorder.total_instructions(), static_cast<std::uint64_t>(1));
}

// ─── TimeTravelRecorder: eviction ─────────────────────────────────

void test_max_snapshots_evicts_oldest() {
    TimeTravelConfig config;
    config.max_snapshots = 2;
    config.snapshot_interval = 1;
    TimeTravelRecorder recorder{config};

    const auto env = Environment::create();
    VM vm{env};

    vm.restore_stack(int_stack({1}));
    recorder.take_snapshot(vm, 1, 10, 0);
    vm.restore_stack(int_stack({2}));
    recorder.take_snapshot(vm, 1, 20, 0);
    vm.restore_stack(int_stack({3}));
    recorder.take_snapshot(vm, 1, 30, 0);

    // Ring buffer capped at 2; the line-10 snapshot was evicted.
    ASSERT_EQ(recorder.snapshot_count(), static_cast<std::size_t>(2));
    ASSERT_EQ(recorder.snapshot_at(0)->line, 20);
    ASSERT_EQ(recorder.latest()->line, 30);
}

// ─── TimeTravelRecorder: navigation ───────────────────────────────

void test_snapshots_at_line() {
    const auto env = Environment::create();
    VM vm{env};
    vm.restore_stack(int_stack({1}));

    TimeTravelRecorder recorder;
    recorder.take_snapshot(vm, 1, 10, 0);
    recorder.take_snapshot(vm, 1, 20, 0);
    recorder.take_snapshot(vm, 1, 10, 0);

    ASSERT_EQ(recorder.snapshots_at_line(1, 10).size(), static_cast<std::size_t>(2));
    ASSERT_EQ(recorder.snapshots_at_line(1, 20).size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(recorder.snapshots_at_line(1, 99).empty());
}

void test_step_back_navigates_and_clamps() {
    const auto env = Environment::create();
    VM vm{env};
    vm.restore_stack(int_stack({1}));

    TimeTravelRecorder recorder;
    recorder.take_snapshot(vm, 1, 10, 0);
    recorder.take_snapshot(vm, 1, 20, 0);
    recorder.take_snapshot(vm, 1, 30, 0);

    ASSERT_EQ(recorder.step_back(0)->line, 30); // current position
    ASSERT_EQ(recorder.step_back(1)->line, 20);
    ASSERT_EQ(recorder.step_back(2)->line, 10);
    // Stepping past the oldest snapshot clamps to the front.
    ASSERT_EQ(recorder.step_back(5)->line, 10);
}

// ─── DebugSession: repeated step_back walks earlier snapshots (B04) ─

// Regression: DebugSession::step_back must advance a per-session cursor so that
// consecutive stepBack requests land on successively earlier snapshots.  The
// previous implementation always restored the snapshot exactly one step before
// the latest, so repeated stepBack stayed pinned to the second-to-last snapshot
// and reverse stepping was effectively limited to a single step.
void test_session_step_back_walks_earlier_snapshots() {
    DebugSession session{EventCallback{}, OutputCallback{}};
    session.enable_time_travel();

    const auto ctx = session.make_hook_context();

    const auto env = Environment::create();
    VM vm{env};

    auto state = std::make_shared<ThreadState>();
    state->thread_id = 1;
    state->vm = &vm;
    ctx.thread_state_manager->add_thread(state);

    // Record three snapshots with distinct value stacks (10, 20, 30).
    auto& recorder = *ctx.time_travel_recorder;
    vm.restore_stack(int_stack({10}));
    recorder->take_snapshot(vm, 1, 10, 0);
    vm.restore_stack(int_stack({20}));
    recorder->take_snapshot(vm, 1, 20, 0);
    vm.restore_stack(int_stack({30}));
    recorder->take_snapshot(vm, 1, 30, 0);

    // First stepBack lands on the second-to-last snapshot (stack {20}).
    ASSERT_TRUE(static_cast<bool>(session.step_back(1)));
    ASSERT_EQ(vm.stack().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(vm.stack()[0].as_integer(), static_cast<std::int64_t>(20));

    // Second stepBack must walk further back to the oldest snapshot (stack {10}),
    // not remain pinned at {20}.
    ASSERT_TRUE(static_cast<bool>(session.step_back(1)));
    ASSERT_EQ(vm.stack()[0].as_integer(), static_cast<std::int64_t>(10));

    // A forward resume resets the cursor: the next stepBack starts over from the
    // latest snapshot again (stack {20}).
    ASSERT_TRUE(static_cast<bool>(session.continue_execution(1)));
    ASSERT_TRUE(static_cast<bool>(session.step_back(1)));
    ASSERT_EQ(vm.stack()[0].as_integer(), static_cast<std::int64_t>(20));
}

void test_clear_resets_recorder() {
    const auto env = Environment::create();
    VM vm{env};
    vm.restore_stack(int_stack({1}));

    TimeTravelRecorder recorder;
    recorder.take_snapshot(vm, 1, 10, 0);
    ASSERT_EQ(recorder.snapshot_count(), static_cast<std::size_t>(1));

    recorder.clear();
    ASSERT_EQ(recorder.snapshot_count(), static_cast<std::size_t>(0));
    ASSERT_EQ(recorder.memory_used(), static_cast<std::size_t>(0));
    ASSERT_EQ(recorder.total_instructions(), static_cast<std::uint64_t>(0));
    ASSERT_TRUE(recorder.latest() == nullptr);
}

// ─── ReplayEngine: restore ────────────────────────────────────────

void test_replay_restores_earlier_stack() {
    const auto env = Environment::create();
    VM vm{env};
    vm.restore_stack(int_stack({1, 2, 3}));

    TimeTravelRecorder recorder;
    recorder.take_snapshot(vm, 1, 10, 0);

    // Advance the live stack past the recorded point.
    vm.restore_stack(int_stack({99}));
    ASSERT_EQ(vm.stack().size(), static_cast<std::size_t>(1));

    // Replaying the snapshot restores the earlier value stack for inspection.
    ReplayEngine::restore_snapshot(vm, *recorder.latest());
    ASSERT_EQ(vm.stack().size(), static_cast<std::size_t>(3));
    ASSERT_EQ(vm.stack()[0].as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(vm.stack()[2].as_integer(), static_cast<std::int64_t>(3));
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Time-Travel Tests");

    // DebugSession error-path guards.
    RUN(test_step_back_without_time_travel_errors);
    RUN(test_reverse_continue_without_time_travel_errors);
    RUN(test_step_back_enabled_but_no_snapshots_errors);
    RUN(test_reverse_continue_enabled_but_no_snapshots_errors);

    // TimeTravelRecorder capture.
    RUN(test_recorder_take_snapshot_captures_stack);
    RUN(test_recorder_tracks_memory);

    // TimeTravelRecorder interval recording.
    RUN(test_on_line_snapshots_at_interval);
    RUN(test_on_line_disabled_records_no_snapshot);

    // TimeTravelRecorder eviction.
    RUN(test_max_snapshots_evicts_oldest);

    // TimeTravelRecorder navigation.
    RUN(test_snapshots_at_line);
    RUN(test_step_back_navigates_and_clamps);
    RUN(test_session_step_back_walks_earlier_snapshots);
    RUN(test_clear_resets_recorder);

    // ReplayEngine restore.
    RUN(test_replay_restores_earlier_stack);

    return SUMMARY();
}
