// DAP TCP transport tests — authentication token generation and the
// challenge/response handshake in accept_client().
//
// Each test constructs a real loopback TcpTransport bound to an OS-assigned
// ephemeral port (port 0) and drives it from a client thread using raw sockets.
// The transport only accepts connections from 127.0.0.1, so these tests never
// touch the network.  A CTest TIMEOUT guards against a hang if accept_client()
// never returns.

#include <cstdint>
#include <string>
#include <thread>

#include "dap_tcp_transport.hpp"
#include "protocol/platform_socket.hpp"
#include "test_framework.hpp"

using namespace luma::dap;
namespace protocol = luma::protocol;

namespace {

// Connect to 127.0.0.1:port, returning an open socket or invalid_socket.
[[nodiscard]] protocol::socket_handle connect_loopback(std::uint16_t port) {
    const auto sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == protocol::invalid_socket) {
        return protocol::invalid_socket;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        protocol::close_socket(sock);
        return protocol::invalid_socket;
    }

    return sock;
}

// Send an entire buffer, returning false on any short/failed write.
bool send_all(protocol::socket_handle sock, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int n = ::send(sock, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

// ─── Token generation ─────────────────────────────────────────────

void test_token_is_128bit_lowercase_hex() {
    const TcpTransport transport{0};
    const auto& token = transport.auth_token();

    // 16 random bytes rendered as hex → 32 characters.
    ASSERT_EQ(token.size(), static_cast<std::size_t>(32));
    for (const char c : token) {
        const bool is_lower_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        ASSERT_TRUE(is_lower_hex);
    }
}

void test_tokens_differ_between_instances() {
    const TcpTransport a{0};
    const TcpTransport b{0};
    // 128 bits of randomness — a collision is astronomically unlikely.
    ASSERT_NE(a.auth_token(), b.auth_token());
}

void test_ephemeral_port_is_assigned() {
    const TcpTransport transport{0};
    // Port 0 asks the OS for an ephemeral port; the bound port must be non-zero.
    ASSERT_TRUE(transport.port() != 0);
}

// ─── Authentication handshake ─────────────────────────────────────

void test_accept_client_authenticates_with_correct_token() {
    const protocol::WinsockGuard guard;
    TcpTransport transport{0};
    const auto port = transport.port();
    const auto token = transport.auth_token();

    std::thread client([port, token]() {
        const auto sock = connect_loopback(port);
        if (sock == protocol::invalid_socket) {
            return;
        }
        (void)send_all(sock, token + "\n");
        protocol::close_socket(sock);
    });

    const bool authenticated = transport.accept_client();
    client.join();

    ASSERT_TRUE(authenticated);
}

void test_accept_client_rejects_wrong_token() {
    const protocol::WinsockGuard guard;
    TcpTransport transport{0};
    const auto port = transport.port();

    std::thread client([port]() {
        const auto sock = connect_loopback(port);
        if (sock == protocol::invalid_socket) {
            return;
        }
        // Exhaust all k_max_auth_attempts with an invalid token.
        for (int i = 0; i < TcpTransport::k_max_auth_attempts; ++i) {
            (void)send_all(sock, "not-the-token\n");
        }
        protocol::close_socket(sock);
    });

    const bool authenticated = transport.accept_client();
    client.join();

    ASSERT_FALSE(authenticated);
}

void test_accept_client_rejects_early_disconnect() {
    const protocol::WinsockGuard guard;
    TcpTransport transport{0};
    const auto port = transport.port();

    std::thread client([port]() {
        const auto sock = connect_loopback(port);
        if (sock == protocol::invalid_socket) {
            return;
        }
        // Close before presenting any token.
        protocol::close_socket(sock);
    });

    const bool authenticated = transport.accept_client();
    client.join();

    ASSERT_FALSE(authenticated);
}

void test_accept_client_authenticates_with_crlf_terminator() {
    // Clients that terminate the token line with CRLF must still authenticate:
    // accept_client() strips a trailing '\r'.
    const protocol::WinsockGuard guard;
    TcpTransport transport{0};
    const auto port = transport.port();
    const auto token = transport.auth_token();

    std::thread client([port, token]() {
        const auto sock = connect_loopback(port);
        if (sock == protocol::invalid_socket) {
            return;
        }
        (void)send_all(sock, token + "\r\n");
        protocol::close_socket(sock);
    });

    const bool authenticated = transport.accept_client();
    client.join();

    ASSERT_TRUE(authenticated);
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP TCP Transport Tests");

    // Token generation.
    RUN(test_token_is_128bit_lowercase_hex);
    RUN(test_tokens_differ_between_instances);
    RUN(test_ephemeral_port_is_assigned);

    // Authentication handshake.
    RUN(test_accept_client_authenticates_with_correct_token);
    RUN(test_accept_client_rejects_wrong_token);
    RUN(test_accept_client_rejects_early_disconnect);
    RUN(test_accept_client_authenticates_with_crlf_terminator);

    return SUMMARY();
}
