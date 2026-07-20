// Http module — TLS session management (Mbed TLS).
// Split from http_module_request.cpp for readability.

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include "runtime/stdlib/io/http_module_connection.hpp"
#include "runtime/stdlib/io/platform_socket.hpp"
#include "runtime/stdlib/io/winsock_init.hpp"

#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#ifdef _WIN32
#include <wincrypt.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#endif
#endif

namespace luma {

#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS

bool load_system_ca_certs(mbedtls_x509_crt* chain) {
    // Cache the raw DER blobs so we can replay them into each connection's chain.
    struct CaCertCache {
        std::vector<std::vector<unsigned char>> der_certs;
        bool loaded{false};
        bool attempted{false};
    };

    static CaCertCache cache = [] {
        CaCertCache c;

#ifdef _WIN32
        HCERTSTORE store = CertOpenSystemStoreA(0, "ROOT");

        if (store != nullptr) {
            PCCERT_CONTEXT cert{nullptr};

            while ((cert = CertEnumCertificatesInStore(store, cert)) != nullptr) {
                c.der_certs.emplace_back(cert->pbCertEncoded,
                                         cert->pbCertEncoded + cert->cbCertEncoded);
            }

            CertCloseStore(store, 0);
        }

#elif defined(__APPLE__)
        CFArrayRef certs{nullptr};
        const OSStatus status = SecTrustCopyAnchorCertificates(&certs);

        if (status == errSecSuccess && certs != nullptr) {
            const CFIndex count = CFArrayGetCount(certs);

            for (CFIndex i = 0; i < count; ++i) {
                const auto ref = static_cast<SecCertificateRef>(
                    const_cast<void*>(CFArrayGetValueAtIndex(certs, i)));
                CFDataRef der_data = SecCertificateCopyData(ref);

                if (der_data != nullptr) {
                    const auto* ptr = CFDataGetBytePtr(der_data);
                    const auto len = static_cast<size_t>(CFDataGetLength(der_data));

                    c.der_certs.emplace_back(ptr, ptr + len);
                    CFRelease(der_data);
                }
            }

            CFRelease(certs);
        }

#else
        // Linux/Unix: try well-known CA bundle paths.
        static constexpr std::array<const char*, 5> ca_paths = {
            "/etc/ssl/certs/ca-certificates.crt",
            "/etc/pki/tls/certs/ca-bundle.crt",
            "/etc/ssl/ca-bundle.pem",
            "/etc/ssl/cert.pem",
            "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
        };

        mbedtls_x509_crt tmp{};
        mbedtls_x509_crt_init(&tmp);

        for (const auto* path : ca_paths) {
            if (mbedtls_x509_crt_parse_file(&tmp, path) >= 0) {
                break;
            }
        }

        // Walk the parsed chain and stash each cert's raw DER.
        for (const mbedtls_x509_crt* cur = &tmp; cur != nullptr; cur = cur->next) {
            if (cur->raw.len > 0) {
                c.der_certs.emplace_back(cur->raw.p, cur->raw.p + cur->raw.len);
            }
        }

        mbedtls_x509_crt_free(&tmp);
#endif

        c.loaded = !c.der_certs.empty();
        c.attempted = true;

        return c;
    }();

    if (!cache.loaded) {
        return false;
    }

    int loaded{0};

    for (const auto& der : cache.der_certs) {
        if (mbedtls_x509_crt_parse_der(chain, der.data(), der.size()) == 0) {
            ++loaded;
        }
    }

    return loaded > 0;
}

// BIO callbacks that wrap a raw SocketHandle for Mbed TLS.
static int tls_bio_send(void* ctx, const unsigned char* buf, size_t len) {
    const auto sock = *static_cast<SocketHandle*>(ctx);
    const auto n =
        ::send(sock, reinterpret_cast<const char*>(buf), static_cast<int>(len), MSG_NOSIGNAL);

    if (n < 0) {
        return -1;
    }

    return static_cast<int>(n);
}

static int tls_bio_recv(void* ctx, unsigned char* buf, size_t len) {
    const auto sock = *static_cast<SocketHandle*>(ctx);
    const auto n = ::recv(sock, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);

    if (n < 0) {
        return -1;
    }

    // n == 0 means the peer closed the connection; return 0 so Mbed TLS
    // sees EOF rather than retrying (MBEDTLS_ERR_SSL_WANT_READ).
    return static_cast<int>(n);
}

TlsConnection::TlsConnection() {
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);
    mbedtls_x509_crt_init(&ca_chain);
}

// Frees all mbedtls resources in reverse init order. Safe to call even
// if handshake() was never called or failed partway through.
TlsConnection::~TlsConnection() {
    if (connected) {
        mbedtls_ssl_close_notify(&ssl);
    }

    if (sock != invalid_socket_handle) {
        platform_socket::close(sock);
    }

    mbedtls_x509_crt_free(&ca_chain);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
}

std::string TlsConnection::handshake(SocketHandle connected_sock, const std::string& host) {
    sock = connected_sock;

    // Seed the RNG.
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, nullptr, 0);

    if (ret != 0) {
        return tls_error("TLS: RNG seed failed", ret);
    }

    // Configure as TLS client.
    ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);

    if (ret != 0) {
        return tls_error("TLS: config defaults failed", ret);
    }

    // Load the platform's trusted root certificates.  Require full
    // certificate verification; if no CA store can be located, fail
    // immediately rather than silently downgrading to optional
    // verification, which would allow MITM attacks.
    const bool have_cas = load_system_ca_certs(&ca_chain);

    if (have_cas) {
        mbedtls_ssl_conf_ca_chain(&conf, &ca_chain, nullptr);
    } else {
        return tls_error("TLS: no system CA certificates available — cannot verify server "
                         "identity",
                         MBEDTLS_ERR_X509_CERT_UNKNOWN_FORMAT);
    }

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);

    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    ret = mbedtls_ssl_setup(&ssl, &conf);

    if (ret != 0) {
        return tls_error("TLS: setup failed", ret);
    }

    // Set hostname for SNI (Server Name Indication).
    ret = mbedtls_ssl_set_hostname(&ssl, host.c_str());

    if (ret != 0) {
        return tls_error("TLS: set hostname failed", ret);
    }

    // Wire up I/O using BIO callbacks over the pre-connected socket.
    mbedtls_ssl_set_bio(&ssl, &this->sock, tls_bio_send, tls_bio_recv, nullptr);

    // Perform TLS handshake.
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return tls_error("TLS: handshake failed", ret);
        }
    }

    connected = true;

    return {};
}

bool TlsConnection::send_data(const std::string& data) {
    std::size_t sent{0};

    while (sent < data.size()) {
        const auto remaining = data.size() - sent;
        const int ret = mbedtls_ssl_write(
            &ssl, reinterpret_cast<const unsigned char*>(data.data() + sent), remaining);

        if (ret < 0) {
            if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }

            return false;
        }

        sent += static_cast<std::size_t>(ret);
    }

    return true;
}

int TlsConnection::recv_data(char* buf, std::size_t len) {
    const int ret = mbedtls_ssl_read(&ssl, reinterpret_cast<unsigned char*>(buf), len);

    if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
        return 0;
    }

    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) {
        return 0;
    }

    return ret; // negative = error, positive = bytes read
}

std::string TlsConnection::tls_error(const std::string& prefix, int code) {
    std::array<char, 128> buf{};

    mbedtls_strerror(code, buf.data(), buf.size());

    return std::format("{}: {}", prefix, buf.data());
}

#endif // LUMA_HAS_TLS

} // namespace luma
