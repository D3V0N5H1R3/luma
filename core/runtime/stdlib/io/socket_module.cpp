#include "runtime/stdlib/io/socket_module.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/io/platform_socket.hpp"
#include "runtime/stdlib/io/socket_io.hpp"
#include "runtime/stdlib/io/winsock_init.hpp"

// ─── socket_module.cpp Socket standard library module. ───────────────────────
//
// Provides low-level TCP socket primitives: connect, listen, accept, send,
// recv, close. Connection lifetime is managed through SocketValue, a
// ref-counted RAII handle stored in the Luma runtime value system.
//
// The RAII socket guard and the looping full-buffer send used by the HTTP
// connection layer are shared via runtime/stdlib/io/socket_io.hpp. This module
// reuses SocketGuard for connect/listen/accept lifetime management but keeps
// its own single-shot send/recv primitives, which intentionally expose the
// lower-level byte counts (including short writes) to Luma programs.

namespace luma {

// ─── Platform helpers ────────────────────────────────────────────────────────
//
// This module uses reinterpret_cast for POSIX/Winsock socket API interop
// (sockaddr casts, setsockopt/getsockopt buffers). These casts are
// unavoidable at the OS boundary and follow the patterns prescribed by
// the socket API documentation.

namespace {

// ─── Named constants ─────────────────────────────────────────────────────────

// Default TCP connect timeout (30 seconds).
constexpr int k_connect_timeout_ms{30'000};

// Maximum single recv/recvfrom buffer allocation (16 MB).
constexpr std::int64_t k_max_recv_buffer_bytes{std::int64_t{16} * 1024 * 1024};

// ─── Failure and guard helpers ───────────────────────────────────────────────

// Build a failure Value of the form "failed to <action>: <system error>".
// Centralises the repeated socket-error reporting pattern.
[[nodiscard]] Value socket_failure(std::string_view action) {
    return make_failure_value(
        std::format("failed to {}: {}", action, platform_socket::last_error()));
}

// Build the failure Value for a host-resolution failure.  Shared by every
// connect/bind path so the wording and error category stay identical.
[[nodiscard]] Value resolve_host_failure(std::string_view host) {
    return make_failure_value(std::format("failed to resolve host: {}", host));
}

// Guard against operating on a closed socket.  Returns a "socket is closed"
// failure when the handle is no longer valid, or nullopt when it is usable.
[[nodiscard]] std::optional<Value> check_socket_open(const std::shared_ptr<SocketValue>& sv) {
    if (!sv->is_valid()) {
        return make_failure_value("socket is closed");
    }

    return std::nullopt;
}

// Guard against exceeding the process-wide open-socket limit.  Returns a
// "<fn>: too many open sockets" failure at the limit, or nullopt otherwise.
[[nodiscard]] std::optional<Value> check_socket_limit(std::string_view fn_name) {
    if (SocketValue::open_count() >= ResourceLimits::max_open_sockets) {
        return make_failure_value(std::format("{}: too many open sockets", fn_name));
    }

    return std::nullopt;
}

// Resolve host:port to an addrinfo list wrapped in a unique_ptr.
using AddrInfoPtr = std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)>;

[[nodiscard]] AddrInfoPtr resolve_address(const std::string& host, int port, int socktype,
                                          bool passive) {
    // Reject hostnames exceeding the DNS limit (RFC 1035).
    if (host.size() > ResourceLimits::max_hostname_length) {
        return AddrInfoPtr{nullptr, freeaddrinfo};
    }

    struct addrinfo hints {};

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;

    if (passive) {
        hints.ai_flags = AI_PASSIVE;
    }

    const std::string port_str{std::to_string(port)};

    struct addrinfo* raw{nullptr};

    const int rc =
        getaddrinfo(host.empty() ? nullptr : host.c_str(), port_str.c_str(), &hints, &raw);

    if (rc != 0) {
        return AddrInfoPtr{nullptr, freeaddrinfo};
    }

    return AddrInfoPtr{raw, freeaddrinfo};
}

// ─── Address formatting ───

// Host and port extracted from a sockaddr.
struct HostPort {
    std::string host;
    int port;
};

// Extract the numeric host and port from an IPv4 or IPv6 sockaddr.  Returns
// nullopt for address families other than AF_INET / AF_INET6.
[[nodiscard]] std::optional<HostPort> sockaddr_host_port(const struct sockaddr_storage& storage) {
    if (storage.ss_family == AF_INET) {
        const auto& addr = reinterpret_cast<const struct sockaddr_in&>(storage);

        char ip[INET_ADDRSTRLEN]{};

        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));

        return HostPort{ip, static_cast<int>(ntohs(addr.sin_port))};
    }

    if (storage.ss_family == AF_INET6) {
        const auto& addr = reinterpret_cast<const struct sockaddr_in6&>(storage);

        char ip[INET6_ADDRSTRLEN]{};

        inet_ntop(AF_INET6, &addr.sin6_addr, ip, sizeof(ip));

        return HostPort{ip, static_cast<int>(ntohs(addr.sin6_port))};
    }

    return std::nullopt;
}

// Format a sockaddr (IPv4 or IPv6) as "host:port", bracketing IPv6 hosts.
[[nodiscard]] std::string format_sockaddr(const struct sockaddr_storage& storage) {
    const auto hp = sockaddr_host_port(storage);

    if (!hp) {
        return "unknown";
    }

    if (storage.ss_family == AF_INET6) {
        return std::format("[{}]:{}", hp->host, hp->port);
    }

    return std::format("{}:{}", hp->host, hp->port);
}

