// ═══════════════════════════════════════════════════════════════════════
// Transport — Error Handling Strategy
// ═══════════════════════════════════════════════════════════════════════
//
// This file implements Content-Length-framed JSON message transport
// shared by the LSP language server and the DAP debug adapter.  Errors
// during the read → parse → dispatch cycle are handled as follows:
//
//   Exceptions (thrown to the caller):
//     • ConnectionClosed — fatal I/O failure (EOF mid-message, broken
//       pipe).  Propagated immediately; the server loop must exit.
//     • ResyncError from resync_to_next_message() — the iteration cap
//       was reached without finding a valid header; the stream is
//       unrecoverably corrupt.  A distinct ParseError subtype so the
//       classifier can treat it as fatal (see error_recovery.hpp).
//
//   Error callback (report_error) — read_message() returns std::nullopt
//   with had_recoverable_read_error() set, so the caller can continue:
//     • Oversized message — the Content-Length is known, so exactly that
//       many body bytes are discarded and the stream stays aligned on the
//       next frame.  Line-based resync cannot be used here: a JSON body
//       has no newline, so it would be read as one over-long "header".
//     • Invalid JSON body — the body was consumed in full, so the stream
//       is already aligned on the next frame and no resync is needed.
//     • Malformed headers (missing Content-Length, over-long header line)
//       — the frame boundary is unknown, so the transport resyncs.
//
//   Resync (resync_to_next_message):
//     • Scans forward discarding input until the next Content-Length
//       header is found (up to max_resync_iterations lines, over-long
//       lines included).  The found header is buffered for the next
//       read_message() call.
//
// See error_recovery.hpp for the full error classification policy,
// consecutive-error tracking, and the shutdown strategy used by the
// LSP and DAP message loops.
// ═══════════════════════════════════════════════════════════════════════

#include "protocol/transport.hpp"

#include <algorithm>
#include <format>
#include <iostream>
#include <utility>

#include "protocol/message_frame.hpp"
#include "protocol/transport_exceptions.hpp"

namespace luma::protocol {

// ─── Default error handler ───

namespace {

void default_error_callback(std::string_view message) {
    std::cerr << message;
}

} // namespace

// ─── Transport ───

Transport::Transport(TransportLimits limits, ErrorCallback error_callback)
    : limits_{limits}, error_callback_{std::move(error_callback)} {}

void Transport::set_error_callback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

void Transport::report_error(std::string_view message) {
    if (error_callback_) {
        error_callback_(message);
    } else {
        default_error_callback(message);
    }
}

std::optional<std::size_t> Transport::parse_headers() {
    std::optional<std::size_t> content_length;

    while (true) {
        // Drain the resync buffer before reading from the stream.
        std::optional<std::string> line;
        if (buffered_header_line_.has_value()) {
            line = std::exchange(buffered_header_line_, std::nullopt);
        } else {
            line = read_line();
        }

        if (!line.has_value()) {
            return std::nullopt; // EOF
        }

        // Empty line marks end of headers.
        if (line->empty()) {
            break;
        }

        // Other headers (e.g. Content-Type) are ignored.
        if (const auto parsed = try_parse_content_length(*line)) {
            content_length = parsed;
        }
    }

    if (!content_length.has_value()) {
        throw ParseError("Message missing Content-Length header");
    }

    return content_length;
}

void Transport::discard_exact(std::size_t count) {
    // Discard in bounded chunks so an absurd Content-Length never turns into
    // a single giant allocation.
    constexpr std::size_t k_discard_chunk_bytes = std::size_t{64} * 1024;

    while (count > 0) {
        const auto chunk = std::min(count, k_discard_chunk_bytes);
        (void)read_exact(chunk);
        count -= chunk;
    }
}

std::optional<JsonValue> Transport::skip_message(std::string_view reason) {
    report_error(std::format("luma: transport parse error: {}\n", reason));
    recoverable_read_error_ = true;
    return std::nullopt;
}

std::optional<JsonValue> Transport::read_message() {
    recoverable_read_error_ = false;

    std::string body;

    try {
        const auto content_length = parse_headers();

        if (!content_length.has_value()) {
            return std::nullopt; // EOF
        }

        if (*content_length > limits_.max_message_bytes) {
            discard_exact(*content_length);

            return skip_message(std::format("Content-Length {} exceeds limit of {} bytes",
                                            *content_length, limits_.max_message_bytes));
        }

        body = read_exact(*content_length);
    } catch (const ConnectionClosed&) {
        // Fatal I/O failure — let it propagate so the server loop can exit.
        throw;
    } catch (const std::exception& e) {
        // Malformed framing: the message boundary is unknown, so scan ahead to
        // the next header.  If resync itself fails, that exception propagates —
        // the stream is unrecoverably corrupt.
        report_error(std::format("luma: transport parse error: {}\n", e.what()));
        resync_to_next_message();
        recoverable_read_error_ = true;
        return std::nullopt;
    }

    try {
        return JsonValue::parse(body);
    } catch (const std::exception& e) {
        // The body was read in full, so the stream already sits on the next
        // frame — skip this message without resyncing.
        return skip_message(e.what());
    }
}

void Transport::resync_to_next_message() {
    // Scan forward discarding input until a Content-Length: header is found.
    // The found header line is buffered so the next read_message() call can
    // process it correctly without losing the message boundary.
    //
    // Iteration cap: limits_.max_resync_iterations (default 1000) prevents
    // runaway scanning on corrupt or non-protocol streams.  An over-long line
    // is discarded like any other non-header line and counts as one
    // iteration.  If the cap is reached without finding a valid header,
    // ResyncError is thrown to signal that the transport is unrecoverably
    // desynchronised.
    for (std::size_t iteration = 0; iteration < limits_.max_resync_iterations; ++iteration) {
        std::optional<std::string> line;

        try {
            line = read_line();
        } catch (const ParseError&) {
            continue; // Over-long line — not a header, keep scanning.
        }

        if (!line.has_value()) {
            return; // EOF reached while resyncing.
        }

        if (try_parse_content_length(*line).has_value()) {
            buffered_header_line_ = std::move(line);
            return;
        }
    }

    throw ResyncError(std::format("Resync failed: no Content-Length header found after {} lines",
                                  limits_.max_resync_iterations));
}

} // namespace luma::protocol
