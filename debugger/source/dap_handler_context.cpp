#include "dap_handler_context.hpp"

#include "dap_response_builders.hpp"
#include "debug_session.hpp"

namespace luma::dap {

DapHandlerContext::DapHandlerContext(DapProtocolHandler& handler) : protocol_handler(handler) {}

DapHandlerContext::~DapHandlerContext() = default;

void DapHandlerContext::create_session(
    std::function<void(const std::string&, const JsonValue&)> event_cb,
    std::function<void(const std::string&, const std::string&)> output_cb) {
    session = std::make_unique<DebugSession>(
        [event_cb = std::move(event_cb)](const std::string& event, const JsonValue& body) {
            event_cb(event, body);
        },
        [output_cb = std::move(output_cb)](const std::string& category, const std::string& text) {
            output_cb(category, text);
        });

    apply_pending_breakpoints();
}

void DapHandlerContext::reset_session(bool emit_exit_events) {
    if (session) {
        session->terminate(emit_exit_events);
        session.reset();
    }

    watch_cache.invalidate();
}

void DapHandlerContext::apply_pending_breakpoints() {
    if (!session) {
        return;
    }

    for (const auto& [bp_path, bp_reqs] : pending_breakpoints) {
        (void)session->set_breakpoints(bp_path, bp_reqs);
    }

    if (!pending_exception_filters.empty()) {
        session->set_exception_breakpoints(pending_exception_filters);
    }

    if (!pending_function_bp_requests.empty()) {
        (void)session->set_function_breakpoints(pending_function_bp_requests);
    }

    for (const auto& data_bp : pending_data_breakpoints) {
        session->set_data_breakpoint(data_bp.data_id, data_bp.access_type, data_bp.condition);
    }
}

HandlerResult DapHandlerContext::launch_with_config(const LaunchConfig& config) {
    create_session([this](const std::string& event,
                          const JsonValue& body) { protocol_handler.send_event(event, body); },
                   [this](const std::string& category, const std::string& text) {
                       protocol_handler.send_event(std::string{kEventOutput},
                                                   make_output_event_body(category, text));
                   });

    const DebugSessionConfig session_config{
        .stop_on_entry = config.stop_on_entry,
        .no_debug = config.no_debug,
        .time_travel = config.time_travel,
    };

    auto error = session->launch(config.program, session_config, config.args, config.cwd);

    if (!error.empty()) {
        return HandlerResult::error(error);
    }

    last_launch_config = config;
    return HandlerResult::ok();
}

} // namespace luma::dap
