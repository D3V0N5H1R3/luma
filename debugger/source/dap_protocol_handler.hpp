#ifndef LUMA_DAP_PROTOCOL_HANDLER_HPP
#define LUMA_DAP_PROTOCOL_HANDLER_HPP

#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>

#include "dap_error_handler.hpp"
#include "json/json.hpp"
#include "protocol/error_recovery.hpp"
#include "protocol/handler_registry.hpp"
#include "protocol/transport.hpp"
#include "protocol/transport_exceptions.hpp"

namespace luma::dap {

using luma::json::JsonValue;

// ═══════════════════════════════════════════════════════════
// DapProtocolHandler — DAP message framing and dispatch layer
//
// Separates the protocol concerns from debug execution logic:
//   - Reading Content-Length framed DAP messages from transport
//   - Parsing JSON and extracting request/response/event structure
//   - Dispatching requests to registered handler callbacks
//   - Constructing and sending well-formed DAP responses/events
//   - Managing protocol-level sequence numbers
//   - Centralised error handling for dispatch failures
//
// This class owns the protocol layer only.  Debug session state,
// breakpoint management, and execution control live elsewhere.
// A higher-level component (e.g. DapServer) registers handlers
// and delegates protocol I/O to this class.
//
// Thread safety:
//   - send_response() and send_event() are serialised by an
//     internal mutex and may be called from any thread.
//   - The message loop (run()) must be called from a single thread.
//   - Handler registration (register_handler) is not thread-safe
//     and must complete before run() is called.
// ═══════════════════════════════════════════════════════════

class DapProtocolHandler {
public:
    // Handler result returned by each request handler.
    struct HandlerResult {
        JsonValue body;
        bool success{true};
        std::string error_message;

        // Optional callback executed after the response is sent.
        // Use for actions that must occur after the client receives the
        // response (e.g. emitting DAP events, setting disconnect flags).
        std::function<void()> post_response_action;

        // Convenience factory for a successful empty response.
        [[nodiscard]] static HandlerResult ok(JsonValue body = JsonValue(JsonValue::ObjectType{})) {
            return HandlerResult{.body = std::move(body),
                                 .success = true,
                                 .error_message = {},
                                 .post_response_action = {}};
        }

        // Convenience factory for an error response.
        [[nodiscard]] static HandlerResult error(std::string message) {
            return HandlerResult{.body = JsonValue(JsonValue::ObjectType{}),
                                 .success = false,
                                 .error_message = std::move(message),
                                 .post_response_action = {}};
        }
    };

    // Handler callable type: receives the request arguments, returns a result.
    using RequestHandler = std::function<HandlerResult(const JsonValue&)>;

    // Callback invoked when the handler signals that a disconnect should occur.
    using DisconnectCallback = std::function<void()>;

    explicit DapProtocolHandler(protocol::Transport& transport) : transport_(transport) {}

    // ─── Handler registration ───

    // Register a handler for a DAP request command.
    // Must be called before run().  Throws on duplicate registration.
    void register_handler(const std::string& command, RequestHandler handler) {
        dispatch_table_.register_handler(command, std::move(handler));
    }

    // ─── Message loop ───

    // Run the message loop, reading and dispatching DAP messages until
    // disconnect or EOF.  Returns 0 on clean shutdown.
    [[nodiscard]] int run() {
        protocol::ErrorRecoveryState recovery;

        while (!disconnected_.load(std::memory_order_acquire)) {
            std::optional<JsonValue> message;

            try {
                message = transport_.read_message();
            } catch (const protocol::ConnectionClosed& e) {
                std::cerr << "DAP: connection closed: " << e.what() << '\n';
                break;
            } catch (const protocol::ParseError& e) {
                std::cerr << "DAP: skipping malformed message: " << e.what() << '\n';
                auto action = recovery.on_error(protocol::ErrorSeverity::transient);
                if (action == protocol::RecoveryAction::shutdown) {
                    std::cerr << "DAP: shutting down after " << recovery.consecutive_errors()
                              << " consecutive read errors\n";
                    break;
                }
                continue;
            } catch (const protocol::TransportError& e) {
                std::cerr << "DAP: transport error: " << e.what() << '\n';
                break;
            } catch (const std::exception& e) {
                std::cerr << "DAP: unexpected error reading message: " << e.what() << '\n';
                auto action = recovery.on_error(protocol::classify_read_error(e));
                if (action == protocol::RecoveryAction::shutdown) {
                    std::cerr << "DAP: shutting down after " << recovery.consecutive_errors()
                              << " consecutive read errors\n";
                    break;
                }
                continue;
            }

            if (!message.has_value()) {
                break; // EOF — editor closed the pipe.
            }

            recovery.on_success();

            if (!message->is_object()) {
                continue;
            }

            const auto type_str = message->get_or<std::string>("type", "");

            if (type_str == "request") {
                dispatch_request(*message);
            }
            // Ignore other message types (responses, events from client).
        }

        return 0;
    }

