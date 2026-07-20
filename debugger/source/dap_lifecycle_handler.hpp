#ifndef LUMA_DAP_LIFECYCLE_HANDLER_HPP
#define LUMA_DAP_LIFECYCLE_HANDLER_HPP

#include "dap_handler_base.hpp"
#include "dap_handler_context.hpp"

namespace luma::dap {

// ─── Lifecycle Handler ───
// Handles DAP lifecycle requests: initialize, launch, disconnect,
// terminate, configurationDone, and restart.

class DapLifecycleHandler : public DapHandler {
public:
    explicit DapLifecycleHandler(DapHandlerContext& ctx) : DapHandler(ctx) {}

    [[nodiscard]] HandlerResult handle_initialize(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_launch(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_disconnect(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_terminate(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_configuration_done();
    [[nodiscard]] HandlerResult handle_restart(const JsonValue& args);
};

} // namespace luma::dap

#endif // LUMA_DAP_LIFECYCLE_HANDLER_HPP