constexpr std::int64_t k_max_port = 65535;

[[nodiscard]] std::optional<Value> validate_port(std::int64_t port) {
    if (port < 0 || port > k_max_port) {
        return make_failure_value("port must be between 0 and 65535");
    }

    return std::nullopt;
}

// Validate max_bytes and allocate a zero-filled receive buffer capped at
// k_max_recv_buffer_bytes.  Fills `buffer` and returns nullopt on success, or a
// failure Value when max_bytes is not positive.
[[nodiscard]] std::optional<Value> make_recv_buffer(std::int64_t max_bytes, std::string& buffer) {
    if (max_bytes <= 0) {
        return make_failure_value("max_bytes must be positive");
    }

    // Cap at a reasonable maximum to prevent excessive allocation.
    const auto buf_size = static_cast<std::size_t>(std::min(max_bytes, k_max_recv_buffer_bytes));

    buffer.assign(buf_size, '\0');

    return std::nullopt;
}

// Build a Socket.Address record { host: string, port: integer } from a resolved
// socket address.  IPv6-aware via sockaddr_host_port (no fragile "host:port"
// string parsing), mirroring the host/port fields of Socket.UdpPacket.
[[nodiscard]] Value make_address_record(const struct sockaddr_storage& addr) {
    const auto hp = sockaddr_host_port(addr);

    if (!hp) {
        return socket_failure("resolve address");
    }

    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Address";
    rec->fields.emplace_back("host", Value{hp->host});
    rec->fields.emplace_back("port", Value{static_cast<std::int64_t>(hp->port)});

    return make_success_value(Value{std::move(rec)});
}

// ─── IP-literal parsing (pure, no OS calls) ──────────────────────────────────

// True for an ASCII hexadecimal digit.
[[nodiscard]] bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Validate a dotted-decimal IPv4 literal.  Returns the canonical "a.b.c.d" form
// (leading zeros stripped) or nullopt when the text is not a valid IPv4 address.
[[nodiscard]] std::optional<std::string> parse_ipv4(std::string_view s) {
    std::array<int, 4> octets{};
    std::size_t i = 0;

    for (int idx = 0; idx < 4; ++idx) {
        int value = 0;
        int digits = 0;

        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            value = (value * 10) + (s[i] - '0');
            ++digits;
            ++i;

            if (digits > 3) {
                return std::nullopt;
            }
        }

        if (digits == 0 || value > 255) {
            return std::nullopt;
        }

        octets[static_cast<std::size_t>(idx)] = value;

        if (idx < 3) {
            if (i >= s.size() || s[i] != '.') {
                return std::nullopt;
            }
            ++i;
        }
    }

    if (i != s.size()) {
        return std::nullopt;
    }

    return std::format("{}.{}.{}.{}", octets[0], octets[1], octets[2], octets[3]);
}

// Count the number of 16-bit chunks in a colon-separated IPv6 group list.  A
// trailing embedded IPv4 (e.g. "::ffff:1.2.3.4") counts as two chunks and sets
// `embedded_ipv4` so the caller can enforce that it only appears in the address
// tail.  Returns -1 on any malformed token, or 0 for an empty part.
[[nodiscard]] int count_ipv6_chunks(std::string_view part, bool& embedded_ipv4) {
    embedded_ipv4 = false;

    if (part.empty()) {
        return 0;
    }

    int chunks = 0;
    std::size_t i = 0;

    while (true) {
        const std::size_t colon = part.find(':', i);
        const std::string_view tok =
            (colon == std::string_view::npos) ? part.substr(i) : part.substr(i, colon - i);

        if (tok.find('.') != std::string_view::npos) {
            // An embedded IPv4 is only legal as the final token of the segment.
            if (colon != std::string_view::npos || !parse_ipv4(tok)) {
                return -1;
            }

            embedded_ipv4 = true;
            chunks += 2;
            break;
        }

        if (tok.empty() || tok.size() > 4) {
            return -1;
        }

        for (const char c : tok) {
            if (!is_hex_digit(c)) {
                return -1;
            }
        }

        ++chunks;

        if (colon == std::string_view::npos) {
            break;
        }

        i = colon + 1;

        if (i == part.size()) {
            return -1; // Trailing single colon (e.g. "1:2:").
        }
    }

    return chunks;
}

// Validate an IPv6 literal (with optional "::" zero-compression and an optional
// embedded IPv4 tail).  Returns the lowercased address on success, else nullopt.
[[nodiscard]] std::optional<std::string> parse_ipv6(std::string_view s) {
    if (s.empty()) {
        return std::nullopt;
    }

    const std::size_t compress = s.find("::");

    if (compress != std::string_view::npos) {
        // At most one "::" is permitted.
        if (s.find("::", compress + 1) != std::string_view::npos) {
            return std::nullopt;
        }

        bool head_ipv4 = false;
        bool tail_ipv4 = false;
        const int head = count_ipv6_chunks(s.substr(0, compress), head_ipv4);
        const int tail = count_ipv6_chunks(s.substr(compress + 2), tail_ipv4);

        // "::" must stand in for at least one zero group, so the explicit chunks
        // total at most 7.  An embedded IPv4 is only legal as the address tail,
        // so it must not appear in the head segment (before the "::").
        if (head < 0 || tail < 0 || head_ipv4 || head + tail > 7) {
            return std::nullopt;
        }
    } else {
        // No compression: exactly eight 16-bit chunks required.  An embedded
        // IPv4 tail is allowed (count_ipv6_chunks accepts it only as the final
        // token), so its flag needs no extra check here.
        bool embedded_ipv4 = false;
        if (count_ipv6_chunks(s, embedded_ipv4) != 8) {
            return std::nullopt;
        }
    }

    std::string lowered{s};

    for (char& c : lowered) {
        if (c >= 'A' && c <= 'F') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }

    return lowered;
}

