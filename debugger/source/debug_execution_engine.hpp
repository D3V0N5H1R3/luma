#ifndef LUMA_DAP_DEBUG_EXECUTION_ENGINE_HPP
#define LUMA_DAP_DEBUG_EXECUTION_ENGINE_HPP

// ─────────────────────────────────────────────────────────────────────────────
// DebugExecutionEngine — owns the execution lifecycle of a debug session.
//
// Responsibilities:
//   - Program launch, compilation gating, and working-directory setup
//   - Configuration phase synchronisation (wait for configurationDone)
//   - VM creation, hook installation, and teardown
//   - Execution control: continue, step-over/into/out, pause
//   - Exception handling coordination
//   - Debug hook implementation: should_break, wait_for_resume, on_exception
//   - Hot-reload source watcher
//
// Holds non-owning references to session-level components
// (ThreadStateManager, BreakpointManager, VariableInspector,
// ExpressionEvaluator) injected at construction.
// ─────────────────────────────────────────────────────────────────────────────

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dap_callback_types.hpp"
#include "dap_handler_types.hpp"
#include "dap_session_types.hpp"
#include "dap_types.hpp"
#include "i_vm_control.hpp"
#include "i_vm_introspection.hpp"

namespace luma {
class VM;
class SourceManager;
struct CompiledFunction;
} // namespace luma

namespace luma::dap {

class BreakpointManager;
class ISourceLocator;
class VMDebugAdapter;
class VariableInspector;
class ExpressionEvaluator;
class HotReloader;
class ThreadStateManager;
class DebugSession; // back-reference for make_hook_context

// ─── Constructor parameter bundles ───

struct SessionComponents {
    DebugSession& session;
    ThreadStateManager& thread_mgr;
    BreakpointManager& bp_mgr;
    VariableInspector& var_inspector;
    ExpressionEvaluator& expr_eval;
};

struct ExecutionCallbacks {
    EventCallback event_cb;
    OutputCallback output_cb;
};

// ═══════════════════════════════════════════════════════════
// DebugExecutionEngine — execution lifecycle manager.
//
// Responsibilities: launch configuration, VM setup, stream redirect,
// execution control, hook registration, exception handling.
//
// These responsibilities are decomposed at the file level rather than
// into separate classes, because they share mutable state (thread
// manager, variable inspector, last-exception data) and a common lock
// ordering:
//   • debug_execution_lifecycle.cpp — launch / setup / teardown
//   • debug_execution_control.cpp   — continue / step / pause / resume
//   • debug_execution_hooks.cpp     — VM hook callbacks (breakpoints,
//                                     stepping, exceptions, log points)
//   • debug_execution_engine.cpp    — construction and shared accessors
// A further split into standalone VmBootstrapper / ExecutionController
// classes is deliberately deferred: it would fragment that shared state
// across objects and duplicate the locking discipline with no functional
// benefit.
//
// DebugSession owns one instance of this class as a private
// implementation detail.  The public interface of DebugSession
// is unchanged; all execution-related methods delegate here.
// ═══════════════════════════════════════════════════════════

class DebugExecutionEngine {
public:
    DebugExecutionEngine(SessionComponents components, ExecutionCallbacks callbacks);

    ~DebugExecutionEngine() noexcept;

    DebugExecutionEngine(const DebugExecutionEngine&) = delete;
    DebugExecutionEngine& operator=(const DebugExecutionEngine&) = delete;
    DebugExecutionEngine(DebugExecutionEngine&&) = delete;
    DebugExecutionEngine& operator=(DebugExecutionEngine&&) = delete;

    // ─── Lifecycle ───

    // Launch the program.  Returns empty string on success, error message on failure.
    // POSTCONDITION on success: execution thread has been started and is waiting
    // for configurationDone() before running user code.
    [[nodiscard]] std::string launch(const std::string& program_path, bool stop_on_entry,
                                     const std::vector<std::string>& args = {},
                                     const std::string& cwd = "", bool no_debug = false);
    void configuration_done();
    void terminate();

