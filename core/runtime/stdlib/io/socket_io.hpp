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

} // namespace luma

#endif // LUMA_RUNTIME_STDLIB_SOCKET_IO_HPP
