#ifndef LUMA_DAP_EXECUTION_HANDLER_HPP
#define LUMA_DAP_EXECUTION_HANDLER_HPP

#include "dap_handler_base.hpp"
#include "dap_handler_context.hpp"

namespace luma::dap {

// ─── Execution Handler ───
// Handles DAP execution control requests: continue, next, stepIn,
// stepOut, pause, and custom Luma extensions.

class DapExecutionHandler : public DapHandler {
public:
    explicit DapExecutionHandler(DapHandlerContext& ctx) : DapHandler(ctx) {}

    [[nodiscard]] HandlerResult handle_continue(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_next(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_step_in(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_step_out(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_pause(const JsonValue& args);

    // ─── Custom Luma extensions ───
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
};

} // namespace luma::dap

#endif // LUMA_DAP_EXECUTION_HANDLER_HPP
