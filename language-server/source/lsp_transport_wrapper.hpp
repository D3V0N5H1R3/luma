#ifndef LUMA_LSP_TRANSPORT_WRAPPER_HPP
#define LUMA_LSP_TRANSPORT_WRAPPER_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json/json.hpp"
#include "lsp_constants.hpp"
#include "lsp_types.hpp"
#include "protocol/transport.hpp"

namespace luma::lsp {

using luma::json::JsonValue;
using luma::protocol::Transport;

// ═══════════════════════════════════════════════════════════════════════
// LspTransportWrapper — owns the LSP transport and write mutex.
//
// Encapsulates all JSON-RPC message sending (responses, errors,
// notifications) and message reading.  Thread-safe: the internal
// write_mutex_ serialises all outbound messages.
//
// Extracted from LspServer to separate transport/communication
// concerns from protocol logic and feature handlers.
// ═══════════════════════════════════════════════════════════════════════

class LspTransportWrapper {
public:
    // `initialized` must outlive this object — it gates window/logMessage
    // notifications (must not be sent before the LSP handshake completes).
    explicit LspTransportWrapper(std::unique_ptr<Transport> transport,
                                 const std::atomic<bool>& initialized);

    ~LspTransportWrapper() = default;

    LspTransportWrapper(const LspTransportWrapper&) = delete;
    LspTransportWrapper& operator=(const LspTransportWrapper&) = delete;

    // ─── Reading ───

    // Read the next JSON-RPC message from the transport.
    // Returns std::nullopt on EOF.
    [[nodiscard]] std::optional<JsonValue> read_message();

    // ─── Sending ───

    void send_response(const JsonValue& id, const JsonValue& result);
    void send_error(const JsonValue& id, int code, std::string_view message);
    void send_notification(std::string_view method, const JsonValue& params);

    // ─── Convenience ───

    void log_message(const std::string& text, int type = constants::message_type::info);
    void publish_diagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics,
                             int version = 0);

    // ─── Progress Reporting ───

    void send_progress_begin(const std::string& token, const std::string& title);
    void send_progress_report(const std::string& token, const std::string& message);
    void send_progress_end(const std::string& token);

private:
    void write_jsonrpc_message(const JsonValue& message);

    // Send a $/progress notification wrapping the given value payload
    // ({ "kind": ... } plus optional fields).  Shared by the three
    // send_progress_* convenience methods.
    void send_progress(const std::string& token, JsonValue value);

    std::unique_ptr<Transport> transport_;
    std::mutex write_mutex_;
    const std::atomic<bool>& initialized_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_TRANSPORT_WRAPPER_HPP
