#ifndef LUMA_DAP_BREAKPOINT_HANDLER_HPP
#define LUMA_DAP_BREAKPOINT_HANDLER_HPP

#include "dap_handler_base.hpp"
#include "dap_handler_context.hpp"

namespace luma::dap {

// ─── Breakpoint Handler ───
// Handles DAP breakpoint requests: setBreakpoints, setFunctionBreakpoints,
// setExceptionBreakpoints, breakpointLocations, and data breakpoints.

class DapBreakpointHandler : public DapHandler {
public:
    explicit DapBreakpointHandler(DapHandlerContext& ctx) : DapHandler(ctx) {}

    [[nodiscard]] HandlerResult handle_set_breakpoints(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_set_function_breakpoints(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_set_exception_breakpoints(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_breakpoint_locations(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_data_breakpoint_info(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_set_data_breakpoints(const JsonValue& args);
};

} // namespace luma::dap

#endif // LUMA_DAP_BREAKPOINT_HANDLER_HPP
