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
        });
}

} // namespace luma
