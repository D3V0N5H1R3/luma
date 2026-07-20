// Http module — connection management and request building.
// Split from http_module.cpp for readability; TLS session management lives in
// http_module_tls.cpp and response parsing in http_module_response.cpp.

#include "runtime/stdlib/io/http_module_request.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/string_utils.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/io/http_module_connection.hpp"
#include "runtime/stdlib/io/http_module_response.hpp"
#include "runtime/stdlib/io/http_proxy.hpp"
#include "runtime/stdlib/io/http_security.hpp"
#include "runtime/stdlib/io/http_url_parser.hpp"
#include "runtime/stdlib/io/platform_socket.hpp"
#include "runtime/stdlib/io/winsock_init.hpp"

namespace luma {

// ─── Platform helpers ────────────────────────────────────────────────────────

namespace {

// RAII owner for a getaddrinfo() result list.
using AddrInfoPtr = std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)>;

// Resolve host:port to a list of candidate addresses (AF_UNSPEC, streaming).
// Returns a null owner when resolution fails; the caller decides how to report
// that.  Centralises the getaddrinfo + freeaddrinfo ownership dance so the
// direct-connect and proxy SSRF paths share one implementation.
[[nodiscard]] AddrInfoPtr resolve_host(const std::string& host, int port) {
    ensure_winsock();

    struct addrinfo hints {};

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const auto port_str = std::to_string(port);

    // getaddrinfo expects a bare IPv6 literal ("::1"), not the bracketed URI
    // authority form ("[::1]") that the URL parser preserves for the Host
    // header and request target.  Strip one surrounding bracket pair if present.
    std::string node = host;

    if (node.size() >= 2 && node.front() == '[' && node.back() == ']') {
        node = node.substr(1, node.size() - 2);
    }

    struct addrinfo* raw_info{nullptr};

    if (getaddrinfo(node.c_str(), port_str.c_str(), &hints, &raw_info) != 0 ||
        raw_info == nullptr) {
        return {nullptr, freeaddrinfo};
    }

    return {raw_info, freeaddrinfo};
}

} // anonymous namespace

// ─── Header extraction ──────────────────────────────────────────────────────

