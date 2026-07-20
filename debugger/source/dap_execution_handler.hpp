#ifndef LUMA_DAP_EXECUTION_HANDLER_HPP
#define LUMA_DAP_EXECUTION_HANDLER_HPP

#include "dap_handler_base.hpp"
#include "dap_handler_context.hpp"

namespace luma::dap {

// ─── Execution Handler ───
// Handles DAP execution control requests: continue, next, stepIn,
// stepOut, stepBack, reverseContinue, pause, and custom Luma extensions.

class DapExecutionHandler : public DapHandler {
public:
    explicit DapExecutionHandler(DapHandlerContext& ctx) : DapHandler(ctx) {}

    [[nodiscard]] HandlerResult handle_continue(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_next(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_step_in(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_step_out(const JsonValue& args);

    // Step back to the previous recorded VM snapshot.
    // Requires time-travel debugging to be enabled (timeTravel: true in launch config).
    // Returns an error if no snapshots are available or time-travel is disabled.
    [[nodiscard]] HandlerResult handle_step_back(const JsonValue& args);

    // Reverse-continue to the earliest recorded VM snapshot. Gated by the same
    // supportsStepBack capability as stepBack (the DAP spec couples both
    // requests). Requires time-travel debugging to be enabled.
    [[nodiscard]] HandlerResult handle_reverse_continue(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_pause(const JsonValue& args);

    // ─── Custom Luma extensions ───
    [[nodiscard]] HandlerResult handle_hot_reload();
    [[nodiscard]] HandlerResult handle_concurrency_state();

private:
    // Unified execution action dispatcher for continue/step commands.
    enum class ExecutionAction {
        Continue,
        StepOver,
        StepIn,
        StepOut
    };
    [[nodiscard]] HandlerResult execute_thread_action(const JsonValue& args,
                                                      ExecutionAction action);

    // Shared tail for stepBack / reverseContinue: surfaces an execution error
    // or emits the "stopped" event that refreshes the editor's state.
    [[nodiscard]] HandlerResult complete_time_travel_step(const ExecutionResult& result,
                                                          int thread_id);
};

} // namespace luma::dap

#endif // LUMA_DAP_EXECUTION_HANDLER_HPP
