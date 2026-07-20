#ifndef LUMA_RUNTIME_STDLIB_NETWORK_SECURITY_HPP
#define LUMA_RUNTIME_STDLIB_NETWORK_SECURITY_HPP

/// Network security utilities for SSRF (Server-Side Request Forgery) prevention.
/// These functions check whether resolved addresses belong to private, reserved,
/// or loopback ranges that should not be accessible from user-initiated HTTP requests.

#include <algorithm>
#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace luma::network {
// Utilities for blocking connections to private, loopback, link-local, and
// reserved IP ranges.  Used by the HTTP module and any future network stdlib
// code to prevent SSRF (Server-Side Request Forgery) attacks.
//
// Public API:
//   is_private_ipv4(uint32_t)          — checks a host-byte-order IPv4 address
//   is_private_address(addrinfo*)      — checks all addresses in a getaddrinfo result
//   detail::safe_addr_cast<T>(sockaddr*) — type-safe sockaddr downcast helper

// -- IPv4 Private Range Constants -------------------------------------------

/// 127.0.0.0/8 — Loopback.
constexpr std::uint32_t k_ipv4_loopback_prefix = 127;

/// 10.0.0.0/8 — RFC 1918 private range.
constexpr std::uint32_t k_ipv4_private_10_prefix = 10;

/// 0.0.0.0/8 — Current network (only valid as source address).
constexpr std::uint32_t k_ipv4_current_net_prefix = 0;

/// 172.16.0.0/12 — RFC 1918 private range (12-bit prefix, compared after >> 20).
constexpr std::uint32_t k_ipv4_private_172_prefix = 0xAC1;

/// 192.168.0.0/16 — RFC 1918 private range.
constexpr std::uint32_t k_ipv4_private_192_168_prefix = 0xC0A8;

/// 169.254.0.0/16 — Link-local (APIPA).
constexpr std::uint32_t k_ipv4_link_local_prefix = 0xA9FE;

/// 100.64.0.0/10 — RFC 6598 carrier-grade NAT shared address space
/// (10-bit prefix, compared after >> 22).
constexpr std::uint32_t k_ipv4_cgnat_prefix = 0x191;

// -- IPv6 Link-Local Constants -----------------------------------------------

/// First byte of fe80::/10 link-local addresses.
constexpr std::uint8_t k_ipv6_link_local_first_byte = 0xFE;

/// Mask applied to second byte for fe80::/10 detection.
constexpr std::uint8_t k_ipv6_link_local_mask = 0xC0;

/// Expected value of (second byte & mask) for fe80::/10 link-local addresses.
constexpr std::uint8_t k_ipv6_link_local_value = 0x80;

/// Marker byte (0xFF) at positions 10 and 11 of ::ffff:0:0/96 IPv4-mapped IPv6 addresses.
constexpr std::uint8_t k_ipv4_mapped_marker = 0xFF;

/// Mask applied to the first byte for fc00::/7 unique-local-address detection.
constexpr std::uint8_t k_ipv6_ula_mask = 0xFE;

/// Expected value of (first byte & mask) for fc00::/7 unique local addresses
/// (covers both the fc00::/8 and fd00::/8 halves).
constexpr std::uint8_t k_ipv6_ula_value = 0xFC;

/// Returns true if the given IPv4 address (in host byte-order) falls within
/// a private, loopback, link-local, or reserved range.
[[nodiscard]] inline bool is_private_ipv4(std::uint32_t ip) noexcept {
    if ((ip >> 24) == k_ipv4_loopback_prefix) {
        return true; // 127.0.0.0/8  loopback
    }
    if ((ip >> 24) == k_ipv4_private_10_prefix) {
        return true; // 10.0.0.0/8   RFC 1918
    }
    if ((ip >> 20) == k_ipv4_private_172_prefix) {
        return true; // 172.16.0.0/12 RFC 1918
    }
    if ((ip >> 16) == k_ipv4_private_192_168_prefix) {
        return true; // 192.168.0.0/16 RFC 1918
    }
    if ((ip >> 16) == k_ipv4_link_local_prefix) {
        return true; // 169.254.0.0/16 link-local
    }
    if ((ip >> 22) == k_ipv4_cgnat_prefix) {
        return true; // 100.64.0.0/10 carrier-grade NAT (RFC 6598)
    }
    if ((ip >> 24) == k_ipv4_current_net_prefix) {
        return true; // 0.0.0.0/8
    }
    return false;
}

/// Returns true if the resolved address is in a private/reserved/loopback range.
/// Supports AF_INET (IPv4), AF_INET6 (IPv6, including IPv4-mapped addresses).
///
/// Type-safe wrapper for sockaddr casts used internally.
namespace detail {

template <typename T>
[[nodiscard]] inline const T* safe_addr_cast(const struct sockaddr* addr) noexcept {
    return reinterpret_cast<const T*>(addr);
}

} // namespace detail

[[nodiscard]] inline bool is_private_address(const struct addrinfo* info) noexcept {
    for (const auto* ai = info; ai != nullptr; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET) {
            const auto* addr = detail::safe_addr_cast<struct sockaddr_in>(ai->ai_addr);
            auto ip = ntohl(addr->sin_addr.s_addr);

            if (is_private_ipv4(ip)) {
                return true;
            }
        } else if (ai->ai_family == AF_INET6) {
            const auto* addr = detail::safe_addr_cast<struct sockaddr_in6>(ai->ai_addr);
            const auto* bytes = addr->sin6_addr.s6_addr;

            // ::1 (loopback)
            if (std::all_of(bytes, bytes + 15, [](std::uint8_t b) { return b == 0; }) &&
                bytes[15] == 1) {
                return true;
            }

            // :: (unspecified)
            if (std::all_of(bytes, bytes + 16, [](std::uint8_t b) { return b == 0; })) {
                return true;
            }

            // fe80::/10 (link-local)
            if (bytes[0] == k_ipv6_link_local_first_byte &&
                (bytes[1] & k_ipv6_link_local_mask) == k_ipv6_link_local_value) {
                return true;
            }

            // fc00::/7 (unique local addresses, RFC 4193)
            if ((bytes[0] & k_ipv6_ula_mask) == k_ipv6_ula_value) {
                return true;
            }

            // ::ffff:0:0/96 (IPv4-mapped) — check the embedded IPv4.
            if (std::all_of(bytes, bytes + 10, [](std::uint8_t b) { return b == 0; }) &&
                bytes[10] == k_ipv4_mapped_marker && bytes[11] == k_ipv4_mapped_marker) {
                auto ip = (static_cast<std::uint32_t>(bytes[12]) << 24) |
                          (static_cast<std::uint32_t>(bytes[13]) << 16) |
                          (static_cast<std::uint32_t>(bytes[14]) << 8) |
                          static_cast<std::uint32_t>(bytes[15]);

                if (is_private_ipv4(ip)) {
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace luma::network

#endif // LUMA_RUNTIME_STDLIB_NETWORK_SECURITY_HPP
