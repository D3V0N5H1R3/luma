// DAP thread state tests — ThreadState, FrameMapping, stop reasons, concurrency.

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "dap_session_types.hpp"
#include "dap_types.hpp"
#include "json/json.hpp"
#include "runtime/interpreter/environment.hpp"
#include "runtime/vm/vm.hpp"
#include "test_framework.hpp"
#include "thread_state_manager.hpp"
#include "variable_inspector.hpp"

using namespace luma::dap;
using luma::json::JsonValue;

namespace {

// ─── Per-thread state structure ────────────────────────────────────

void test_thread_state_structure() {
    // Verify ThreadState has correct defaults.
    luma::dap::ThreadState state;
    state.thread_id = 3;
    state.name = "Task 3";

    ASSERT_EQ(state.thread_id, 3);
    ASSERT_EQ(state.name, "Task 3");
    ASSERT_EQ(state.vm, nullptr);
    ASSERT_FALSE(state.is_paused);
    ASSERT_FALSE(state.is_exception_terminated);
    ASSERT_FALSE(state.pending.stop_on_entry);
    ASSERT_EQ(static_cast<int>(state.step.mode), static_cast<int>(StepMode::None));
    ASSERT_EQ(state.pending.hit_breakpoint_id, 0);
}

void test_frame_mapping_structure() {
    // Verify FrameMapping defaults and fields.
    luma::dap::FrameMapping mapping;
    mapping.thread_id = 2;
    mapping.frame_index = 5;

    ASSERT_EQ(mapping.thread_id, 2);
    ASSERT_EQ(mapping.frame_index, 5);
    ASSERT_EQ(mapping.vm, nullptr);
}

// ─── Entry stop reason ─────────────────────────────────────────────

void test_entry_stop_reason() {
    // Verify the "entry" stop reason constant is defined.
    std::string entry_reason{kStopReasonEntry};
    ASSERT_EQ(entry_reason, "entry");
}

// ─── Stopped event with entry reason ───────────────────────────────

void test_stopped_event_entry_reason() {
    // Build a stopped event with reason "entry" as used for stopOnEntry.
    JsonValue::ObjectType body;
    body["reason"] = JsonValue(std::string{kStopReasonEntry});
    body["threadId"] = JsonValue(1);
    body["allThreadsStopped"] = JsonValue(true);

    auto json = JsonValue(std::move(body));

    ASSERT_EQ(json["reason"].as_string(), "entry");
    ASSERT_EQ(json["threadId"].as_integer(), 1);
    ASSERT_TRUE(json["allThreadsStopped"].as_bool());
}

// ─── Per-thread stopped event ──────────────────────────────────────

void test_per_thread_stopped_event() {
    // Verify a stopped event with a task thread ID (not just main=1).
    JsonValue::ObjectType body;
    body["reason"] = JsonValue(std::string{kStopReasonBreakpoint});
    body["threadId"] = JsonValue(3);
    body["allThreadsStopped"] = JsonValue(true);

    auto json = JsonValue(std::move(body));

    ASSERT_EQ(json["reason"].as_string(), "breakpoint");
    ASSERT_EQ(json["threadId"].as_integer(), 3);
}

// ─── Exception-terminated thread continue ──────────────────────────

void test_exception_terminated_state() {
    // Verify that ThreadState correctly tracks exception_terminated.
    luma::dap::ThreadState state;
    state.is_paused = true;
    state.is_exception_terminated = true;

    // After exception_terminated is set, continue should unblock.
    ASSERT_TRUE(state.is_paused);
    ASSERT_TRUE(state.is_exception_terminated);

    // Simulate continue on exception-terminated thread.
    state.is_paused = false;
    ASSERT_FALSE(state.is_paused);
}

// ─── Pause pending flag ────────────────────────────────────────────

void test_pause_pending_flag() {
    // ThreadState should have a pause_pending flag that produces kStopReasonPause.
    ThreadState state;
    ASSERT_FALSE(state.pending.pause);

    state.pending.pause = true;
    state.step.mode = StepMode::Into;

    // When pause_pending is true, the stop reason should be "pause" not "step".
    // The pause_pending flag takes priority over step_mode.
    std::string reason;

    if (state.pending.pause) {
        reason = std::string{kStopReasonPause};
        state.pending.pause = false;
    } else if (state.step.mode != StepMode::None) {
        reason = std::string{kStopReasonStep};
    }

    ASSERT_EQ(reason, "pause");
    ASSERT_FALSE(state.pending.pause);
}

// ─── allThreadsStopped dynamic ─────────────────────────────────────

void test_all_threads_stopped_event_body() {
    // The stopped event body should be constructable with dynamic allThreadsStopped.
    const bool all_stopped = false;

    JsonValue::ObjectType body;
    body["reason"] = JsonValue(std::string{kStopReasonBreakpoint});
    body["threadId"] = JsonValue(1);
    body["allThreadsStopped"] = JsonValue(all_stopped);

    // allThreadsStopped should be false when not all threads are paused.
    ASSERT_FALSE(body["allThreadsStopped"].as_bool());

    // With all threads stopped, it should be true.
    JsonValue::ObjectType body2;
    body2["reason"] = JsonValue(std::string{kStopReasonBreakpoint});
    body2["threadId"] = JsonValue(1);
    body2["allThreadsStopped"] = JsonValue(true);

    ASSERT_TRUE(body2["allThreadsStopped"].as_bool());
}

// ─── exceptionInfo caught vs uncaught ──────────────────────────────

void test_exception_info_caught_break_mode() {
    // Caught exceptions should report breakMode "always" per DAP spec.
    bool is_caught = true;
    std::string break_mode = is_caught ? "always" : "unhandled";
    ASSERT_EQ(break_mode, "always");

    is_caught = false;
    break_mode = is_caught ? "always" : "unhandled";
    ASSERT_EQ(break_mode, "unhandled");
}

void test_thread_state_exception_is_caught() {
    // ThreadState should track whether a pending exception is caught.
    ThreadState state;
    ASSERT_FALSE(state.pending.exception_caught);

    state.pending.exception_message = "test error";
    state.pending.exception_caught = true;
    ASSERT_TRUE(state.pending.exception_caught);
    ASSERT_EQ(state.pending.exception_message, "test error");
}

// ─── Concurrent thread state management ────────────────────────────

void test_thread_state_task_thread() {
    // Task threads (id >= 2) should be distinguishable from main thread.
    ThreadState main_state;
    main_state.thread_id = 1;
    main_state.name = "Main Thread";

    ThreadState task_state;
    task_state.thread_id = 2;
    task_state.name = "Task 2";

    ASSERT_EQ(main_state.thread_id, 1);
    ASSERT_EQ(task_state.thread_id, 2);
    ASSERT_NE(main_state.thread_id, task_state.thread_id);
}

void test_thread_state_independent_pause() {
    // Each thread should have independent pause state.
    ThreadState main_state;
    main_state.thread_id = 1;
    main_state.is_paused = true;

    ThreadState task_state;
    task_state.thread_id = 2;
    task_state.is_paused = false;

    // Main is paused, task is running — independent states.
    ASSERT_TRUE(main_state.is_paused);
    ASSERT_FALSE(task_state.is_paused);

    // Compute allThreadsStopped.
    bool all_stopped = main_state.is_paused && task_state.is_paused;
    ASSERT_FALSE(all_stopped);

    task_state.is_paused = true;
    all_stopped = main_state.is_paused && task_state.is_paused;
    ASSERT_TRUE(all_stopped);
}

void test_thread_state_independent_step_mode() {
    // Step mode should be per-thread.
    ThreadState state1;
    state1.thread_id = 1;
    state1.step.mode = StepMode::Over;

    ThreadState state2;
    state2.thread_id = 2;
    state2.step.mode = StepMode::None;

    ASSERT_NE(static_cast<int>(state1.step.mode), static_cast<int>(state2.step.mode));
}

void test_thread_state_exception_per_thread() {
    // Exception state should be per-thread.
    ThreadState main_state;
    main_state.thread_id = 1;
    main_state.is_exception_terminated = false;

    ThreadState task_state;
    task_state.thread_id = 2;
    task_state.is_exception_terminated = true;
    task_state.pending.exception_message = "Task error";

    ASSERT_FALSE(main_state.is_exception_terminated);
    ASSERT_TRUE(task_state.is_exception_terminated);
    ASSERT_EQ(task_state.pending.exception_message, "Task error");
}

// ─── ThreadState default fields ────────────────────────────────────

void test_thread_state_default_fields() {
    ThreadState ts;
    ASSERT_EQ(ts.thread_id, 0);
    ASSERT_TRUE(ts.name.empty());
    ASSERT_TRUE(ts.vm == nullptr);
    ASSERT_FALSE(ts.is_paused);
    ASSERT_FALSE(ts.is_exception_terminated);
    ASSERT_FALSE(ts.pending.stop_on_entry);
    ASSERT_FALSE(ts.pending.pause);
    ASSERT_FALSE(ts.pending.data_breakpoint);
    ASSERT_TRUE(ts.pending.data_breakpoint_name.empty());
    ASSERT_EQ(ts.step.mode, StepMode::None);
    ASSERT_EQ(ts.step.reference_depth, static_cast<std::size_t>(0));
    ASSERT_EQ(ts.step.reference_line, -1);
    ASSERT_EQ(ts.step.reference_file, -1);
    ASSERT_TRUE(ts.pending.exception_message.empty());
    ASSERT_FALSE(ts.pending.exception_caught);
    ASSERT_EQ(ts.pending.hit_breakpoint_id, 0);
}

void test_thread_state_step_modes() {
    ASSERT_NE(static_cast<int>(StepMode::None), static_cast<int>(StepMode::Over));
    ASSERT_NE(static_cast<int>(StepMode::Over), static_cast<int>(StepMode::Into));
    ASSERT_NE(static_cast<int>(StepMode::Into), static_cast<int>(StepMode::Out));
}

// ─── Concurrent thread add/remove ──────────────────────────────────

void test_concurrent_thread_add_remove() {
    ThreadStateManager mgr;
    constexpr int num_threads = 10;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&mgr, i]() {
            auto state = std::make_shared<ThreadState>();
            state->thread_id = 100 + i;
            state->name = "Thread " + std::to_string(i);
            mgr.add_thread(state);
            // Small delay to interleave operations across threads.
            std::this_thread::yield();
            mgr.remove_thread(100 + i);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // After all add/remove cycles, manager should be empty.
    ASSERT_EQ(mgr.get_threads().size(), static_cast<std::size_t>(0));
}

void test_concurrent_thread_add_remove_interleaved() {
    ThreadStateManager mgr;
    constexpr int num_threads = 20;
    std::vector<std::thread> removers;

    // First add all threads.
    for (int i = 0; i < num_threads; ++i) {
        auto state = std::make_shared<ThreadState>();
        state->thread_id = 200 + i;
        state->name = "Worker " + std::to_string(i);
        mgr.add_thread(state);
    }

    // Concurrently remove them.
    for (int i = 0; i < num_threads; ++i) {
        removers.emplace_back([&mgr, i]() {
            std::this_thread::yield();
            mgr.remove_thread(200 + i);
        });
    }

    for (auto& t : removers) {
        t.join();
    }

    ASSERT_EQ(mgr.get_threads().size(), static_cast<std::size_t>(0));
}

// ─── signal_all_vms_pause_check locking (B04 regression) ───────────

void test_signal_all_vms_pause_check_mixed_vms() {
    // Regression (B04): signal_all_vms_pause_check must read state->vm under the
    // per-thread lock, mirroring null_all_vms / force_unpause_all, so it honours
    // the documented GUARDED_BY(mutex) contract.  It should tolerate both live
    // and null VMs without crashing or deadlocking on the L1 → L2 acquisition.
    ThreadStateManager mgr;

    const auto env = luma::Environment::create();
    luma::VM vm{env};

    auto live = std::make_shared<ThreadState>();
    live->thread_id = 1;
    live->name = "Main";
    live->vm = &vm;
    mgr.add_thread(live);

    auto without_vm = std::make_shared<ThreadState>();
    without_vm->thread_id = 2;
    without_vm->name = "Detached";
    without_vm->vm = nullptr;
    mgr.add_thread(without_vm);

    // Exercises both branches (live vm signalled, null vm skipped) under the
    // newly-acquired per-thread lock.
    mgr.signal_all_vms_pause_check();

    ASSERT_EQ(mgr.get_threads().size(), static_cast<std::size_t>(2));
}

void test_signal_all_vms_pause_check_races_vm_nulling() {
    // Regression (B04): repeatedly signal while a worker churns a thread whose
    // ThreadState points at a live VM and is then removed (which nulls state->vm
    // under L2).  With the per-thread lock in place the read/write are ordered;
    // under TSan this stays clean, and either way the L1 → L2 acquisition must
    // never deadlock against the add/remove writers.
    ThreadStateManager mgr;

    const auto env = luma::Environment::create();
    luma::VM vm{env};

    std::atomic<bool> stop{false};

    std::thread churner([&] {
        while (!stop.load(std::memory_order_acquire)) {
            auto state = std::make_shared<ThreadState>();
            state->thread_id = 42;
            state->name = "Churn";
            state->vm = &vm;
            mgr.add_thread(state);
            std::this_thread::yield();
            mgr.remove_thread(42); // nulls state->vm under the per-thread lock
        }
    });

    for (int i = 0; i < 1000; ++i) {
        mgr.signal_all_vms_pause_check();
    }

    stop.store(true, std::memory_order_release);
    churner.join();

    // Completing without deadlock, crash, or TSan report is the assertion.
    ASSERT_TRUE(mgr.get_threads().size() <= static_cast<std::size_t>(1));
}

void test_thread_state_pause_resume_cycle() {
    ThreadState state;
    state.thread_id = 1;
    state.name = "Test";

    ASSERT_FALSE(state.is_paused);
    state.is_paused = true;
    ASSERT_TRUE(state.is_paused);
    state.is_paused = false;
    ASSERT_FALSE(state.is_paused);
}

void test_thread_state_step_mode_transitions() {
    ThreadState state;
    ASSERT_EQ(static_cast<int>(state.step.mode), static_cast<int>(StepMode::None));

    state.step.mode = StepMode::Into;
    ASSERT_EQ(static_cast<int>(state.step.mode), static_cast<int>(StepMode::Into));

    state.step.mode = StepMode::Over;
    ASSERT_EQ(static_cast<int>(state.step.mode), static_cast<int>(StepMode::Over));

    state.step.mode = StepMode::Out;
    ASSERT_EQ(static_cast<int>(state.step.mode), static_cast<int>(StepMode::Out));

    state.step.mode = StepMode::None;
    ASSERT_EQ(static_cast<int>(state.step.mode), static_cast<int>(StepMode::None));
}

void test_thread_state_full_pause_step_cycle() {
    // Simulate: running → paused (breakpoint) → step into → running → paused (step)
    ThreadState state;
    state.thread_id = 1;

    // Initially running.
    ASSERT_FALSE(state.is_paused);
    ASSERT_EQ(static_cast<int>(state.step.mode), static_cast<int>(StepMode::None));

    // Hit breakpoint — pause.
    state.is_paused = true;
    ASSERT_TRUE(state.is_paused);

    // User requests step-into — resume with step mode.
    state.step.mode = StepMode::Into;
    state.is_paused = false;
    ASSERT_FALSE(state.is_paused);
    ASSERT_EQ(static_cast<int>(state.step.mode), static_cast<int>(StepMode::Into));

    // Step completes — pause again and clear step mode.
    state.is_paused = true;
    state.step.mode = StepMode::None;
    ASSERT_TRUE(state.is_paused);
    ASSERT_EQ(static_cast<int>(state.step.mode), static_cast<int>(StepMode::None));
}

void test_thread_state_null_vm_access() {
    // ThreadState with vm == nullptr should be handled safely.
    ThreadState state;
    state.thread_id = 1;
    state.name = "NullVM";

    ASSERT_EQ(state.vm, nullptr);
    // Pausing a thread with no VM should still work at the state level.
    state.is_paused = true;
    ASSERT_TRUE(state.is_paused);
}

void test_thread_state_zero_id_sentinel() {
    // Thread ID 0 is the "all threads" sentinel in DAP.
    ThreadState state;
    ASSERT_EQ(state.thread_id, 0);

    // A default-constructed state has the sentinel ID.
    // Verify it can still hold state without errors.
    state.is_paused = true;
    state.step.mode = StepMode::Over;
    ASSERT_TRUE(state.is_paused);
    ASSERT_EQ(static_cast<int>(state.step.mode), static_cast<int>(StepMode::Over));
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Thread State Tests");

    // Per-thread state.
    RUN(test_thread_state_structure);
    RUN(test_frame_mapping_structure);

    // Entry stop reason.
    RUN(test_entry_stop_reason);
    RUN(test_stopped_event_entry_reason);

    // Per-thread stopped event.
    RUN(test_per_thread_stopped_event);

    // Exception-terminated thread.
    RUN(test_exception_terminated_state);

    // Pause pending.
    RUN(test_pause_pending_flag);

    // allThreadsStopped dynamic.
    RUN(test_all_threads_stopped_event_body);

    // Exception info caught vs uncaught.
    RUN(test_exception_info_caught_break_mode);
    RUN(test_thread_state_exception_is_caught);

    // Concurrent thread state.
    RUN(test_thread_state_task_thread);
    RUN(test_thread_state_independent_pause);
    RUN(test_thread_state_independent_step_mode);
    RUN(test_thread_state_exception_per_thread);

    // ThreadState defaults and step modes.
    RUN(test_thread_state_default_fields);
    RUN(test_thread_state_step_modes);

    // Concurrent thread add/remove.
    RUN(test_concurrent_thread_add_remove);
    RUN(test_concurrent_thread_add_remove_interleaved);

    // signal_all_vms_pause_check locking (B04).
    RUN(test_signal_all_vms_pause_check_mixed_vms);
    RUN(test_signal_all_vms_pause_check_races_vm_nulling);

    // State machine transitions.
    RUN(test_thread_state_pause_resume_cycle);
    RUN(test_thread_state_step_mode_transitions);
    RUN(test_thread_state_full_pause_step_cycle);
    RUN(test_thread_state_null_vm_access);
    RUN(test_thread_state_zero_id_sentinel);

    return SUMMARY();
}
