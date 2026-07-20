#include "dap_server.hpp"

#include <type_traits>

#include "dap_helpers.hpp"
#include "dap_response_builders.hpp"
#include "dap_types.hpp"
#include "debug_session.hpp"

namespace luma::dap {

// DebugSession is forward-declared in dap_handler_context.hpp; define the
// destructor here where the complete type is available via debug_session.hpp.
DapServer::~DapServer() = default;

DapServer::DapServer(protocol::Transport& transport)
    : protocol_handler_(transport),
      ctx_(protocol_handler_),
      lifecycle_(ctx_),
      execution_(ctx_),
      breakpoints_(ctx_),
      inspection_(ctx_) {
    // When a transport write failure causes an automatic disconnect,
    // terminate any running debug session.
    protocol_handler_.set_disconnect_callback([this]() {
        if (ctx_.has_session()) {
            ctx_.session->terminate();
        }
    });

    init_dispatch_table();
}

void DapServer::enable_auth(std::string token) {
    ctx_.auth_token = std::move(token);
}

void DapServer::init_dispatch_table() {
    // Common handler wrapper factory.  Wraps any handler callable with
    // auth checking and result conversion.  Exceptions thrown by a handler
    // propagate to DapProtocolHandler::dispatch_request, which provides the
    // single dispatch-level error boundary (auth check happens first so an
    // unauthenticated request never reaches the handler).
    // Handlers that accept `const JsonValue&` are forwarded the request
    // args; handlers that take no arguments are called directly.
    auto make_handler = [this](const std::string& command, auto handler_fn) {
        protocol_handler_.register_handler(
            command,
            [this, handler_fn](const JsonValue& args
                               [[maybe_unused]]) -> DapProtocolHandler::HandlerResult {
                if (ctx_.auth_failed) {
                    return DapProtocolHandler::HandlerResult::error(std::string(kErrorAuthFailed));
                }

                HandlerResult result = [&]() {
                    if constexpr (std::is_invocable_v<decltype(handler_fn), const JsonValue&>) {
                        return handler_fn(args);
                    } else {
                        return handler_fn();
                    }
                }();
                return convert_result(std::move(result));
            });
    };

    // --- Lifecycle ---
    make_handler("initialize",
                 [this](const JsonValue& a) { return lifecycle_.handle_initialize(a); });
    make_handler("launch", [this](const JsonValue& a) { return lifecycle_.handle_launch(a); });
    make_handler("disconnect",
                 [this](const JsonValue& a) { return lifecycle_.handle_disconnect(a); });
    make_handler("terminate",
                 [this](const JsonValue& a) { return lifecycle_.handle_terminate(a); });
    make_handler("restart", [this](const JsonValue& a) { return lifecycle_.handle_restart(a); });

    make_handler("configurationDone", [this]() { return lifecycle_.handle_configuration_done(); });

    // --- Breakpoints ---
    make_handler("setBreakpoints",
                 [this](const JsonValue& a) { return breakpoints_.handle_set_breakpoints(a); });
    make_handler("setFunctionBreakpoints", [this](const JsonValue& a) {
        return breakpoints_.handle_set_function_breakpoints(a);
    });
    make_handler("setExceptionBreakpoints", [this](const JsonValue& a) {
        return breakpoints_.handle_set_exception_breakpoints(a);
    });
    make_handler("breakpointLocations", [this](const JsonValue& a) {
        return breakpoints_.handle_breakpoint_locations(a);
    });
    make_handler("dataBreakpointInfo", [this](const JsonValue& a) {
        return breakpoints_.handle_data_breakpoint_info(a);
    });
    make_handler("setDataBreakpoints", [this](const JsonValue& a) {
        return breakpoints_.handle_set_data_breakpoints(a);
    });

    // --- Execution control ---
    make_handler("continue", [this](const JsonValue& a) { return execution_.handle_continue(a); });
    make_handler("next", [this](const JsonValue& a) { return execution_.handle_next(a); });
    make_handler("stepIn", [this](const JsonValue& a) { return execution_.handle_step_in(a); });
    make_handler("stepOut", [this](const JsonValue& a) { return execution_.handle_step_out(a); });
    make_handler("stepBack", [this](const JsonValue& a) { return execution_.handle_step_back(a); });
    make_handler("reverseContinue",
                 [this](const JsonValue& a) { return execution_.handle_reverse_continue(a); });
    make_handler("pause", [this](const JsonValue& a) { return execution_.handle_pause(a); });

    // --- State inspection ---
    make_handler("threads", [this](const JsonValue& a) { return inspection_.handle_threads(a); });
    make_handler("stackTrace",
                 [this](const JsonValue& a) { return inspection_.handle_stack_trace(a); });
    make_handler("scopes", [this](const JsonValue& a) { return inspection_.handle_scopes(a); });
    make_handler("variables",
                 [this](const JsonValue& a) { return inspection_.handle_variables(a); });
    make_handler("evaluate", [this](const JsonValue& a) { return inspection_.handle_evaluate(a); });
    make_handler("setVariable",
                 [this](const JsonValue& a) { return inspection_.handle_set_variable(a); });
    make_handler("completions",
                 [this](const JsonValue& a) { return inspection_.handle_completions(a); });

    // --- Sources ---
    make_handler("loadedSources",
                 [this](const JsonValue& a) { return inspection_.handle_loaded_sources(a); });
    make_handler("source", [this](const JsonValue& a) { return inspection_.handle_source(a); });

    // --- Exception info ---
    make_handler("exceptionInfo",
                 [this](const JsonValue& a) { return inspection_.handle_exception_info(a); });

    // --- Step targets ---
    make_handler("stepInTargets",
                 [this](const JsonValue& a) { return inspection_.handle_step_in_targets(a); });

    // --- Custom Luma extensions ---
    make_handler("luma/hotReload", [this]() { return execution_.handle_hot_reload(); });
    make_handler("luma/concurrencyState",
                 [this]() { return execution_.handle_concurrency_state(); });
}

int DapServer::run() {
    return protocol_handler_.run();
}

// --- Result conversion ---

DapProtocolHandler::HandlerResult DapServer::convert_result(HandlerResult result) {
    DapProtocolHandler::HandlerResult proto_result;
    proto_result.body = std::move(result.body);
    proto_result.success = result.success;
    proto_result.error_message = std::move(result.error_message);

    // Convert PostResponseAction to a post_response_action callback.
    switch (result.post_action) {
        case PostResponseAction::SendInitialized:
            proto_result.post_response_action = [this]() {
                protocol_handler_.send_event(std::string{kEventInitialized},
                                             JsonValue(JsonValue::ObjectType{}));
            };
            break;
        case PostResponseAction::Disconnect:
            proto_result.post_response_action = [this]() {
                protocol_handler_.signal_disconnect();
            };
            break;
        case PostResponseAction::None:
            break;
    }

    return proto_result;
}

} // namespace luma::dap
