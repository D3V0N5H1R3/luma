#ifndef LUMA_RUNTIME_STDLIB_SOCKET_IO_HPP
#define LUMA_RUNTIME_STDLIB_SOCKET_IO_HPP

// Internal header — shared TCP socket lifecycle helpers.
//
// Provides the RAII SocketGuard and a looping send_all primitive consumed by
// both the Socket stdlib module (socket_module.cpp) and the HTTP connection
// layer (http_module_connection.hpp / PlainConnection).  Centralising them
// keeps the two modules from independently re-implementing socket close and
// full-buffer send logic.
//
// Note: socket_module deliberately also exposes lower-level single-shot send /
// recv primitives to Luma programs; those raw operations intentionally remain
// in the module rather than being forced through this shared helper.

#include <algorithm>
#include <climits>
#include <cstddef>

#include "runtime/stdlib/io/platform_socket.hpp"

namespace luma {

// RAII guard that closes a socket on scope exit unless released.  Used on the
// error paths of connect / listen / accept and during HTTP connection setup so
// a partially-initialised socket is never leaked.
struct SocketGuard {
    SocketHandle handle;

    explicit SocketGuard(SocketHandle h) : handle(h) {}

    ~SocketGuard() noexcept {
        if (handle != invalid_socket_handle) {
            platform_socket::close(handle);
        }
    }

    void release() {
        handle = invalid_socket_handle;
    }

    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;
};

// Send the entire buffer over a connected socket, looping until every byte is
// written.  Returns false if the peer closes the connection or an error occurs
// mid-write.  Large buffers are chunked to stay within the platform's int-sized
// send length.
[[nodiscard]] inline bool send_all(SocketHandle sock, const char* data, std::size_t size) {
    std::size_t sent{0};

    while (sent < size) {
        const auto chunk_size =
            static_cast<int>(std::min(size - sent, static_cast<std::size_t>(INT_MAX)));
        const auto n = ::send(sock, data + sent,
                              static_cast<platform_socket::io_length_t>(chunk_size), MSG_NOSIGNAL);

        if (n <= 0) {
            return false;
        }

        sent += static_cast<std::size_t>(n);
    }

    return true;
}

// Details of the LAST failed attempt of tcp_connect_any.
struct TcpConnectFailure {
    bool socket_failed{false}; // Creating the socket itself failed.
    bool timed_out{false};     // The connect did not complete within the timeout.
    int error_code{0};         // Platform-native error code (errno / WSAGetLastError()).
};

// Connect to the first reachable address in a getaddrinfo() result list, trying
// each candidate in order (RFC 6724 ordering, as returned by the resolver) until
// one connects.  Trying only the first entry made, e.g., "localhost" fail
// against a server listening on 127.0.0.1 whenever the resolver listed ::1
// first.  Each attempt gets the full `timeout_ms`.  Returns the connected,
// blocking, non-inheritable socket (caller takes ownership), or
// invalid_socket_handle with the last attempt's failure in `failure`.  Shared by
// the Socket module and the HTTP connection layer.
[[nodiscard]] inline SocketHandle tcp_connect_any(const struct addrinfo* addresses, int timeout_ms,
                                                  TcpConnectFailure* failure = nullptr) {
    TcpConnectFailure last{};

    for (const auto* ai = addresses; ai != nullptr; ai = ai->ai_next) {
        const SocketHandle sock =
            platform_socket::create_socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);

        if (sock == invalid_socket_handle) {
            last = TcpConnectFailure{.socket_failed = true,
                                     .timed_out = false,
                                     .error_code = platform_socket::last_error_code()};

            continue;
        }

        SocketGuard guard{sock};

        bool timed_out{false};
        int error_code{0};

        if (tcp_connect_with_timeout(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen),
                                     timeout_ms, &timed_out, &error_code)) {
            guard.release();

            return sock;
        }

        last = TcpConnectFailure{
            .socket_failed = false, .timed_out = timed_out, .error_code = error_code};
    }

    if (failure != nullptr) {
        *failure = last;
    }

    return invalid_socket_handle;
}

} // namespace luma

#endif // LUMA_RUNTIME_STDLIB_SOCKET_IO_HPP
