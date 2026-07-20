#include "dap_tcp_transport.hpp"

#include <array>
#include <climits>
#include <format>
#include <iostream>
#include <random>
#include <string>

#include "protocol/message_frame.hpp"
#include "protocol/transport_exceptions.hpp"

namespace luma::dap {

TcpTransport::TcpTransport(std::uint16_t port) : port_{port}, auth_token_{generate_token()} {
    listen_socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (listen_socket_ == protocol::invalid_socket) {
        throw protocol::ConnectionClosed("Failed to create socket");
    }

    // Guard ensures cleanup on any subsequent failure.
    SocketGuard guard{listen_socket_};

    // Allow port reuse.
    int opt = 1;
    ::setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, protocol::sockopt_ptr(&opt),
                 sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Only accept local connections.
    addr.sin_port = htons(port_);

    if (::bind(listen_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw protocol::ConnectionClosed(std::format("Failed to bind to port {}", port_));
    }

    if (::listen(listen_socket_, 1) != 0) {
        throw protocol::ConnectionClosed("Failed to listen");
    }

    // If port was 0, retrieve the assigned port.
    if (port_ == 0) {
        sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);

        if (::getsockname(listen_socket_, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) {
            port_ = ntohs(bound.sin_port);
        }
    }

    guard.dismiss(); // Ownership transferred to the class members.

    std::cerr << "DAP server listening on port " << port_ << "\n";
    std::cerr << "DAP-AUTH-TOKEN: " << auth_token_ << "\n";
}

TcpTransport::~TcpTransport() noexcept {
    close();
}

bool TcpTransport::accept_client() {
    client_socket_ = ::accept(listen_socket_, nullptr, nullptr);

    if (client_socket_ == protocol::invalid_socket) {
        return false;
    }

    // Authenticate: the client must send the token as a single line.
    for (int attempt = 0; attempt < k_max_auth_attempts; ++attempt) {
        auto line = read_auth_line();

        if (!line.has_value()) {
            std::cerr << "DAP auth: connection closed before authentication\n";
            close();
            return false;
        }

        // Strip trailing \r if present.
        auto token = *line;
        if (!token.empty() && token.back() == '\r') {
            token.pop_back();
        }

        if (token == auth_token_) {
            return true;
        }

        std::cerr << "DAP auth: invalid token (attempt " << (attempt + 1) << "/"
                  << k_max_auth_attempts << ")\n";
    }

    std::cerr << "DAP auth: maximum attempts exceeded, closing connection\n";
    close();
    return false;
}

std::optional<std::string> TcpTransport::read_auth_line() {
    std::string line;

    while (true) {
        char c{};
        const int n = ::recv(client_socket_, &c, 1, 0);

        if (n <= 0) {
            return std::nullopt;
        }

        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            return line;
        }

        line += c;

        if (line.size() >= max_header_length()) {
            return std::nullopt;
        }
    }
}

std::string TcpTransport::generate_token() {
    static constexpr std::size_t k_token_bytes = 16; // 128 bits
    std::random_device rd;
    std::array<std::uint8_t, k_token_bytes> bytes{};

    for (auto& b : bytes) {
        b = static_cast<std::uint8_t>(rd());
    }

    static constexpr const char* hex_chars = "0123456789abcdef";
    std::string token;
    token.reserve(k_token_bytes * 2);

    for (const auto b : bytes) {
        token += hex_chars[(b >> 4) & 0x0F];
        token += hex_chars[b & 0x0F];
    }

    return token;
}

void TcpTransport::close() {
    if (client_socket_ != protocol::invalid_socket) {
        protocol::close_socket(client_socket_);
        client_socket_ = protocol::invalid_socket;
    }

    if (listen_socket_ != protocol::invalid_socket) {
        protocol::close_socket(listen_socket_);
        listen_socket_ = protocol::invalid_socket;
    }
}

std::size_t TcpTransport::read_raw(std::span<char> buf) {
    const auto chunk = (std::min)(buf.size(), static_cast<std::size_t>(INT_MAX));
    const int n = ::recv(client_socket_, buf.data(), static_cast<int>(chunk), 0);

    if (n < 0) {
        throw protocol::ConnectionClosed("Read error on socket");
    }

    return static_cast<std::size_t>(n); // 0 = connection closed (EOF)
}

void TcpTransport::write_message(const JsonValue& message) {
    const std::scoped_lock lock(write_mutex_);

    auto framed = protocol::write_framed_message(message.to_string());

    auto send_all = [this](const std::string& data) {
        std::size_t sent = 0;

        while (sent < data.size()) {
            const int n =
                ::send(client_socket_, data.data() + sent, static_cast<int>(data.size() - sent), 0);

            if (n <= 0) {
                throw protocol::ConnectionClosed("Send failed: connection closed");
            }

            sent += static_cast<std::size_t>(n);
        }
    };

    send_all(framed);
}

} // namespace luma::dap
