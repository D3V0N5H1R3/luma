#ifndef LUMA_DAP_TCP_TRANSPORT_HPP
#define LUMA_DAP_TCP_TRANSPORT_HPP

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "json/json.hpp"
#include "protocol/buffered_transport.hpp"
#include "protocol/platform_socket.hpp"

namespace luma::dap {

using luma::json::JsonValue;

// RAII guard that closes a socket on destruction unless dismissed.
// Used to clean up a partially constructed transport if setup fails.
class SocketGuard {
public:
    explicit SocketGuard(protocol::socket_handle& sock) noexcept : sock_{sock} {}

    ~SocketGuard() noexcept {
        release();
    }

    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;

    // Relinquish ownership — the socket will not be closed on destruction.
    void dismiss() noexcept {
        dismissed_ = true;
    }

private:
    void release() noexcept {
        if (dismissed_) {
            return;
        }

        if (sock_ != protocol::invalid_socket) {
            protocol::close_socket(sock_);
            sock_ = protocol::invalid_socket;
        }
    }

    protocol::socket_handle& sock_;
    bool dismissed_{false};
};

// TCP-based transport for the Debug Adapter Protocol.
// Listens on a port and accepts a single client connection for remote
// debugging (e.g., from a remote editor or CI test harness).
class TcpTransport : public protocol::BufferedTransport {
public:
    // Maximum number of authentication attempts before closing the connection.
    static constexpr int k_max_auth_attempts = 3;

    explicit TcpTransport(std::uint16_t port);
    ~TcpTransport() noexcept override;

    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;

    // Wait for a client to connect and authenticate.
    // The client must send the auth token (printed to stderr at
    // startup) as a single line before DAP messages begin.
    // Returns false on error or authentication failure.
    [[nodiscard]] bool accept_client();

    // The authentication token that the client must present.
    [[nodiscard]] const std::string& auth_token() const {
        return auth_token_;
    }

    // The port the listening socket is bound to.  When constructed with port 0
    // the OS assigns an ephemeral port during bind(); this returns the assigned
    // value so callers (and tests) can direct clients to the right port.
    [[nodiscard]] std::uint16_t port() const noexcept {
        return port_;
    }

    // Write a DAP message to the connected client.
    void write_message(const JsonValue& message) override;

    // Close the connection and listening socket.
    void close();

protected:
    [[nodiscard]] std::size_t read_raw(std::span<char> buf) override;

private:
    // Read a single newline-terminated line from the client socket.
    // Used during authentication handshake (before buffered I/O begins).
    [[nodiscard]] std::optional<std::string> read_auth_line();

    // Generate a cryptographically random hex token.
    [[nodiscard]] static std::string generate_token();

    // Winsock lifecycle guard.  Declared first so WSAStartup runs before any
    // socket is created and WSACleanup runs after all sockets are closed.
    // No-op on POSIX.
    protocol::WinsockGuard winsock_guard_;
    std::uint16_t port_;
    protocol::socket_handle listen_socket_{protocol::invalid_socket};
    protocol::socket_handle client_socket_{protocol::invalid_socket};
    // Leaf-level lock — never held while acquiring any other mutex.
    std::mutex write_mutex_;
    std::string auth_token_;
};

} // namespace luma::dap

#endif // LUMA_DAP_TCP_TRANSPORT_HPP
