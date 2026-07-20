// http_security.cpp — HTTP request security validation utilities.
//
// Extracted from http_module.cpp.  Provides SSRF prevention (private
// address blocking), CRLF injection detection, and hostname length
// validation.

#include "runtime/stdlib/io/http_security.hpp"

#include <format>
#include <string>
#include <utility>
#include <vector>

#include "common/resource_limits.hpp"
#include "runtime/stdlib/io/network_security.hpp"

namespace luma {

std::string validate_request_line_security(const std::string& method, const std::string& host,
                                           const std::string& path, const std::string& query) {
    for (const auto& [label, val] : {std::pair<const char*, const std::string&>{"method", method},
                                     {"host", host},
                                     {"path", path},
                                     {"query", query}}) {
        if (val.find('\r') != std::string::npos || val.find('\n') != std::string::npos) {
            return std::format("Http: {} contains illegal CR/LF characters", label);
        }
    }

    return {};
}

std::string
validate_headers_security(const std::vector<std::pair<std::string, std::string>>& headers) {
    for (const auto& [name, value] : headers) {
        if (name.find('\r') != std::string::npos || name.find('\n') != std::string::npos ||
            value.find('\r') != std::string::npos || value.find('\n') != std::string::npos) {
            return std::format("Http: header '{}' contains illegal CR/LF characters", name);
        }
    }

    return {};
}

std::string validate_hostname_length(const std::string& host) {
    if (host.size() > ResourceLimits::max_hostname_length) {
        return std::string{"Http: hostname too long"};
    }

    return {};
}

std::string validate_resolved_address(const struct addrinfo* info, const std::string& host) {
    if (luma::network::is_private_address(info)) {
        return std::format("Http: connection to private/reserved address '{}' is blocked", host);
    }

    return {};
}

} // namespace luma
