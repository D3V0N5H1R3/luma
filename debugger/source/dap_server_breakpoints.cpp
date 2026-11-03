#include <format>
#include <limits>

#include "breakpoint_manager.hpp"
#include "dap_breakpoint_handler.hpp"
#include "dap_breakpoint_validator.hpp"
#include "dap_helpers.hpp"
#include "dap_response_builders.hpp"
#include "dap_types.hpp"
#include "debug_session.hpp"
#include "debugger_messages.hpp"

namespace luma::dap {

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

    if (ctx_.has_session()) {
        // Store pending breakpoints so a restart can re-apply them.
        ctx_.pending_breakpoints[source_path] = requests;

        auto result = ctx_.session->set_breakpoints(source_path, requests);

        for (auto& bp : result) {
            validate_breakpoint_fields(bp, ctx_.compiled_bp_cache);
            breakpoints_array.push_back(serialise_breakpoint(bp));
        }

        return HandlerResult::ok(make_breakpoints_response(breakpoints_array));
    }

    // No session yet — return unverified breakpoints and keep the requests for
    // launch.  Each gets an ID unique across every file (negative, so it never
    // collides with session-allocated IDs) and the request carries that ID, so
    // the breakpoint keeps it once it is resolved and the later `breakpoint`
    // event updates the very breakpoint the editor is showing.
    for (auto& req : requests) {
        req.id = ctx_.next_pending_breakpoint_id--;
        breakpoints_array.push_back(
            make_breakpoint_response(req.id, false, source_path, req.line,
                                     std::string{messages::breakpoint::pending_launch}));
    }

    ctx_.pending_breakpoints[source_path] = std::move(requests);

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

} // namespace luma::dap
