#ifndef LUMA_STDLIB_HTTP_URL_PARSER_HPP
#define LUMA_STDLIB_HTTP_URL_PARSER_HPP

// Lightweight URL parser for HTTP(S) requests.
//
// Extracts scheme, host, port, path, and query from a URL string.
// Header-only so it can be reused by any module that needs URL parsing.

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace luma {

constexpr int k_default_http_port = 80;
constexpr int k_default_https_port = 443;

// Returns the default port for the given scheme.
[[nodiscard]] constexpr int default_port_for_scheme(std::string_view scheme) noexcept {
    return (scheme == "https") ? k_default_https_port : k_default_http_port;
}

// Parse a decimal port string, falling back to `fallback` when the value is
// missing or malformed (empty, non-numeric, or out of range).
[[nodiscard]] inline int parse_port_or(const std::string& port_str, int fallback) noexcept {
    try {
        return std::stoi(port_str);
    } catch (const std::invalid_argument&) {
        return fallback;
    } catch (const std::out_of_range&) {
        return fallback;
    }
}

struct ParsedUrl {
    std::string scheme{};
    std::string host{};
    int port{k_default_http_port};
    std::string path{"/"};
    std::string query{};
};

[[nodiscard]] inline ParsedUrl parse_url(const std::string& url) {
    ParsedUrl result{};

    auto pos = url.find("://");

    if (pos != std::string::npos) {
        result.scheme = url.substr(0, pos);

        std::ranges::transform(result.scheme, result.scheme.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        pos += 3;
    } else {
        result.scheme = "http";

        pos = 0;
    }

    // Find path start.
    auto path_start = url.find('/', pos);

    std::string authority{};

    if (path_start != std::string::npos) {
        authority = url.substr(pos, path_start - pos);

        result.path = url.substr(path_start);
    } else {
        authority = url.substr(pos);

        result.path = "/";
    }

    // Separate query from path.
    auto query_start = result.path.find('?');

    if (query_start != std::string::npos) {
        result.query = result.path.substr(query_start + 1);
        result.path = result.path.substr(0, query_start);
    }

    // Parse host:port (IPv6-aware).
    const auto scheme_default_port = default_port_for_scheme(result.scheme);

    if (!authority.empty() && authority[0] == '[') {
        // IPv6 address in brackets: [2001:db8::1]:8080
        auto bracket_end = authority.find(']');

        if (bracket_end != std::string::npos) {
            result.host = authority.substr(0, bracket_end + 1);

            auto colon = authority.find(':', bracket_end + 1);

            if (colon != std::string::npos) {
                const auto port_str = authority.substr(colon + 1);

                result.port = parse_port_or(port_str, scheme_default_port);
            } else {
                result.port = scheme_default_port;
            }
        } else {
            result.host = authority;
            result.port = scheme_default_port;
        }
    } else {
        // Strip userinfo (user:pass@) before searching for the port colon.
        auto at_pos = authority.find('@');
        auto host_authority =
            (at_pos != std::string::npos) ? authority.substr(at_pos + 1) : authority;

        auto colon = host_authority.rfind(':');

        if (colon != std::string::npos) {
            result.host = host_authority.substr(0, colon);

            const auto port_str = host_authority.substr(colon + 1);

            result.port = parse_port_or(port_str, scheme_default_port);
        } else {
            result.host = std::string(host_authority);
            result.port = scheme_default_port;
        }
    }

    return result;
}

} // namespace luma

#endif // LUMA_STDLIB_HTTP_URL_PARSER_HPP
