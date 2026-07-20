// Http module — response reading and parsing.
// Split from http_module_request.cpp for readability.

#include "runtime/stdlib/io/http_module_response.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "common/resource_limits.hpp"
#include "common/string_utils.hpp"
#include "runtime/stdlib/io/http_module_connection.hpp"
#include "runtime/stdlib/io/http_module_request.hpp"

namespace luma {

namespace {

// Maximum raw HTTP response size accepted before the reader bails out.  Reads the
// central limit on each call so LUMA_LIMIT_MAX_HTTP_RESPONSE_SIZE overrides apply.
[[nodiscard]] inline std::size_t max_http_resp_size() {
    return ResourceLimits::max_http_response_size;
}

// Fixed read buffer size for streaming an HTTP response off the socket.
constexpr std::size_t k_http_response_buffer_size{4096};

// Stream bytes from `conn` into `raw` in fixed-size chunks until `should_stop(raw)`
// reports the read is complete, the peer closes the connection (recv <= 0), or the
// response size cap is reached.  Centralises the recv → append → cap loop shared by
// every framing-specific reader below; each caller supplies its own stop predicate.
template <typename StopPredicate>
void read_response_chunks(Connection& conn, std::string& raw, StopPredicate should_stop) {
    std::array<char, k_http_response_buffer_size> buf{};

    while (!should_stop(raw)) {
        if (raw.size() >= max_http_resp_size()) {
            break;
        }

        const auto n = conn.recv_data(buf.data(), buf.size());

        if (n <= 0) {
            break;
        }

        raw.append(buf.data(), static_cast<std::size_t>(n));
    }
}

// Parse a decimal HTTP status code, returning nullopt when it is malformed
// (empty, non-numeric, or out of range).
[[nodiscard]] std::optional<int> parse_status_code(const std::string& code_str) {
    try {
        return std::stoi(code_str);
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

// Transfer framing derived from the response headers.
struct TransferFraming {
    std::size_t content_length{0};
    bool has_content_length{false};
    bool is_chunked{false};
};

// Read from the connection until the header terminator (CRLF CRLF) appears or
// the size cap is reached.
void read_until_headers_complete(Connection& conn, std::string& raw) {
    read_response_chunks(conn, raw, [](const std::string& data) {
        return data.find("\r\n\r\n") != std::string::npos;
    });
}

// Parse Content-Length / Transfer-Encoding from the header block.
[[nodiscard]] TransferFraming parse_transfer_framing(const std::string& headers_str) {
    TransferFraming framing{};

    std::istringstream hstream{headers_str};
    std::string line{};

    while (std::getline(hstream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Match header names case-insensitively without allocating a lowercased
        // copy of the entire line each iteration.
        constexpr std::string_view content_length_name{"content-length:"};

        if (case_insensitive_equal(std::string_view{line}.substr(0, content_length_name.size()),
                                   content_length_name)) {
            const auto val = line.substr(content_length_name.size());

            auto trimmed = val;
            auto s = trimmed.find_first_not_of(' ');

            if (s != std::string::npos) {
                trimmed = trimmed.substr(s);
            }

            try {
                framing.content_length = std::stoull(trimmed);
                framing.has_content_length = true;
            } catch (const std::invalid_argument&) { // NOLINT(bugprone-empty-catch)
                // Malformed Content-Length -- ignore and fall back to other methods.
            } catch (const std::out_of_range&) { // NOLINT(bugprone-empty-catch)
                // Malformed Content-Length -- ignore and fall back to other methods.
            }
        }

        if (case_insensitive_contains(line, "transfer-encoding:") &&
            case_insensitive_contains(line, "chunked")) {
            framing.is_chunked = true;
        }
    }

    return framing;
}

// Read remaining body bytes for a Content-Length framed response.
void read_body_by_content_length(Connection& conn, std::string& raw, std::size_t body_start,
                                 std::size_t content_length) {
    read_response_chunks(conn, raw, [body_start, content_length](const std::string& data) {
        return data.size() - body_start >= content_length;
    });
}

// Read a chunked-transfer body until the terminal chunk, then decode it in
// place, replacing the chunked bytes in `raw` with the decoded body.
void read_and_decode_chunked_body(Connection& conn, std::string& raw, std::size_t body_start) {
    // Read chunked transfer encoding until we see the terminal "0\r\n".
    read_response_chunks(conn, raw, [body_start](const std::string& data) {
        return data.find("0\r\n\r\n", body_start) != std::string::npos ||
               data.find("\r\n0\r\n", body_start) != std::string::npos;
    });

    // Decode chunked body.
    std::string decoded_body{};

    auto chunk_pos = body_start;

    while (chunk_pos < raw.size()) {
        auto chunk_end = raw.find("\r\n", chunk_pos);

        if (chunk_end == std::string::npos) {
            break;
        }

        const auto size_str = raw.substr(chunk_pos, chunk_end - chunk_pos);

        std::size_t chunk_size{0};

        try {
            chunk_size = std::stoull(size_str, nullptr, 16);
        } catch (const std::invalid_argument&) {
            break;
        } catch (const std::out_of_range&) {
            break;
        }

        if (chunk_size == 0) {
            break;
        }

        if (chunk_size > max_http_resp_size() ||
            decoded_body.size() + chunk_size > max_http_resp_size()) {
            break;
        }

        auto data_start = chunk_end + 2;

        if (data_start + chunk_size > raw.size()) {
            break;
        }

        decoded_body.append(raw, data_start, chunk_size);

        chunk_pos = data_start + chunk_size + 2; // skip \r\n
    }

    // Replace body in raw response.  Truncate the chunked bytes in place and
    // append the decoded body, avoiding the extra full-body copy that
    // raw.substr(0, body_start) + decoded_body would allocate.
    raw.resize(body_start);
    raw += decoded_body;
}

// Fallback when neither Content-Length nor chunked framing is present: read
// until the peer closes the connection.
void read_body_until_close(Connection& conn, std::string& raw) {
    read_response_chunks(conn, raw, [](const std::string&) { return false; });
}

} // namespace

[[nodiscard]] HttpResponse parse_response(const std::string& raw) {
    HttpResponse resp{};

    // Find end of status line
    auto line_end = raw.find("\r\n");

    if (line_end == std::string::npos) {
        return resp;
    }

    const auto status_line = raw.substr(0, line_end);

    // Parse "HTTP/1.x STATUS REASON"
    auto sp1 = status_line.find(' ');

    if (sp1 == std::string::npos) {
        return resp;
    }

    auto sp2 = status_line.find(' ', sp1 + 1);

    // Status line is "HTTP/1.x STATUS [REASON]"; the reason phrase is optional.
    const auto code_str = (sp2 == std::string::npos) ? status_line.substr(sp1 + 1)
                                                     : status_line.substr(sp1 + 1, sp2 - sp1 - 1);

    const auto code = parse_status_code(code_str);

    if (!code) {
        return resp;
    }

    resp.status_code = *code;

    if (sp2 != std::string::npos) {
        resp.reason = status_line.substr(sp2 + 1);
    }

    // Parse headers
    auto pos = line_end + 2;

    const std::size_t max_header_count = ResourceLimits::max_http_header_count;
    const std::size_t max_header_value_size = ResourceLimits::max_http_header_value_size;

    while (pos < raw.size()) {
        auto next_end = raw.find("\r\n", pos);

        if (next_end == std::string::npos || next_end == pos) {
            pos = (next_end == std::string::npos) ? raw.size() : next_end + 2;

            break;
        }

        const auto header_line = raw.substr(pos, next_end - pos);

        auto colon = header_line.find(':');

        if (colon != std::string::npos) {
            auto name = header_line.substr(0, colon);
            auto value = header_line.substr(colon + 1);

            // Trim whitespace from value
            auto vstart = value.find_first_not_of(' ');

            if (vstart != std::string::npos) {
                value = value.substr(vstart);
            }

            // Enforce per-header value length limit.
            if (value.size() > max_header_value_size) {
                value = value.substr(0, max_header_value_size);
            }

            // Lowercase header name
            name = to_lower_copy(name);

            resp.headers.emplace_back(std::move(name), std::move(value));

            // Enforce maximum header count to prevent memory exhaustion.
            if (resp.headers.size() >= max_header_count) {
                pos = next_end + 2;

                break;
            }
        }

        pos = next_end + 2;
    }

    // Body is everything after headers
    if (pos < raw.size()) {
        resp.body = raw.substr(pos);
    }

    return resp;
}

// Read the full HTTP response, handling Content-Length and chunked transfer.
[[nodiscard]] std::string read_http_response(Connection& conn) {
    std::string raw{};

    read_until_headers_complete(conn, raw);

    const auto header_end = raw.find("\r\n\r\n");

    if (header_end == std::string::npos) {
        return raw;
    }

    const auto headers_str = raw.substr(0, header_end);
    const auto body_start = header_end + 4;

    const auto framing = parse_transfer_framing(headers_str);

    if (framing.has_content_length) {
        read_body_by_content_length(conn, raw, body_start, framing.content_length);
    } else if (framing.is_chunked) {
        read_and_decode_chunked_body(conn, raw, body_start);
    } else {
        read_body_until_close(conn, raw);
    }

    return raw;
}

} // namespace luma
