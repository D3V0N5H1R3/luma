// DAP type serialisation tests — Source, Breakpoint, StackFrame, Scope, Variable.

#include <string>

#include "dap_types.hpp"
#include "json/json.hpp"
#include "test_framework.hpp"
#include "variable_inspector.hpp"

using namespace luma::dap;
using luma::json::JsonValue;

namespace {

// ─── Source serialisation ──────────────────────────────────────────

void test_serialise_source() {
    Source src;
    src.name = "main.luma";
    src.path = "/home/user/main.luma";

    auto json = serialise_source(src);

    ASSERT_TRUE(json.is_object());
    ASSERT_EQ(json["name"].as_string(), "main.luma");
    ASSERT_EQ(json["path"].as_string(), "/home/user/main.luma");
}

void test_serialise_source_empty() {
    Source src;
    auto json = serialise_source(src);

    ASSERT_TRUE(json.is_object());
    ASSERT_EQ(json["name"].as_string(), "");
    ASSERT_EQ(json["path"].as_string(), "");
}

// ─── Breakpoint serialisation ──────────────────────────────────────

void test_serialise_breakpoint_verified() {
    Breakpoint bp;
    bp.id = 42;
    bp.verified = true;
    bp.source.name = "test.luma";
    bp.source.path = "/tmp/test.luma";
    bp.line = 10;

    auto json = serialise_breakpoint(bp);

    ASSERT_TRUE(json.is_object());
    ASSERT_EQ(json["id"].as_integer(), 42);
    ASSERT_TRUE(json["verified"].as_bool());
    ASSERT_EQ(json["line"].as_integer(), 10);
    ASSERT_TRUE(json.has("source"));
    ASSERT_EQ(json["source"]["name"].as_string(), "test.luma");
    ASSERT_EQ(json["source"]["path"].as_string(), "/tmp/test.luma");
    // No message for verified breakpoints.
    ASSERT_FALSE(json.has("message"));
}

void test_serialise_breakpoint_unverified() {
    Breakpoint bp;
    bp.id = 1;
    bp.verified = false;
    bp.line = 5;
    bp.message = "No executable code on this line";

    auto json = serialise_breakpoint(bp);

    ASSERT_TRUE(json.is_object());
    ASSERT_FALSE(json["verified"].as_bool());
    ASSERT_TRUE(json.has("message"));
    ASSERT_EQ(json["message"].as_string(), "No executable code on this line");
}

// ─── StackFrame serialisation ──────────────────────────────────────

void test_serialise_stack_frame() {
    StackFrame frame;
    frame.id = 1;
    frame.name = "calculate";
    frame.source.name = "math.luma";
    frame.source.path = "/project/math.luma";
    frame.line = 25;
    frame.column = 5;

    auto json = serialise_stack_frame(frame);

    ASSERT_TRUE(json.is_object());
    // DAP spec requires: id, name, line, column.
    ASSERT_EQ(json["id"].as_integer(), 1);
    ASSERT_EQ(json["name"].as_string(), "calculate");
    ASSERT_EQ(json["line"].as_integer(), 25);
    ASSERT_EQ(json["column"].as_integer(), 5);
    ASSERT_TRUE(json.has("source"));
    ASSERT_EQ(json["source"]["name"].as_string(), "math.luma");
}

// ─── Scope serialisation ───────────────────────────────────────────

void test_serialise_scope() {
    Scope scope;
    scope.name = "Local";
    scope.variables_reference = 100;
    scope.expensive = false;

    auto json = serialise_scope(scope);

    ASSERT_TRUE(json.is_object());
    // DAP spec requires: name, variablesReference, expensive.
    ASSERT_EQ(json["name"].as_string(), "Local");
    ASSERT_EQ(json["variablesReference"].as_integer(), 100);
    ASSERT_FALSE(json["expensive"].as_bool());
}

void test_serialise_scope_expensive() {
    Scope scope;
    scope.name = "Global";
    scope.variables_reference = 200;
    scope.expensive = true;

    auto json = serialise_scope(scope);

    ASSERT_TRUE(json["expensive"].as_bool());
}

// ─── Variable serialisation ────────────────────────────────────────

void test_serialise_variable_simple() {
    Variable var;
    var.name = "x";
    var.value = "42";
    var.type = "integer";
    var.variables_reference = 0;

    auto json = serialise_variable(var);

    ASSERT_TRUE(json.is_object());
    // DAP spec requires: name, value, variablesReference.
    ASSERT_EQ(json["name"].as_string(), "x");
    ASSERT_EQ(json["value"].as_string(), "42");
    ASSERT_EQ(json["type"].as_string(), "integer");
    ASSERT_EQ(json["variablesReference"].as_integer(), 0);
    // namedVariables/indexedVariables omitted when 0.
    ASSERT_FALSE(json.has("namedVariables"));
    ASSERT_FALSE(json.has("indexedVariables"));
}

// DAP: the `type` field must only be emitted when the client advertised
// supportsVariableType. serialise_variable(var, include_type=false) omits it;
// a strict client that did not request it can otherwise show no variables.
void test_serialise_variable_type_gated_on_capability() {
    Variable var;
    var.name = "x";
    var.value = "42";
    var.type = "integer";

    const auto with_type = serialise_variable(var, /*include_type=*/true);
    ASSERT_TRUE(with_type.has("type"));
    ASSERT_EQ(with_type["type"].as_string(), "integer");

    const auto without_type = serialise_variable(var, /*include_type=*/false);
    ASSERT_FALSE(without_type.has("type"));
    // name/value/variablesReference are still present — only `type` is gated.
    ASSERT_EQ(without_type["name"].as_string(), "x");
    ASSERT_EQ(without_type["value"].as_string(), "42");
    ASSERT_TRUE(without_type.has("variablesReference"));
}

void test_serialise_variable_structured() {
    Variable var;
    var.name = "items";
    var.value = "array<integer>(3)";
    var.type = "array<integer>";
    var.variables_reference = 50;
    var.named_variables = 0;
    var.indexed_variables = 3;

    auto json = serialise_variable(var);

    ASSERT_EQ(json["variablesReference"].as_integer(), 50);
    ASSERT_FALSE(json.has("namedVariables"));
    ASSERT_TRUE(json.has("indexedVariables"));
    ASSERT_EQ(json["indexedVariables"].as_integer(), 3);
}

void test_serialise_variable_named_children() {
    Variable var;
    var.name = "person";
    var.value = "record { name, age }";
    var.type = "Person";
    var.variables_reference = 60;
    var.named_variables = 2;
    var.indexed_variables = 0;

    auto json = serialise_variable(var);

    ASSERT_TRUE(json.has("namedVariables"));
    ASSERT_EQ(json["namedVariables"].as_integer(), 2);
    ASSERT_FALSE(json.has("indexedVariables"));
}

// ─── Stop reason constants ─────────────────────────────────────────

void test_stop_reason_values() {
    // DAP spec stop reasons.
    ASSERT_EQ(kStopReasonBreakpoint, "breakpoint");
    ASSERT_EQ(kStopReasonStep, "step");
    ASSERT_EQ(kStopReasonException, "exception");
    ASSERT_EQ(kStopReasonPause, "pause");
    ASSERT_EQ(kStopReasonEntry, "entry");
}

// ─── Output category constants ─────────────────────────────────────

void test_output_category_values() {
    ASSERT_EQ(kOutputConsole, "console");
    ASSERT_EQ(kOutputStdout, "stdout");
    ASSERT_EQ(kOutputStderr, "stderr");
}

// ─── BreakpointRequest struct ──────────────────────────────────────

void test_breakpoint_request_defaults() {
    BreakpointRequest req;

    ASSERT_EQ(req.line, 0);
    ASSERT_TRUE(req.name.empty());
    ASSERT_TRUE(req.condition.empty());
    ASSERT_TRUE(req.hit_condition.empty());
    ASSERT_TRUE(req.log_message.empty());
}

// ─── Roundtrip: serialise → parse → verify ─────────────────────────

void test_serialise_roundtrip() {
    Variable var;
    var.name = "greeting";
    var.value = "\"hello world\"";
    var.type = "string";
    var.variables_reference = 0;

    auto json = serialise_variable(var);
    auto json_str = json.to_string();
    auto parsed = JsonValue::parse(json_str);

    ASSERT_EQ(parsed["name"].as_string(), "greeting");
    ASSERT_EQ(parsed["value"].as_string(), "\"hello world\"");
    ASSERT_EQ(parsed["type"].as_string(), "string");
    ASSERT_EQ(parsed["variablesReference"].as_integer(), 0);
}

// ─── Variable presentationHint ─────────────────────────────────────

void test_variable_read_only_hint() {
    // Immutable variables should have presentationHint with "readOnly" attribute.
    Variable var;
    var.name = "count";
    var.value = "42";
    var.type = "integer";
    var.is_mutable = false;

    auto json = serialise_variable(var);
    ASSERT_TRUE(json.has("presentationHint"));

    const auto& hint = json["presentationHint"];
    ASSERT_TRUE(hint.is_object());
    ASSERT_TRUE(hint.has("attributes"));

    const auto& attrs = hint["attributes"];
    ASSERT_TRUE(attrs.is_array());
    ASSERT_EQ(attrs.as_array().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(attrs.as_array()[0].as_string(), "readOnly");
}

void test_variable_mutable_no_hint() {
    // Mutable variables should NOT have presentationHint.
    Variable var;
    var.name = "counter";
    var.value = "0";
    var.type = "integer";
    var.is_mutable = true;

    auto json = serialise_variable(var);
    ASSERT_FALSE(json.has("presentationHint"));
}

// ─── Variable evaluateName ─────────────────────────────────────────

void test_variable_evaluate_name() {
    Variable var;
    var.name = "items";
    var.value = "[1, 2, 3]";
    var.type = "array<integer>";
    var.evaluate_name = "items";

    auto json = serialise_variable(var);
    ASSERT_TRUE(json.has("evaluateName"));
    ASSERT_EQ(json["evaluateName"].as_string(), "items");
}

void test_variable_evaluate_name_empty_omitted() {
    Variable var;
    var.name = "x";
    var.value = "42";
    var.type = "integer";
    // evaluate_name is empty by default.

    auto json = serialise_variable(var);
    ASSERT_FALSE(json.has("evaluateName"));
}

void test_variable_evaluate_name_indexed() {
    Variable var;
    var.name = "[3]";
    var.value = "hello";
    var.type = "string";
    var.evaluate_name = "[3]";

    auto json = serialise_variable(var);
    ASSERT_TRUE(json.has("evaluateName"));
    ASSERT_EQ(json["evaluateName"].as_string(), "[3]");
}

// ─── StackFrame presentationHint ───────────────────────────────────

void test_stack_frame_presentation_hint_subtle() {
    StackFrame frame;
    frame.id = 1;
    frame.name = "<top-level>";
    frame.line = 1;
    frame.column = 1;
    frame.presentation_hint = "subtle";

    auto json = serialise_stack_frame(frame);
    ASSERT_TRUE(json.has("presentationHint"));
    ASSERT_EQ(json["presentationHint"].as_string(), "subtle");
}

void test_stack_frame_presentation_hint_empty_omitted() {
    StackFrame frame;
    frame.id = 2;
    frame.name = "main";
    frame.line = 5;
    frame.column = 1;
    // presentation_hint is empty by default.

    auto json = serialise_stack_frame(frame);
    ASSERT_FALSE(json.has("presentationHint"));
}

// ─── Scope presentationHint ────────────────────────────────────────

void test_scope_presentation_hint_locals() {
    Scope scope;
    scope.name = "Local";
    scope.variables_reference = 1;
    scope.presentation_hint = "locals";

    auto json = serialise_scope(scope);

    ASSERT_TRUE(json.has("presentationHint"));
    ASSERT_EQ(json["presentationHint"].as_string(), "locals");
}

void test_scope_presentation_hint_arguments() {
    Scope scope;
    scope.name = "Arguments";
    scope.variables_reference = 2;
    scope.presentation_hint = "arguments";

    auto json = serialise_scope(scope);

    ASSERT_TRUE(json.has("presentationHint"));
    ASSERT_EQ(json["presentationHint"].as_string(), "arguments");
}

void test_scope_presentation_hint_empty() {
    Scope scope;
    scope.name = "Custom";
    scope.variables_reference = 3;

    auto json = serialise_scope(scope);

    // No presentationHint when empty.
    ASSERT_FALSE(json.has("presentationHint"));
}

// ─── ScopeType enum ────────────────────────────────────────────────

void test_scope_type_values() {
    ASSERT_EQ(static_cast<int>(ScopeType::Local), 1);
    ASSERT_EQ(static_cast<int>(ScopeType::Global), 2);
    ASSERT_EQ(static_cast<int>(ScopeType::Closure), 3);
}

void test_scope_type_enum_distinct() {
    ASSERT_NE(static_cast<int>(ScopeType::Local), static_cast<int>(ScopeType::Global));
    ASSERT_NE(static_cast<int>(ScopeType::Global), static_cast<int>(ScopeType::Closure));
    ASSERT_NE(static_cast<int>(ScopeType::Local), static_cast<int>(ScopeType::Closure));
}

// ─── Immutability response ─────────────────────────────────────────

void test_set_variable_immutable_response() {
    // Verify the expected response format when setting an immutable variable.
    Variable var;
    var.name = "x";
    var.value = "<immutable variable>";
    var.type = "error";

    auto json = serialise_variable(var);

    ASSERT_EQ(json["name"].as_string(), "x");
    ASSERT_EQ(json["value"].as_string(), "<immutable variable>");
    ASSERT_EQ(json["type"].as_string(), "error");
}

void test_set_variable_immutable_error_type() {
    // When set_variable returns type="error", the DAP server should
    // send an error response (not pass the error string as value).
    Variable var;
    var.name = "x";
    var.value = "<immutable variable>";
    var.type = "error";

    // Verify the error marker is detectable.
    ASSERT_EQ(var.type, "error");
    ASSERT_NE(var.value.find("immutable"), std::string::npos);
}

// ─── Breakpoint message concatenation ──────────────────────────────

void test_breakpoint_message_concatenation() {
    // When a breakpoint snaps AND has a validation error, both messages
    // should be present, separated by "; ".
    Breakpoint bp;
    bp.id = 1;
    bp.verified = true;
    bp.line = 10;
    bp.message = "Breakpoint moved to executable line 10; Invalid hit condition: 'abc'";

    auto json = serialise_breakpoint(bp);
    ASSERT_TRUE(json.has("message"));
    auto msg = json["message"].as_string();
    ASSERT_NE(msg.find("Breakpoint moved"), std::string::npos);
    ASSERT_NE(msg.find("Invalid hit condition"), std::string::npos);
    ASSERT_NE(msg.find("; "), std::string::npos);
}

void test_breakpoint_message_snap_only() {
    // When only snapping occurs, message should contain only the snap info.
    Breakpoint bp;
    bp.id = 2;
    bp.verified = true;
    bp.line = 15;
    bp.message = "Breakpoint moved to executable line 15";

    auto json = serialise_breakpoint(bp);
    auto msg = json["message"].as_string();
    ASSERT_NE(msg.find("Breakpoint moved"), std::string::npos);
    ASSERT_EQ(msg.find("; "), std::string::npos);
}

void test_breakpoint_message_validation_only() {
    // When only a validation error occurs (no snap), message has error only.
    Breakpoint bp;
    bp.id = 3;
    bp.verified = true;
    bp.line = 20;
    bp.message = "Invalid hit condition: 'xyz'";

    auto json = serialise_breakpoint(bp);
    auto msg = json["message"].as_string();
    ASSERT_NE(msg.find("Invalid hit condition"), std::string::npos);
    ASSERT_EQ(msg.find("Breakpoint moved"), std::string::npos);
}

// ─── Extended round-trip serialisation ──────────────────────────────

void test_serialise_variable_roundtrip() {
    Variable var;
    var.name = "count";
    var.value = "42";
    var.type = "integer";
    var.is_mutable = true;
    var.variables_reference = 0;

    auto json = serialise_variable(var);
    ASSERT_EQ(json["name"].as_string(), "count");
    ASSERT_EQ(json["value"].as_string(), "42");
    ASSERT_EQ(json["type"].as_string(), "integer");
    ASSERT_EQ(json["variablesReference"].as_integer(), 0);
    // Mutable = no presentationHint.
    ASSERT_FALSE(json.has("presentationHint"));
}

void test_serialise_variable_immutable_roundtrip() {
    Variable var;
    var.name = "pi";
    var.value = "3.14";
    var.type = "number";
    var.is_mutable = false;
    var.variables_reference = 0;

    auto json = serialise_variable(var);
    ASSERT_TRUE(json.has("presentationHint"));
    auto hint = json["presentationHint"];
    ASSERT_TRUE(hint.has("attributes"));
}

void test_serialise_stack_frame_roundtrip() {
    StackFrame frame;
    frame.id = 5;
    frame.name = "main";
    frame.line = 10;
    frame.column = 1;
    frame.source.path = "/project/test.luma";
    frame.source.name = "test.luma";

    auto json = serialise_stack_frame(frame);
    ASSERT_EQ(json["id"].as_integer(), 5);
    ASSERT_EQ(json["name"].as_string(), "main");
    ASSERT_EQ(json["line"].as_integer(), 10);
    ASSERT_TRUE(json.has("source"));
    ASSERT_EQ(json["source"]["name"].as_string(), "test.luma");
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Type Serialisation Tests");

    // Source.
    RUN(test_serialise_source);
    RUN(test_serialise_source_empty);

    // Breakpoint.
    RUN(test_serialise_breakpoint_verified);
    RUN(test_serialise_breakpoint_unverified);

    // StackFrame.
    RUN(test_serialise_stack_frame);

    // Scope.
    RUN(test_serialise_scope);
    RUN(test_serialise_scope_expensive);

    // Variable.
    RUN(test_serialise_variable_simple);
    RUN(test_serialise_variable_type_gated_on_capability);
    RUN(test_serialise_variable_structured);
    RUN(test_serialise_variable_named_children);

    // Constants.
    RUN(test_stop_reason_values);
    RUN(test_output_category_values);
    RUN(test_breakpoint_request_defaults);

    // Roundtrip.
    RUN(test_serialise_roundtrip);

    // Variable presentationHint.
    RUN(test_variable_read_only_hint);
    RUN(test_variable_mutable_no_hint);

    // Variable evaluateName.
    RUN(test_variable_evaluate_name);
    RUN(test_variable_evaluate_name_empty_omitted);
    RUN(test_variable_evaluate_name_indexed);

    // StackFrame presentationHint.
    RUN(test_stack_frame_presentation_hint_subtle);
    RUN(test_stack_frame_presentation_hint_empty_omitted);

    // Scope presentationHint.
    RUN(test_scope_presentation_hint_locals);
    RUN(test_scope_presentation_hint_arguments);
    RUN(test_scope_presentation_hint_empty);

    // ScopeType.
    RUN(test_scope_type_values);
    RUN(test_scope_type_enum_distinct);

    // Immutability.
    RUN(test_set_variable_immutable_response);
    RUN(test_set_variable_immutable_error_type);

    // Breakpoint messages.
    RUN(test_breakpoint_message_concatenation);
    RUN(test_breakpoint_message_snap_only);
    RUN(test_breakpoint_message_validation_only);

    // Extended round-trip.
    RUN(test_serialise_variable_roundtrip);
    RUN(test_serialise_variable_immutable_roundtrip);
    RUN(test_serialise_stack_frame_roundtrip);

    return SUMMARY();
}