    // ─── Execution control ───

    [[nodiscard]] ExecutionResult continue_execution(int thread_id);
    [[nodiscard]] ExecutionResult step_over(int thread_id);
    [[nodiscard]] ExecutionResult step_into(int thread_id);
    [[nodiscard]] ExecutionResult step_out(int thread_id);
    [[nodiscard]] ExecutionResult pause(int thread_id);

    // ─── State queries ───

    [[nodiscard]] bool is_running() const noexcept {
        return state_.load() == SessionState::Running;
    }

    [[nodiscard]] std::string last_exception_message() const;
    [[nodiscard]] bool last_exception_is_caught() const;

    [[nodiscard]] SourceManager* source_manager() const {
        return source_manager_.get();
    }

    [[nodiscard]] VM* vm() const {
        return vm_.get();
    }

    // Interface accessors for decoupled VM access.
    [[nodiscard]] IVMControl* vm_control() const;
    [[nodiscard]] IVMIntrospection* vm_introspection() const;

    [[nodiscard]] std::shared_ptr<std::vector<CompiledFunction>> compiled_functions() const {
        return compiled_functions_;
    }

    [[nodiscard]] std::shared_ptr<CompiledFunction> compiled_top_level() const {
        return compiled_top_level_;
    }

    // ─── Hot reload ───

    [[nodiscard]] int check_for_source_changes();

    // ─── Debug hook methods — called from install_debug_hooks ───

    [[nodiscard]] bool should_break(int file_id, int line, std::size_t frame_depth);
    [[nodiscard]] bool wait_for_resume();
    [[nodiscard]] bool on_exception(const std::string& message, bool is_caught);

    // ─── Lock helpers (config / exception — used by install_debug_hooks) ───

    [[nodiscard]] OrderedLockGuard<DapLockId> lock_config() const;
    [[nodiscard]] OrderedUniqueLock<DapLockId> lock_config_unique() const;
    [[nodiscard]] OrderedLockGuard<DapLockId> lock_exception() const;

    // NOTE: lock_config() returns a non-unique RAII guard (shared lock semantics:
    // multiple readers can hold it simultaneously, though in practice config is
    // single-threaded during startup).  lock_config_unique() is required when
    // calling condition_variable::wait() which needs an underlying unique_lock.

private:
    using StepSetupFn = std::function<void(ThreadState&, IVMControl&, IVMIntrospection&)>;

    // Build the StepSetupFn that records the reference frame depth (and, for
    // step-over, the source location) into the thread's step state.  Returns
    // nullptr for modes that need no setup (step-into and None).
    [[nodiscard]] static StepSetupFn make_step_setup(StepMode mode);

    // Common helper used by step_over, step_into, step_out.  Derives the
    // per-mode step setup internally via make_step_setup().
    [[nodiscard]] ExecutionResult resume_thread(int thread_id, StepMode mode);

    // Unpause a single thread while its state mutex is held.
    // Handles exception-terminated threads, paused-count bookkeeping,
    // step-mode reset, and VM pause-check request.
    // Returns true if the thread was exception-terminated (cv already
    // notified; caller should skip further processing for this thread).
    [[nodiscard]] bool unpause_thread_locked(ThreadState& state);

    // Clear stale exception state and invalidate variable references before a
    // thread is resumed or stepped.  Shared by continue_execution and
    // resume_thread.
    void prepare_for_execution_resume();

    // ─── Launch helpers ───

    [[nodiscard]] std::string validate_launch_config(const std::string& program_path,
                                                     const std::string& cwd);
    void setup_hot_reloader();
    void start_execution_thread(bool stop_on_entry, const std::vector<std::string>& args,
                                const std::string& cwd, bool no_debug);

    // ─── Execution helpers ───

    [[nodiscard]] bool wait_for_configuration();

