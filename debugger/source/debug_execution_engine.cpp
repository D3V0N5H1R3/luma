#include "debug_execution_engine.hpp"

#include <filesystem>
#include <format>
#include <iostream>

#include "analysis/source/source_manager.hpp"
#include "breakpoint_manager.hpp"
#include "common/scope_guard.hpp"
#include "compiled_breakpoint.hpp"
#include "dap_breakpoint_validator.hpp"
#include "dap_response_builders.hpp"
#include "dap_types.hpp"
#include "debug_stream_utils.hpp"
#include "diagnostic_log.hpp"
#include "runtime/stdlib/system/process_module.hpp"
#include "runtime/vm/vm.hpp"
#include "thread_state_manager.hpp"
#include "variable_inspector.hpp"
#include "vm_debug_adapter.hpp"

namespace luma::dap {

// ─── Constructor / destructor ───

DebugExecutionEngine::DebugExecutionEngine(SessionComponents components,
                                           ExecutionCallbacks callbacks)
    : session_(components.session),
      thread_mgr_(components.thread_mgr),
      bp_mgr_(components.bp_mgr),
      var_inspector_(components.var_inspector),
      expr_eval_(components.expr_eval),
      event_callback_(std::move(callbacks.event_cb)),
      output_callback_(std::move(callbacks.output_cb)) {}

DebugExecutionEngine::~DebugExecutionEngine() noexcept {
    // terminate() can throw (std::system_error from a lock or the join); a
    // destructor must not, so report it instead of calling std::terminate.
    try {
        terminate();
    } catch (const std::exception& e) {
        write_adapter_diagnostic(std::string{"DAP: error terminating session: "} + e.what());
    }
}

// ─── Execution thread body ───

void DebugExecutionEngine::run_execution(const std::shared_ptr<std::vector<CompiledFunction>>& fns,
                                         const std::shared_ptr<CompiledFunction>& top,
                                         std::shared_ptr<ThreadState> main_state,
                                         const std::vector<std::string>& program_args,
                                         const std::string& working_dir, bool no_debug) {
    // Set the thread-local ID so current_thread() can find this thread's state
    // without acquiring the thread registry lock on every debug hook invocation.
    tl_debug_thread_id = k_main_thread_id;

    if (!wait_for_configuration()) {
        return;
    }

    // Resolve breakpoints now that configuration is done, and tell the client
    // about every one that became bound: it was reported as unverified when it
    // was set before the program was compiled.  Validate its condition, hit
    // condition, and log message the way setBreakpoints does, so an invalid
    // expression is not announced as verified.
    CompiledBreakpointCache validation_cache;

    for (auto& breakpoint : bp_mgr_.resolve_pending_breakpoints()) {
        validate_breakpoint_fields(breakpoint, validation_cache);
        event_callback_(std::string{k_event_breakpoint},
                        make_breakpoint_event_body(k_breakpoint_reason_changed,
                                                   serialise_breakpoint(breakpoint)));
    }

    if (!setup_program_environment(program_args, working_dir)) {
        handle_execution_result(1);
        return;
    }

    setup_vm_hooks(main_state, no_debug);
    execute_with_stream_redirect(fns, top, std::move(main_state));
}

// ─── run_execution helpers ───

bool DebugExecutionEngine::setup_program_environment(const std::vector<std::string>& program_args,
                                                     const std::string& working_dir) {
    if (!program_args.empty()) {
        set_program_args(program_args);
    }

    if (!working_dir.empty()) {
        std::error_code ec;
        std::filesystem::current_path(working_dir, ec);

        if (ec) {
            output_callback_(std::string{k_output_stderr},
                             std::format("Failed to set working directory '{}': {}\n", working_dir,
                                         ec.message()));
            return false;
        }
    }

    return true;
}

void DebugExecutionEngine::execute_with_stream_redirect(
    const std::shared_ptr<std::vector<CompiledFunction>>& fns,
    const std::shared_ptr<CompiledFunction>& top, std::shared_ptr<ThreadState> main_state) {
    int exit_code = 0;

    {
        // Redirect stdout/stderr through DAP output events.  The buffers are
        // scoped so their destructors flush any trailing partial line *before*
        // the terminated/exited events below; otherwise the program's last
        // output would arrive after the client already ended the session.
        LineBufferedStreamBuf line_buf([this](const std::string& text) {
            output_callback_(std::string{k_output_stdout}, text);
        });
        const StreamRedirectGuard cout_guard(std::cout, &line_buf);

        LineBufferedStreamBuf cerr_buf([this](const std::string& text) {
            output_callback_(std::string{k_output_stderr}, text);
        });
        const StreamRedirectGuard cerr_guard(std::cerr, &cerr_buf);

        // Run under the session's cancellation token: the VM checks it at its
        // cancellation points, and blocking channel waits poll it through the
        // thread's active token.
        vm_->set_cancellation_token(program_cancel_token_);
        const auto* const previous_cancel_token = luma::detail::active_cancel_token;
        luma::detail::active_cancel_token = &program_cancel_token_;
        const ScopeGuard restore_cancel_token{
            [previous_cancel_token] { luma::detail::active_cancel_token = previous_cancel_token; }};

        try {
            vm_adapter_->execute(*fns, *top);
        } catch (const std::exception& e) {
            flush_program_output();
            handle_unhandled_exception(e, main_state);
            exit_code = 1;
        }
    }

    handle_execution_result(exit_code);
}

void DebugExecutionEngine::flush_program_output() {
    std::cout.flush();
    std::cerr.flush();
}

void DebugExecutionEngine::handle_unhandled_exception(const std::exception& e,
                                                      std::shared_ptr<ThreadState>& main_state) {
    bool already_reported = false;

    {
        const auto lock = lock_exception();
        already_reported = main_uncaught_stop_reported_;
        last_exception_message_ = e.what();
        last_exception_is_caught_ = false;
    }

    // The uncaught-exception filter already stopped at the throw site (with the
    // frames still intact); stopping again after the unwind would only repeat
    // the same exception with an empty stack.
    if (already_reported || execution_stop_token_.stop_requested()) {
        return;
    }

    // Mark the thread paused *before* emitting the stopped event.  A client that
    // auto-continues on the exception stop can otherwise race ahead of the
    // paused flag: continue_execution would see is_paused == false, skip the
    // unpause, and notify an unregistered waiter — the wakeup is lost and the
    // wait below blocks forever.  Setting the flag under the ThreadState lock
    // first (mirroring resolve_stop_state) closes that window.
    {
        const auto lock = thread_mgr_.lock_state(*main_state);
        main_state->is_paused = true;
        thread_mgr_.increment_paused_count();
        main_state->is_exception_terminated = true;
        main_state->exception_message = e.what();
        main_state->exception_caught = false;
    }

    JsonValue::ObjectType body;
    body["reason"] = JsonValue(std::string{k_stop_reason_exception});
    body["description"] = JsonValue(std::string(e.what()));
    body["threadId"] = JsonValue(k_main_thread_id);
    body["text"] = JsonValue(std::string(e.what()));
    body["allThreadsStopped"] = JsonValue(thread_mgr_.all_threads_stopped());
    event_callback_(std::string{k_event_stopped}, JsonValue(std::move(body)));

    {
        auto lock = thread_mgr_.lock_state_unique(*main_state);
        main_state->cv.wait(lock.underlying(), [this, &main_state] {
            return !main_state->is_paused || execution_stop_token_.stop_requested();
        });
    }
}

void DebugExecutionEngine::handle_execution_result(int exit_code) {
    state_ = SessionState::Terminated;

    // A restart tears the old run down via terminate(false). In that
    // case the client keeps the session and must not see terminated/exited, or it
    // would end the debug session before the replacement run starts.
    if (suppress_exit_events_.load(std::memory_order_relaxed)) {
        return;
    }

    event_callback_(std::string{k_event_terminated}, JsonValue(JsonValue::ObjectType{}));

    JsonValue::ObjectType exit_body;
    exit_body["exitCode"] = JsonValue(exit_code);
    event_callback_(std::string{k_event_exited}, JsonValue(std::move(exit_body)));
}

} // namespace luma::dap
