#include "debug_execution_engine.hpp"

#include <filesystem>
#include <format>
#include <iostream>

#include "analysis/source/source_manager.hpp"
#include "breakpoint_manager.hpp"
#include "dap_types.hpp"
#include "debug_stream_utils.hpp"
#include "hot_reloader.hpp"
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
    terminate();
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

    // Resolve breakpoints now that configuration is done.
    bp_mgr_.resolve_pending_breakpoints();
    bp_mgr_.resolve_function_breakpoints();

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
            output_callback_(std::string{kOutputStderr},
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
    // Redirect stdout/stderr through DAP output events.
    LineBufferedStreamBuf line_buf(
        [this](const std::string& text) { output_callback_(std::string{kOutputStdout}, text); });
    const StreamRedirectGuard cout_guard(std::cout, &line_buf);

    LineBufferedStreamBuf cerr_buf(
        [this](const std::string& text) { output_callback_(std::string{kOutputStderr}, text); });
    const StreamRedirectGuard cerr_guard(std::cerr, &cerr_buf);

    try {
        vm_adapter_->execute(*fns, *top);
    } catch (const std::exception& e) {
        handle_unhandled_exception(e, main_state);
        handle_execution_result(1);
        return;
    }

    handle_execution_result(0);
}

void DebugExecutionEngine::handle_unhandled_exception(const std::exception& e,
                                                      std::shared_ptr<ThreadState>& main_state) {
    bool already_handled = false;

    {
        const auto lock = lock_exception();

        if (!last_exception_message_.empty()) {
            already_handled = true;
        }

        last_exception_message_ = e.what();
        last_exception_is_caught_ = false;
    }

    if (already_handled || execution_stop_token_.stop_requested()) {
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
    }

    JsonValue::ObjectType body;
    body["reason"] = JsonValue(std::string{kStopReasonException});
    body["description"] = JsonValue(std::string(e.what()));
    body["threadId"] = JsonValue(1);
    body["text"] = JsonValue(std::string(e.what()));
    body["allThreadsStopped"] = JsonValue(thread_mgr_.all_threads_stopped());
    event_callback_(std::string{kEventStopped}, JsonValue(std::move(body)));

    {
        auto lock = thread_mgr_.lock_state_unique(*main_state);
        main_state->cv.wait(lock.underlying(), [this, &main_state] {
            return !main_state->is_paused || execution_stop_token_.stop_requested();
        });
    }
}

void DebugExecutionEngine::handle_execution_result(int exit_code) {
    state_ = SessionState::Terminated;

    // A restart / hot reload tears the old run down via terminate(false). In that
    // case the client keeps the session and must not see terminated/exited, or it
    // would end the debug session before the replacement run starts.
    if (suppress_exit_events_.load(std::memory_order_relaxed)) {
        return;
    }

    event_callback_(std::string{kEventTerminated}, JsonValue(JsonValue::ObjectType{}));

    JsonValue::ObjectType exit_body;
    exit_body["exitCode"] = JsonValue(exit_code);
    event_callback_(std::string{kEventExited}, JsonValue(std::move(exit_body)));
}

} // namespace luma::dap
