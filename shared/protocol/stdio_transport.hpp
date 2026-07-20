#ifndef LUMA_PROTOCOL_STDIO_TRANSPORT_HPP
#define LUMA_PROTOCOL_STDIO_TRANSPORT_HPP

#include <mutex>
#include <span>

#include "json/json.hpp"
#include "protocol/buffered_transport.hpp"

namespace luma::protocol {

using luma::json::JsonValue;

// Concrete transport for Content-Length framed JSON over stdin/stdout.
//
// Shared by both the language server (LSP) and debug adapter (DAP).
// Writes are mutex-protected so the transport is safe to use from
// multiple threads (the DAP server sends events from background
// threads; the LSP server is single-threaded but pays negligible
// cost for the uncontended lock).
//
// An optional read timeout allows callers to poll for input without
// blocking indefinitely (used by the DAP server for graceful
// shutdown).
class StdioTransport : public BufferedTransport {
public:
    using BufferedTransport::BufferedTransport;

    // Set the read timeout in milliseconds.  0 = no timeout (default).
    void set_read_timeout(unsigned int timeout_ms) {
        read_timeout_ms_ = timeout_ms;
    }

    // Write a JSON message with Content-Length framing to stdout.
    // Thread-safe.
    void write_message(const JsonValue& message) override;

protected:
    [[nodiscard]] std::size_t read_raw(std::span<char> buf) override;

private:
    std::mutex write_mutex_;
    unsigned int read_timeout_ms_{0};
};

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_STDIO_TRANSPORT_HPP
