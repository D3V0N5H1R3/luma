#ifndef LUMA_DAP_SERVER_HPP
#define LUMA_DAP_SERVER_HPP

#include <string>

#include "dap_breakpoint_handler.hpp"
#include "dap_execution_handler.hpp"
#include "dap_handler_context.hpp"
#include "dap_inspection_handler.hpp"
#include "dap_lifecycle_handler.hpp"
#include "dap_protocol_handler.hpp"
#include "protocol/transport.hpp"

namespace luma::dap {

class DebugSession;

// ─── DAP Response Policy ───
// Return error for protocol violations (missing required fields in request).
// Return empty/default result for runtime conditions (no session active,
// empty lists, no data).  This matches VS Code's DAP client expectations.

class DapServer {
public:
    explicit DapServer(protocol::Transport& transport);
    ~DapServer();

    // ─── Authentication ───
    // Enable token-based authentication for remote (TCP) debugging.
    // When set, the initialize handler checks for a matching
    // "lumaAuthToken" field in the request arguments.  If the token
    // does not match, an error response is sent and all subsequent
    // requests are rejected.
    //
    // NOTE: For stdio-based transports this is not meaningful because
    // the editor and debugger share the same process pipe.  This is
    // provided for future TCP mode where the transport is network-facing.
    void enable_auth(std::string token);

    // Run the message loop until disconnect. Returns exit code.
    [[nodiscard]] int run();

private:
    // Build the command -> handler dispatch table.
    void init_dispatch_table();

    // Convert a HandlerResult (with PostResponseAction) into a
    // DapProtocolHandler::HandlerResult (with post_response_action callback).
    [[nodiscard]] DapProtocolHandler::HandlerResult convert_result(HandlerResult result);

    // ─── Protocol layer ───
    DapProtocolHandler protocol_handler_;

    // ─── Shared state for all handler groups ───
    DapHandlerContext ctx_;

    // ─── Handler groups ───
    DapLifecycleHandler lifecycle_;
    DapExecutionHandler execution_;
    DapBreakpointHandler breakpoints_;
    DapInspectionHandler inspection_;
};

} // namespace luma::dap

#endif // LUMA_DAP_SERVER_HPP
