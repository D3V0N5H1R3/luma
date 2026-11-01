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
    // Read framed input from the process's standard input.
    StdioTransport() = default;

    // Read framed input from `input_fd` instead of standard input.  The debug
    // adapter uses this to keep the protocol stream on a private duplicate of
    // stdin while the debuggee's own stdin is pointed elsewhere, so a program
    // that reads input cannot consume protocol bytes.
    explicit StdioTransport(int input_fd) : input_fd_{input_fd} {}

    // Write a JSON message with Content-Length framing to stdout.
    // Thread-safe.
    void write_message(const JsonValue& message) override;

protected:
    [[nodiscard]] std::size_t read_raw(std::span<char> buf) override;

private:
    // File descriptor to read from; k_use_stdin selects the process's stdin.
    static constexpr int k_use_stdin{-1};

    int input_fd_{k_use_stdin};
    std::mutex write_mutex_;
};

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_STDIO_TRANSPORT_HPP
