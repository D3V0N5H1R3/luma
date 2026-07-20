#ifndef LUMA_RUNTIME_STDLIB_HTTP_SECURITY_HPP
#define LUMA_RUNTIME_STDLIB_HTTP_SECURITY_HPP

// Internal header — HTTP request security validation utilities.
// Extracted from http_module.cpp for readability.

#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace luma {

struct ParsedUrl;

// Validate that the HTTP method, host, path, and query do not contain
// CR/LF characters (prevents request-line injection / CRLF injection).
// Returns an error message on failure, or an empty string on success.
[[nodiscard]] std::string validate_request_line_security(const std::string& method,
                                                         const std::string& host,
                                                         const std::string& path,
                                                         const std::string& query);

// Validate that header names and values do not contain CR/LF characters
// (prevents HTTP header injection / request smuggling).
// Returns an error message on failure, or an empty string on success.
[[nodiscard]] std::string
validate_headers_security(const std::vector<std::pair<std::string, std::string>>& headers);

// Validate that the hostname is not too long.
// Returns an error message on failure, or an empty string on success.
[[nodiscard]] std::string validate_hostname_length(const std::string& host);

// Check whether any of the resolved addresses are in a private/reserved
// range (SSRF prevention).  Returns an error message if blocked, or an
// empty string if the address is safe.
[[nodiscard]] std::string validate_resolved_address(const struct addrinfo* info,
                                                    const std::string& host);

} // namespace luma

#endif // LUMA_RUNTIME_STDLIB_HTTP_SECURITY_HPP
