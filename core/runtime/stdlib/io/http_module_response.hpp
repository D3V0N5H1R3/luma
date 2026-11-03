#ifndef LUMA_RUNTIME_STDLIB_HTTP_MODULE_RESPONSE_HPP
#define LUMA_RUNTIME_STDLIB_HTTP_MODULE_RESPONSE_HPP

// Internal header — HTTP response reading and parsing.
// Extracted from http_module_request.cpp so that response framing/parsing lives
// apart from request execution (http_module_request.cpp) and TLS session
// management (http_module_tls.cpp).

#include <cstddef>
#include <string>
#include <string_view>

#include "runtime/stdlib/io/http_module_connection.hpp"
#include "runtime/stdlib/io/http_module_request.hpp"

namespace luma {

// Incremental decoder for a chunked transfer-encoded body (RFC 9112 §7.1).
// feed() is handed the WHOLE raw response buffer each time more bytes arrive
// and resumes from where the previous call stopped, appending each complete
// chunk to body() — so decoding a response is O(n) overall instead of
// re-parsing every chunk after every recv.  Chunk sizes are parsed with an
// overflow check and bounded by `max_body_size`, and the parse position
// strictly advances, so a hostile size line can neither wrap the position nor
// loop forever.
class ChunkedBodyDecoder {
public:
    enum class Status {
        NeedMore,  // The terminal chunk has not arrived yet.
        Complete,  // The terminal (zero-size) chunk was reached; body() is final.
        Malformed, // An invalid chunk-size line or a missing chunk terminator.
        TooLarge,  // The decoded body would exceed `max_body_size`.
    };

    ChunkedBodyDecoder(std::size_t body_start, std::size_t max_body_size);

    // Decode as much of `data` (the raw response so far) as possible.  Once the
    // status leaves NeedMore it is final and further calls are no-ops.
    Status feed(std::string_view data);

    [[nodiscard]] Status status() const noexcept {
        return status_;
    }

    [[nodiscard]] const std::string& body() const noexcept {
        return body_;
    }

private:
    std::size_t pos_;
    std::size_t max_body_size_;
    std::string body_{};
    Status status_{Status::NeedMore};
};

// Outcome of read_http_response: the raw response (headers + decoded body) and,
// when the response could not be read completely, why.  A truncated, oversized
// or malformed response is an error, never a silently shortened success.
struct HttpReadResult {
    std::string raw{};
    std::string error{}; // Empty when the response was read completely.

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

// Read a full HTTP response from the connection, handling Content-Length,
// chunked transfer encoding (which takes precedence over Content-Length), and
// connection-close framing.  `request_method` is the method of the request the
// response answers: per RFC 9112 §6.3 rule 1, a response to HEAD and every
// 1xx / 204 / 304 response ends at the header block whatever its framing
// headers say (a HEAD response's Content-Length describes the body a GET would
// have returned).  Interim 1xx responses (other than 101 Switching Protocols)
// are skipped and the final response that follows is read instead.
[[nodiscard]] HttpReadResult read_http_response(Connection& conn, std::string_view request_method);

// Parse a raw HTTP response (status line, headers, body) into an HttpResponse.
[[nodiscard]] HttpResponse parse_response(const std::string& raw);

} // namespace luma

#endif // LUMA_RUNTIME_STDLIB_HTTP_MODULE_RESPONSE_HPP
