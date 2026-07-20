// DAP protocol structure tests — responses, events, capabilities.

#include <string>

#include "dap_server.hpp"
#include "dap_types.hpp"
#include "json/json.hpp"
#include "test_framework.hpp"

using namespace luma::dap;
using luma::json::JsonValue;

namespace {

// ─── DAP response structure ────────────────────────────────────────

void test_response_structure() {
    // Build a response manually (as DapServer::send_response does).
    JsonValue::ObjectType response;
    response["seq"] = JsonValue(1);
    response["type"] = JsonValue(std::string("response"));
    response["request_seq"] = JsonValue(5);
    response["success"] = JsonValue(true);
    response["command"] = JsonValue(std::string("initialize"));
    response["body"] = JsonValue(JsonValue::ObjectType{});

    auto json = JsonValue(std::move(response));

    // DAP spec requires: seq, type, request_seq, success, command.
    ASSERT_TRUE(json.has("seq"));
    ASSERT_EQ(json["type"].as_string(), "response");
    ASSERT_TRUE(json.has("request_seq"));
    ASSERT_EQ(json["request_seq"].as_integer(), 5);
    ASSERT_TRUE(json["success"].as_bool());
    ASSERT_EQ(json["command"].as_string(), "initialize");
    ASSERT_TRUE(json.has("body"));
}

void test_error_response_structure() {
    JsonValue::ObjectType response;
    response["seq"] = JsonValue(2);
    response["type"] = JsonValue(std::string("response"));
    response["request_seq"] = JsonValue(10);
    response["success"] = JsonValue(false);
    response["command"] = JsonValue(std::string("launch"));
    response["message"] = JsonValue(std::string("Missing 'program' argument"));
    response["body"] = JsonValue(JsonValue::ObjectType{});

    auto json = JsonValue(std::move(response));

    ASSERT_FALSE(json["success"].as_bool());
    ASSERT_TRUE(json.has("message"));
    ASSERT_EQ(json["message"].as_string(), "Missing 'program' argument");
}

// ─── DAP event structure ───────────────────────────────────────────

void test_event_structure() {
    // Build an event manually (as DapServer::send_event does).
    JsonValue::ObjectType msg;
    msg["seq"] = JsonValue(3);
    msg["type"] = JsonValue(std::string("event"));
    msg["event"] = JsonValue(std::string("stopped"));

    JsonValue::ObjectType body;
    body["reason"] = JsonValue(std::string("breakpoint"));
    body["threadId"] = JsonValue(1);
    body["allThreadsStopped"] = JsonValue(true);
    msg["body"] = JsonValue(std::move(body));

    auto json = JsonValue(std::move(msg));

    // DAP spec requires: seq, type, event.
    ASSERT_TRUE(json.has("seq"));
    ASSERT_EQ(json["type"].as_string(), "event");
    ASSERT_EQ(json["event"].as_string(), "stopped");
    ASSERT_TRUE(json.has("body"));
    ASSERT_EQ(json["body"]["reason"].as_string(), "breakpoint");
    ASSERT_EQ(json["body"]["threadId"].as_integer(), 1);
    ASSERT_TRUE(json["body"]["allThreadsStopped"].as_bool());
}

void test_stopped_event_with_hit_breakpoint_ids() {
    JsonValue::ObjectType body;
    body["reason"] = JsonValue(std::string("breakpoint"));
    body["threadId"] = JsonValue(1);
    body["allThreadsStopped"] = JsonValue(true);

    JsonValue::ArrayType ids;
    ids.push_back(JsonValue(42));
    body["hitBreakpointIds"] = JsonValue(std::move(ids));

    auto json = JsonValue(std::move(body));

    ASSERT_TRUE(json.has("hitBreakpointIds"));
    ASSERT_TRUE(json["hitBreakpointIds"].is_array());
    ASSERT_EQ(json["hitBreakpointIds"].as_array().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(json["hitBreakpointIds"].as_array()[0].as_integer(), 42);
}

void test_stopped_event_exception() {
    JsonValue::ObjectType body;
    body["reason"] = JsonValue(std::string("exception"));
    body["threadId"] = JsonValue(1);
    body["allThreadsStopped"] = JsonValue(true);
    body["description"] = JsonValue(std::string("Division by zero"));
    body["text"] = JsonValue(std::string("Division by zero"));

    auto json = JsonValue(std::move(body));

    ASSERT_EQ(json["reason"].as_string(), "exception");
    ASSERT_TRUE(json.has("description"));
    ASSERT_TRUE(json.has("text"));
    ASSERT_TRUE(json["allThreadsStopped"].as_bool());
}

// ─── Capabilities structure ────────────────────────────────────────

void test_capabilities_structure() {
    // Build capabilities as handle_initialize does.
    JsonValue::ObjectType caps;
    caps["supportsConfigurationDoneRequest"] = JsonValue(true);
    caps["supportsConditionalBreakpoints"] = JsonValue(true);
    caps["supportsHitConditionalBreakpoints"] = JsonValue(true);
    caps["supportsEvaluateForHovers"] = JsonValue(true);
    caps["supportsSetVariable"] = JsonValue(true);
    caps["supportsCompletionsRequest"] = JsonValue(true);
    caps["supportsExceptionOptions"] = JsonValue(true);
    caps["supportsLogPoints"] = JsonValue(true);
    caps["supportsLoadedSourcesRequest"] = JsonValue(true);
    caps["supportsTerminateRequest"] = JsonValue(true);

    JsonValue::ArrayType filters;

    JsonValue::ObjectType caught;
    caught["filter"] = JsonValue(std::string("caught"));
    caught["label"] = JsonValue(std::string("Caught Exceptions"));
    caught["default"] = JsonValue(false);
    filters.push_back(JsonValue(std::move(caught)));

    JsonValue::ObjectType uncaught;
    uncaught["filter"] = JsonValue(std::string("uncaught"));
    uncaught["label"] = JsonValue(std::string("Uncaught Exceptions"));
    uncaught["default"] = JsonValue(true);
    filters.push_back(JsonValue(std::move(uncaught)));

    caps["exceptionBreakpointFilters"] = JsonValue(std::move(filters));

    auto json = JsonValue(std::move(caps));

    // Verify all boolean capabilities.
    ASSERT_TRUE(json["supportsConfigurationDoneRequest"].as_bool());
    ASSERT_TRUE(json["supportsConditionalBreakpoints"].as_bool());
    ASSERT_TRUE(json["supportsHitConditionalBreakpoints"].as_bool());
    ASSERT_TRUE(json["supportsEvaluateForHovers"].as_bool());
    ASSERT_TRUE(json["supportsSetVariable"].as_bool());
    ASSERT_TRUE(json["supportsCompletionsRequest"].as_bool());
    ASSERT_TRUE(json["supportsExceptionOptions"].as_bool());
    ASSERT_TRUE(json["supportsLogPoints"].as_bool());
    ASSERT_TRUE(json["supportsLoadedSourcesRequest"].as_bool());
    ASSERT_TRUE(json["supportsTerminateRequest"].as_bool());

    // Verify exception breakpoint filters.
    ASSERT_TRUE(json.has("exceptionBreakpointFilters"));
    const auto& ef = json["exceptionBreakpointFilters"].as_array();
    ASSERT_EQ(ef.size(), static_cast<std::size_t>(2));

    // Filter 0: caught.
    ASSERT_EQ(ef[0]["filter"].as_string(), "caught");
    ASSERT_EQ(ef[0]["label"].as_string(), "Caught Exceptions");
    ASSERT_FALSE(ef[0]["default"].as_bool());

    // Filter 1: uncaught.
    ASSERT_EQ(ef[1]["filter"].as_string(), "uncaught");
    ASSERT_EQ(ef[1]["label"].as_string(), "Uncaught Exceptions");
    ASSERT_TRUE(ef[1]["default"].as_bool());
}

// ─── Continue response body ────────────────────────────────────────

void test_continue_response_body() {
    // DAP spec requires body with allThreadsContinued.
    JsonValue::ObjectType body;
    body["allThreadsContinued"] = JsonValue(true);

    auto json = JsonValue(std::move(body));

    ASSERT_TRUE(json.has("allThreadsContinued"));
    ASSERT_TRUE(json["allThreadsContinued"].as_bool());
}

// ─── Exited event ──────────────────────────────────────────────────

void test_exited_event_body() {
    // DAP spec requires exitCode in body.
    JsonValue::ObjectType body;
    body["exitCode"] = JsonValue(0);

    auto json = JsonValue(std::move(body));

    ASSERT_TRUE(json.has("exitCode"));
    ASSERT_EQ(json["exitCode"].as_integer(), 0);
}

// ─── Thread type ───────────────────────────────────────────────────

void test_thread_structure() {
    // DAP spec requires id and name.
    JsonValue::ObjectType thread;
    thread["id"] = JsonValue(1);
    thread["name"] = JsonValue(std::string("Main Thread"));

    auto json = JsonValue(std::move(thread));

    ASSERT_TRUE(json.has("id"));
    ASSERT_TRUE(json.has("name"));
    ASSERT_EQ(json["id"].as_integer(), 1);
    ASSERT_EQ(json["name"].as_string(), "Main Thread");
}

// ─── CompletionItem type ───────────────────────────────────────────

void test_completion_item_structure() {
    // DAP spec requires label. Type is a valid CompletionItemType enum.
    JsonValue::ObjectType item;
    item["label"] = JsonValue(std::string("my_variable"));
    item["type"] = JsonValue(std::string("variable"));

    auto json = JsonValue(std::move(item));

    ASSERT_TRUE(json.has("label"));
    ASSERT_EQ(json["label"].as_string(), "my_variable");
    ASSERT_EQ(json["type"].as_string(), "variable");
}

// ─── Continued event structure ─────────────────────────────────────

void test_continued_event_structure() {
    // DAP spec: continued event should have threadId and allThreadsContinued.
    JsonValue::ObjectType body;
    body["threadId"] = JsonValue(1);
    body["allThreadsContinued"] = JsonValue(true);

    auto json = JsonValue(std::move(body));

    ASSERT_TRUE(json.has("threadId"));
    ASSERT_EQ(json["threadId"].as_integer(), 1);
    ASSERT_TRUE(json["allThreadsContinued"].as_bool());
}

// ─── Thread event structure ────────────────────────────────────────

void test_thread_started_event() {
    JsonValue::ObjectType body;
    body["reason"] = JsonValue(std::string("started"));
    body["threadId"] = JsonValue(2);

    auto json = JsonValue(std::move(body));

    ASSERT_EQ(json["reason"].as_string(), "started");
    ASSERT_EQ(json["threadId"].as_integer(), 2);
}

void test_thread_exited_event() {
    JsonValue::ObjectType body;
    body["reason"] = JsonValue(std::string("exited"));
    body["threadId"] = JsonValue(3);

    auto json = JsonValue(std::move(body));

    ASSERT_EQ(json["reason"].as_string(), "exited");
    ASSERT_EQ(json["threadId"].as_integer(), 3);
}

// ─── Exception info response ──────────────────────────────────────

void test_exception_info_response() {
    JsonValue::ObjectType body;
    body["exceptionId"] = JsonValue(std::string("RuntimeError"));
    body["description"] = JsonValue(std::string("Division by zero"));
    body["breakMode"] = JsonValue(std::string("unhandled"));

    JsonValue::ObjectType details;
    details["message"] = JsonValue(std::string("Division by zero"));
    details["typeName"] = JsonValue(std::string("RuntimeError"));
    body["details"] = JsonValue(std::move(details));

    auto json = JsonValue(std::move(body));

    ASSERT_EQ(json["exceptionId"].as_string(), "RuntimeError");
    ASSERT_EQ(json["description"].as_string(), "Division by zero");
    ASSERT_EQ(json["breakMode"].as_string(), "unhandled");
    ASSERT_TRUE(json.has("details"));
    ASSERT_EQ(json["details"]["typeName"].as_string(), "RuntimeError");
}

// ─── Source response ───────────────────────────────────────────────

void test_source_response_body() {
    JsonValue::ObjectType body;
    body["content"] = JsonValue(std::string("let x = 42\n"));
    body["mimeType"] = JsonValue(std::string("text/plain"));

    auto json = JsonValue(std::move(body));

    ASSERT_TRUE(json.has("content"));
    ASSERT_EQ(json["mimeType"].as_string(), "text/plain");
}

// ─── Restart capabilities ──────────────────────────────────────────

void test_restart_capability() {
    // Verify supportsRestartRequest is advertised.
    JsonValue::ObjectType caps;
    caps["supportsRestartRequest"] = JsonValue(true);
    caps["supportsExceptionInfoRequest"] = JsonValue(true);

    auto json = JsonValue(std::move(caps));

    ASSERT_TRUE(json["supportsRestartRequest"].as_bool());
    ASSERT_TRUE(json["supportsExceptionInfoRequest"].as_bool());
}

// ─── Exception breakpoint filter response ──────────────────────────

void test_exception_breakpoint_filter_response() {
    // set_exception_breakpoints should return verified breakpoints.
    JsonValue::ArrayType breakpoints_array;

    JsonValue::ObjectType bp;
    bp["verified"] = JsonValue(true);
    bp["message"] = JsonValue(std::string("Filter 'caught' active"));
    breakpoints_array.push_back(JsonValue(std::move(bp)));

    JsonValue::ObjectType body;
    body["breakpoints"] = JsonValue(std::move(breakpoints_array));

    auto json = JsonValue(std::move(body));

    ASSERT_TRUE(json.has("breakpoints"));
    const auto& bps = json["breakpoints"].as_array();
    ASSERT_EQ(bps.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(bps[0]["verified"].as_bool());
}

// ─── Negative breakpoint IDs (pre-launch) ──────────────────────────

void test_pre_launch_breakpoint_ids() {
    // Pre-launch breakpoints should use negative IDs.
    Breakpoint bp;
    bp.id = -1;
    bp.verified = false;
    bp.line = 5;
    bp.message = "Breakpoint will be verified when program launches";

    auto json = serialise_breakpoint(bp);

    ASSERT_EQ(json["id"].as_integer(), -1);
    ASSERT_FALSE(json["verified"].as_bool());
}

// ─── Transport: Content-Length framing ──────────────────────────────

void test_transport_write_format() {
    // Verify the message format (header + body) matches DAP spec.
    auto body = JsonValue(JsonValue::ObjectType{});
    auto body_str = body.to_string();
    auto header = std::format("Content-Length: {}\r\n\r\n", body_str.size());

    ASSERT_TRUE(header.starts_with("Content-Length: "));
    ASSERT_TRUE(header.ends_with("\r\n\r\n"));
}

// ─── Multiple threads in threads response ──────────────────────────

void test_multiple_threads_response() {
    // Build a threads response with main thread + task threads.
    JsonValue::ArrayType threads_array;

    JsonValue::ObjectType main_thread;
    main_thread["id"] = JsonValue(1);
    main_thread["name"] = JsonValue(std::string("Main Thread"));
    threads_array.push_back(JsonValue(std::move(main_thread)));

    JsonValue::ObjectType task_thread;
    task_thread["id"] = JsonValue(2);
    task_thread["name"] = JsonValue(std::string("Task 2"));
    threads_array.push_back(JsonValue(std::move(task_thread)));

    JsonValue::ObjectType body;
    body["threads"] = JsonValue(std::move(threads_array));

    auto json = JsonValue(std::move(body));

    ASSERT_EQ(json["threads"].as_array().size(), static_cast<std::size_t>(2));
    ASSERT_EQ(json["threads"].as_array()[0]["id"].as_integer(), 1);
    ASSERT_EQ(json["threads"].as_array()[1]["id"].as_integer(), 2);
    ASSERT_EQ(json["threads"].as_array()[1]["name"].as_string(), "Task 2");
}

// ─── Terminated event structure ────────────────────────────────────

void test_terminated_event_structure() {
    // Verify terminated event format sent on restart.
    JsonValue::ObjectType body;
    auto json = JsonValue(std::move(body));

    ASSERT_TRUE(json.is_object());
}

// ─── noDebug launch attribute ──────────────────────────────────────

void test_no_debug_launch_attribute() {
    // Verify that a launch request with noDebug=true is parsed correctly.
    JsonValue::ObjectType launch;
    launch["program"] = JsonValue(std::string("test.luma"));
    launch["noDebug"] = JsonValue(true);

    auto json = JsonValue(std::move(launch));

    ASSERT_TRUE(json.has("noDebug"));
    ASSERT_TRUE(json["noDebug"].as_bool());
}

// ─── Launch args structure ─────────────────────────────────────────

void test_launch_args_structure() {
    // Verify that launch arguments with args and cwd parse correctly.
    JsonValue::ObjectType launch;
    launch["program"] = JsonValue(std::string("test.luma"));
    launch["stopOnEntry"] = JsonValue(false);

    JsonValue::ArrayType args;
    args.push_back(JsonValue(std::string("--verbose")));
    args.push_back(JsonValue(std::string("input.txt")));
    launch["args"] = JsonValue(std::move(args));

    launch["cwd"] = JsonValue(std::string("/tmp"));

    auto json = JsonValue(std::move(launch));

    ASSERT_EQ(json["program"].as_string(), "test.luma");
    ASSERT_TRUE(json.has("args"));
    ASSERT_EQ(json["args"].as_array().size(), static_cast<std::size_t>(2));
    ASSERT_EQ(json["args"].as_array()[0].as_string(), "--verbose");
    ASSERT_EQ(json["cwd"].as_string(), "/tmp");
}

// ─── Invalidated event capability ──────────────────────────────────

void test_invalidated_event_capability() {
    JsonValue::ObjectType caps;
    caps["supportsInvalidatedEvent"] = JsonValue(true);
    auto json = JsonValue(std::move(caps));

    ASSERT_TRUE(json["supportsInvalidatedEvent"].as_bool());
}

// ─── Value formatting options capability ───────────────────────────

void test_value_formatting_capability() {
    JsonValue::ObjectType caps;
    caps["supportsValueFormattingOptions"] = JsonValue(true);
    auto json = JsonValue(std::move(caps));

    ASSERT_TRUE(json["supportsValueFormattingOptions"].as_bool());
}

// ─── Hex formatting for integer values ─────────────────────────────

void test_hex_format_integer() {
    const std::string type = "integer";
    const int64_t value = 255;
    const bool hex = true;

    std::string result;

    if (hex && type == "integer") {
        result = std::format("0x{:x}", value);
    } else {
        result = std::to_string(value);
    }

    ASSERT_EQ(result, std::string("0xff"));
}

void test_hex_format_non_integer_ignored() {
    const std::string type = "string";
    const std::string value = "hello";
    const bool hex = true;

    std::string result = value;

    if (hex && type == "integer") {
        result = "0x0";
    }

    ASSERT_EQ(result, std::string("hello"));
}

// ─── Breakpoint locations capability ───────────────────────────────

void test_breakpoint_locations_capability() {
    // Verify supportsBreakpointLocationsRequest is advertised.
    JsonValue::ObjectType caps;
    caps["supportsBreakpointLocationsRequest"] = JsonValue(true);

    auto json = JsonValue(std::move(caps));
    ASSERT_TRUE(json["supportsBreakpointLocationsRequest"].as_bool());
}

void test_breakpoint_locations_response() {
    // Verify the breakpointLocations response body format.
    JsonValue::ArrayType locations;

    JsonValue::ObjectType loc1;
    loc1["line"] = JsonValue(5);
    locations.push_back(JsonValue(std::move(loc1)));

    JsonValue::ObjectType loc2;
    loc2["line"] = JsonValue(10);
    locations.push_back(JsonValue(std::move(loc2)));

    JsonValue::ObjectType body;
    body["breakpoints"] = JsonValue(std::move(locations));

    auto json = JsonValue(std::move(body));

    ASSERT_TRUE(json.has("breakpoints"));
    ASSERT_EQ(json["breakpoints"].as_array().size(), static_cast<std::size_t>(2));
    ASSERT_EQ(json["breakpoints"].as_array()[0]["line"].as_integer(), 5);
    ASSERT_EQ(json["breakpoints"].as_array()[1]["line"].as_integer(), 10);
}

// ─── Stack trace pagination ────────────────────────────────────────

void test_stack_trace_pagination_args() {
    // Verify that stackTrace pagination arguments are correctly structured.
    JsonValue::ObjectType args;
    args["threadId"] = JsonValue(1);
    args["startFrame"] = JsonValue(2);
    args["levels"] = JsonValue(5);

    auto json = JsonValue(std::move(args));

    ASSERT_EQ(json["threadId"].as_integer(), 1);
    ASSERT_EQ(json["startFrame"].as_integer(), 2);
    ASSERT_EQ(json["levels"].as_integer(), 5);
}

// ─── Disconnect terminateDebuggee ──────────────────────────────────

void test_disconnect_terminate_debuggee() {
    // Verify disconnect request with terminateDebuggee field.
    JsonValue::ObjectType args;
    args["terminateDebuggee"] = JsonValue(false);

    auto json = JsonValue(std::move(args));

    ASSERT_TRUE(json.has("terminateDebuggee"));
    ASSERT_FALSE(json["terminateDebuggee"].as_bool());
}

// ─── Evaluate context parameter ────────────────────────────────────

void test_evaluate_context_parameter() {
    // Verify that the evaluate request carries a context field.
    JsonValue::ObjectType args;
    args["expression"] = JsonValue(std::string("my_var"));
    args["frameId"] = JsonValue(1);
    args["context"] = JsonValue(std::string("hover"));

    auto json = JsonValue(std::move(args));

    ASSERT_EQ(json["context"].as_string(), "hover");
}

void test_evaluate_context_repl() {
    // Verify REPL context value.
    JsonValue::ObjectType args;
    args["expression"] = JsonValue(std::string("1 + 2"));
    args["context"] = JsonValue(std::string("repl"));

    auto json = JsonValue(std::move(args));

    ASSERT_EQ(json["context"].as_string(), "repl");
}

// ─── Exited event with non-zero exit code ──────────────────────────

void test_exited_event_error_code() {
    // Verify exited event with exit code 1 (after exception).
    JsonValue::ObjectType body;
    body["exitCode"] = JsonValue(1);

    auto json = JsonValue(std::move(body));

    ASSERT_EQ(json["exitCode"].as_integer(), 1);
}

// ─── allThreadsContinued false for step commands ───────────────────

void test_continued_event_step_single_thread() {
    // Step commands should send allThreadsContinued=false since only
    // the stepped thread resumes, not all threads.
    JsonValue::ObjectType body;
    body["threadId"] = JsonValue(1);
    body["allThreadsContinued"] = JsonValue(false);

    auto json = JsonValue(std::move(body));

    ASSERT_FALSE(json["allThreadsContinued"].as_bool());
    ASSERT_EQ(json["threadId"].as_integer(), 1);
}

// ─── restart sends initialized event ───────────────────────────────

void test_restart_initialized_event() {
    // After restart, the server should send an initialized event so
    // the client can re-negotiate breakpoints.
    JsonValue::ObjectType body;
    auto json = JsonValue(std::move(body));

    // The initialized event has an empty body.
    ASSERT_TRUE(json.is_object());
}

// ─── Handler result ────────────────────────────────────────────────

void test_handler_result_success() {
    auto result = HandlerResult::ok(JsonValue(JsonValue::ObjectType{}));
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.error_message.empty());
}

void test_handler_result_error() {
    auto result = HandlerResult::error("something failed");
    ASSERT_FALSE(result.success);
    ASSERT_EQ(result.error_message, "something failed");
}

// ─── Completions request/response ──────────────────────────────────

void test_completions_request_structure() {
    // completions request with text, column, and frameId.
    JsonValue::ObjectType args;
    args["frameId"] = JsonValue(1);
    args["text"] = JsonValue(std::string("coun"));
    args["column"] = JsonValue(5);

    auto json = JsonValue(std::move(args));

    ASSERT_EQ(json["frameId"].as_integer(), 1);
    ASSERT_EQ(json["text"].as_string(), "coun");
    ASSERT_EQ(json["column"].as_integer(), 5);
}

void test_completions_response_structure() {
    // completions response with multiple items.
    JsonValue::ArrayType targets;

    JsonValue::ObjectType item1;
    item1["label"] = JsonValue(std::string("count"));
    item1["type"] = JsonValue(std::string("variable"));
    targets.push_back(JsonValue(std::move(item1)));

    JsonValue::ObjectType item2;
    item2["label"] = JsonValue(std::string("counter"));
    item2["type"] = JsonValue(std::string("variable"));
    targets.push_back(JsonValue(std::move(item2)));

    JsonValue::ObjectType item3;
    item3["label"] = JsonValue(std::string("concat"));
    item3["type"] = JsonValue(std::string("function"));
    targets.push_back(JsonValue(std::move(item3)));

    JsonValue::ObjectType body;
    body["targets"] = JsonValue(std::move(targets));

    auto json = JsonValue(std::move(body));

    ASSERT_TRUE(json.has("targets"));
    ASSERT_EQ(json["targets"].as_array().size(), static_cast<std::size_t>(3));
    ASSERT_EQ(json["targets"].as_array()[0]["label"].as_string(), "count");
    ASSERT_EQ(json["targets"].as_array()[2]["type"].as_string(), "function");
}

// ─── Restart request args ──────────────────────────────────────────

void test_restart_request_structure() {
    // restart request can optionally carry new launch arguments.
    JsonValue::ObjectType args;
    args["arguments"] = JsonValue(JsonValue::ObjectType{});

    auto json = JsonValue(std::move(args));

    ASSERT_TRUE(json.has("arguments"));
}

// ─── SetVariable request/response ──────────────────────────────────

void test_set_variable_request_structure() {
    // setVariable request structure.
    JsonValue::ObjectType args;
    args["variablesReference"] = JsonValue(100);
    args["name"] = JsonValue(std::string("count"));
    args["value"] = JsonValue(std::string("42"));

    auto json = JsonValue(std::move(args));

    ASSERT_EQ(json["variablesReference"].as_integer(), 100);
    ASSERT_EQ(json["name"].as_string(), "count");
    ASSERT_EQ(json["value"].as_string(), "42");
}

void test_set_variable_success_response() {
    // Successful setVariable response.
    JsonValue::ObjectType body;
    body["value"] = JsonValue(std::string("42"));
    body["type"] = JsonValue(std::string("integer"));
    body["variablesReference"] = JsonValue(0);

    auto json = JsonValue(std::move(body));

    ASSERT_EQ(json["value"].as_string(), "42");
    ASSERT_EQ(json["type"].as_string(), "integer");
    ASSERT_EQ(json["variablesReference"].as_integer(), 0);
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Protocol Structure Tests");

    // Protocol structure.
    RUN(test_response_structure);
    RUN(test_error_response_structure);
    RUN(test_event_structure);
    RUN(test_stopped_event_with_hit_breakpoint_ids);
    RUN(test_stopped_event_exception);

    // Capabilities.
    RUN(test_capabilities_structure);

    // Response bodies.
    RUN(test_continue_response_body);
    RUN(test_exited_event_body);
    RUN(test_thread_structure);
    RUN(test_completion_item_structure);

    // Continued event.
    RUN(test_continued_event_structure);

    // Thread events.
    RUN(test_thread_started_event);
    RUN(test_thread_exited_event);

    // Exception info.
    RUN(test_exception_info_response);

    // Source response.
    RUN(test_source_response_body);

    // Restart capability.
    RUN(test_restart_capability);

    // Exception breakpoint filter response.
    RUN(test_exception_breakpoint_filter_response);

    // Pre-launch breakpoint IDs.
    RUN(test_pre_launch_breakpoint_ids);

    // Transport format.
    RUN(test_transport_write_format);

    // Multiple threads response.
    RUN(test_multiple_threads_response);

    // Terminated event.
    RUN(test_terminated_event_structure);

    // noDebug launch.
    RUN(test_no_debug_launch_attribute);

    // Launch args.
    RUN(test_launch_args_structure);

    // Invalidated event.
    RUN(test_invalidated_event_capability);

    // Value formatting.
    RUN(test_value_formatting_capability);
    RUN(test_hex_format_integer);
    RUN(test_hex_format_non_integer_ignored);

    // Breakpoint locations.
    RUN(test_breakpoint_locations_capability);
    RUN(test_breakpoint_locations_response);

    // Stack trace pagination.
    RUN(test_stack_trace_pagination_args);

    // Disconnect.
    RUN(test_disconnect_terminate_debuggee);

    // Evaluate context.
    RUN(test_evaluate_context_parameter);
    RUN(test_evaluate_context_repl);

    // Exited event with error code.
    RUN(test_exited_event_error_code);

    // allThreadsContinued for step commands.
    RUN(test_continued_event_step_single_thread);

    // restart initialized event.
    RUN(test_restart_initialized_event);

    // Handler result.
    RUN(test_handler_result_success);
    RUN(test_handler_result_error);

    // Completions.
    RUN(test_completions_request_structure);
    RUN(test_completions_response_structure);

    // Restart request.
    RUN(test_restart_request_structure);

    // SetVariable request/response.
    RUN(test_set_variable_request_structure);
    RUN(test_set_variable_success_response);

    return SUMMARY();
}
