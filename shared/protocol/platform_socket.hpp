#ifndef LUMA_PROTOCOL_PLATFORM_SOCKET_HPP
#define LUMA_PROTOCOL_PLATFORM_SOCKET_HPP

// Lean platform socket primitives for the LSP/DAP transport layer.
//
// Abstracts the Windows (Winsock2) vs POSIX socket differences that the
// protocol transports need: the native handle type, the invalid-handle
// sentinel, closing a socket, the setsockopt/getsockopt pointer cast, and
// Winsock initialisation/teardown (a no-op on POSIX).
//
// This is intentionally smaller than stdlib/io/platform_socket.hpp and carries
// no dependency on the interpreter's value types, so protocol-layer code can
// consume it without pulling in the runtime.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "protocol/transport_exceptions.hpp"

namespace luma::protocol {

// Native socket handle: an unsigned SOCKET on Winsock, a file descriptor on POSIX.
#ifdef _WIN32
using socket_handle = SOCKET;
inline constexpr socket_handle invalid_socket = INVALID_SOCKET;
#else
using socket_handle = int;
inline constexpr socket_handle invalid_socket = -1;
#endif

// Close a socket using the platform-appropriate call.
inline void close_socket(socket_handle h) {
#ifdef _WIN32
    ::closesocket(h);
#else
    ::close(h);
#endif
}

// Cast a pointer for use with setsockopt / getsockopt.
// Windows expects char*, POSIX expects void*; reinterpret_cast<char*> is
// correct on both platforms.
template <typename T> inline const char* sockopt_ptr(const T* p) {
    return reinterpret_cast<const char*>(p);
}

template <typename T> inline char* sockopt_ptr(T* p) {
    return reinterpret_cast<char*>(p);
}

// RAII guard for Winsock initialisation.  Calls WSAStartup on construction and
// WSACleanup on destruction; a no-op on POSIX.  Throws ConnectionClosed if
// WSAStartup fails.  Declare one before any socket members so that Winsock is
// initialised first and torn down last.
class WinsockGuard {
public:
    WinsockGuard() {
#ifdef _WIN32
        WSADATA wsa_data;

        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            throw ConnectionClosed("WSAStartup failed");
        }
#endif
    }

    ~WinsockGuard() {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    WinsockGuard(const WinsockGuard&) = delete;
    WinsockGuard& operator=(const WinsockGuard&) = delete;
    WinsockGuard(WinsockGuard&&) = delete;
    WinsockGuard& operator=(WinsockGuard&&) = delete;
};

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_PLATFORM_SOCKET_HPP
