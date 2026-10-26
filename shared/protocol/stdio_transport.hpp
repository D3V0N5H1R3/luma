#ifndef LUMA_PROTOCOL_STDIO_TRANSPORT_HPP
#define LUMA_PROTOCOL_STDIO_TRANSPORT_HPP

#include <cstddef>
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
// Reads block until data arrives or stdin reaches EOF; both servers rely
// on the editor closing the pipe to signal shutdown.
class StdioTransport : public BufferedTransport {
public:
    using BufferedTransport::BufferedTransport;

    // Write a JSON message with Content-Length framing to stdout.
    // Thread-safe.
    void write_message(const JsonValue& message) override;

protected:
    [[nodiscard]] std::size_t read_raw(std::span<char> buf) override;

private:
    std::mutex write_mutex_;
};

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_STDIO_TRANSPORT_HPP
