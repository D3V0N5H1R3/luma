#ifndef LUMA_DAP_DEBUG_SESSION_HPP
#define LUMA_DAP_DEBUG_SESSION_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "breakpoint_manager.hpp"
#include "dap_callback_types.hpp"
#include "dap_handler_types.hpp"
#include "dap_session_types.hpp"
#include "dap_types.hpp"
#include "expression_evaluator.hpp"
#include "thread_state_manager.hpp"
#include "variable_inspector.hpp"
#include "vm_hook_registry.hpp"

namespace luma {
class VM;
struct SourceFile;
} // namespace luma

namespace luma::dap {

class ExpressionEvaluator;
class DebugExecutionEngine; // owned as unique_ptr; full definition in debug_execution_engine.hpp

// ─── Session configuration ───
// Bundles all configuration needed to launch a debug session.
// Passed through DebugSession::launch() so the session can apply
// all settings internally without the caller needing to call
// multiple methods.

struct DebugSessionConfig {
    bool stop_on_entry{false};
    bool no_debug{false};
};

// Owns 4 major subsystems: BreakpointManager, VariableInspector,
// DebugExecutionEngine, ExpressionEvaluator.
//
// ═══════════════════════════════════════════════════════════
// DebugSession — slim orchestrator that delegates to:
//   - BreakpointManager      (all breakpoint state and logic)
//   - VariableInspector      (reference registry, variable expansion)
//   - ExpressionEvaluator    (expression evaluation with caching)
//   - ThreadStateManager     (per-thread pause/step state registry)
//   - DebugExecutionEngine   (execution lifecycle, VM, config phase)
//
// Lock ordering (session-level locks) — see dap_session_types.hpp for the
// canonical ordering documentation and debug-build enforcement guards.
//
// Breakpoint and variable reference locks are internal to
// their respective components.
// ═══════════════════════════════════════════════════════════

class DebugSession {
public:
    explicit DebugSession(EventCallback event_cb, OutputCallback output_cb);
    ~DebugSession() noexcept;

    DebugSession(const DebugSession&) = delete;
    DebugSession& operator=(const DebugSession&) = delete;
    DebugSession(DebugSession&&) = delete;
    DebugSession& operator=(DebugSession&&) = delete;

    // ─── Lifecycle ───

    // Launch the program with the given configuration.
    // Returns an empty string on success, or an error message on failure.
    // The returned string MUST be checked — callers should propagate it
    // as a DAP error response if non-empty.
    [[nodiscard]] std::string launch(const std::string& program_path,
                                     const DebugSessionConfig& config,
                                     const std::vector<std::string>& args = {},
                                     const std::string& cwd = "");
    void configuration_done();
    void terminate(bool emit_exit_events = true);

    // ─── Breakpoints (delegated to BreakpointManager) ───

    [[nodiscard]] std::vector<Breakpoint>
    set_breakpoints(const std::string& path, const std::vector<BreakpointRequest>& breakpoints);
    void set_exception_breakpoints(const std::vector<std::string>& filters);

    // ─── Execution control ───

    [[nodiscard]] ExecutionResult continue_execution(int thread_id);
    [[nodiscard]] ExecutionResult step_over(int thread_id);
    [[nodiscard]] ExecutionResult step_into(int thread_id);
    [[nodiscard]] ExecutionResult step_out(int thread_id);
    [[nodiscard]] ExecutionResult pause(int thread_id);

    // ─── State inspection (delegated to VariableInspector) ───

    [[nodiscard]] std::vector<StackFrame> get_stack_trace(int thread_id) const;
    [[nodiscard]] std::vector<Scope> get_scopes(int frame_id) const;
    [[nodiscard]] std::vector<Variable> get_variables(int reference, int start = 0, int count = 0,
                                                      const std::string& filter = "") const;

    // Get the total count of named and indexed variables for a reference.
    [[nodiscard]] std::pair<int, int> get_variable_counts(int reference) const;

    [[nodiscard]] Variable evaluate(int frame_id, const std::string& expression,
                                    EvaluationContext context = EvaluationContext::Default) const;

    // ─── Modification ───

    [[nodiscard]] Variable set_variable(int variables_reference, const std::string& name,
                                        const std::string& value) const;

    // ─── Completions ───

    [[nodiscard]] std::vector<std::pair<std::string, std::string>>
    get_completions(int frame_id, const std::string& text) const;

    // ─── Sources ───

    [[nodiscard]] std::vector<Source> get_loaded_sources() const;

    // ─── Breakpoint locations ───

    [[nodiscard]] std::vector<int> get_breakpoint_locations(const std::string& path, int start_line,
                                                            int end_line) const;

    // ─── Thread listing ───

    [[nodiscard]] std::vector<std::pair<int, std::string>> get_threads() const;

    // Check if a thread ID is valid.
    [[nodiscard]] bool is_thread_valid(int thread_id) const;

    // ─── Query ───

    [[nodiscard]] bool is_running() const;

    [[nodiscard]] std::string last_exception_message() const;
    [[nodiscard]] bool last_exception_is_caught() const;
    [[nodiscard]] std::optional<std::pair<std::string, bool>>
    exception_info(int thread_id) const;
    [[nodiscard]] std::string get_source_content(const std::string& path) const;

    // ─── Hook installation ───

    // Create a narrowed context for VM hook installation.
    // The returned context holds non-owning pointers to internal components
    // and must not outlive this DebugSession.
    [[nodiscard]] HookInstallationContext make_hook_context();

private:
    // Two-phase initialization: configure expression evaluator and execution engine.
    // Called from the constructor after all members are default-initialised.
    void initialize_components();

    // Thread resolver for VariableInspector callbacks.
    [[nodiscard]] VariableInspector::ThreadResolver make_thread_resolver() const;

    // Build a DAP Source descriptor (absolute path + basename) from a loaded
    // source file.
    [[nodiscard]] static Source make_source(const SourceFile& file);

    // Invoke `fn` for each loaded source file (ids 1..N) until `fn` returns
    // false or the source manager runs out of files.  No-op when no source
    // manager is active.
    void for_each_source_file(const std::function<bool(const SourceFile&)>& fn) const;

    // ─── State ───

    EventCallback event_callback_;
    OutputCallback output_callback_;

    // ─── Extracted components ───
    // Declaration order is significant: each member is initialised in this
    // order, so later members may safely reference earlier ones.
    //
    // Component lifecycle and dependencies:
    //   1. breakpoint_manager_    — no dependencies on other components
    //   2. thread_state_manager_  — no dependencies on other components
    //   3. variable_inspector_    — depends on thread_state_manager_ for locking
    //   4. expression_evaluator_  — depends on variable_inspector_
    //   5. execution_engine_      — depends on all above components
    //
    // Destruction order is automatic (reverse declaration order) but documented here
    // for maintenance. If reordering members, ensure dependencies are respected.

    BreakpointManager breakpoint_manager_;
    ThreadStateManager thread_state_manager_;
    VariableInspector variable_inspector_;
    std::unique_ptr<ExpressionEvaluator> expression_evaluator_;

    // Owns the execution lifecycle.  Created in the constructor body after
    // all other members are fully initialised.
    std::unique_ptr<DebugExecutionEngine> execution_engine_;
};

} // namespace luma::dap

#endif // LUMA_DAP_DEBUG_SESSION_HPP
