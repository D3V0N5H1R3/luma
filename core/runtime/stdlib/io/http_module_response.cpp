// Http module — response reading and parsing.
// Split from http_module_request.cpp for readability.

#include "runtime/stdlib/io/http_module_response.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

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

// Why read_response_chunks stopped.
enum class ReadStop {
    Done,         // The stop predicate reported the read complete.
    Closed,       // The peer closed the connection, or recv failed / timed out.
    LimitReached, // The raw response hit max_http_resp_size().
};

// Stream bytes from `conn` into `raw` in fixed-size chunks until `should_stop(raw)`
// reports the read is complete, the peer closes the connection (recv <= 0), or the
// response size cap is reached, and report which of the three happened so the
// caller can tell a complete response from a truncated one.  Centralises the
// recv → append → cap loop shared by every framing-specific reader below; each
// caller supplies its own stop predicate.
template <typename StopPredicate>
[[nodiscard]] ReadStop read_response_chunks(Connection& conn, std::string& raw,
                                            StopPredicate should_stop) {
    std::array<char, k_http_response_buffer_size> buf{};

    while (!should_stop(raw)) {
        if (raw.size() >= max_http_resp_size()) {
            return ReadStop::LimitReached;
        }

        const auto n = conn.recv_data(buf.data(), buf.size());

        if (n <= 0) {
            return ReadStop::Closed;
        }

        raw.append(buf.data(), static_cast<std::size_t>(n));
    }

    return ReadStop::Done;
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

// Status code from the status line ("HTTP/1.x STATUS [REASON]") at the start of
// `headers`, or nullopt when it is malformed.
[[nodiscard]] std::optional<int> status_code_of(const std::string& headers) {
    const auto status_line = headers.substr(0, headers.find("\r\n"));
    const auto sp1 = status_line.find(' ');

    if (sp1 == std::string::npos) {
        return std::nullopt;
    }

    const auto sp2 = status_line.find(' ', sp1 + 1);

    return parse_status_code(status_line.substr(
        sp1 + 1, (sp2 == std::string::npos) ? std::string::npos : sp2 - sp1 - 1));
}

constexpr int k_status_informational_min{100};
constexpr int k_status_informational_max{199};
constexpr int k_status_switching_protocols{101};
constexpr int k_status_no_content{204};
constexpr int k_status_not_modified{304};

[[nodiscard]] bool is_informational(std::optional<int> status) {
    return status && *status >= k_status_informational_min && *status <= k_status_informational_max;
}

// An interim response precedes the final one on the same connection.  101
// Switching Protocols is final: the connection stops speaking HTTP after it.
[[nodiscard]] bool is_interim_status(std::optional<int> status) {
    return is_informational(status) && *status != k_status_switching_protocols;
}

// RFC 9112 §6.3 rule 1: a response to HEAD, and any 1xx, 204, or 304 response,
// ends at the empty line after its header fields.
[[nodiscard]] bool has_no_body(std::string_view request_method, std::optional<int> status) {
    if (case_insensitive_equal(request_method, "HEAD")) {
        return true;
    }

    return is_informational(status) ||
           (status && (*status == k_status_no_content || *status == k_status_not_modified));
}

// Read from the connection until the header terminator (CRLF CRLF) appears or
// the read stops.  The search resumes where the previous one left off (minus the
// three bytes a terminator split across two reads could start with), so header
// reading stays linear in the response size.
[[nodiscard]] ReadStop read_until_headers_complete(Connection& conn, std::string& raw) {
    std::size_t search_from{0};

    return read_response_chunks(conn, raw, [&search_from](const std::string& data) {
        if (data.find("\r\n\r\n", search_from) != std::string::npos) {
            return true;
        }

        search_from = (data.size() > 3) ? data.size() - 3 : 0;

        return false;
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

[[nodiscard]] std::string too_large_error() {
    return std::format("response exceeds the maximum size of {} bytes", max_http_resp_size());
}

// Read the remaining body bytes of a Content-Length framed response.  Returns an
// error message (empty on success) when the declared length exceeds the size
// cap or the connection ends before every declared byte arrived.
[[nodiscard]] std::string read_body_by_content_length(Connection& conn, std::string& raw,
                                                      std::size_t body_start,
                                                      std::size_t content_length) {
    if (content_length > max_http_resp_size() - std::min(body_start, max_http_resp_size())) {
        return too_large_error();
    }

    const auto stop =
        read_response_chunks(conn, raw, [body_start, content_length](const std::string& data) {
            return data.size() - body_start >= content_length;
        });

    if (stop != ReadStop::Done) {
        return std::format("connection closed after {} of {} body bytes", raw.size() - body_start,
                           content_length);
    }

    // Drop anything the peer sent past the declared length.
    raw.resize(body_start + content_length);

    return {};
}

// Read a chunked-transfer body until the terminal chunk, decoding it
// incrementally as data arrives, then replace the chunked bytes in `raw` with
// the decoded body.  Returns an error message (empty on success) for a
// malformed or oversized chunk stream, or one the peer cut short.
[[nodiscard]] std::string read_and_decode_chunked_body(Connection& conn, std::string& raw,
                                                       std::size_t body_start) {
    ChunkedBodyDecoder decoder{body_start, max_http_resp_size()};

    const auto stop = read_response_chunks(conn, raw, [&decoder](const std::string& data) {
        return decoder.feed(data) != ChunkedBodyDecoder::Status::NeedMore;
    });

    switch (decoder.status()) {
        case ChunkedBodyDecoder::Status::Complete:
            break;
        case ChunkedBodyDecoder::Status::Malformed:
            return "malformed chunked transfer encoding";
        case ChunkedBodyDecoder::Status::TooLarge:
            return too_large_error();
        case ChunkedBodyDecoder::Status::NeedMore:
            return (stop == ReadStop::LimitReached)
                       ? too_large_error()
                       : std::string{"connection closed before the chunked body was complete"};
    }

    // Replace body in raw response.  Truncate the chunked bytes in place and
    // append the decoded body, avoiding the extra full-body copy that
    // raw.substr(0, body_start) + decoded_body would allocate.
    raw.resize(body_start);
    raw += decoder.body();

    return {};
}

// Fallback when neither Content-Length nor chunked framing is present: read
// until the peer closes the connection.  Hitting the size cap first is an
// error, not a silently truncated body.
[[nodiscard]] std::string read_body_until_close(Connection& conn, std::string& raw) {
    const auto stop = read_response_chunks(conn, raw, [](const std::string&) { return false; });

    return (stop == ReadStop::LimitReached) ? too_large_error() : std::string{};
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

// ─── ChunkedBodyDecoder ───────────────────────────────────────────────────────

ChunkedBodyDecoder::ChunkedBodyDecoder(std::size_t body_start, std::size_t max_body_size)
    : pos_{body_start}, max_body_size_{max_body_size} {}

ChunkedBodyDecoder::Status ChunkedBodyDecoder::feed(std::string_view data) {
    // Upper bound on a chunk-size line (hex digits plus chunk extensions); a
    // longer unterminated line is garbage, not a header still in transit.
    constexpr std::size_t k_max_chunk_header_line{4096};

    while (status_ == Status::NeedMore) {
        const auto line_end = data.find("\r\n", pos_);

        if (line_end == std::string_view::npos) {
            if (data.size() - std::min(pos_, data.size()) > k_max_chunk_header_line) {
                status_ = Status::Malformed;
            }

            break; // Incomplete chunk header — need more data.
        }

        // chunk-size [ ";" chunk-ext ] — only the hex size matters here.
        auto size_field = data.substr(pos_, line_end - pos_);

        if (const auto ext = size_field.find(';'); ext != std::string_view::npos) {
            size_field = size_field.substr(0, ext);
        }

        while (!size_field.empty() && (size_field.back() == ' ' || size_field.back() == '\t')) {
            size_field.remove_suffix(1);
        }

        std::uint64_t chunk_size{0};
        const auto [ptr, ec] = std::from_chars(
            size_field.data(), size_field.data() + size_field.size(), chunk_size, 16);

        // from_chars rejects empty input and a sign, and reports overflow as
        // result_out_of_range instead of wrapping (the old stoull-based scan
        // let a 2^64-20 size wrap `pos` back onto itself and spin forever).
        if (size_field.empty() || ec != std::errc{} ||
            ptr != size_field.data() + size_field.size()) {
            status_ = Status::Malformed;

            break;
        }

        if (chunk_size == 0) {
            status_ = Status::Complete; // Terminal chunk; trailers are not needed.

            break;
        }

        if (chunk_size > max_body_size_ - body_.size()) {
            status_ = Status::TooLarge;

            break;
        }

        const auto data_start = line_end + 2;
        const auto size = static_cast<std::size_t>(chunk_size);

        // Wait until the whole chunk and its trailing CRLF have arrived.
        if (data.size() < data_start || data.size() - data_start < size + 2) {
            break;
        }

        if (data.substr(data_start + size, 2) != "\r\n") {
            status_ = Status::Malformed;

            break;
        }

        body_.append(data.substr(data_start, size));

        // Strictly advances: size >= 1 plus at least five framing bytes.
        pos_ = data_start + size + 2;
    }

    return status_;
}

// Read the full HTTP response, handling Content-Length and chunked transfer.
[[nodiscard]] HttpReadResult read_http_response(Connection& conn, std::string_view request_method) {
    HttpReadResult result{};
    std::string& raw = result.raw;

    // A server may send any number of interim 1xx responses (100 Continue, 103
    // Early Hints) before the final one; bound how many are skipped so a
    // hostile peer cannot keep the reader busy forever with tiny ones.
    constexpr int k_max_interim_responses{32};

    for (int interim = 0;; ++interim) {
        const auto header_stop = read_until_headers_complete(conn, raw);

        const auto header_end = raw.find("\r\n\r\n");

        if (header_end == std::string::npos) {
            if (header_stop == ReadStop::LimitReached) {
                result.error = too_large_error();
            } else if (!raw.empty()) {
                result.error = "connection closed before the response headers were complete";
            }

            return result;
        }

        const auto headers_str = raw.substr(0, header_end);
        const auto body_start = header_end + 4;
        const auto status = status_code_of(headers_str);

        if (is_interim_status(status)) {
            if (interim + 1 >= k_max_interim_responses) {
                result.error = "too many interim (1xx) responses";

                return result;
            }

            // Discard the interim response; the final one follows directly.
            raw.erase(0, body_start);

            continue;
        }

        // RFC 9112 §6.3 rule 1: these responses never carry a body, whatever
        // Content-Length / Transfer-Encoding claim.
        if (has_no_body(request_method, status)) {
            raw.resize(body_start);

            return result;
        }

        const auto framing = parse_transfer_framing(headers_str);

        // RFC 9112 §6.3: Transfer-Encoding overrides Content-Length when both
        // are present (a sender must not send both; honouring Content-Length
        // there is a request-smuggling / truncation hazard).
        if (framing.is_chunked) {
            result.error = read_and_decode_chunked_body(conn, raw, body_start);
        } else if (framing.has_content_length) {
            result.error =
                read_body_by_content_length(conn, raw, body_start, framing.content_length);
        } else {
            result.error = read_body_until_close(conn, raw);
        }

        return result;
    }
}

} // namespace luma