    // ─── Sending messages ───

    // Send a DAP response message.  Thread-safe.
    void send_response(int request_seq, const std::string& command, const JsonValue& body,
                       bool success = true, const std::string& message = "") {
        bool write_failed = false;

        {
            const std::lock_guard<std::mutex> lock(send_mutex_);

            JsonValue::ObjectType response;
            response["seq"] = JsonValue(sequence_number_++);
            response["type"] = JsonValue(std::string("response"));
            response["request_seq"] = JsonValue(request_seq);
            response["success"] = JsonValue(success);
            response["command"] = JsonValue(command);
            response["body"] = body;

            if (!success && !message.empty()) {
                response["message"] = JsonValue(message);
            }

            try {
                transport_.write_message(JsonValue(std::move(response)));
            } catch (const std::exception&) {
                // Broken pipe — editor closed the connection.
                write_failed = true;
            }
        }

        // Invoke the disconnect callback *after* releasing send_mutex_.  The
        // callback terminates the session and joins the execution thread, which
        // may itself be blocked on send_mutex_ inside send_event(); signalling
        // while holding the lock would deadlock.
        if (write_failed) {
            signal_disconnect();
        }
    }

    // Send a DAP event message.  Thread-safe.
    void send_event(const std::string& event, const JsonValue& body) {
        const std::lock_guard<std::mutex> lock(send_mutex_);

        JsonValue::ObjectType msg;
        msg["seq"] = JsonValue(sequence_number_++);
        msg["type"] = JsonValue(std::string("event"));
        msg["event"] = JsonValue(event);
        msg["body"] = body;

        try {
            transport_.write_message(JsonValue(std::move(msg)));
        } catch (const std::exception&) {
            // Broken pipe — editor closed the connection.
            // Only set the flag; do NOT terminate session here because
            // send_event may be called from the execution thread.
            disconnected_.store(true, std::memory_order_release);
        }
    }

    // ─── Protocol state ───

    // Signal that the protocol should disconnect after the current message.
    void signal_disconnect() {
        disconnected_.store(true, std::memory_order_release);

        if (disconnect_callback_) {
            disconnect_callback_();
        }
    }

    // Check whether the protocol layer is in a disconnected state.
    [[nodiscard]] bool is_disconnected() const {
        return disconnected_.load(std::memory_order_acquire);
    }

    // Set a callback invoked when a transport write failure causes
    // an automatic disconnect.  This allows the owning server to
    // clean up session state (e.g. terminate a running program).
    void set_disconnect_callback(DisconnectCallback callback) {
        disconnect_callback_ = std::move(callback);
    }

private:
    // Dispatch a single DAP request message to the appropriate handler.
    void dispatch_request(const JsonValue& message) {
        const auto command = message.get_or<std::string>("command", "");
        const auto seq = message.get_or<int>("seq", 0);

        if (command.empty() || seq == 0) {
            return; // Malformed request — skip.
        }

        const auto arguments = message.has("arguments") ? message["arguments"] : JsonValue();

        const auto* handler = dispatch_table_.find(command);

        if (!handler) {
            send_response(seq, command, JsonValue(JsonValue::ObjectType{}), false,
                          "Unsupported command");
            return;
        }

        try {
            auto result = (*handler)(arguments);

            send_response(seq, command, result.body, result.success, result.error_message);

            if (result.post_response_action) {
                result.post_response_action();
            }
        } catch (...) {
            // Single dispatch-level error boundary.  Handler exceptions,
            // send_response failures, and post-response actions are all
            // classified here through the shared classifier so the
            // exception-to-message mapping lives in exactly one place
            // (see classify_exception in dap_error_handler.hpp).
            const auto classified = classify_exception(command, std::current_exception());
            send_response(seq, command, JsonValue(JsonValue::ObjectType{}), false,
                          classified.message);
        }
    }

    // ─── Transport and protocol state ───
    protocol::Transport& transport_;
    int sequence_number_{1}; // GUARDED_BY(send_mutex_)

    std::atomic<bool> disconnected_{false};

    // Serialises send_response and send_event to guarantee monotonic seq.
    std::mutex send_mutex_; // GUARDED_BY: sequence_number_

    // Dispatch table: command name → handler.
    protocol::HandlerRegistry<RequestHandler> dispatch_table_;

    // Optional callback for transport-triggered disconnects.
    DisconnectCallback disconnect_callback_;
};

} // namespace luma::dap

#endif // LUMA_DAP_PROTOCOL_HANDLER_HPP
