#ifndef LUMA_LSP_HANDLER_REGISTRY_HPP
#define LUMA_LSP_HANDLER_REGISTRY_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

#include "json/json.hpp"
#include "protocol/handler_registry.hpp"

namespace luma::lsp {

using luma::json::JsonValue;

// Owns the LSP method→handler dispatch tables.
//
// Separates handler registration and lookup from the server's
// message-loop and lifecycle logic.  The server populates the
// registry during initialisation, then queries it on every
// incoming request or notification.
//
// Delegates to the shared HandlerRegistry template for the
// actual map operations.  The LSP-specific concern is having
// two separate maps: one for requests (returns JsonValue) and
// one for notifications (returns void).
//
// Thread safety: the registry is populated once during startup
// (single-threaded) and thereafter accessed read-only from the
// message loop, so no internal locking is needed.
class LspHandlerRegistry {
public:
    using RequestHandler = std::function<JsonValue(const JsonValue&)>;
    using NotificationHandler = std::function<void(const JsonValue&)>;

    // Register a handler for a request method (expects a response).
    void register_request(std::string method, RequestHandler handler) {
        request_handlers_.register_handler(std::move(method), std::move(handler));
    }

    // Register a handler for a notification method (fire-and-forget).
    void register_notification(std::string method, NotificationHandler handler) {
        notification_handlers_.register_handler(std::move(method), std::move(handler));
    }

    // Look up a request handler by method name.
    // Returns nullptr if no handler is registered for the method.
    [[nodiscard]] const RequestHandler* find_request(const std::string& method) const {
        return request_handlers_.find(method);
    }

    // Look up a notification handler by method name.
    // Returns nullptr if no handler is registered for the method.
    [[nodiscard]] const NotificationHandler* find_notification(const std::string& method) const {
        return notification_handlers_.find(method);
    }

    // Number of registered request handlers.
    [[nodiscard]] std::size_t request_count() const {
        return request_handlers_.size();
    }

    // Number of registered notification handlers.
    [[nodiscard]] std::size_t notification_count() const {
        return notification_handlers_.size();
    }

private:
    protocol::HandlerRegistry<RequestHandler> request_handlers_;
    protocol::HandlerRegistry<NotificationHandler> notification_handlers_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_HANDLER_REGISTRY_HPP
