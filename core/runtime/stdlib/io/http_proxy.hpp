#ifndef LUMA_RUNTIME_STDLIB_HTTP_PROXY_HPP
#define LUMA_RUNTIME_STDLIB_HTTP_PROXY_HPP

// Internal header — HTTP proxy resolution from the environment.
//
// Honours the de-facto standard proxy environment variables so requests
// succeed on networks that only permit outbound traffic through a proxy:
//   HTTPS_PROXY / https_proxy   — proxy for https:// targets
//   HTTP_PROXY  / http_proxy    — proxy for http:// targets
//   ALL_PROXY   / all_proxy     — fallback for either scheme
//   NO_PROXY    / no_proxy      — comma-separated hosts to reach directly
//
// Only HTTP proxies are supported (CONNECT tunnelling for https, absolute-URI
// forwarding for http); SOCKS proxies are intentionally ignored.

#include <optional>
#include <string>

#include "common/base64_codec.hpp"
#include "common/platform_utils.hpp"
#include "common/string_utils.hpp"
#include "runtime/stdlib/io/http_url_parser.hpp"

namespace luma {

struct ProxyTarget {
    std::string host{};
    int port{0};
    std::string auth{}; // full "Basic <base64>" header value, or empty
};

// Returns true when `host` matches any entry in a NO_PROXY-style list.
// Entries match the host exactly or as a domain suffix ("example.com" and
// ".example.com" both match "api.example.com"); "*" matches everything.
[[nodiscard]] inline bool host_in_no_proxy(const std::string& host, const std::string& list) {
    const auto h = to_lower_copy(host);

    std::size_t start{0};

    while (start <= list.size()) {
        const auto comma = list.find(',', start);
        const auto raw =
            list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        auto token = to_lower_copy(trim(raw));

        if (!token.empty()) {
            if (token == "*") {
                return true;
            }

            if (token.front() == '.') {
                token.erase(0, 1);
            }

            if (h == token) {
                return true;
            }

            if (h.size() > token.size()) {
                const auto suffix = "." + token;

                if (h.compare(h.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    return true;
                }
            }
        }

        if (comma == std::string::npos) {
            break;
        }

        start = comma + 1;
    }

    return false;
}

// Parse a proxy URL such as "http://user:pass@host:8880" into host/port/auth.
// Returns nullopt for empty input or unsupported (e.g. SOCKS) proxy schemes.
[[nodiscard]] inline std::optional<ProxyTarget> parse_proxy_url(const std::string& raw_in) {
    std::string raw = trim(raw_in);

    if (raw.empty()) {
        return std::nullopt;
    }

    std::string scheme = "http";
    const auto scheme_pos = raw.find("://");

    if (scheme_pos != std::string::npos) {
        scheme = to_lower_copy(raw.substr(0, scheme_pos));
        raw = raw.substr(scheme_pos + 3);
    }

    // Only HTTP-style proxies are supported.
    if (scheme.rfind("socks", 0) == 0) {
        return std::nullopt;
    }

    // Drop any trailing path component.
    const auto slash = raw.find('/');

    if (slash != std::string::npos) {
        raw = raw.substr(0, slash);
    }

    // Split optional userinfo ("user:pass@").
    std::string userinfo;
    const auto at = raw.rfind('@');

    if (at != std::string::npos) {
        userinfo = raw.substr(0, at);
        raw = raw.substr(at + 1);
    }

    if (raw.empty()) {
        return std::nullopt;
    }

    ProxyTarget target{};

    if (raw.front() == '[') {
        // Bracketed IPv6 literal: [::1]:8880
        const auto rb = raw.find(']');

        if (rb == std::string::npos) {
            return std::nullopt;
        }

        target.host = raw.substr(0, rb + 1);

        const auto colon = raw.find(':', rb + 1);

        if (colon != std::string::npos) {
            target.port = parse_port_or(raw.substr(colon + 1), 0);
        }
    } else {
        const auto colon = raw.rfind(':');

        if (colon != std::string::npos) {
            target.host = raw.substr(0, colon);
            target.port = parse_port_or(raw.substr(colon + 1), 0);
        } else {
            target.host = raw;
        }
    }

    if (target.host.empty()) {
        return std::nullopt;
    }

    if (target.port <= 0) {
        target.port = default_port_for_scheme(scheme);
    }

    if (!userinfo.empty()) {
        target.auth = "Basic " + base64_encode(userinfo);
    }

    return target;
}

// Select the proxy (if any) to use for a request to the given scheme/host,
// consulting NO_PROXY and the scheme-specific / fallback proxy variables.
[[nodiscard]] inline std::optional<ProxyTarget> select_proxy(const std::string& scheme,
                                                             const std::string& host) {
    auto no_proxy = safe_getenv("NO_PROXY");

    if (!no_proxy) {
        no_proxy = safe_getenv("no_proxy");
    }

    if (no_proxy && host_in_no_proxy(host, *no_proxy)) {
        return std::nullopt;
    }

    std::optional<std::string> proxy_url;

    if (scheme == "https") {
        proxy_url = safe_getenv("HTTPS_PROXY");

        if (!proxy_url) {
            proxy_url = safe_getenv("https_proxy");
        }
    } else {
        proxy_url = safe_getenv("HTTP_PROXY");

        if (!proxy_url) {
            proxy_url = safe_getenv("http_proxy");
        }
    }

    if (!proxy_url) {
        proxy_url = safe_getenv("ALL_PROXY");
    }

    if (!proxy_url) {
        proxy_url = safe_getenv("all_proxy");
    }

    if (!proxy_url || proxy_url->empty()) {
        return std::nullopt;
    }

    return parse_proxy_url(*proxy_url);
}

} // namespace luma

#endif // LUMA_RUNTIME_STDLIB_HTTP_PROXY_HPP