// Build a Socket.IpAddress choice value carrying the canonical address text.  The
// short runtime type_name "IpAddress" and the single "address" payload field must
// match the choice registered in stdlib_type_arities.cpp.
[[nodiscard]] Value make_ip_address(std::string variant, std::string address) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "IpAddress";
    cv->variant = std::move(variant);
    cv->fields.emplace_back(Value{std::move(address)});

    return Value{std::move(cv)};
}

// ─── Typed transport errors (the Socket.Error choice) ────────────────────────
//
// The *_typed slice of the module (connect_typed / listen_typed / send_typed /
// receive_typed) classifies a transport failure into a Socket.Error variant so a
// program can branch on the category instead of substring-matching an opaque
// message.  The string-error connect/listen/send/receive functions are left
// untouched.  Mirrors http_error_variant()/make_http_error_choice() in
// http_module_request.cpp.

// Map a platform-neutral error category to its Socket.Error choice variant name.
// The names must match the Socket.Error ChoiceDeclaration in
// core/analysis/types/stdlib_type_arities.cpp exactly (PascalCase).
[[nodiscard]] std::string_view socket_error_variant(platform_socket::ErrorCategory category) {
    switch (category) {
        case platform_socket::ErrorCategory::ConnectionRefused:
            return "ConnectionRefused";
        case platform_socket::ErrorCategory::TimedOut:
            return "Timeout";
        case platform_socket::ErrorCategory::HostUnreachable:
            return "HostUnreachable";
        case platform_socket::ErrorCategory::AddressInUse:
            return "AddressInUse";
        case platform_socket::ErrorCategory::ConnectionReset:
            return "ConnectionReset";
        case platform_socket::ErrorCategory::NotConnected:
            return "NotConnected";
        case platform_socket::ErrorCategory::Other:
            return "Other";
    }

    return "Other";
}

// Wrap a Socket.Error variant name in a ChoiceValue.  The runtime short name
// "Error" matches how the type checker registers the choice from
// stdlib_type_arities.cpp; the qualified "Socket.Error" is resolved separately by
// the type checker (mirroring make_http_error_choice / make_ip_address).
[[nodiscard]] Value make_socket_error_choice(std::string_view variant) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "Error";
    cv->variant = std::string{variant};

    return Value{std::move(cv)};
}

// Build a failure result carrying the Socket.Error variant for a category.
[[nodiscard]] Value socket_error_failure(platform_socket::ErrorCategory category) {
    return Value{ResultValue::failure(make_socket_error_choice(socket_error_variant(category)))};
}

// Build a failure result carrying the Socket.Error variant for the current
// platform socket error (errno / WSAGetLastError()).  Call immediately after the
// failing syscall so the error code is still current.
[[nodiscard]] Value socket_error_failure_from_last() {
    return socket_error_failure(
        platform_socket::classify_error(platform_socket::last_error_code()));
}

// Typed counterpart of check_socket_open: a closed handle is a NotConnected
// transport error.  Returns nullopt when the socket is usable.
[[nodiscard]] std::optional<Value> check_socket_open_typed(const std::shared_ptr<SocketValue>& sv) {
    if (!sv->is_valid()) {
        return socket_error_failure(platform_socket::ErrorCategory::NotConnected);
    }

    return std::nullopt;
}

} // namespace

// ─── Module registration ─────────────────────────────────────────────────────

