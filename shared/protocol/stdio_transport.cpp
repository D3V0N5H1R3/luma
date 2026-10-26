#include "protocol/stdio_transport.hpp"

#include <cstdio>

#include "protocol/message_frame.hpp"
#include "protocol/transport_exceptions.hpp"

#ifdef _WIN32
#include <io.h>
#else
#include <cerrno>

#include <unistd.h>
#endif

namespace luma::protocol {

std::size_t StdioTransport::read_raw(std::span<char> buf) {
#ifdef _WIN32
    const auto count = _read(_fileno(stdin), buf.data(), static_cast<unsigned int>(buf.size()));
#else
    auto count = ::read(STDIN_FILENO, buf.data(), buf.size());

    // A signal delivered mid-read is not a broken connection — retry.
    while (count < 0 && errno == EINTR) {
        count = ::read(STDIN_FILENO, buf.data(), buf.size());
    }
#endif

    if (count < 0) {
        throw ConnectionClosed("Read error on stdin");
    }

    return static_cast<std::size_t>(count); // 0 = EOF
}

void StdioTransport::write_message(const JsonValue& message) {
    const std::scoped_lock lock(write_mutex_);

    // Write the framing header and body as two writes rather than concatenating
    // them into one buffer — the body (which can be large: completion lists,
    // semantic tokens, document symbols) is never copied a second time.  Both
    // writes happen under the lock, so the message is still emitted atomically
    // with respect to other writers.
    const std::string body = message.to_string();
    const std::string header = content_length_header(body.size());

    if (std::fwrite(header.data(), 1, header.size(), stdout) != header.size() ||
        std::fwrite(body.data(), 1, body.size(), stdout) != body.size()) {
        throw ConnectionClosed("Failed to write complete message to stdout");
    }

    if (std::fflush(stdout) == EOF) {
        throw ConnectionClosed("Failed to flush message to stdout");
    }
}

} // namespace luma::protocol
