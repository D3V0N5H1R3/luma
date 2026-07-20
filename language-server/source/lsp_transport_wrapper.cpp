#include "lsp_transport_wrapper.hpp"

#include <format>
#include <iostream>

#include "lsp_constants.hpp"

namespace luma::lsp {

LspTransportWrapper::LspTransportWrapper(std::unique_ptr<Transport> transport,
                                         const std::atomic<bool>& initialized)
    : transport_(std::move(transport)), initialized_(initialized) {}

std::optional<JsonValue> LspTransportWrapper::read_message() {
    return transport_->read_message();
}

// ═══════════════════════════════════════════════════════════
// Sending
// ═══════════════════════════════════════════════════════════

void LspTransportWrapper::write_jsonrpc_message(const JsonValue& message) {
    const std::scoped_lock lock(write_mutex_);
    transport_->write_message(message);
}

void LspTransportWrapper::send_response(const JsonValue& id, const JsonValue& result) {
    write_jsonrpc_message(json::JsonBuilder()
                              .set("jsonrpc", JsonValue("2.0"))
                              .set("id", id)
                              .set("result", result)
                              .build());
}

void LspTransportWrapper::send_error(const JsonValue& id, int code, std::string_view message) {
    write_jsonrpc_message(
        json::JsonBuilder()
            .set("jsonrpc", JsonValue("2.0"))
            .set("id", id)
            .set("error",
                 json::JsonBuilder().set("code", code).set("message", std::string{message}).build())
            .build());
}

void LspTransportWrapper::send_notification(std::string_view method, const JsonValue& params) {
    write_jsonrpc_message(json::JsonBuilder()
                              .set("jsonrpc", JsonValue("2.0"))
                              .set("method", std::string{method})
                              .set("params", params)
                              .build());
}

// ═══════════════════════════════════════════════════════════
// Convenience
// ═══════════════════════════════════════════════════════════

void LspTransportWrapper::log_message(const std::string& text, int type) {
    std::cerr << std::format("[luma-lsp] {}\n", text);

    if (initialized_.load(std::memory_order_acquire)) {
        send_notification(constants::method::log_message,
                          json::JsonBuilder()
                              .set("type", JsonValue(static_cast<int64_t>(type)))
                              .set("message", JsonValue(text))
                              .build());
    }
}

void LspTransportWrapper::publish_diagnostics(const std::string& uri,
                                              const std::vector<Diagnostic>& diagnostics,
                                              int version) {
    JsonValue::ArrayType diag_array;
    for (const auto& d : diagnostics) {
        diag_array.push_back(serialise_diagnostic(d));
    }

    JsonValue::ObjectType obj{
        {"uri", JsonValue(uri)},
        {"diagnostics", JsonValue(std::move(diag_array))},
    };

    if (version > 0) {
        obj.emplace("version", JsonValue(static_cast<int64_t>(version)));
    }

    send_notification(constants::method::publish_diagnostics, JsonValue(std::move(obj)));
}

// ═══════════════════════════════════════════════════════════
// Progress reporting
// ═══════════════════════════════════════════════════════════

void LspTransportWrapper::send_progress(const std::string& token, JsonValue value) {
    send_notification(constants::method::progress, JsonValue(JsonValue::ObjectType{
                                                       {"token", JsonValue(token)},
                                                       {"value", std::move(value)},
                                                   }));
}

void LspTransportWrapper::send_progress_begin(const std::string& token, const std::string& title) {
    send_progress(token, JsonValue(JsonValue::ObjectType{
                             {"kind", JsonValue("begin")},
                             {"title", JsonValue(title)},
                             {"cancellable", JsonValue(false)},
                         }));
}

void LspTransportWrapper::send_progress_report(const std::string& token,
                                               const std::string& message) {
    send_progress(token, JsonValue(JsonValue::ObjectType{
                             {"kind", JsonValue("report")},
                             {"message", JsonValue(message)},
                         }));
}

void LspTransportWrapper::send_progress_end(const std::string& token) {
    send_progress(token, JsonValue(JsonValue::ObjectType{
                             {"kind", JsonValue("end")},
                         }));
}

} // namespace luma::lsp