void register_socket_ns(const EnvPtr& env) {
    ModuleBuilder{"Socket", env} // Socket.connect(string host, integer port) -> result<socket>
        // Establish a TCP connection to a remote host.
        // A 30-second connect timeout is applied automatically.
        .func("connect", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Socket.connect", loc);

            ensure_winsock();

            const auto& host = args[0].as_string();
            const auto port_val = expect_integer(args[1], "Socket.connect", loc);

            if (auto err = validate_port(port_val)) {
                return *err;
            }

            const auto port = static_cast<int>(port_val);

            auto info = resolve_address(host, port, SOCK_STREAM, false);

            if (!info) {
                return resolve_host_failure(host);
            }

            if (auto err = check_socket_limit("Socket.connect")) {
                return *err;
            }

            const SocketHandle sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);

            if (sock == invalid_socket_handle) {
                return socket_failure("create socket");
            }

            SocketGuard guard{sock};

            if (!tcp_connect_with_timeout(sock, info->ai_addr, static_cast<int>(info->ai_addrlen),
                                          k_connect_timeout_ms)) {
                return socket_failure("connect");
            }

            auto sv = std::make_shared<SocketValue>(sock, SocketRole::Client);

            guard.release();

            return make_success_value(Value{std::move(sv)});
        })
        // Socket.connect_typed(string host, integer port) -> result<socket, Socket.Error>
        // Opt-in typed-error variant of Socket.connect: a transport failure is
        // surfaced as a Socket.Error choice (ConnectionRefused / Timeout /
        // HostUnreachable / ...) rather than an opaque string, so a program can
        // match the category — retry only on Timeout, fall back only on
        // ConnectionRefused.  Mirrors Http.get_typed; string-error Socket.connect
        // is left untouched.
        .func("connect_typed", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Socket.connect_typed", loc);

            ensure_winsock();

            const auto& host = args[0].as_string();
            const auto port_val = expect_integer(args[1], "Socket.connect_typed", loc);

            if (port_val < 0 || port_val > k_max_port) {
                return socket_error_failure(platform_socket::ErrorCategory::Other);
            }

            const auto port = static_cast<int>(port_val);

            auto info = resolve_address(host, port, SOCK_STREAM, false);

            if (!info) {
                return socket_error_failure(platform_socket::ErrorCategory::HostUnreachable);
            }

            if (SocketValue::open_count() >= ResourceLimits::max_open_sockets) {
                return socket_error_failure(platform_socket::ErrorCategory::Other);
            }

            const SocketHandle sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);

            if (sock == invalid_socket_handle) {
                return socket_error_failure_from_last();
            }

            SocketGuard guard{sock};

            bool timed_out = false;
            int error_code = 0;

            if (!tcp_connect_with_timeout(sock, info->ai_addr, static_cast<int>(info->ai_addrlen),
                                          k_connect_timeout_ms, &timed_out, &error_code)) {
                if (timed_out) {
                    return socket_error_failure(platform_socket::ErrorCategory::TimedOut);
                }

                return socket_error_failure(platform_socket::classify_error(error_code));
            }

            auto sv = std::make_shared<SocketValue>(sock, SocketRole::Client);

            guard.release();

            return make_success_value(Value{std::move(sv)});
        })
        // Socket.listen(string host, integer port) -> result<socket>
        // Create a TCP server socket bound to the given address and port.
        .func("listen", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Socket.listen", loc);

            ensure_winsock();

            const auto& host = args[0].as_string();
            const auto port_val = expect_integer(args[1], "Socket.listen", loc);

            if (auto err = validate_port(port_val)) {
                return *err;
            }

            const auto port = static_cast<int>(port_val);

            auto info = resolve_address(host, port, SOCK_STREAM, true);

            if (!info) {
                return resolve_host_failure(host);
            }

            if (auto err = check_socket_limit("Socket.listen")) {
                return *err;
            }

            const SocketHandle sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);

            if (sock == invalid_socket_handle) {
                return socket_failure("create socket");
            }

            SocketGuard guard{sock};

            // Allow address reuse to avoid bind failures on restart.
            const int opt{1};

            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt),
                       sizeof(opt));

            if (::bind(sock, info->ai_addr,
                       static_cast<platform_socket::addr_length_t>(info->ai_addrlen)) != 0) {
                return socket_failure("bind");
            }

            if (::listen(sock, SOMAXCONN) != 0) {
                return socket_failure("listen");
            }

            auto sv = std::make_shared<SocketValue>(sock, SocketRole::Server);

            guard.release();

            return make_success_value(Value{std::move(sv)});
        })
        // Socket.listen_typed(string host, integer port) -> result<socket, Socket.Error>
        // Opt-in typed-error variant of Socket.listen: a bind clash on an already-
        // used port surfaces as Socket.Error.AddressInUse instead of an opaque
        // string, so a server can react to the category.  String-error
        // Socket.listen is left untouched.
        .func("listen_typed", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Socket.listen_typed", loc);

            ensure_winsock();

            const auto& host = args[0].as_string();
            const auto port_val = expect_integer(args[1], "Socket.listen_typed", loc);

            if (port_val < 0 || port_val > k_max_port) {
                return socket_error_failure(platform_socket::ErrorCategory::Other);
            }

            const auto port = static_cast<int>(port_val);

            auto info = resolve_address(host, port, SOCK_STREAM, true);

            if (!info) {
                return socket_error_failure(platform_socket::ErrorCategory::HostUnreachable);
            }

            if (SocketValue::open_count() >= ResourceLimits::max_open_sockets) {
                return socket_error_failure(platform_socket::ErrorCategory::Other);
            }

            const SocketHandle sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);

            if (sock == invalid_socket_handle) {
                return socket_error_failure_from_last();
            }

            SocketGuard guard{sock};

            const int opt{1};

            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt),
                       sizeof(opt));

            if (::bind(sock, info->ai_addr,
                       static_cast<platform_socket::addr_length_t>(info->ai_addrlen)) != 0) {
                return socket_error_failure_from_last();
            }

            if (::listen(sock, SOMAXCONN) != 0) {
                return socket_error_failure_from_last();
            }

            auto sv = std::make_shared<SocketValue>(sock, SocketRole::Server);

            guard.release();

            return make_success_value(Value{std::move(sv)});
        })
        // Socket.accept(socket server) -> result<socket>
        // Accept an incoming connection on a server socket.
        .func("accept", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.accept", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            if (sv->role != SocketRole::Server) {
                return make_failure_value("socket is not a server socket");
            }

            struct sockaddr_storage addr {};

            auto addr_len = static_cast<socklen_t>(sizeof(addr));

            const SocketHandle client =
                ::accept(sv->handle.load(), reinterpret_cast<struct sockaddr*>(&addr), &addr_len);

            if (client == invalid_socket_handle) {
                return socket_failure("accept");
            }

            SocketGuard guard{client};

            // Check after accept: the OS has already created the fd, so we must
            // close it if the limit is exceeded.
            if (auto err = check_socket_limit("Socket.accept")) {
                return *err;
            }

            auto csv = std::make_shared<SocketValue>(client, SocketRole::Client);

            guard.release();

            return make_success_value(Value{std::move(csv)});
        })
        // Socket.send(socket s, string data) -> result<integer>
        // Send data through the socket.  Returns the number of bytes sent.
        .func("send", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.send", loc);

            (void)expect_string(args[1], "Socket.send", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            const auto& data = args[1].as_string();

            const auto sent =
                ::send(sv->handle.load(), data.c_str(),
                       static_cast<platform_socket::io_length_t>(data.size()), MSG_NOSIGNAL);

            if (sent < 0) {
                return socket_failure("send");
            }

            return make_success_value(Value{static_cast<std::int64_t>(sent)});
        })
        // Socket.send_typed(socket s, string data) -> result<integer, Socket.Error>
        // Opt-in typed-error variant of Socket.send: a broken connection surfaces
        // as Socket.Error.ConnectionReset (or NotConnected for a closed handle)
        // instead of an opaque string.  String-error Socket.send is unchanged.
        .func("send_typed", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.send_typed", loc);

            (void)expect_string(args[1], "Socket.send_typed", loc);

            if (auto err = check_socket_open_typed(sv)) {
                return *err;
            }

            const auto& data = args[1].as_string();

            const auto sent =
                ::send(sv->handle.load(), data.c_str(),
                       static_cast<platform_socket::io_length_t>(data.size()), MSG_NOSIGNAL);

            if (sent < 0) {
                return socket_error_failure_from_last();
            }

            return make_success_value(Value{static_cast<std::int64_t>(sent)});
        })
        // Socket.receive(socket s, integer max_bytes) -> result<string>
        // Receive up to max_bytes from the socket.
        .func("receive", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.receive", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            const auto max_bytes = expect_integer(args[1], "Socket.receive", loc);

            std::string buffer;

            if (auto err = make_recv_buffer(max_bytes, buffer)) {
                return *err;
            }

            const auto received =
                ::recv(sv->handle.load(), buffer.data(),
                       static_cast<platform_socket::io_length_t>(buffer.size()), 0);

            if (received < 0) {
                return socket_failure("receive");
            }

            buffer.resize(static_cast<std::size_t>(received));

            return make_success_value(Value{std::move(buffer)});
        })
        // Socket.receive_typed(socket s, integer max_bytes) -> result<string, Socket.Error>
        // Opt-in typed-error variant of Socket.receive: a reset peer surfaces as
        // Socket.Error.ConnectionReset, a receive-timeout as Timeout, and a closed
        // handle as NotConnected, instead of an opaque string.  String-error
        // Socket.receive is unchanged.
        .func("receive_typed", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.receive_typed", loc);

            if (auto err = check_socket_open_typed(sv)) {
                return *err;
            }

            const auto max_bytes = expect_integer(args[1], "Socket.receive_typed", loc);

            if (max_bytes <= 0) {
                return socket_error_failure(platform_socket::ErrorCategory::Other);
            }

            const auto buf_size =
                static_cast<std::size_t>(std::min(max_bytes, k_max_recv_buffer_bytes));

            std::string buffer(buf_size, '\0');

            const auto received =
                ::recv(sv->handle.load(), buffer.data(),
                       static_cast<platform_socket::io_length_t>(buffer.size()), 0);

            if (received < 0) {
                return socket_error_failure_from_last();
            }

            buffer.resize(static_cast<std::size_t>(received));

            return make_success_value(Value{std::move(buffer)});
        })
        // Socket.send_all(socket s, string data) -> result<boolean>
        // Send every byte of data, looping until the whole buffer is written or a
        // socket error occurs.  Unlike Socket.send (which exposes short writes as a
        // byte count), send_all guarantees the full payload is delivered on success.
        .func("send_all", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.send_all", loc);

            (void)expect_string(args[1], "Socket.send_all", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            const auto& data = args[1].as_string();

            if (!send_all(sv->handle.load(), data.data(), data.size())) {
                return socket_failure("send");
            }

            return make_success_value(Value{true});
        })
        // Socket.send_all_typed(socket s, string data) -> result<boolean, Socket.Error>
        // Opt-in typed-error variant of Socket.send_all: a broken connection
        // surfaces as Socket.Error.ConnectionReset (or NotConnected for a closed
        // handle) instead of an opaque string.  String-error Socket.send_all is
        // unchanged.
        .func("send_all_typed", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.send_all_typed", loc);

            (void)expect_string(args[1], "Socket.send_all_typed", loc);

            if (auto err = check_socket_open_typed(sv)) {
                return *err;
            }

            const auto& data = args[1].as_string();

            if (!send_all(sv->handle.load(), data.data(), data.size())) {
                return socket_error_failure_from_last();
            }

            return make_success_value(Value{true});
        })
        // Socket.receive_all(socket s) -> result<string>
        // Read until the peer closes the connection (recv returns 0) or an error
        // occurs, accumulating the whole stream into a string bounded by
        // ResourceLimits::max_string_size.
        .func("receive_all", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.receive_all", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            std::string result;
            std::array<char, 4096> buffer{};

            while (true) {
                const auto received =
                    ::recv(sv->handle.load(), buffer.data(),
                           static_cast<platform_socket::io_length_t>(buffer.size()), 0);

                if (received < 0) {
                    return socket_failure("receive");
                }

                if (received == 0) {
                    break; // Peer performed an orderly shutdown.
                }

                if (result.size() + static_cast<std::size_t>(received) >
                    ResourceLimits::max_string_size) {
                    return make_failure_value(
                        "Socket.receive_all: response exceeds maximum string size");
                }

                result.append(buffer.data(), static_cast<std::size_t>(received));
            }

            return make_success_value(Value{std::move(result)});
        })
        // Socket.receive_line(socket s) -> result<string>
        // Read up to and including the next '\n'.  Reads a byte at a time so no
        // persistent buffer is needed; returns the line with its trailing newline,
        // or the residual bytes without a newline if the peer closes first.  The
        // result is bounded by ResourceLimits::max_string_size.
        .func("receive_line", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.receive_line", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            std::string result;

            while (true) {
                char ch{};

                const auto received = ::recv(sv->handle.load(), &ch,
                                             static_cast<platform_socket::io_length_t>(1), 0);

                if (received < 0) {
                    return socket_failure("receive");
                }

                if (received == 0) {
                    break; // Peer closed before a newline arrived.
                }

                if (result.size() + 1 > ResourceLimits::max_string_size) {
                    return make_failure_value(
                        "Socket.receive_line: line exceeds maximum string size");
                }

                result.push_back(ch);

                if (ch == '\n') {
                    break;
                }
            }

            return make_success_value(Value{std::move(result)});
        })
        // Socket.connect_timeout(string host, integer port, integer timeout_ms)
        //     -> result<socket>
        // Establish a TCP connection with an explicit connect timeout (in
        // milliseconds) via a non-blocking connect and select() on the fd, failing
        // if the deadline elapses.  Like Socket.connect but with a caller-chosen
        // timeout instead of the fixed 30-second default.
        .func("connect_timeout", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Socket.connect_timeout", loc);

            ensure_winsock();

            const auto& host = args[0].as_string();
            const auto port_val = expect_integer(args[1], "Socket.connect_timeout", loc);

            if (auto err = validate_port(port_val)) {
                return *err;
            }

            const auto timeout_val = expect_integer(args[2], "Socket.connect_timeout", loc);

            if (timeout_val < 0 || timeout_val > INT_MAX) {
                return make_failure_value("timeout value out of range");
            }

            const auto port = static_cast<int>(port_val);

            auto info = resolve_address(host, port, SOCK_STREAM, false);

            if (!info) {
                return resolve_host_failure(host);
            }

            if (auto err = check_socket_limit("Socket.connect_timeout")) {
                return *err;
            }

            const SocketHandle sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);

            if (sock == invalid_socket_handle) {
                return socket_failure("create socket");
            }

            SocketGuard guard{sock};

            if (!tcp_connect_with_timeout(sock, info->ai_addr, static_cast<int>(info->ai_addrlen),
                                          static_cast<int>(timeout_val))) {
                return socket_failure("connect");
            }

            auto sv = std::make_shared<SocketValue>(sock, SocketRole::Client);

            guard.release();

            return make_success_value(Value{std::move(sv)});
        })
        // Socket.connect_timeout_typed(string host, integer port, integer timeout_ms)
        //     -> result<socket, Socket.Error>
        // Opt-in typed-error variant of Socket.connect_timeout: an elapsed deadline
        // surfaces as Socket.Error.Timeout and other transport failures as their
        // matching variant, so a program can retry only on Timeout.  String-error
        // Socket.connect_timeout is left untouched.
        .func("connect_timeout_typed", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            (void)expect_string(args[0], "Socket.connect_timeout_typed", loc);

            ensure_winsock();

            const auto& host = args[0].as_string();
            const auto port_val = expect_integer(args[1], "Socket.connect_timeout_typed", loc);

            if (port_val < 0 || port_val > k_max_port) {
                return socket_error_failure(platform_socket::ErrorCategory::Other);
            }

            const auto timeout_val = expect_integer(args[2], "Socket.connect_timeout_typed", loc);

            if (timeout_val < 0 || timeout_val > INT_MAX) {
                return socket_error_failure(platform_socket::ErrorCategory::Other);
            }

            const auto port = static_cast<int>(port_val);

            auto info = resolve_address(host, port, SOCK_STREAM, false);

            if (!info) {
                return socket_error_failure(platform_socket::ErrorCategory::HostUnreachable);
            }

            if (SocketValue::open_count() >= ResourceLimits::max_open_sockets) {
                return socket_error_failure(platform_socket::ErrorCategory::Other);
            }

            const SocketHandle sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);

            if (sock == invalid_socket_handle) {
                return socket_error_failure_from_last();
            }

            SocketGuard guard{sock};

            bool timed_out = false;
            int error_code = 0;

            if (!tcp_connect_with_timeout(sock, info->ai_addr, static_cast<int>(info->ai_addrlen),
                                          static_cast<int>(timeout_val), &timed_out, &error_code)) {
                if (timed_out) {
                    return socket_error_failure(platform_socket::ErrorCategory::TimedOut);
                }

                return socket_error_failure(platform_socket::classify_error(error_code));
            }

            auto sv = std::make_shared<SocketValue>(sock, SocketRole::Client);

            guard.release();

            return make_success_value(Value{std::move(sv)});
        })
        // Socket.send_bytes(socket s, array<integer> bytes) -> result<integer>
        // Send raw bytes (each element an integer 0-255) over the socket, looping
        // until the whole buffer is written.  Returns the number of bytes sent.
        .func("send_bytes", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.send_bytes", loc);
            const auto& bytes = expect_array(args[1], "Socket.send_bytes", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            // Validate and pack every element before sending so an out-of-range or
            // non-integer element fails without a partial write.  Each element must
            // be an integer in 0-255 (the same byte convention as FileSystem and
            // String.to_bytes).
            std::string buffer;
            buffer.reserve(bytes->elements->size());

            for (const auto& elem : *bytes->elements) {
                if (!elem.is_integer()) {
                    return make_failure_value(
                        "Socket.send_bytes: every element must be an integer byte (0-255)");
                }

                const auto byte = elem.as_integer();

                if (byte < 0 || byte > 255) {
                    return make_failure_value(std::format(
                        "Socket.send_bytes: byte value out of range (0-255): {}", byte));
                }

                buffer += static_cast<char>(static_cast<std::uint8_t>(byte));
            }

            if (!send_all(sv->handle.load(), buffer.data(), buffer.size())) {
                return socket_failure("send");
            }

            return make_success_value(Value{static_cast<std::int64_t>(buffer.size())});
        })
        // Socket.receive_bytes(socket s, integer max) -> result<array<integer>>
        // Receive up to max raw bytes and return them as an array of integers
        // (0-255).  A shorter array (including empty) means the peer sent fewer
        // bytes or closed the connection.
        .func("receive_bytes", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.receive_bytes", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            const auto max_bytes = expect_integer(args[1], "Socket.receive_bytes", loc);

            std::string buffer;

            if (auto err = make_recv_buffer(max_bytes, buffer)) {
                return *err;
            }

            const auto received =
                ::recv(sv->handle.load(), buffer.data(),
                       static_cast<platform_socket::io_length_t>(buffer.size()), 0);

            if (received < 0) {
                return socket_failure("receive");
            }

            auto arr = std::make_shared<ArrayValue>();
            arr->elements->reserve(static_cast<std::size_t>(received));

            for (std::size_t i = 0; i < static_cast<std::size_t>(received); ++i) {
                arr->elements->emplace_back(
                    static_cast<std::int64_t>(static_cast<std::uint8_t>(buffer[i])));
            }

            return make_success_value(Value{std::move(arr)});
        })
        // Socket.close(socket s) -> null
        // Close the socket.
        .func("close", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto sv = expect_socket(args[0], "Socket.close", loc);

            if (sv->is_valid()) {
                sv->close();
            }

            return NullValue{};
        })
        // Socket.set_timeout(socket s, integer milliseconds) -> result<boolean>
        // Set send and receive timeout on the socket.
        .func("set_timeout", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.set_timeout", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            const auto ms_val = expect_integer(args[1], "Socket.set_timeout", loc);

            if (ms_val < 0 || ms_val > INT_MAX) {
                return make_failure_value("timeout value out of range");
            }

            const auto ms = static_cast<int>(ms_val);

            if (platform_socket::set_timeout(sv->handle.load(), ms)) {
                return make_success_value(Value{true});
            }

            return socket_failure("set timeout");
        })
        // Socket.is_connected(socket s) -> boolean
        // Check whether the socket handle is still valid.
        .func("is_connected", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.is_connected", loc);

            return Value{sv->is_valid()};
        })
        // Socket.local_address(socket s) -> result<string>
        // Return the local address of the socket as "host:port".
        .func("local_address", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.local_address", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            struct sockaddr_storage addr {};

            auto addr_len = static_cast<socklen_t>(sizeof(addr));

            if (getsockname(sv->handle.load(), reinterpret_cast<struct sockaddr*>(&addr),
                            &addr_len) != 0) {
                return socket_failure("get local address");
            }

            const std::string result{format_sockaddr(addr)};

            return make_success_value(Value{result});
        })
        // Socket.remote_address(socket s) -> result<string>
        // Return the remote address of the socket as "host:port".
        .func("remote_address", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.remote_address", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            struct sockaddr_storage addr {};

            auto addr_len = static_cast<socklen_t>(sizeof(addr));

            if (getpeername(sv->handle.load(), reinterpret_cast<struct sockaddr*>(&addr),
                            &addr_len) != 0) {
                return socket_failure("get remote address");
            }

            const std::string result{format_sockaddr(addr)};

            return make_success_value(Value{result});
        })
        // Socket.local_address_parts(socket s) -> result<Socket.Address>
        // Like local_address, but returns a structured { host, port } record so
        // the caller need not re-parse a "host:port" string (IPv6-safe).
        .func("local_address_parts", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.local_address_parts", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            struct sockaddr_storage addr {};

            auto addr_len = static_cast<socklen_t>(sizeof(addr));

            if (getsockname(sv->handle.load(), reinterpret_cast<struct sockaddr*>(&addr),
                            &addr_len) != 0) {
                return socket_failure("get local address");
            }

            return make_address_record(addr);
        })
        // Socket.remote_address_parts(socket s) -> result<Socket.Address>
        // Like remote_address, but returns a structured { host, port } record.
        .func("remote_address_parts", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.remote_address_parts", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            struct sockaddr_storage addr {};

            auto addr_len = static_cast<socklen_t>(sizeof(addr));

            if (getpeername(sv->handle.load(), reinterpret_cast<struct sockaddr*>(&addr),
                            &addr_len) != 0) {
                return socket_failure("get remote address");
            }

            return make_address_record(addr);
        })
        // Socket.udp_create() -> result<socket>
        // Create an unbound UDP socket.
        .func("udp_create", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            ensure_winsock();

            if (auto err = check_socket_limit("Socket.udp_create")) {
                return *err;
            }

            const SocketHandle sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

            if (sock == invalid_socket_handle) {
                return socket_failure("create UDP socket");
            }

            // Guard the raw fd until ownership transfers to SocketValue, so a
            // concurrent open-count race that makes the SocketValue constructor
            // throw at the limit does not leak the fd (matches connect/listen).
            SocketGuard guard{sock};

            auto sv = std::make_shared<SocketValue>(sock, SocketRole::Client);

            guard.release();

            return make_success_value(Value{std::move(sv)});
        })
        // Socket.udp_bind(socket s, string host, integer port) -> result<boolean>
        // Bind a UDP socket to a local address and port.
        .func("udp_bind", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.udp_bind", loc);

            (void)expect_string(args[1], "Socket.udp_bind", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            const auto& host = args[1].as_string();
            const auto port_val = expect_integer(args[2], "Socket.udp_bind", loc);

            if (auto err = validate_port(port_val)) {
                return *err;
            }

            const auto port = static_cast<int>(port_val);

            auto info = resolve_address(host, port, SOCK_DGRAM, true);

            if (!info) {
                return resolve_host_failure(host);
            }

            if (::bind(sv->handle.load(), info->ai_addr,
                       static_cast<platform_socket::addr_length_t>(info->ai_addrlen)) != 0) {
                return socket_failure("bind");
            }

            return make_success_value(Value{true});
        })
        // Socket.udp_send(socket s, string data, string host, integer port)
        //     -> result<integer>
        // Send data to a specific host and port via UDP.
        .func("udp_send", 4)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.udp_send", loc);

            (void)expect_string(args[1], "Socket.udp_send", loc);

            (void)expect_string(args[2], "Socket.udp_send", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            const auto& data = args[1].as_string();
            const auto& host = args[2].as_string();
            const auto port_val = expect_integer(args[3], "Socket.udp_send", loc);

            if (auto err = validate_port(port_val)) {
                return *err;
            }

            const auto port = static_cast<int>(port_val);

            auto info = resolve_address(host, port, SOCK_DGRAM, false);

            if (!info) {
                return resolve_host_failure(host);
            }

            const auto sent =
                ::sendto(sv->handle.load(), data.c_str(),
                         static_cast<platform_socket::io_length_t>(data.size()), 0, info->ai_addr,
                         static_cast<platform_socket::addr_length_t>(info->ai_addrlen));

            if (sent < 0) {
                return socket_failure("send");
            }

            return make_success_value(Value{static_cast<std::int64_t>(sent)});
        })
        // Socket.udp_receive(socket s, integer max_bytes)
        //     -> result<UdpPacket>
        // Receive data and the sender's address.  Returns a UdpPacket
        // record with fields "data", "host", and "port".
        .func("udp_receive", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& sv = expect_socket(args[0], "Socket.udp_receive", loc);

            if (auto err = check_socket_open(sv)) {
                return *err;
            }

            const auto max_bytes = expect_integer(args[1], "Socket.udp_receive", loc);

            std::string buffer;

            if (auto err = make_recv_buffer(max_bytes, buffer)) {
                return *err;
            }

            struct sockaddr_storage sender_addr {};

            auto addr_len = static_cast<socklen_t>(sizeof(sender_addr));

            const auto received =
                ::recvfrom(sv->handle.load(), buffer.data(),
                           static_cast<platform_socket::io_length_t>(buffer.size()), 0,
                           reinterpret_cast<struct sockaddr*>(&sender_addr), &addr_len);

            if (received < 0) {
                return socket_failure("receive");
            }

            buffer.resize(static_cast<std::size_t>(received));

            std::string ip;
            int sender_port{0};

            if (const auto hp = sockaddr_host_port(sender_addr)) {
                ip = hp->host;
                sender_port = hp->port;
            }

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "UdpPacket";
            rec->fields.emplace_back("data", Value{std::move(buffer)});
            rec->fields.emplace_back("host", Value{std::move(ip)});
            rec->fields.emplace_back("port", Value{static_cast<std::int64_t>(sender_port)});

            return make_success_value(Value{std::move(rec)});
        })
        // Socket.parse_ip(string) -> result<Socket.IpAddress>
        // Validate and classify an IP literal without any OS call.  IPv4 is
        // returned canonicalised (leading zeros stripped); IPv6 lowercased.
        .func("parse_ip", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& text = expect_string(args[0], "Socket.parse_ip", loc);

            if (auto v4 = parse_ipv4(text)) {
                return make_success_value(make_ip_address("V4", *std::move(v4)));
            }

            if (auto v6 = parse_ipv6(text)) {
                return make_success_value(make_ip_address("V6", *std::move(v6)));
            }

            return make_failure_value(std::format("not a valid IP address: '{}'", text));
        })
        // Socket.ip_to_string(Socket.IpAddress) -> string
        // Render a parsed address back to its canonical text.
        .func("ip_to_string", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            if (!args[0].is_choice()) {
                throw RuntimeError{"Socket.ip_to_string: expected a Socket.IpAddress", loc,
                                   "build one with Socket.parse_ip(text)"};
            }

            const auto& cv = args[0].as_choice();

            if (cv->type_name != "IpAddress" || cv->fields.empty() || !cv->fields[0].is_string()) {
                throw RuntimeError{"Socket.ip_to_string: expected a Socket.IpAddress", loc,
                                   "build one with Socket.parse_ip(text)"};
            }

            return Value{cv->fields[0].as_string()};
        });
}

} // namespace luma
