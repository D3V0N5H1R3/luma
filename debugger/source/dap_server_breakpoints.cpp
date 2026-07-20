#include <format>
#include <limits>

#include "breakpoint_manager.hpp"
#include "dap_breakpoint_handler.hpp"
#include "dap_breakpoint_validator.hpp"
#include "dap_helpers.hpp"
#include "dap_response_builders.hpp"
#include "dap_types.hpp"
#include "debug_session.hpp"

namespace luma::dap {

// ─── Pending / unverified breakpoints ───

// Populate `out` with unverified pending responses for each request.
// Uses negative IDs to avoid collision with session-assigned IDs.
// `line_fn(req)` → the line number to report; `msg_fn(req)` → the message.
template <typename LineFn, typename MsgFn>
static void apply_pending_or_unverified(JsonValue::ArrayType& out,
                                        const std::vector<BreakpointRequest>& requests,
                                        const std::string& source, LineFn line_fn, MsgFn msg_fn) {
    int pending_id = -1;
    for (const auto& req : requests) {
        out.push_back(
            make_breakpoint_response(pending_id--, false, source, line_fn(req), msg_fn(req)));
    }
}

// ─── Exception filter parsing ───

// Extract the list of exception filter IDs from a setExceptionBreakpoints
// request's "filters" array.  Returns an empty vector if the field is
// absent or not an array.
static std::vector<std::string> parse_exception_filters(const JsonValue& args) {
    std::vector<std::string> filters;

    const auto& filter_array = args.get("filters");
    if (filter_array.is_array()) {
        for (const auto& f : filter_array.as_array()) {
            if (f.is_string()) {
                filters.push_back(f.as_string());
            }
        }
    }

    return filters;
}

// ─── Breakpoints ───

HandlerResult DapBreakpointHandler::handle_set_breakpoints(const JsonValue& args) {
    JsonValue::ArrayType breakpoints_array;

    if (!args.is_object() || !args.has("source")) {
        return HandlerResult::ok(make_breakpoints_response(breakpoints_array));
    }

    const auto& source = args["source"];
    std::string source_path;

    if (source.has("path")) {
        source_path = source.get_or<std::string>("path", "");
    }

    auto requests = parse_breakpoint_requests(args);

    // Store pending breakpoints so they survive before session creation.
    ctx_.pending_breakpoints[source_path] = requests;

    if (ctx_.has_session()) {
        auto result = ctx_.session->set_breakpoints(source_path, requests);

        for (auto& bp : result) {
            validate_breakpoint_fields(bp, ctx_.compiled_bp_cache);
            breakpoints_array.push_back(serialise_breakpoint(bp));
        }
    } else {
        // No session yet — return unverified breakpoints (will be applied at launch).
        apply_pending_or_unverified(
            breakpoints_array, requests, source_path,
            [](const BreakpointRequest& req) { return req.line; },
            [](const BreakpointRequest& /*req*/) {
                return std::string("Breakpoint will be verified when program launches");
            });
    }

    return HandlerResult::ok(make_breakpoints_response(breakpoints_array));
}

HandlerResult DapBreakpointHandler::handle_set_function_breakpoints(const JsonValue& args) {
    auto requests = parse_breakpoint_requests(args);

    // Store names for pre-launch re-application.
    ctx_.pending_function_bp_requests = requests;

    JsonValue::ArrayType breakpoints_array;

    if (ctx_.has_session()) {
        auto result = ctx_.session->set_function_breakpoints(requests);

        for (const auto& bp : result) {
            breakpoints_array.push_back(serialise_breakpoint(bp));
        }
    } else {
        // No session yet — return unverified breakpoints.
        apply_pending_or_unverified(
            breakpoints_array, requests, "", [](const BreakpointRequest& /*req*/) { return 0; },
            [](const BreakpointRequest& req) {
                return std::format("Function '{}' will be verified at launch", req.name);
            });
    }

    return HandlerResult::ok(make_breakpoints_response(breakpoints_array));
}

HandlerResult DapBreakpointHandler::handle_set_exception_breakpoints(const JsonValue& args) {
    auto filters = parse_exception_filters(args);

    // Store for pre-launch application.
    ctx_.pending_exception_filters = filters;

    if (ctx_.has_session()) {
        ctx_.session->set_exception_breakpoints(filters);
    }

    // Return validated filter responses per DAP spec.
    JsonValue::ArrayType breakpoints_array;

    for (const auto& filter : filters) {
        JsonValue::ObjectType bp;
        bp["verified"] = JsonValue(true);
        bp["message"] = JsonValue(std::format("Filter '{}' active", filter));
        breakpoints_array.emplace_back(std::move(bp));
    }

    return HandlerResult::ok(make_breakpoints_response(breakpoints_array));
}

HandlerResult DapBreakpointHandler::handle_breakpoint_locations(const JsonValue& args) {
    JsonValue::ArrayType locations_array;

    if (args.is_object() && args.has("source") && ctx_.has_session()) {
        const auto& source = args["source"];
        std::string source_path;

        if (source.has("path")) {
            source_path = source.get_or<std::string>("path", "");
        }

        int start_line = 0;
        int end_line = std::numeric_limits<int>::max();

        const auto& start_line_val = args.get("line");
        if (start_line_val.is_integer()) {
            start_line = narrow_int(start_line_val.as_integer());
        }

        const auto& end_line_val = args.get("endLine");
        if (end_line_val.is_integer()) {
            end_line = narrow_int(end_line_val.as_integer());
        } else {
            end_line = start_line;
        }

        auto lines = ctx_.session->get_breakpoint_locations(source_path, start_line, end_line);

        for (const int line : lines) {
            JsonValue::ObjectType loc;
            loc["line"] = JsonValue(line);
            locations_array.emplace_back(std::move(loc));
        }
    }

    return HandlerResult::ok(make_breakpoints_response(locations_array));
}

// ─── Data breakpoints ───

HandlerResult DapBreakpointHandler::handle_data_breakpoint_info(const JsonValue& args) {
    JsonValue::ObjectType body;

    const auto name = args.get_or<std::string>("name", "");

    if (name.empty()) {
        body["dataId"] = JsonValue();
        body["description"] = JsonValue(std::string("Data breakpoints require a variable name"));
        body["accessTypes"] = JsonValue(JsonValue::ArrayType{});
        return HandlerResult::ok(JsonValue(std::move(body)));
    }

    // Return a data ID that the client can use to set the breakpoint.
    // Format: "varname" — the session will watch for writes to this variable.
    body["dataId"] = JsonValue(name);
    body["description"] = JsonValue(std::format("Break on write to '{}'", name));

    JsonValue::ArrayType access_types;
    access_types.emplace_back(std::string("write"));
    access_types.emplace_back(std::string("readWrite"));
    body["accessTypes"] = JsonValue(std::move(access_types));

    return HandlerResult::ok(JsonValue(std::move(body)));
}

HandlerResult DapBreakpointHandler::handle_set_data_breakpoints(const JsonValue& args) {
    if (!args.is_object() || !args.has("breakpoints") || !args["breakpoints"].is_array()) {
        return HandlerResult::error(
            std::string{messages::request::set_data_breakpoints_missing_array});
    }

    // setDataBreakpoints replaces the full set, so rebuild the pending mirror
    // from scratch.  Mirroring (like the line/function/exception handlers) lets
    // apply_pending_breakpoints() restore them when the session is recreated,
    // e.g. on luma/hotReload — otherwise the reloaded program loses them.
    ctx_.pending_data_breakpoints.clear();

    if (ctx_.has_session()) {
        ctx_.session->clear_data_breakpoints();
    }

    JsonValue::ArrayType breakpoints_array;
    const auto& bp_list = args["breakpoints"].as_array();

    for (const auto& bp : bp_list) {
        JsonValue::ObjectType bp_result;

        const auto data_id = bp.get_or<std::string>("dataId", "");

        if (!data_id.empty()) {
            const auto access_type = bp.get_or<std::string>("accessType", "write");
            const auto condition = bp.get_or<std::string>("condition", "");

            ctx_.pending_data_breakpoints.push_back(
                DataBreakpointRequest{data_id, access_type, condition});

            if (ctx_.has_session()) {
                // Register the data breakpoint with the session.
                ctx_.session->set_data_breakpoint(data_id, access_type, condition);
            }

            bp_result["verified"] = JsonValue(true);
            bp_result["id"] = JsonValue(static_cast<int64_t>(breakpoints_array.size() + 1));
        } else {
            bp_result["verified"] = JsonValue(false);
            bp_result["message"] = JsonValue(std::string("Missing dataId"));
        }

        breakpoints_array.emplace_back(std::move(bp_result));
    }

    return HandlerResult::ok(make_breakpoints_response(breakpoints_array));
}

} // namespace luma::dap
