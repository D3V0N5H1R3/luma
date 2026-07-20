#ifndef LUMA_RUNTIME_STDLIB_HTTP_MODULE_RESPONSE_HPP
#define LUMA_RUNTIME_STDLIB_HTTP_MODULE_RESPONSE_HPP

// Internal header — HTTP response reading and parsing.
// Extracted from http_module_request.cpp so that response framing/parsing lives
// apart from request execution (http_module_request.cpp) and TLS session
// management (http_module_tls.cpp).

#include <string>

#include "runtime/stdlib/io/http_module_connection.hpp"
#include "runtime/stdlib/io/http_module_request.hpp"

namespace luma {

// Read a full HTTP response from the connection, handling Content-Length,
// chunked transfer encoding, and connection-close framing.
[[nodiscard]] std::string read_http_response(Connection& conn);

// Parse a raw HTTP response (status line, headers, body) into an HttpResponse.
[[nodiscard]] HttpResponse parse_response(const std::string& raw);

} // namespace luma

#endif // LUMA_RUNTIME_STDLIB_HTTP_MODULE_RESPONSE_HPP
