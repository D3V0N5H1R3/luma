#ifndef LUMA_RUNTIME_STDLIB_HTTP_MODULE_REQUEST_HPP
#define LUMA_RUNTIME_STDLIB_HTTP_MODULE_REQUEST_HPP

// Internal header — HTTP request building, response parsing, and execution.
// Extracted from http_module.cpp for readability.

#include <string>
#include <utility>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// ─── Response representation ─────────────────────────────────────────────────

struct HttpResponse {
    int status_code{0};
    std::string reason{};
    std::string body{};
    std::vector<std::pair<std::string, std::string>> headers{};
};

// ─── Header extraction ──────────────────────────────────────────────────────

// Extract HTTP headers from a Luma dictionary value.
[[nodiscard]] std::vector<std::pair<std::string, std::string>>
extract_headers(const DictionaryValue& dict, const SourceLocation& loc);

// ─── Request execution ──────────────────────────────────────────────────────

// Perform a full HTTP request and return a result<Response> value.
[[nodiscard]] Value
do_http_request(const std::string& method, const std::string& url, const std::string& body,
                const std::vector<std::pair<std::string, std::string>>& extra_headers,
                int timeout_ms, const SourceLocation& loc);

// Opt-in typed-error counterpart of do_http_request: on transport failure the
// result carries a typed Http.Error choice (result<Http.Response, Http.Error>)
// classifying the failure category, rather than an opaque string message.  A
// successful exchange returns the same Http.Response record as do_http_request.
[[nodiscard]] Value
do_http_request_typed(const std::string& method, const std::string& url, const std::string& body,
                      const std::vector<std::pair<std::string, std::string>>& extra_headers,
                      int timeout_ms, const SourceLocation& loc);

// ─── Plain-text request execution ───────────────────────────────────────────

// Plain-data result of an HTTP request: the response body on transport success,
// or an error message. Unlike do_http_request this builds no Luma Value, so it
// is suitable for callers that need the raw body without constructing
// interpreter objects.
struct HttpFetchResult {
    bool ok{false};
    int status_code{0};
    std::string body{};
    std::vector<std::pair<std::string, std::string>> headers{};
    std::string error{};
};

// Perform a full HTTP request and return the response body as plain text.
// On transport success the body is returned regardless of HTTP status code
// (mirroring the browser fetch().text() semantics); on failure an error
// message is returned.
[[nodiscard]] HttpFetchResult
do_http_fetch_text(const std::string& method, const std::string& url, const std::string& body,
                   const std::vector<std::pair<std::string, std::string>>& extra_headers,
                   int timeout_ms, const SourceLocation& loc);

} // namespace luma

#endif // LUMA_RUNTIME_STDLIB_HTTP_MODULE_REQUEST_HPP
