#include <format>

#include "dap_helpers.hpp"
#include "dap_inspection_handler.hpp"
#include "dap_response_builders.hpp"
#include "dap_types.hpp"
#include "debug_session.hpp"

namespace luma::dap {

namespace {

// Watch and Clipboard evaluations are side-effect-free reads that the editor
// issues repeatedly, so their results are safe to cache.  Other contexts (e.g.
// REPL input) may have side effects and must always be re-evaluated.
[[nodiscard]] bool is_cacheable_context(EvaluationContext context) {
    return context == EvaluationContext::Watch || context == EvaluationContext::Clipboard;
}

// Body returned by exceptionInfo when there is no exception to report
// (no session, or no exception recorded on the session).
[[nodiscard]] JsonValue make_empty_exception_info_body() {
    JsonValue::ObjectType body;
    body["exceptionId"] = JsonValue(std::string(""));
    body["breakMode"] = JsonValue(std::string("never"));
    return JsonValue(std::move(body));
}

} // namespace

// ─── State inspection ───

HandlerResult DapInspectionHandler::handle_threads(const JsonValue& /*args*/) {
    return build_session_array_response(
        ctx_.session, [](DebugSession& s) { return s.get_threads(); },
        [](const auto& t) { return make_thread_object(t.first, t.second); }, make_threads_response);
}

HandlerResult DapInspectionHandler::handle_stack_trace(const JsonValue& args) {
    const int thread_id = extract_thread_id(args);

    // Validate thread ID.
    if (ctx_.session && !ctx_.session->is_thread_valid(thread_id)) {
        return HandlerResult::error(std::string{messages::request::invalid_thread_id});
    }

    int start_frame = 0;
    int levels = 0; // 0 = return all frames.

    if (args.is_object()) {
        start_frame = std::max(0, args.get_or<int>("startFrame", 0));
        levels = std::max(0, args.get_or<int>("levels", 0));
    }

    JsonValue::ArrayType frames_array;
    int total_frames = 0;

    if (ctx_.has_session()) {
        auto frames = ctx_.session->get_stack_trace(thread_id);
        total_frames = static_cast<int>(frames.size());

        // Apply pagination: skip 'startFrame' entries, return up to 'levels'.
        // Compute the end frame in 64-bit to avoid signed overflow: a hostile
        // client may send startFrame and levels each up to INT_MAX, whose sum
        // overflows int (UB).  Both are clamped to >= 0 above, so the widened
        // sum is non-negative and the min with total_frames fits back in int.
        const int end_frame =
            (levels > 0) ? static_cast<int>(std::min<long long>(
                               static_cast<long long>(start_frame) + static_cast<long long>(levels),
                               static_cast<long long>(total_frames)))
                         : total_frames;

        for (int i = start_frame; i < end_frame; ++i) {
            frames_array.push_back(serialise_stack_frame(frames[static_cast<std::size_t>(i)]));
        }
    }

    return HandlerResult::ok(make_stack_trace_response(frames_array, total_frames));
}

HandlerResult DapInspectionHandler::handle_scopes(const JsonValue& args) {
    const int frame_id = args.get_or<int>("frameId", 0);

    return build_session_array_response(
        ctx_.session, [frame_id](DebugSession& s) { return s.get_scopes(frame_id); },
        serialise_scope, make_scopes_response);
}

HandlerResult DapInspectionHandler::handle_variables(const JsonValue& args) {
    const auto request = parse_variables_request(args);

    JsonValue::ArrayType vars_array;

    if (ctx_.has_session()) {
        auto vars = ctx_.session->get_variables(request.reference, request.start, request.count,
                                                request.filter);

        apply_variable_formatting(vars, request.format_hex);

        for (const auto& var : vars) {
            vars_array.push_back(serialise_variable(var));
        }
    }

    JsonValue::ObjectType body;
    body["variables"] = JsonValue(std::move(vars_array));

    // Add pagination fields if the reference has structured children.
    if (ctx_.session && request.reference != 0) {
        auto [named_count, indexed_count] = ctx_.session->get_variable_counts(request.reference);

        if (named_count > 0) {
            body["namedVariables"] = JsonValue(static_cast<int64_t>(named_count));
        }

        if (indexed_count > 0) {
            body["indexedVariables"] = JsonValue(static_cast<int64_t>(indexed_count));
        }
    }

    return HandlerResult::ok(JsonValue(std::move(body)));
}

DapInspectionHandler::VariablesRequest
DapInspectionHandler::parse_variables_request(const JsonValue& args) {
    VariablesRequest request;

    if (args.is_object()) {
        request.reference = args.get_or<int>("variablesReference", 0);
        request.start = args.get_or<int>("start", 0);
        request.count = args.get_or<int>("count", 0);
        request.filter = args.get_or<std::string>("filter", "");

        const auto& format_obj = args.get("format");
        if (format_obj.is_object()) {
            request.format_hex = format_obj.get_or<bool>("hex", false);
        }
    }

    return request;
}

void DapInspectionHandler::apply_variable_formatting(std::vector<Variable>& vars, bool format_hex) {
    if (!format_hex) {
        return;
    }

    for (auto& var : vars) {
        if (var.type == "integer") {
            var.value = format_value_hex(var.value);
        }
    }
}

HandlerResult DapInspectionHandler::handle_evaluate(const JsonValue& args) {
    std::string expression;
    int frame_id{0};
    std::string context;
    bool format_hex = false;

    if (args.is_object()) {
        expression = args.get_or<std::string>("expression", "");
        frame_id = args.get_or<int>("frameId", 0);
        context = args.get_or<std::string>("context", "");

        const auto& format_obj = args.get("format");
        if (format_obj.is_object()) {
            format_hex = format_obj.get_or<bool>("hex", false);
        }
    }

    const auto eval_context = parse_evaluation_context(context);

    JsonValue::ObjectType body;

    if (ctx_.has_session()) {
        if (auto cached = try_cached_evaluate(frame_id, expression, eval_context)) {
            return *cached;
        }

        const auto result = ctx_.session->evaluate(frame_id, expression, eval_context);
        auto display = result.value;

        if (format_hex && result.type == "integer") {
            display = format_value_hex(display);
        }

        body["result"] = JsonValue(display);
        body["type"] = JsonValue(result.type);
        body["variablesReference"] = JsonValue(result.variables_reference);

        cache_evaluate_result(frame_id, expression, eval_context, display, result.type,
                              result.variables_reference);
    } else {
        body["result"] = JsonValue(std::string("<no session>"));
        body["variablesReference"] = JsonValue(0);
    }

    return HandlerResult::ok(JsonValue(std::move(body)));
}

std::optional<HandlerResult>
DapInspectionHandler::try_cached_evaluate(int frame_id, const std::string& expression,
                                          EvaluationContext context) const {
    if (!is_cacheable_context(context)) {
        return std::nullopt;
    }

    const auto cached = ctx_.watch_cache.get(frame_id, expression);
    if (!cached.has_value()) {
        return std::nullopt;
    }

    return HandlerResult::ok(
        make_evaluate_body(cached->result, cached->type, cached->variables_reference));
}

void DapInspectionHandler::cache_evaluate_result(int frame_id, const std::string& expression,
                                                 EvaluationContext context,
                                                 const std::string& display,
                                                 const std::string& type, int variables_reference) {
    if (!is_cacheable_context(context)) {
        return;
    }

    ctx_.watch_cache.put(frame_id, expression,
                         WatchCache::Entry{.expression = expression,
                                           .frame_id = frame_id,
                                           .result = display,
                                           .type = type,
                                           .variables_reference = variables_reference});
}

// ─── Modification ───

HandlerResult DapInspectionHandler::handle_set_variable(const JsonValue& args) {
    // Validate required fields per DAP spec.
    if (!args.is_object() || !args.has("variablesReference") || !args.has("name") ||
        !args.has("value")) {
        return HandlerResult::error(std::string{messages::request::set_variable_missing_fields});
    }

    const int reference = args.get_or<int>("variablesReference", 0);
    const auto name = args.get_or<std::string>("name", "");
    const auto value = args.get_or<std::string>("value", "");

    if (ctx_.has_session()) {
        auto result = ctx_.session->set_variable(reference, name, value);

        // If set_variable returns type "error", send a DAP error response
        // instead of silently returning the error string as the value.
        if (result.type == "error") {
            return HandlerResult::error(result.value);
        }

        // Invalidate cached watch results so subsequent evaluate requests
        // reflect the modified variable value.
        ctx_.invalidate_watches();

        return HandlerResult::ok(
            make_set_variable_body(result.value, result.type, result.variables_reference));
    }

    return HandlerResult::ok(make_set_variable_body(value, "", 0));
}

// ─── Completions ───

HandlerResult DapInspectionHandler::handle_completions(const JsonValue& args) {
    int frame_id{0};
    std::string text;

    if (args.is_object()) {
        frame_id = args.get_or<int>("frameId", 0);
        text = args.get_or<std::string>("text", "");
    }

    return build_session_array_response(
        ctx_.session,
        [frame_id, &text](DebugSession& s) { return s.get_completions(frame_id, text); },
        [](const auto& c) { return make_completion_item(c.first, c.second); },
        make_targets_response);
}

// ─── Sources ───

HandlerResult DapInspectionHandler::handle_loaded_sources(const JsonValue& /*args*/) {
    return build_session_array_response(
        ctx_.session, [](DebugSession& s) { return s.get_loaded_sources(); }, serialise_source,
        make_sources_response);
}

HandlerResult DapInspectionHandler::handle_source(const JsonValue& args) {
    if (args.is_object() && args.has("source") && args["source"].has("path")) {
        auto path = args["source"]["path"].as_string();

        if (ctx_.has_session()) {
            auto content = ctx_.session->get_source_content(path);

            if (!content.empty()) {
                return HandlerResult::ok(make_source_content_body(content));
            }
        }
    }

    return HandlerResult::ok(make_source_content_body(""));
}

// ─── Exception info ───

HandlerResult DapInspectionHandler::handle_exception_info(const JsonValue& /*args*/) {
    if (!ctx_.has_session()) {
        return HandlerResult::ok(make_empty_exception_info_body());
    }

    auto message = ctx_.session->last_exception_message();

    if (message.empty()) {
        return HandlerResult::ok(make_empty_exception_info_body());
    }

    JsonValue::ObjectType body;
    body["exceptionId"] = JsonValue(std::string("RuntimeError"));
    body["description"] = JsonValue(message);
    body["breakMode"] =
        JsonValue(std::string(ctx_.session->last_exception_is_caught() ? "always" : "unhandled"));

    JsonValue::ObjectType details;
    details["message"] = JsonValue(message);
    details["typeName"] = JsonValue(std::string("RuntimeError"));
    body["details"] = JsonValue(std::move(details));

    return HandlerResult::ok(JsonValue(std::move(body)));
}

// ─── Step-in targets ───

HandlerResult DapInspectionHandler::handle_step_in_targets(const JsonValue& args) {
    JsonValue::ArrayType targets_array;

    if (args.is_object() && ctx_.has_session()) {
        const int frame_id = args.get_or<int>("frameId", 0);

        // Get the current line's callable expressions as step-in targets.
        auto completions = ctx_.session->get_completions(frame_id, "");

        int target_id = 1;

        for (const auto& [name, type] : completions) {
            if (type == "function" || type == "method") {
                JsonValue::ObjectType target;
                target["id"] = JsonValue(target_id++);
                target["label"] = JsonValue(name);
                targets_array.emplace_back(std::move(target));
            }
        }
    }

    return HandlerResult::ok(make_targets_response(std::move(targets_array)));
}

} // namespace luma::dap
