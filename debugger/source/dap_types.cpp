#include "dap_types.hpp"

namespace luma::dap {

using luma::json::JsonBuilder;

JsonValue serialise_source(const Source& src) {
    return JsonBuilder().set("name", src.name).set("path", src.path).build();
}

JsonValue serialise_breakpoint(const Breakpoint& bp) {
    return JsonBuilder()
        .set("id", bp.id)
        .set("verified", bp.verified)
        .set("source", serialise_source(bp.source))
        .set("line", bp.line)
        .set_if(!bp.message.empty(), "message", bp.message)
        .set_if(!bp.reason.empty(), "reason", bp.reason)
        .build();
}

JsonValue serialise_stack_frame(const StackFrame& frame) {
    return JsonBuilder()
        .set("id", frame.id)
        .set("name", frame.name)
        .set("source", serialise_source(frame.source))
        .set("line", frame.line)
        .set("column", frame.column)
        .set_if(!frame.presentation_hint.empty(), "presentationHint", frame.presentation_hint)
        .build();
}

JsonValue serialise_scope(const Scope& scope) {
    return JsonBuilder()
        .set("name", scope.name)
        .set("variablesReference", scope.variables_reference)
        .set("expensive", scope.expensive)
        .set_if(!scope.presentation_hint.empty(), "presentationHint", scope.presentation_hint)
        .build();
}

// Luma polarity: is_mutable = true means the variable is writable.
// DAP convention: omitting presentationHint defaults to writable.
// We only emit presentationHint { attributes: ["readOnly"] } when !is_mutable,
// so read-only variables are explicitly marked and writable ones use the default.
JsonValue serialise_variable(const Variable& var, bool include_type) {
    JsonValue::ObjectType obj;
    obj["name"] = JsonValue(var.name);
    obj["value"] = JsonValue(var.value);

    // DAP: `type` must only be sent when the client advertised
    // supportsVariableType. A client that did not (some editors default it off)
    // may reject a variable carrying an unexpected `type`, leaving its Variables
    // view empty — so gate it on the negotiated capability.
    if (include_type) {
        obj["type"] = JsonValue(var.type);
    }

    obj["variablesReference"] = JsonValue(var.variables_reference);

    if (var.named_variables > 0) {
        obj["namedVariables"] = JsonValue(var.named_variables);
    }

    if (var.indexed_variables > 0) {
        obj["indexedVariables"] = JsonValue(var.indexed_variables);
    }

    if (!var.evaluate_name.empty()) {
        obj["evaluateName"] = JsonValue(var.evaluate_name);
    }

    if (!var.is_mutable) {
        JsonValue::ObjectType hint;
        JsonValue::ArrayType attrs;
        attrs.emplace_back(std::string("readOnly"));
        hint["attributes"] = JsonValue(std::move(attrs));
        obj["presentationHint"] = JsonValue(std::move(hint));
    }

    return JsonValue(std::move(obj));
}

} // namespace luma::dap
