#ifndef LUMA_PROTOCOL_TRANSPORT_HPP
#define LUMA_PROTOCOL_TRANSPORT_HPP

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "json/json.hpp"
#include "protocol/constants.hpp"

namespace luma::protocol {

using luma::json::JsonValue;

// Callback type for transport error reporting.  Receives the error
// message string.  The default callback writes to stderr.
using ErrorCallback = std::function<void(std::string_view)>;

// Configurable limits for message framing.  Each field defaults to the
// corresponding protocol-layer constant; override individual limits with
// designated initialisers, e.g. TransportLimits{.max_message_bytes = 1024}.
struct TransportLimits {
    std::size_t max_message_bytes = k_default_max_message_bytes;
    std::size_t max_header_length = k_default_max_header_length;
    std::size_t max_resync_iterations = k_default_max_resync_iterations;
};

// Abstract transport interface for reading/writing Content-Length framed
// JSON messages.  Both the language server (LSP) and the debug adapter
// (DAP) share this framing protocol, so the parsing and serialisation
// logic lives here once.
class Transport {
public:
    explicit Transport(TransportLimits limits = {}, ErrorCallback error_callback = nullptr);
    virtual ~Transport() = default;

    // Replace the error callback.  Pass nullptr to restore the default
    // (stderr) handler.
    void set_error_callback(ErrorCallback callback);

    // Read one JSON message.  Returns nullopt on EOF.
    // The default implementation uses read_line() / read_exact()
    // to handle Content-Length framing.  Mock transports may override.
    [[nodiscard]] virtual std::optional<JsonValue> read_message();

    // Write a JSON message with Content-Length framing.
    virtual void write_message(const JsonValue& message) = 0;

protected:
    // Subclasses implement these two I/O primitives.

    // Read a single header line (terminated by \r\n).
    // Returns nullopt on EOF.
    [[nodiscard]] virtual std::optional<std::string> read_line() = 0;

    // Read exactly `count` bytes from the input stream.
    [[nodiscard]] virtual std::string read_exact(std::size_t count) = 0;

    // Parse all header lines until the blank separator line.
    // Returns the Content-Length value, or std::nullopt on EOF.
    // Throws on malformed or missing Content-Length.
    [[nodiscard]] std::optional<std::size_t> parse_headers();

    // After a parse error, scan forward discarding input until the next
    // Content-Length: header is found and buffer it for the next read_message()
    // call so the stream is left in a recoverable state.
    // Throws ParseError if the iteration cap is reached without finding a
    // valid header.
    void resync_to_next_message();

    // Report a transport error via the configured callback.
    void report_error(std::string_view message);

    // Maximum header line length (bytes).  Subclasses may read this
    // for validation during non-buffered I/O (e.g. authentication).
    [[nodiscard]] std::size_t max_header_length() const noexcept {
        return limits_.max_header_length;
    }

private:
    // Configurable limits for message framing.
    TransportLimits limits_;

    // Stores a header line found during resync so it is re-fed on the next
    // read_message() call.
    std::optional<std::string> buffered_header_line_;

    // Error reporting callback.  When null, the default stderr handler is used.
    ErrorCallback error_callback_;
};

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_TRANSPORT_HPP
