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
//     • ParseError from resync_to_next_message() — the iteration cap
//       was reached without finding a valid header; the stream is
//       unrecoverably corrupt.
//
//   Error callback (report_error):
//     • Transient parse errors in read_message() — a single message
//       had bad framing or invalid JSON.  The error is reported via
//       the configured ErrorCallback (defaults to stderr), then the
//       transport resyncs silently and returns std::nullopt so the
//       caller can continue to the next message.
//
//   Silent resync (resync_to_next_message):
//     • After a transient parse error, the transport scans forward
//       discarding input until the next Content-Length header is found
//       (up to max_resync_iterations lines).  The found header is
//       buffered for the next read_message() call.
//
// See error_recovery.hpp for the full error classification policy,
// consecutive-error tracking, and the shutdown strategy used by the
// LSP and DAP message loops.
// ═══════════════════════════════════════════════════════════════════════

#include "protocol/transport.hpp"

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
    std::size_t content_length{0};
    bool found_length{false};

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

        // Delegate Content-Length parsing to the free function.
        auto parsed = try_parse_content_length(*line);

        if (parsed.has_value()) {
            content_length = *parsed;

            // Sanity check: reject absurdly large messages to prevent OOM.
            if (content_length > limits_.max_message_bytes) {
                throw ParseError(std::format("Content-Length {} exceeds limit of {} bytes",
                                             content_length, limits_.max_message_bytes));
            }

            found_length = true;
        }
        // Other headers (e.g. Content-Type) are ignored.
    }

    if (!found_length) {
        throw ParseError("Message missing Content-Length header");
    }

    return content_length;
}

std::optional<JsonValue> Transport::read_message() {
    try {
        auto content_length = parse_headers();

        if (!content_length.has_value()) {
            return std::nullopt; // EOF
        }

        // Read the JSON body.
        auto body = read_exact(*content_length);

        return JsonValue::parse(body);
    } catch (const ConnectionClosed&) {
        // Fatal I/O failure — let it propagate so the server loop can exit.
        throw;
    } catch (const std::exception& e) {
        // Recoverable parse error — report via callback (not re-throw) so
        // callers see nullopt instead of a double-reported error.  If resync
        // itself fails, that exception propagates — the stream is
        // unrecoverably corrupt.
        report_error(std::format("luma: transport parse error: {}\n", e.what()));
        resync_to_next_message();
        return std::nullopt;
    }
}

void Transport::resync_to_next_message() {
    // Scan forward discarding input until a Content-Length: header is found.
    // The found header line is buffered so the next read_message() call can
    // process it correctly without losing the message boundary.
    //
    // Iteration cap: limits_.max_resync_iterations (default 1000) prevents
    // runaway scanning on corrupt or non-protocol streams.  If the cap is
    // reached without finding a valid header, ParseError is thrown to signal
    // that the transport is unrecoverably desynchronised.
    for (std::size_t iteration = 0; iteration < limits_.max_resync_iterations; ++iteration) {
        auto line = read_line();
        if (!line.has_value()) {
            return; // EOF reached while resyncing.
        }
        if (try_parse_content_length(*line).has_value()) {
            buffered_header_line_ = std::move(line);
            return;
        }
    }

    throw ParseError(std::format("Resync failed: no Content-Length header found after {} lines",
                                 limits_.max_resync_iterations));
}

} // namespace luma::protocol
