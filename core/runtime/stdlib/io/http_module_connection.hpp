#ifndef LUMA_RUNTIME_STDLIB_HTTP_MODULE_CONNECTION_HPP
#define LUMA_RUNTIME_STDLIB_HTTP_MODULE_CONNECTION_HPP

// Internal header — HTTP connection I/O abstraction.
// Extracted from http_module.cpp so that request and response code can
// share the Connection interface without circular dependencies.

#include <climits>
#include <cstddef>
#include <memory>
#include <string>

#include "runtime/stdlib/io/platform_socket.hpp"
#include "runtime/stdlib/io/socket_io.hpp"

#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif

namespace luma {

// ─── I/O abstraction ─────────────────────────────────────────────────────────

// Interface for sending/receiving data over a connection (plain TCP or TLS).
struct Connection {
    virtual ~Connection() = default;
    [[nodiscard]] virtual bool send_data(const std::string& data) = 0;
    [[nodiscard]] virtual int recv_data(char* buf, std::size_t len) = 0;

    Connection() = default;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;
};

// ─── RAII socket guard ───────────────────────────────────────────────────────
//
// SocketGuard now lives in the shared runtime/stdlib/io/socket_io.hpp header so the
// Socket module and the HTTP connection layer share one implementation.

// ─── Plain TCP connection ────────────────────────────────────────────────────

struct PlainConnection final : Connection {
    SocketHandle sock;

    explicit PlainConnection(SocketHandle s) : sock(s) {}

    ~PlainConnection() override {
        if (sock != invalid_socket_handle) {
            platform_socket::close(sock);
        }
    }

    [[nodiscard]] bool send_data(const std::string& data) override {
        return send_all(sock, data.data(), data.size());
    }

    [[nodiscard]] int recv_data(char* buf, std::size_t len) override {
        return static_cast<int>(
            ::recv(sock, buf, static_cast<platform_socket::io_length_t>(len), 0));
    }
};

// ─── TLS connection ─────────────────────────────────────────────────────────

#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS

// Load the platform's trusted root CA certificates into an Mbed TLS chain.
// Returns true if at least one certificate was loaded successfully.
[[nodiscard]] bool load_system_ca_certs(mbedtls_x509_crt* chain);

// RAII wrapper for an mbedtls TLS session.
//
// All five mbedtls contexts (ssl, conf, ctr_drbg, entropy, ca_chain) are
// value-initialised in the constructor and freed in the destructor, so
// resources are released even if handshake() throws or an early return
// occurs.
struct TlsConnection final : Connection {
    mbedtls_ssl_context ssl{};
    mbedtls_ssl_config conf{};
    mbedtls_ctr_drbg_context ctr_drbg{};
    mbedtls_entropy_context entropy{};
    mbedtls_x509_crt ca_chain{};
    SocketHandle sock{invalid_socket_handle};
    bool connected{false};

    TlsConnection();
    ~TlsConnection() override;

    // Perform TLS handshake over an already-connected socket.
    // Takes ownership of the socket. Returns empty string on success,
    // or an error message on failure.
    [[nodiscard]] std::string handshake(SocketHandle connected_sock, const std::string& host);

    [[nodiscard]] bool send_data(const std::string& data) override;
    [[nodiscard]] int recv_data(char* buf, std::size_t len) override;

private:
    [[nodiscard]] static std::string tls_error(const std::string& prefix, int code);
};

#endif // LUMA_HAS_TLS

} // namespace luma

#endif // LUMA_RUNTIME_STDLIB_HTTP_MODULE_CONNECTION_HPP
