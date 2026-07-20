#ifndef LUMA_STDLIB_PLATFORM_SOCKET_HPP
#define LUMA_STDLIB_PLATFORM_SOCKET_HPP

// Platform-agnostic socket primitives.
//
// Abstracts the differences between Windows (Winsock2) and POSIX socket
// APIs into a uniform inline interface.  Intended for use by stdlib
// modules that perform low-level socket operations.
//
// The SocketHandle type and invalid_socket_handle constant live in
// runtime/interpreter/value.hpp and are re-exported here for convenience.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// MSG_NOSIGNAL is not available on Windows or macOS.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include <array>
#include <cstddef>
#include <cstring>
#include <string>

#include "runtime/interpreter/value.hpp"

namespace luma::platform_socket {

// send()/recv() take a size_t byte count on POSIX but an int on Winsock, and
// bind()/connect() take a socklen_t on POSIX but an int on Winsock.  These
// aliases let call sites cast lengths to the type the platform expects without
// provoking sign-conversion warnings on either toolchain.
#ifdef _WIN32
using io_length_t = int;
using addr_length_t = int;
#else
using io_length_t = std::size_t;
using addr_length_t = socklen_t;
#endif

// ─── Close ───

inline void close(SocketHandle h) {
#ifdef _WIN32
    closesocket(h);
#else
    ::close(h);
#endif
}

// ─── Error reporting ───

// Return the last socket error as a human-readable string.
inline std::string last_error() {
#ifdef _WIN32
    const int err = WSAGetLastError();

    std::array<char, 256> buf{};

    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   static_cast<DWORD>(err), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf.data(),
                   static_cast<DWORD>(buf.size()), nullptr);

    // Trim trailing newline from FormatMessage.
    std::string msg{buf.data()};

    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.pop_back();
    }

    return msg;
#else
    return std::string{strerror(errno)};
#endif
}

// Check whether the last socket error indicates that a non-blocking
// connect is still in progress.
[[nodiscard]] inline bool is_connect_in_progress() {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EINPROGRESS;
#endif
}

// Retrieve the SO_ERROR value from a socket.
// Returns the error code (0 means no error), or -1 if getsockopt fails.
[[nodiscard]] inline int get_pending_error(SocketHandle h) {
    int err{0};

#ifdef _WIN32
    int len{static_cast<int>(sizeof(err))};

    if (getsockopt(h, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0) {
        return -1;
    }
#else
    socklen_t len{sizeof(err)};

    if (getsockopt(h, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
        return -1;
    }
#endif

    return err;
}

// ─── Timeout ───

// Set send and receive timeouts on a socket.
inline bool set_timeout(SocketHandle h, int ms) {
#ifdef _WIN32
    const DWORD timeout{static_cast<DWORD>(ms)};

    return setsockopt(h, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                      sizeof(timeout)) == 0 &&
           setsockopt(h, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
                      sizeof(timeout)) == 0;
#else
    struct timeval tv {};

    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    return setsockopt(h, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
           setsockopt(h, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

// ─── Non-blocking mode ───

// Switch a socket to non-blocking mode.
// On POSIX, returns the original fcntl flags for later restoration.
// On Windows, returns 0 on success.
// Returns -1 on error.
inline int set_non_blocking(SocketHandle h) {
#ifdef _WIN32
    u_long mode{1};

    if (ioctlsocket(h, FIONBIO, &mode) != 0) {
        return -1;
    }

    return 0;
#else
    const int flags = fcntl(h, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }

    if (fcntl(h, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }

    return flags;
#endif
}

// Restore a socket to blocking mode.
// saved_flags is the value previously returned by set_non_blocking()
// (used on POSIX only; ignored on Windows).
inline void set_blocking(SocketHandle h, [[maybe_unused]] int saved_flags) {
#ifdef _WIN32
    u_long mode{0};

    ioctlsocket(h, FIONBIO, &mode);
#else
    fcntl(h, F_SETFL, saved_flags);
#endif
}

// ─── select() helpers ───

// First argument for select().
// Windows ignores nfds; POSIX requires the highest fd + 1.
inline int select_nfds(SocketHandle h) {
#ifdef _WIN32
    (void)h;

    return 0;
#else
    return static_cast<int>(h) + 1;
#endif
}

// ─── setsockopt / getsockopt pointer cast ───

// Cast a pointer for use with setsockopt / getsockopt.
// Windows expects char*, POSIX expects void*.  Using
// reinterpret_cast<const char*> is correct on both platforms.
template <typename T> inline const char* sockopt_ptr(const T* p) {
    return reinterpret_cast<const char*>(p);
}

template <typename T> inline char* sockopt_ptr(T* p) {
    return reinterpret_cast<char*>(p);
}

} // namespace luma::platform_socket

namespace luma {

// ─── Shared TCP connect-with-timeout ───
//
// Used by both http_module.cpp and socket_module.cpp.
// Attempts a non-blocking connect and waits up to timeout_ms for completion.
// Returns true on success, false on timeout or error.
// The socket is left in blocking mode on return.

[[nodiscard]] inline bool tcp_connect_with_timeout(SocketHandle sock, const struct sockaddr* addr,
                                                   int addrlen, int timeout_ms) {
    const int saved_flags = platform_socket::set_non_blocking(sock);

    if (saved_flags < 0) {
        return false;
    }

    const int rc = ::connect(sock, addr, static_cast<platform_socket::addr_length_t>(addrlen));

    if (rc == 0) {
        platform_socket::set_blocking(sock, saved_flags);
        return true;
    }

    if (!platform_socket::is_connect_in_progress()) {
        platform_socket::set_blocking(sock, saved_flags);
        return false;
    }

    fd_set wr{};
    FD_ZERO(&wr);
    FD_SET(sock, &wr);

    fd_set ex{};
    FD_ZERO(&ex);
    FD_SET(sock, &ex);

    struct timeval tv {};

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    const int sel = select(platform_socket::select_nfds(sock), nullptr, &wr, &ex, &tv);

    platform_socket::set_blocking(sock, saved_flags);

    if (sel <= 0) {
        return false;
    }

    const int err = platform_socket::get_pending_error(sock);

    return err == 0;
}

} // namespace luma

#endif // LUMA_STDLIB_PLATFORM_SOCKET_HPP