    // Install debug hooks into the VM for the given thread.
    // PRECONDITION: vm_ must have been created (called from run_execution).
    // PRECONDITION: main_state must be registered in thread_mgr_.
    void setup_vm_hooks(const std::shared_ptr<ThreadState>& main_state, bool no_debug);

    // ─── Execution thread body ───

    void run_execution(const std::shared_ptr<std::vector<CompiledFunction>>& fns,
                       const std::shared_ptr<CompiledFunction>& top,
                       std::shared_ptr<ThreadState> main_state,
                       const std::vector<std::string>& program_args, const std::string& working_dir,
                       bool no_debug);

    // ─── run_execution helpers ───

    [[nodiscard]] bool setup_program_environment(const std::vector<std::string>& program_args,
                                                 const std::string& working_dir);
    void execute_with_stream_redirect(const std::shared_ptr<std::vector<CompiledFunction>>& fns,
                                      const std::shared_ptr<CompiledFunction>& top,
                                      std::shared_ptr<ThreadState> main_state);
    void handle_unhandled_exception(const std::exception& e,
                                    std::shared_ptr<ThreadState>& main_state);
    void handle_execution_result(int exit_code);

    // ─── Debug hook helpers ───

    [[nodiscard]] bool evaluate_step_mode(ThreadState& state, int file_id, int line,
                                          std::size_t frame_depth);
    [[nodiscard]] bool determine_stop_reason(ThreadState& state, int file_id, int line,
                                             std::size_t frame_depth);
    [[nodiscard]] bool evaluate_breakpoint_hit(ThreadState& state, int file_id, int line,
                                               std::size_t frame_depth);

    // Safe condition evaluation with error reporting to output callback.
    [[nodiscard]] std::string evaluate_condition_safe(VM* vm, std::size_t frame_depth,
                                                      const std::string& condition);

    struct StopInfo {
        std::string reason;
        std::string exception_text;
        int hit_breakpoint_id{0};
    };

    [[nodiscard]] StopInfo resolve_stop_state(ThreadState& state);
    void emit_stopped_event(int thread_id, const StopInfo& info);
    void emit_log_message(const std::string& log_message, VM* vm, std::size_t frame_depth);
    [[nodiscard]] std::string format_log_message_expressions(const std::string& log_message, VM* vm,
                                                             std::size_t frame_depth);

    // ─── Non-owning references to session-level components ───

    DebugSession& session_;
    ThreadStateManager& thread_mgr_;
    BreakpointManager& bp_mgr_;
    VariableInspector& var_inspector_;
    ExpressionEvaluator& expr_eval_;

    const EventCallback event_callback_;
    const OutputCallback output_callback_;

    // ─── Execution state ───

    std::jthread execution_thread_;
    std::atomic<SessionState> state_{SessionState::Idle};
    std::stop_token execution_stop_token_;

    // Configuration synchronisation (level 3 — startup only, never nested).
    mutable std::mutex config_mutex_;
    std::condition_variable config_cv_;
    bool is_config_done_{false}; // GUARDED_BY(config_mutex_)

    // Exception state (leaf lock — never held with any other session lock).
    mutable std::mutex exception_mutex_;
    std::string last_exception_message_;   // GUARDED_BY(exception_mutex_)
    bool last_exception_is_caught_{false}; // GUARDED_BY(exception_mutex_)

    // VM and compiled program.
    std::unique_ptr<VM> vm_;
    std::unique_ptr<VMDebugAdapter> vm_adapter_;
    std::unique_ptr<SourceManager> source_manager_;
    std::string program_path_;
    std::shared_ptr<std::vector<CompiledFunction>> compiled_functions_;
    std::shared_ptr<CompiledFunction> compiled_top_level_;

    // Hot-reload file watcher (created lazily on launch).
    std::unique_ptr<HotReloader> hot_reloader_;

    // Adapter that bridges SourceManager → ISourceLocator.
    std::unique_ptr<ISourceLocator> source_locator_;
};

} // namespace luma::dap

#endif // LUMA_DAP_DEBUG_EXECUTION_ENGINE_HPP
