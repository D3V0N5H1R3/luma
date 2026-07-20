#include "dap_helpers.hpp"
#include "dap_lifecycle_handler.hpp"
#include "debug_session.hpp"

namespace luma::dap {

// ═══════════════════════════════════════════════════════════
// DapLifecycleHandler
// ═══════════════════════════════════════════════════════════

HandlerResult DapLifecycleHandler::handle_initialize(const JsonValue& args) {
    // If auth is enabled, verify the client-provided token.
    if (!ctx_.auth_token.empty()) {
        const auto client_token = args.get_or<std::string>("lumaAuthToken", "");

        if (client_token != ctx_.auth_token) {
            ctx_.auth_failed = true;
            return HandlerResult::error(std::string{messages::request::initialize_auth_failed});
        }
    }

    // Record client capabilities and build negotiated server response.
    ctx_.feature_manager.receive_client_capabilities(args);

    HandlerResult result;
    result.body = ctx_.feature_manager.build_capabilities_json();
    result.post_action = PostResponseAction::SendInitialized;
    return result;
}

HandlerResult DapLifecycleHandler::handle_launch(const JsonValue& args) {
    auto config = parse_launch_config(args);

    if (config.program.empty()) {
        return HandlerResult::error(std::string{messages::request::launch_missing_program});
    }

    return ctx_.launch_with_config(config);
}

HandlerResult DapLifecycleHandler::handle_disconnect(const JsonValue& args) {
    // DAP spec: terminateDebuggee defaults to true for launch sessions.
    const bool terminate_debuggee = args.get_or<bool>("terminateDebuggee", true);

    if (ctx_.has_session() && terminate_debuggee) {
        ctx_.session->terminate();
    }

    ctx_.session.reset();

    HandlerResult result;
    result.post_action = PostResponseAction::Disconnect;
    return result;
}

HandlerResult DapLifecycleHandler::handle_terminate(const JsonValue& /*args*/) {
    if (ctx_.has_session()) {
        ctx_.session->terminate();
    }

    return HandlerResult::ok();
}

HandlerResult DapLifecycleHandler::handle_configuration_done() {
    if (ctx_.has_session()) {
        ctx_.session->configuration_done();
    }

    return HandlerResult::ok();
}

HandlerResult DapLifecycleHandler::handle_restart(const JsonValue& args) {
    // Determine launch arguments — prefer new arguments, fall back to previous config.
    LaunchConfig config;

    if (args.is_object() && args.has("arguments")) {
        config = parse_launch_config(args["arguments"]);
    } else {
        config = ctx_.last_launch_config;
    }

    if (config.program.empty()) {
        return HandlerResult::error(std::string{messages::request::restart_missing_program});
    }

    ctx_.reset_session();

    // Re-launch with the same or updated arguments.
    auto result = ctx_.launch_with_config(config);

    if (!result.success) {
        return result;
    }

    // Send initialized event so the client can re-negotiate breakpoints
    // and send configurationDone when ready (per DAP spec).
    result.post_action = PostResponseAction::SendInitialized;
    return result;
}

} // namespace luma::dap
