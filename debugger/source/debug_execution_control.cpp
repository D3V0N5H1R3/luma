#include "breakpoint_manager.hpp"
#include "dap_error_handler.hpp"
#include "dap_types.hpp"
#include "debug_execution_engine.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_introspection.hpp"
#include "thread_state_manager.hpp"
#include "variable_inspector.hpp"
#include "vm_assert.hpp"
#include "vm_debug_adapter.hpp"

namespace luma::dap {

// ─── Thread iteration helper ───

// Apply a per-thread function to a single thread or all threads.
// When thread_id is 0, iterates all threads from a snapshot.
// Returns an error only when a specific thread_id is not found.
template <typename Fn>
static ExecutionResult apply_to_all_threads(ThreadStateManager& thread_mgr, int thread_id, Fn fn) {
    if (thread_id == 0) {
        auto all_states = thread_mgr.all_threads_snapshot();

        for (auto& state : all_states) {
            fn(state);
        }

        return ExecutionResult::ok();
    }

    auto state = thread_mgr.get_thread(thread_id);

    if (!state) {
        return ExecutionResult::error(error_messages::unknown_thread_id(thread_id));
    }

    fn(state);
    return ExecutionResult::ok();
}

// ─── Pause-check helper ───

// Request a VM pause-check for the thread if it has an attached VM.
static void safe_request_pause_check(ThreadState& state) {
    if (state.vm != nullptr) {
        state.vm->request_pause_check();
    }
}

// ─── Unpause helper ───

bool DebugExecutionEngine::unpause_thread_locked(ThreadState& state) {
    if (state.is_exception_terminated) {
        if (state.is_paused) {
            thread_mgr_.decrement_paused_count();
        }

        state.is_paused = false;
        state.cv.notify_all();
        return true;
    }

    state.step.mode = StepMode::None;

    if (state.is_paused) {
        thread_mgr_.decrement_paused_count();
    }

    state.is_paused = false;

    safe_request_pause_check(state);

    return false;
}

// Clear stale exception state and invalidate variable references before a
// thread is resumed or stepped.
void DebugExecutionEngine::prepare_for_execution_resume() {
    {
        const auto lock = lock_exception();
        last_exception_message_.clear();
    }

    var_inspector_.invalidate_refs();
}

// ─── Execution control ───

ExecutionResult DebugExecutionEngine::continue_execution(int thread_id) {
    prepare_for_execution_resume();

    return apply_to_all_threads(thread_mgr_, thread_id,
                                [this](const std::shared_ptr<ThreadState>& state) {
                                    {
                                        const auto lock = thread_mgr_.lock_state(*state);

                                        if (unpause_thread_locked(*state)) {
                                            return;
                                        }
                                    }

                                    state->cv.notify_all();
                                });
}

ExecutionResult DebugExecutionEngine::resume_thread(int thread_id, StepMode mode) {
    auto state = thread_mgr_.get_thread(thread_id);

    if (!state) {
        return ExecutionResult::error(error_messages::unknown_thread_id(thread_id));
    }

    prepare_for_execution_resume();

    {
        const auto lock = thread_mgr_.lock_state(*state);
        state->step.mode = mode;

        if (state->vm != nullptr) {
            if (const StepSetupFn setup_fn = make_step_setup(mode)) {
                VMDebugAdapter adapter(*state->vm);
                setup_fn(*state, adapter, adapter);
            }
        }

        safe_request_pause_check(*state);

        if (state->is_paused) {
            thread_mgr_.decrement_paused_count();
        }

        state->is_paused = false;
    }

    state->cv.notify_all();
    return ExecutionResult::ok();
}

// Build a StepSetupFn that captures the current frame depth (and optionally
// the current source location) into the thread's step state.  Step-into
// needs no setup; step-over additionally records line/file to detect
// movement; step-out only needs the depth.
DebugExecutionEngine::StepSetupFn DebugExecutionEngine::make_step_setup(StepMode mode) {
    switch (mode) {
        case StepMode::Over:
            return [](ThreadState& ts, IVMControl& /*control*/, IVMIntrospection& intro) {
                ts.step.reference_depth = intro.depth();
                auto loc = intro.current_location();
                ts.step.reference_line = loc.line;
                ts.step.reference_file = loc.file_id;
            };

        case StepMode::Out:
            return [](ThreadState& ts, IVMControl& /*control*/, IVMIntrospection& intro) {
                ts.step.reference_depth = intro.depth();
            };

        case StepMode::Into:
        case StepMode::None:
            return nullptr;
    }

    return nullptr;
}

ExecutionResult DebugExecutionEngine::step_over(int thread_id) {
    return resume_thread(thread_id, StepMode::Over);
}

ExecutionResult DebugExecutionEngine::step_into(int thread_id) {
    return resume_thread(thread_id, StepMode::Into);
}

ExecutionResult DebugExecutionEngine::step_out(int thread_id) {
    return resume_thread(thread_id, StepMode::Out);
}

ExecutionResult DebugExecutionEngine::pause(int thread_id) {
    return apply_to_all_threads(thread_mgr_, thread_id,
                                [this](const std::shared_ptr<ThreadState>& state) {
                                    const auto lock = thread_mgr_.lock_state(*state);

                                    if (!state->is_paused) {
                                        state->pending.pause = true;
                                        state->step.mode = StepMode::Into;
                                    }

                                    safe_request_pause_check(*state);
                                });
}

} // namespace luma::dap
