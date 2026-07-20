#ifndef LUMA_DAP_RESPONSE_BUILDERS_HPP
#define LUMA_DAP_RESPONSE_BUILDERS_HPP

#include <string>
#include <string_view>

#include "json/json.hpp"

namespace luma::dap {

using luma::json::JsonBuilder;
using luma::json::JsonValue;

// ═══════════════════════════════════════════════════════════
// DAP response/event body builders
//
// Reusable helpers for constructing the JSON objects sent as
// event bodies or response bodies in DAP messages.  Keeps the
// handler files focused on control flow rather than JSON
// boilerplate.
// ═══════════════════════════════════════════════════════════

// ─── Event bodies ───

// Build the body for a "continued" event.
[[nodiscard]] inline JsonValue make_continued_event_body(int thread_id,
                                                         bool all_threads_continued) {
    return JsonBuilder()
        .set("threadId", thread_id)
        .set("allThreadsContinued", all_threads_continued)
        .build();
}

// Build the body for a "stopped" event.
[[nodiscard]] inline JsonValue make_stopped_event_body(std::string_view reason, int thread_id,
                                                       bool all_threads_stopped) {
    return JsonBuilder()
        .set("reason", std::string(reason))
        .set("threadId", thread_id)
        .set("allThreadsStopped", all_threads_stopped)
        .build();
}

// Build the body for an "output" event.
[[nodiscard]] inline JsonValue make_output_event_body(std::string_view category,
                                                      std::string_view output) {
    return JsonBuilder()
        .set("category", std::string(category))
        .set("output", std::string(output))
        .build();
}

// Build the body for a "thread" event (e.g. thread started/exited).
[[nodiscard]] inline JsonValue make_thread_event_body(std::string_view reason, int thread_id) {
    return JsonBuilder().set("reason", std::string(reason)).set("threadId", thread_id).build();
}

// ─── Object builders ───

// Build a DAP Thread object.
[[nodiscard]] inline JsonValue make_thread_object(int id, const std::string& name) {
    return JsonBuilder().set("id", id).set("name", name).build();
}

// Build a DAP CompletionItem object.
[[nodiscard]] inline JsonValue make_completion_item(const std::string& label,
                                                    const std::string& type) {
    return JsonBuilder().set("label", label).set("type", type).build();
}

// ─── Response body factories ───

// Build a response body containing a "variables" array.
[[nodiscard]] inline JsonValue make_variables_response(const JsonValue::ArrayType& vars) {
    return JsonBuilder().set("variables", JsonValue(vars)).build();
}

// Build a response body containing a "scopes" array.
[[nodiscard]] inline JsonValue make_scopes_response(const JsonValue::ArrayType& scopes) {
    return JsonBuilder().set("scopes", JsonValue(scopes)).build();
}

// Build a DAP Breakpoint response JSON object.
// Convenience builder for constructing individual breakpoint responses
// without creating a Breakpoint struct first.
[[nodiscard]] inline JsonValue make_breakpoint_response(int id, bool verified,
                                                        const std::string& source_path = "",
                                                        int line = 0,
                                                        const std::string& message = "") {
    auto source = JsonBuilder().set("name", std::string{}).set("path", source_path).build();

    return JsonBuilder()
        .set("id", id)
        .set("verified", verified)
        .set("line", line)
        .set("source", std::move(source))
        .set_if(!message.empty(), "message", message)
        .build();
}

// Build a response body containing a "breakpoints" array.
[[nodiscard]] inline JsonValue make_breakpoints_response(const JsonValue::ArrayType& breakpoints) {
    return JsonBuilder().set("breakpoints", JsonValue(breakpoints)).build();
}

// Build a response body containing a "stackFrames" array and "totalFrames" count.
[[nodiscard]] inline JsonValue make_stack_trace_response(const JsonValue::ArrayType& frames,
                                                         int total) {
    return JsonBuilder().set("stackFrames", JsonValue(frames)).set("totalFrames", total).build();
}

// ─── Additional response body factories ───

// Build a response body containing a "threads" array.
[[nodiscard]] inline JsonValue make_threads_response(JsonValue::ArrayType threads) {
    return JsonBuilder().set("threads", JsonValue(std::move(threads))).build();
}

// Build a response body containing a "targets" array.
[[nodiscard]] inline JsonValue make_targets_response(JsonValue::ArrayType targets) {
    return JsonBuilder().set("targets", JsonValue(std::move(targets))).build();
}

// Build a response body containing a "sources" array.
[[nodiscard]] inline JsonValue make_sources_response(JsonValue::ArrayType sources) {
    return JsonBuilder().set("sources", JsonValue(std::move(sources))).build();
}

// Build a body for evaluate responses with result, type, and variablesReference.
// NOTE: The DAP spec uses "result" for evaluate and "value" for setVariable,
// even though both carry the formatted value string.  These two builders are
// intentionally separate to use the correct protocol key.
[[nodiscard]] inline JsonValue
make_evaluate_body(const std::string& result, const std::string& type, int variables_reference) {
    return JsonBuilder()
        .set("result", result)
        .set("type", type)
        .set("variablesReference", variables_reference)
        .build();
}

// Build a body for setVariable responses with value, type, and variablesReference.
[[nodiscard]] inline JsonValue
make_set_variable_body(const std::string& value, const std::string& type, int variables_reference) {
    return JsonBuilder()
        .set("value", value)
        .set("type", type)
        .set("variablesReference", variables_reference)
        .build();
}

// Build a body for source content responses.
[[nodiscard]] inline JsonValue
make_source_content_body(const std::string& content, const std::string& mime_type = "text/plain") {
    return JsonBuilder().set("content", content).set("mimeType", mime_type).build();
}

// Build a body with a boolean success flag and message.
[[nodiscard]] inline JsonValue make_status_body(bool success, const std::string& message) {
    return JsonBuilder().set("success", success).set("message", message).build();
}

} // namespace luma::dap

#endif // LUMA_DAP_RESPONSE_BUILDERS_HPP
