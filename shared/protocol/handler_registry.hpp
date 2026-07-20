#ifndef LUMA_PROTOCOL_HANDLER_REGISTRY_HPP
#define LUMA_PROTOCOL_HANDLER_REGISTRY_HPP

#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "common/string_hash.hpp"

namespace luma::protocol {

// ═══════════════════════════════════════════════════════════
// HandlerRegistry — generic method→handler dispatch map
//
// A thin, type-safe wrapper around an unordered map from
// method name (string) to handler callable.  Used by both
// the LSP and DAP servers to store their dispatch tables.
//
// Template parameter:
//   HandlerFn — the handler callable type, e.g.
//     std::function<JsonValue(const JsonValue&)>   (LSP requests)
//     std::function<void(const JsonValue&)>         (LSP notifications)
//     std::function<HandlerResult(const JsonValue&)>(DAP requests)
//
// Thread safety: the registry is populated once during
// startup (single-threaded) and thereafter accessed read-only
// from the message loop, so no internal locking is needed.
// ═══════════════════════════════════════════════════════════

template <typename HandlerFn>
    requires std::move_constructible<HandlerFn>
class HandlerRegistry {
public:
    // Register a handler for a method name.
    // Throws if a handler is already registered for the given method.
    void register_handler(std::string method, HandlerFn handler) {
        auto [_, inserted] = handlers_.emplace(std::move(method), std::move(handler));
        if (!inserted) {
            throw std::logic_error("duplicate handler registration");
        }
    }

    // Look up a handler by method name.
    // Returns nullptr if no handler is registered for the method.
    [[nodiscard]] const HandlerFn* find(std::string_view method) const {
        auto it = handlers_.find(method);
        return it != handlers_.end() ? &it->second : nullptr;
    }

    // Number of registered handlers.
    [[nodiscard]] std::size_t size() const {
        return handlers_.size();
    }

private:
    luma::StringMap<HandlerFn> handlers_;
};

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_HANDLER_REGISTRY_HPP
