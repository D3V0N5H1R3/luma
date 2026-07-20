#ifndef LUMA_DAP_HELPERS_HPP
#define LUMA_DAP_HELPERS_HPP

#include <cstddef>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/narrow_int.hpp"
#include "dap_handler_types.hpp"
#include "dap_types.hpp"
#include "debugger_messages.hpp"
#include "json/json_helpers.hpp"

namespace luma::dap {

class DebugSession;

using luma::narrow_int;
using luma::json::JsonValue;
using luma::json::try_extract_field;

// Extract thread_id from DAP arguments, defaulting to 1.
[[nodiscard]] inline int extract_thread_id(const JsonValue& args) {
    return args.get_or<int>("threadId", 1);
}

// Convert a frame depth or frame count to the index of the top (innermost)
// frame.  Returns 0 when there are no frames so callers can index safely.
[[nodiscard]] inline int top_frame_index(std::size_t depth_or_count) noexcept {
    return depth_or_count > 0 ? static_cast<int>(depth_or_count) - 1 : 0;
}

// ─── Session validation ───

// Require an active debug session or throw.
// Handlers that fundamentally need a running session (execution control,
// modification) should call this at the top.  The dispatch loop catches
// the exception and converts it into a DAP error response.
// Handlers that can tolerate a missing session (breakpoints, inspection
// reads, lifecycle no-ops) should instead guard with ctx_.has_session();
// see the no-session policy note on DapHandlerContext::has_session.
[[nodiscard]] inline DebugSession& require_session(std::unique_ptr<DebugSession>& session) {
    if (!session) {
        throw std::runtime_error(std::string{messages::session::no_active_session});
    }
    return *session;
}

// ─── Launch configuration ───

// Parsed launch/restart arguments extracted from DAP JSON.
struct LaunchConfig {
    std::string program;
    bool stop_on_entry{false};
    bool no_debug{false};
    bool time_travel{false};
    std::vector<std::string> args;
    std::string cwd;
};

// Parse a LaunchConfig from DAP launch/restart arguments.
// Returns a LaunchConfig with the program field empty if not present.
[[nodiscard]] inline LaunchConfig parse_launch_config(const JsonValue& args) {
    LaunchConfig config;

    if (!args.is_object()) {
        return config;
    }

    config.program = args.get_or<std::string>("program", "");
    config.stop_on_entry = args.get_or<bool>("stopOnEntry", false);
    config.no_debug = args.get_or<bool>("noDebug", false);
    config.time_travel = args.get_or<bool>("timeTravel", false);

    const auto& arg_array = args.get("args");
    if (arg_array.is_array()) {
        for (const auto& arg : arg_array.as_array()) {
            if (arg.is_string()) {
                config.args.push_back(arg.as_string());
            }
        }
    }

    config.cwd = args.get_or<std::string>("cwd", "");

    return config;
}

// ─── Breakpoint request parsing ───

// Parse a single BreakpointRequest from a DAP JSON breakpoint object.
[[nodiscard]] inline BreakpointRequest parse_breakpoint_request(const JsonValue& bp) {
    BreakpointRequest req;
    req.line = bp.get_or<int>("line", 0);
    req.name = bp.get_or<std::string>("name", "");
    req.condition = bp.get_or<std::string>("condition", "");
    req.hit_condition = bp.get_or<std::string>("hitCondition", "");
    req.log_message = bp.get_or<std::string>("logMessage", "");
    return req;
}

// Parse an array of BreakpointRequests from a DAP JSON "breakpoints" array.
[[nodiscard]] inline std::vector<BreakpointRequest>
parse_breakpoint_requests(const JsonValue& args, const std::string& array_key = "breakpoints") {
    std::vector<BreakpointRequest> requests;

    const auto& bp_array = args.get(array_key);
    if (!bp_array.is_array()) {
        return requests;
    }

    for (const auto& bp : bp_array.as_array()) {
        requests.push_back(parse_breakpoint_request(bp));
    }

    return requests;
}

// Format an integer value string as hexadecimal. Returns the original string on failure.
[[nodiscard]] inline std::string format_value_hex(const std::string& value) {
    try {
        const auto int_val = std::stoll(value);
        return std::format("0x{:x}", int_val);
    } catch (...) {
        return "<formatting error>";
    }
}

// ─── Session collection responses ───

// Generic session array response builder.  Reduces boilerplate for handlers
// that follow the pattern:
//   1. Check for an active session
//   2. Query the session for a collection
//   3. Serialise each element into JSON
//   4. Wrap the array in a response body
// If no session is active, returns an empty-array response.
//
// Parameters:
//   session       — the active DebugSession (nullptr = no session active)
//   getter        — callable: (DebugSession&) -> Container
//   serialize     — callable: (const Item&) -> JsonValue
//   make_response — callable: (JsonValue::ArrayType) -> JsonValue
template <typename GetterFn, typename SerializeFn, typename ResponseFn>
[[nodiscard]] inline HandlerResult
build_session_array_response(const std::unique_ptr<DebugSession>& session, GetterFn getter,
                             SerializeFn serialize, ResponseFn make_response) {
    JsonValue::ArrayType items;

    if (session) {
        auto collection = getter(*session);

        for (const auto& item : collection) {
            items.push_back(serialize(item));
        }
    }

    return HandlerResult::ok(make_response(std::move(items)));
}

} // namespace luma::dap

#endif // LUMA_DAP_HELPERS_HPP
