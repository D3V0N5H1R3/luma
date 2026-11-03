#include <format>
#include <utility>

#include "breakpoint_manager.hpp"
#include "compiled_breakpoint.hpp"
#include "dap_helpers.hpp"
#include "dap_types.hpp"
#include "debug_execution_engine.hpp"
#include "debug_session.hpp"
#include "expression_evaluator.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_introspection.hpp"
#include "thread_state_manager.hpp"
#include "variable_inspector.hpp"
#include "vm_assert.hpp"
#include "vm_debug_adapter.hpp"
#include "vm_hook_registry.hpp"

namespace luma::dap {

// ─── VM hook installation ───

void DebugExecutionEngine::setup_vm_hooks(const std::shared_ptr<ThreadState>& main_state,
                                          bool no_debug) {
    auto global_env = Environment::create();
    register_all(global_env);

    vm_ = std::make_unique<VM>(global_env);
    vm_adapter_ = std::make_unique<VMDebugAdapter>(*vm_);

    {
        // ThreadState::vm is guarded by the state's mutex: the protocol thread
        // reads it (pause, setBreakpoints, evaluate) concurrently with startup.
        const auto lock = thread_mgr_.lock_state(*main_state);
        main_state->vm = vm_.get();
    }

    if (!no_debug) {
        install_debug_hooks(*vm_, session_.make_hook_context());
    }

    if (!no_debug && main_state->pending.stop_on_entry) {
        const auto lock = thread_mgr_.lock_state(*main_state);
        main_state->step.mode = StepMode::Into;
        vm_adapter_->request_pause_check();
    } else if (bp_mgr_.has_active_breakpoints()) {
        vm_adapter_->request_pause_check();
    }
}

// ─── Debug hook ───

bool DebugExecutionEngine::should_break(int file_id, int line, std::size_t frame_depth) {
    if (execution_stop_token_.stop_requested()) {
        return true;
    }

    auto state = thread_mgr_.current_thread();

    if (!state) {
        return false;
    }

    const auto line_state = update_line_state(*state, file_id, line, frame_depth);

    if (line_state.step_complete) {
        return true;
    }

    if (line_state.returned_to_caller_line) {
        // Back on the line that made the call: its breakpoints already fired
        // (or were skipped) when execution first reached it.  Stay armed so the
        // next line change is checked.
        if (bp_mgr_.has_active_breakpoints() && state->vm != nullptr) {
            state->vm->request_pause_check();
        }

        return false;
    }

    return determine_stop_reason(*state, file_id, line, frame_depth);
}

// Evaluate whether the current step mode's depth/location criteria are met.
// Encapsulates step-over (same or shallower depth, different location),
// step-into (always stops), and step-out (shallower depth) semantics.
[[nodiscard]] static bool evaluate_step_mode_depth(const StepState& step, int file_id, int line,
                                                   std::size_t frame_depth) {
    switch (step.mode) {
        case StepMode::Into:
            return true;

        case StepMode::Over:
            return frame_depth <= step.reference_depth &&
                   (line != step.reference_line || file_id != step.reference_file);

        case StepMode::Out:
            return frame_depth < step.reference_depth;

        case StepMode::None:
            return false;
    }

    return false;
}

DebugExecutionEngine::LineHookState
DebugExecutionEngine::update_line_state(ThreadState& state, int file_id, int line,
                                        std::size_t frame_depth) {
    const auto lock = thread_mgr_.lock_state(state);
    LineHookState result;

    // A return to the caller lands back on the line that made the call, at a
    // shallower depth.  An exception unwinding to a handler on another line
    // also arrives shallower, but at a different line, so it is not matched.
    if (frame_depth > 0) {
        const auto slot = frame_depth - 1;
        const std::pair<int, int> location{file_id, line};

        result.returned_to_caller_line = frame_depth < state.last_hook_depth &&
                                         slot < state.frame_lines.size() &&
                                         state.frame_lines[slot] == location;

        state.frame_lines.resize(frame_depth, std::pair<int, int>{-1, -1});
        state.frame_lines[slot] = location;
    }

    state.last_hook_depth = frame_depth;
    result.step_complete = evaluate_step_mode_depth(state.step, file_id, line, frame_depth);
    return result;
}

bool DebugExecutionEngine::determine_stop_reason(ThreadState& state, int file_id, int line,
                                                 std::size_t frame_depth) {
    // Query the breakpoint set for this exact location in a single locked,
    // hash-indexed lookup.  A previous fast-path guard called
    // BreakpointManager::has_breakpoints_for_file_id() before this point, which
    // acquired the breakpoint mutex a second time and — for function
    // breakpoints — performed an O(function-breakpoints) linear scan on every
    // line change.  check_breakpoint() already returns "no hit" cheaply through
    // O(1) index lookups when this line has no breakpoint, so the guard only
    // duplicated the lock and the lookup without changing the outcome.
    if (evaluate_breakpoint_hit(state, file_id, line, frame_depth)) {
        return true;
    }

    // Keep requesting pause checks while breakpoints are active so the hook
    // continues to fire on subsequent line changes.
    if (bp_mgr_.has_active_breakpoints()) {
        if (state.vm != nullptr) {
            state.vm->request_pause_check();
        }
    }

    return false;
}

std::string DebugExecutionEngine::evaluate_condition_safe(VM* vm, std::size_t frame_depth,
                                                          const std::string& condition) {
    const int top_frame = top_frame_index(frame_depth);

    try {
        auto result = expr_eval_.evaluate(vm, top_frame, condition);

        if (result.value.starts_with("<") && output_callback_) {
            output_callback_(
                std::string{k_output_console},
                std::format("Breakpoint condition error: '{}' -- {}\n", condition, result.value));
        }

        return result.value;
    } catch (const std::exception& e) {
        if (output_callback_) {
            output_callback_(
                std::string{k_output_console},
                std::format("Breakpoint condition crashed: '{}' -- {}\n", condition, e.what()));
        }
        return "";
    } catch (...) {
        // Non-std::exception failure — report to debug console and treat
        // the condition as unmet so the breakpoint doesn't fire.
        if (output_callback_) {
            output_callback_(std::string{k_output_console},
                             std::format("Breakpoint condition crashed: '{}'\n", condition));
        }
        return "";
    }
}

bool DebugExecutionEngine::evaluate_breakpoint_hit(ThreadState& state, int file_id, int line,
                                                   std::size_t frame_depth) {
    // Check breakpoints via BreakpointManager.
    // VM must be active here — this is called from the debug hook which only
    // fires while the VM is executing.
    LUMA_ASSERT_VM(state);

    auto check_result = bp_mgr_.check_breakpoint(
        file_id, line, [this, &state, frame_depth](const std::string& condition) -> std::string {
            return evaluate_condition_safe(state.vm, frame_depth, condition);
        });

    for (const auto& log_message : check_result.log_messages) {
        emit_log_message(log_message, state.vm, frame_depth);
    }

    if (check_result.should_break) {
        const auto ts_lock = thread_mgr_.lock_state(state);
        state.pending.hit_breakpoint_id = check_result.hit_breakpoint_id;
        return true;
    }

    return false;
}

// ─── Resume helpers ───

DebugExecutionEngine::StopInfo DebugExecutionEngine::resolve_stop_state(ThreadState& state) {
    StopInfo info;

    const auto lock = thread_mgr_.lock_state(state);

    // Consume the pending breakpoint hit on every stop so a stale ID can never
    // be attributed to a later, unrelated stop.
    const int hit_breakpoint_id = std::exchange(state.pending.hit_breakpoint_id, 0);

    if (!state.pending.exception_message.empty()) {
        info.reason = std::string{k_stop_reason_exception};
        info.exception_text = state.pending.exception_message;
        state.pending.exception_message.clear();
    } else if (state.pending.stop_on_entry) {
        info.reason = std::string{k_stop_reason_entry};
        state.pending.stop_on_entry = false;
    } else if (state.pending.pause) {
        info.reason = std::string{k_stop_reason_pause};
        state.pending.pause = false;
    } else if (hit_breakpoint_id != 0) {
        // Checked before the step mode: a breakpoint hit while stepping over or
        // out of a call is a breakpoint stop, not a completed step.
        info.reason = std::string{k_stop_reason_breakpoint};
        info.hit_breakpoint_id = hit_breakpoint_id;
    } else if (state.step.mode != StepMode::None) {
        info.reason = std::string{k_stop_reason_step};
    } else {
        info.reason = std::string{k_stop_reason_breakpoint};
    }

    if (state.vm != nullptr) {
        const VMDebugAdapter adapter(*state.vm);
        state.step.reference_depth = adapter.depth();
        auto loc = adapter.current_location();
        state.step.reference_line = loc.line;
        state.step.reference_file = loc.file_id;
    }

    state.step.mode = StepMode::None;
    state.is_paused = true;
    thread_mgr_.increment_paused_count();

    return info;
}

void DebugExecutionEngine::emit_stopped_event(int thread_id, const StopInfo& info) {
    flush_program_output();

    JsonValue::ObjectType body;
    body["reason"] = JsonValue(info.reason);
    body["threadId"] = JsonValue(thread_id);
    body["allThreadsStopped"] = JsonValue(thread_mgr_.all_threads_stopped());

    if (!info.exception_text.empty()) {
        body["description"] = JsonValue(info.exception_text);
        body["text"] = JsonValue(info.exception_text);
    }

    if (info.hit_breakpoint_id != 0) {
        JsonValue::ArrayType ids;
        ids.emplace_back(info.hit_breakpoint_id);
        body["hitBreakpointIds"] = JsonValue(std::move(ids));
    }

    event_callback_(std::string{k_event_stopped}, JsonValue(std::move(body)));
}

// ─── Wait for resume ───

bool DebugExecutionEngine::wait_for_resume() {
    if (execution_stop_token_.stop_requested()) {
        return false;
    }

    auto state = thread_mgr_.current_thread();

    if (!state) {
        return false;
    }

    const int thread_id = state->thread_id;
    auto info = resolve_stop_state(*state);
    emit_stopped_event(thread_id, info);

    auto lock = thread_mgr_.lock_state_unique(*state);
    state->cv.wait(lock.underlying(), [this, &state] {
        return !state->is_paused || execution_stop_token_.stop_requested();
    });

    return !execution_stop_token_.stop_requested();
}

// ─── Exception hook ───

bool DebugExecutionEngine::on_exception(const std::string& message, bool is_caught) {
    if ((is_caught && bp_mgr_.break_on_caught()) || (!is_caught && bp_mgr_.break_on_uncaught())) {
        {
            const auto lock = lock_exception();
            last_exception_message_ = message;
            last_exception_is_caught_ = is_caught;

            // An uncaught exception on the main thread ends the program; once
            // the user has seen it at the throw site, the post-unwind stop in
            // handle_unhandled_exception() would only repeat it.
            if (!is_caught && tl_debug_thread_id == k_main_thread_id) {
                main_uncaught_stop_reported_ = true;
            }
        }

        auto state = thread_mgr_.current_thread();

        if (state) {
            const auto lock = thread_mgr_.lock_state(*state);
            state->pending.exception_message = message;
            state->pending.exception_caught = is_caught;
            state->exception_message = message;
            state->exception_caught = is_caught;
        }

        return true;
    }

    return false;
}

// ─── Log message ───

void DebugExecutionEngine::emit_log_message(const std::string& log_message, VM* vm,
                                            std::size_t frame_depth) {
    if (!output_callback_ || log_message.empty()) {
        return;
    }

    const std::string output = format_log_message_expressions(log_message, vm, frame_depth);
    output_callback_(std::string{k_output_console}, output + "\n");
}

std::string DebugExecutionEngine::format_log_message_expressions(const std::string& log_message,
                                                                 VM* vm, std::size_t frame_depth) {
    const int top_frame = top_frame_index(frame_depth);
    std::string output;

    for (const auto& part : parse_log_template(log_message)) {
        if (part.is_expression) {
            output +=
                expr_eval_.evaluate(vm, top_frame, part.text, EvaluationContext::Default).value;
        } else {
            output += part.text;
        }
    }

    return output;
}

} // namespace luma::dap
