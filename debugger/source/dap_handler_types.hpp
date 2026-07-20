#ifndef LUMA_DAP_HANDLER_TYPES_HPP
#define LUMA_DAP_HANDLER_TYPES_HPP

// ─────────────────────────────────────────────────────────────────────────────
// DAP handler result types shared across all handler groups.
//
// Extracted from dap_server.hpp so that handler classes and the context
// can use these types without depending on the DapServer class itself.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>

#include "json/json.hpp"

namespace luma::dap {

using luma::json::JsonValue;

// ─── Execution result ───
// Replaces std::optional<std::string> with inverted semantics
// (nullopt=success, value=error) for execution control methods.

struct ExecutionResult {
    bool success;
    std::string error_message;

    [[nodiscard]] static ExecutionResult ok() {
        return {true, {}};
    }

    [[nodiscard]] static ExecutionResult error(std::string msg) {
        return {false, std::move(msg)};
    }

    explicit operator bool() const {
        return success;
    }
};

// ─── Post-response actions ───
// Actions the dispatch loop performs after sending a handler's response.
// Only one action can be taken per response; use None for most handlers.
enum class PostResponseAction {
    // No additional action after response is sent.
    None,

    // Emit the "initialized" event after the response is sent.
    // Used exclusively by the initialize handler to signal that the
    // client may now send configuration requests (setBreakpoints, etc.).
    SendInitialized,

    // Mark the session as disconnected after the response is sent.
    // Used by disconnect and terminate handlers to cleanly exit the
    // protocol message loop.
    Disconnect,

    // NOTE: Add new values here when handlers need to perform additional
    // protocol-level actions after sending a response.  Keep enum values
    // orthogonal — each should represent a single, distinct action.
};

// ─── Handler result ───
// Returned by each handler to convey both the response body and any
// post-response actions (e.g., emit an event or disconnect).

struct HandlerResult {
    JsonValue body;
    bool success{true};
    std::string error_message;

    // Action the dispatch loop should perform after sending the response.
    PostResponseAction post_action{PostResponseAction::None};

    // Convenience factory for a successful empty response.
    [[nodiscard]] static HandlerResult ok(JsonValue body = JsonValue(JsonValue::ObjectType{})) {
        return HandlerResult{.body = std::move(body),
                             .success = true,
                             .error_message = {},
                             .post_action = PostResponseAction::None};
    }

    // Convenience factory for an error response.
    [[nodiscard]] static HandlerResult error(std::string message) {
        return HandlerResult{.body = JsonValue(JsonValue::ObjectType{}),
                             .success = false,
                             .error_message = std::move(message),
                             .post_action = PostResponseAction::None};
    }
};

} // namespace luma::dap

#endif // LUMA_DAP_HANDLER_TYPES_HPP
