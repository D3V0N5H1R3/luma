#include "protocol/stdio_transport.hpp"

#include <cstdio>

#include "protocol/message_frame.hpp"
#include "protocol/transport_exceptions.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <io.h>
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace luma::protocol {

namespace {

// Wait until stdin has data available or the timeout elapses.  Returns true
// if data may be available (proceed to read), false if the timeout elapsed.
// Throws ConnectionClosed on a wait/poll failure.  Factors out the
// platform-specific polling shared by the timeout path of read_raw().
[[nodiscard]] bool wait_for_stdin(unsigned int timeout_ms) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);

    if (h == INVALID_HANDLE_VALUE) {
        return true; // Cannot wait on this handle — fall through to a blocking read.
    }

    const DWORD wait_result = WaitForSingleObject(h, static_cast<DWORD>(timeout_ms));

    if (wait_result == WAIT_TIMEOUT) {
        return false;
    }

    if (wait_result != WAIT_OBJECT_0) {
        throw ConnectionClosed("WaitForSingleObject failed on stdin");
    }

    return true;
#else
    struct pollfd pfd{};

    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    const int poll_result = poll(&pfd, 1, static_cast<int>(timeout_ms));

    if (poll_result == 0) {
        return false;
    }

    if (poll_result < 0) {
        throw ConnectionClosed("Read error on stdin (poll)");
    }

    return true;
#endif
}

} // namespace

std::size_t StdioTransport::read_raw(std::span<char> buf) {
    // If a timeout is configured, wait for data availability before reading.
    if (read_timeout_ms_ > 0 && !wait_for_stdin(read_timeout_ms_)) {
        return 0; // Timeout — no data.
    }

#ifdef _WIN32
    const auto count = _read(_fileno(stdin), buf.data(), static_cast<unsigned int>(buf.size()));
#else
    const auto count = ::read(STDIN_FILENO, buf.data(), buf.size());
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