[[nodiscard]] std::vector<std::pair<std::string, std::string>>
extract_headers(const DictionaryValue& dict, const SourceLocation& loc) {
    std::vector<std::pair<std::string, std::string>> headers{};
    headers.reserve(dict.entries.size());

    for (const auto& [k, v] : dict.entries) {
        if (!v.is_string()) {
            throw RuntimeError{std::format("HTTP header '{}': expected string value, got '{}'", k,
                                           v.display_type_name()),
                               loc, "all header values must be strings"};
        }
        headers.emplace_back(k, v.as_string());
    }

    return headers;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ─── Request building ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// Validate scheme, host, request-line, and header fields before any network
// activity. Returns an error message, or an empty string when the request is
// safe to send.
[[nodiscard]] std::string
validate_http_request(const std::string& method, const ParsedUrl& parsed, bool is_https,
                      const std::vector<std::pair<std::string, std::string>>& extra_headers) {
    if (is_https) {
#if !defined(LUMA_HAS_TLS) || !LUMA_HAS_TLS
        return "Http: HTTPS requires TLS support (build with LUMA_FEATURE_TLS=ON)";
#endif
    } else if (parsed.scheme != "http") {
        return std::format("Http: unsupported scheme '{}'", parsed.scheme);
    }

    if (parsed.host.empty()) {
        return "Http: empty host";
    }

    const auto hostname_err = validate_hostname_length(parsed.host);

    if (!hostname_err.empty()) {
        return hostname_err;
    }

    // Reject method, host, path, and query containing CR or LF to prevent
    // HTTP request-line injection (CRLF injection / request smuggling).
    const auto crlf_err =
        validate_request_line_security(method, parsed.host, parsed.path, parsed.query);

    if (!crlf_err.empty()) {
        return crlf_err;
    }

    // Validate headers for CRLF injection before writing them.
    return validate_headers_security(extra_headers);
}

// Build the raw HTTP/1.1 request string (status line, Host, standard headers,
// caller headers, and body). When `proxy_absolute` is true the request line
// carries the absolute target URI, as required when forwarding through an HTTP
// proxy.
[[nodiscard]] std::string
build_http_request_string(const std::string& method, const ParsedUrl& parsed,
                          const std::string& body,
                          const std::vector<std::pair<std::string, std::string>>& extra_headers,
                          bool proxy_absolute = false) {
    auto request_path = parsed.path;

    if (!parsed.query.empty()) {
        request_path += "?" + parsed.query;
    }

    const int default_port = default_port_for_scheme(parsed.scheme);

    std::string request_target = request_path;

    if (proxy_absolute) {
        const auto& scheme = parsed.scheme.empty() ? std::string{"http"} : parsed.scheme;
        request_target = scheme + "://" + parsed.host;

        if (parsed.port != default_port) {
            request_target += ":" + std::to_string(parsed.port);
        }

        request_target += request_path;
    }

    std::string request = method + " " + request_target + " HTTP/1.1\r\n";
    request += "Host: " + parsed.host;

    if (parsed.port != default_port) {
        request += ":" + std::to_string(parsed.port);
    }

    request += "\r\n";
    request += "Connection: close\r\n";
    request += "User-Agent: Luma/1.0\r\n";

    if (!body.empty()) {
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";

        // Add Content-Type if not in extra_headers.
        const bool has_content_type = std::ranges::any_of(extra_headers, [](const auto& header) {
            return case_insensitive_equal(header.first, "content-type");
        });

        if (!has_content_type) {
            request += "Content-Type: application/octet-stream\r\n";
        }
    }

    for (const auto& [name, value] : extra_headers) {
        request += name;
        request += ": ";
        request += value;
        request += "\r\n";
    }

    request += "\r\n";
    request += body;

    return request;
}

// Resolve, optionally SSRF-validate, create, and connect a TCP socket to the
// given host:port. Returns the connected socket on success (caller takes
// ownership), or invalid_socket_handle with error_out populated on failure.
//
// `ssrf_check` is true for direct connections to the request target (so private
// or reserved addresses are blocked) and false for connections to a configured
// proxy, which is commonly an internal host the user has explicitly trusted via
// the environment.
[[nodiscard]] SocketHandle connect_tcp_socket(const std::string& host, int port, int timeout_ms,
                                              bool ssrf_check, std::string& error_out) {
    const auto info = resolve_host(host, port);

    if (!info) {
        error_out = std::format("Http: cannot resolve host '{}'", host);
        return invalid_socket_handle;
    }

    // Block connections to private/reserved IP ranges to prevent SSRF attacks.
    if (ssrf_check) {
        const auto ssrf_err = validate_resolved_address(info.get(), host);

        if (!ssrf_err.empty()) {
            error_out = ssrf_err;
            return invalid_socket_handle;
        }
    }

    // Create socket and connect using the resolved address (no second DNS
    // lookup) to eliminate the TOCTOU window a second resolution would open.
    const auto sock = ::socket(info->ai_family, info->ai_socktype, info->ai_protocol);

    if (sock == invalid_socket_handle) {
        error_out = "Http: cannot create socket";
        return invalid_socket_handle;
    }

    SocketGuard guard{sock};

    if (!tcp_connect_with_timeout(sock, info->ai_addr, static_cast<int>(info->ai_addrlen),
                                  timeout_ms)) {
        error_out = std::format("Http: connection to {}:{} failed", host, port);
        return invalid_socket_handle;
    }

    (void)platform_socket::set_timeout(sock, timeout_ms);

    guard.release();

    return sock;
}

// Connect directly to the request target, with SSRF validation.
[[nodiscard]] SocketHandle connect_http_socket(const ParsedUrl& parsed, int timeout_ms,
                                               std::string& error_out) {
    return connect_tcp_socket(parsed.host, parsed.port, timeout_ms, /*ssrf_check=*/true, error_out);
}

// Best-effort SSRF check on the request target when the request will be sent
// through a proxy. The proxy itself does the real resolution and connection, so
// this only resolves the target locally to reject obvious private/reserved
// destinations (literal RFC 1918 IPs, loopback, etc.); if the host cannot be
// resolved locally the check is skipped and the proxy is left to handle it.
// Returns an SSRF error message when the target is blocked, otherwise empty.
[[nodiscard]] std::string proxy_target_ssrf_error(const std::string& host, int port) {
    const auto info = resolve_host(host, port);

    if (!info) {
        return {};
    }

    return validate_resolved_address(info.get(), host);
}

// Establish a CONNECT tunnel through an already-connected proxy socket so a
// subsequent TLS handshake can run end-to-end with the origin server. Returns
// an empty string on success, or an error message on failure.
[[nodiscard]] std::string proxy_connect_tunnel(SocketHandle sock, const std::string& host, int port,
                                               const std::string& auth_header) {
    std::string req =
        std::format("CONNECT {}:{} HTTP/1.1\r\nHost: {}:{}\r\n", host, port, host, port);

    if (!auth_header.empty()) {
        req += "Proxy-Authorization: " + auth_header + "\r\n";
    }

    req += "User-Agent: Luma/1.0\r\n\r\n";

    std::size_t sent{0};

    while (sent < req.size()) {
        const auto chunk =
            static_cast<int>(std::min(req.size() - sent, static_cast<std::size_t>(INT_MAX)));
        const auto n = ::send(sock, req.data() + sent,
                              static_cast<platform_socket::io_length_t>(chunk), MSG_NOSIGNAL);

        if (n <= 0) {
            return "proxy CONNECT: send failed";
        }

        sent += static_cast<std::size_t>(n);
    }

    // Read until the end of the proxy's response headers.
    std::string resp;
    std::array<char, 1024> buf{};

    // Cap the buffered CONNECT response headers at 64 KiB.
    constexpr std::size_t max_response_bytes = static_cast<std::size_t>(64) * 1024;

    bool headers_complete{false};

    while (!headers_complete && resp.size() < max_response_bytes) {
        const auto n =
            ::recv(sock, buf.data(), static_cast<platform_socket::io_length_t>(buf.size()), 0);

        if (n <= 0) {
            break;
        }

        // The 4-byte terminator can straddle the previous tail by at most 3
        // bytes, so resume the scan just before the freshly appended region
        // instead of re-scanning the whole buffer each iteration (O(n) overall).
        const std::size_t scan_from = resp.size() >= 3 ? resp.size() - 3 : 0;
        resp.append(buf.data(), static_cast<std::size_t>(n));
        headers_complete = resp.find("\r\n\r\n", scan_from) != std::string::npos;
    }

    const auto line_end = resp.find("\r\n");

    if (line_end == std::string::npos) {
        return "proxy CONNECT: no response from proxy";
    }

    // Status line: "HTTP/1.1 200 Connection established".
    const auto status_line = resp.substr(0, line_end);
    const auto sp = status_line.find(' ');

    if (sp == std::string::npos) {
        return "proxy CONNECT: malformed proxy response";
    }

    int status_code{0};

    try {
        status_code = std::stoi(status_line.substr(sp + 1));
    } catch (const std::exception&) {
        return "proxy CONNECT: malformed proxy response";
    }

    if (status_code < 200 || status_code >= 300) {
        return std::format("proxy CONNECT failed (status {})", status_code);
    }

    return {};
}

// Convert a parsed HttpResponse into a Luma Response record value.
[[nodiscard]] Value build_http_response_record(const HttpResponse& resp) {
    auto rec = std::make_shared<RecordValue>();
    rec->type_name = "Response";
    rec->fields.emplace_back("status", Value{static_cast<std::int64_t>(resp.status_code)});
    rec->fields.emplace_back("reason", Value{resp.reason});
    rec->fields.emplace_back("body", Value{resp.body});

    auto headers_dict = std::make_shared<DictionaryValue>();
    // Pre-build the empty hash index so each set() below is O(1), keeping the
    // build O(n) rather than O(n^2).
    headers_dict->rebuild_index();

    for (const auto& [name, value] : resp.headers) {
        headers_dict->set(name, Value{value});
    }

    rec->fields.emplace_back("headers", Value{std::move(headers_dict)});

    return make_success_value(Value{std::move(rec)});
}

// Transport-level outcome of an HTTP exchange: either a successfully parsed
// response, or an error message. Shared by do_http_request (record-returning
// API) and do_http_fetch_text (plain-text API) so the connection, TLS,
// send and receive logic lives in exactly one place.
struct RawHttpOutcome {
    bool ok{false};
    HttpResponse resp{};
    std::string error{};

    [[nodiscard]] static RawHttpOutcome fail(std::string message) {
        return {.ok = false, .resp = {}, .error = std::move(message)};
    }

    [[nodiscard]] static RawHttpOutcome success(HttpResponse response) {
        return {.ok = true, .resp = std::move(response), .error = {}};
    }
};

[[nodiscard]] RawHttpOutcome
perform_http(const std::string& method, const std::string& url, const std::string& body,
             const std::vector<std::pair<std::string, std::string>>& extra_headers,
             int timeout_ms) {
    const auto parsed = parse_url(url);

    const bool is_https = (parsed.scheme == "https");

    const auto validation_err = validate_http_request(method, parsed, is_https, extra_headers);

    if (!validation_err.empty()) {
        return RawHttpOutcome::fail(validation_err);
    }

    const auto proxy = select_proxy(parsed.scheme, parsed.host);

    // Establish connection and send/receive.
    std::unique_ptr<Connection> conn;
    std::string conn_err;
    std::string request;

    if (proxy.has_value()) {
        // Preserve SSRF protection even when proxying: reject private/reserved
        // targets up front so a configured proxy cannot be used to reach
        // internal hosts the direct path would block.
        const auto target_ssrf_err = proxy_target_ssrf_error(parsed.host, parsed.port);

        if (!target_ssrf_err.empty()) {
            return RawHttpOutcome::fail(target_ssrf_err);
        }

        // Connect to the proxy. The private-address SSRF guard is intentionally
        // not applied here: proxies are frequently internal hosts the user has
        // explicitly opted into via the environment.
        const auto sock = connect_tcp_socket(proxy->host, proxy->port, timeout_ms,
                                             /*ssrf_check=*/false, conn_err);

        if (sock == invalid_socket_handle) {
            return RawHttpOutcome::fail(conn_err);
        }

#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS
        if (is_https) {
            SocketGuard guard{sock};

            // Tunnel to the origin, then run TLS end-to-end through the tunnel.
            const auto tunnel_err =
                proxy_connect_tunnel(sock, parsed.host, parsed.port, proxy->auth);

            if (!tunnel_err.empty()) {
                return RawHttpOutcome::fail(std::format("Http: {}", tunnel_err));
            }

            auto tls = std::make_unique<TlsConnection>();

            guard.release();

            const auto err = tls->handshake(sock, parsed.host);

            if (!err.empty()) {
                return RawHttpOutcome::fail(std::format("Http: {}", err));
            }

            conn = std::move(tls);
            request = build_http_request_string(method, parsed, body, extra_headers);
        } else
#endif // LUMA_HAS_TLS
        {
            conn = std::make_unique<PlainConnection>(sock);

            // Plain HTTP is forwarded with an absolute-URI request line; attach
            // proxy credentials if the proxy URL carried any.
            auto headers = extra_headers;

            if (!proxy->auth.empty()) {
                headers.emplace_back("Proxy-Authorization", proxy->auth);
            }

            request = build_http_request_string(method, parsed, body, headers,
                                                /*proxy_absolute=*/true);
        }
    } else {
        request = build_http_request_string(method, parsed, body, extra_headers);

#if defined(LUMA_HAS_TLS) && LUMA_HAS_TLS
        if (is_https) {
            const auto sock = connect_http_socket(parsed, timeout_ms, conn_err);

            if (sock == invalid_socket_handle) {
                return RawHttpOutcome::fail(conn_err);
            }

            // Guard the connected socket until ownership transfers to the TLS
            // connection, so it is not leaked if the allocation below throws
            // (mirrors the proxy HTTPS path above).
            SocketGuard guard{sock};

            // Hand the connected socket to TLS for the handshake.
            auto tls = std::make_unique<TlsConnection>();

            guard.release();

            const auto err = tls->handshake(sock, parsed.host);

            if (!err.empty()) {
                return RawHttpOutcome::fail(std::format("Http: {}", err));
            }

            conn = std::move(tls);
        } else
#endif // LUMA_HAS_TLS
        {
            const auto sock = connect_http_socket(parsed, timeout_ms, conn_err);

            if (sock == invalid_socket_handle) {
                return RawHttpOutcome::fail(conn_err);
            }

            conn = std::make_unique<PlainConnection>(sock);
        }
    }

    // Send request.
    if (!conn->send_data(request)) {
        return RawHttpOutcome::fail("Http: failed to send request");
    }

    // Read response.
    const auto raw_response = read_http_response(*conn);

    if (raw_response.empty()) {
        return RawHttpOutcome::fail("Http: empty response");
    }

    return RawHttpOutcome::success(parse_response(raw_response));
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// ─── Request execution ──────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

Value do_http_request(const std::string& method, const std::string& url, const std::string& body,
                      const std::vector<std::pair<std::string, std::string>>& extra_headers,
                      int timeout_ms, [[maybe_unused]] const SourceLocation& loc) {
    auto outcome = perform_http(method, url, body, extra_headers, timeout_ms);

    if (!outcome.ok) {
        return make_failure_value(outcome.error);
    }

    return build_http_response_record(outcome.resp);
}

HttpFetchResult
do_http_fetch_text(const std::string& method, const std::string& url, const std::string& body,
                   const std::vector<std::pair<std::string, std::string>>& extra_headers,
                   int timeout_ms, [[maybe_unused]] const SourceLocation& loc) {
    auto outcome = perform_http(method, url, body, extra_headers, timeout_ms);

    if (!outcome.ok) {
        return {.ok = false, .body = {}, .error = std::move(outcome.error)};
    }

    return {.ok = true, .body = std::move(outcome.resp.body), .error = {}};
}

} // namespace luma
