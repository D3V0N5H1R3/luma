#ifndef LUMA_PROTOCOL_BUFFERED_TRANSPORT_HPP
#define LUMA_PROTOCOL_BUFFERED_TRANSPORT_HPP

#include <cstddef>
#include <optional>
#include <span>
#include <string>

#include "protocol/constants.hpp"
#include "protocol/transport.hpp"

namespace luma::protocol {

// Transport base with an internal read buffer.
//
// Subclasses implement read_raw() — the single I/O primitive that fills
// a caller-provided buffer from whatever source (stdin, socket, pipe).
// Header-line parsing and exact-byte reading are handled here once,
// eliminating duplicate framing logic in each concrete transport.
class BufferedTransport : public Transport {
public:
    using Transport::Transport;

protected:
    [[nodiscard]] std::optional<std::string> read_line() override;
    [[nodiscard]] std::string read_exact(std::size_t count) override;

    // Read up to buf.size() bytes into buf.
    // Returns the number of bytes actually read.
    // Returns 0 on EOF or timeout (no data available).
    // Throws on I/O error.
    //
    // Timeout behaviour is defined per subclass.  StdioTransport supports an
    // opt-in read timeout via set_read_timeout() (implemented with poll() on
    // POSIX and WaitForSingleObject() on Windows); when unset it blocks until
    // data arrives or EOF.  There is intentionally no portable base-class
    // timeout — a generic solution would require a watchdog thread or
    // per-transport platform I/O, which is more invasive than warranted.  The
    // DAP and LSP servers rely on the editor closing the pipe to signal
    // shutdown, so the practical risk of an infinite hang is low.
    [[nodiscard]] virtual std::size_t read_raw(std::span<char> buf) = 0;

private:
    // Refill the internal buffer from read_raw().  Returns false on EOF.
    bool refill();

    // Returns true if buffered data is available (refilling if needed).
    [[nodiscard]] bool ensure_buffered();

    // Throws ParseError if the accumulated header line exceeds the limit.
    void enforce_header_length(const std::string& line) const;

    char read_buf_[k_read_buffer_size]{};
    std::size_t read_pos_{0};
    std::size_t read_end_{0};
};

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_BUFFERED_TRANSPORT_HPP
