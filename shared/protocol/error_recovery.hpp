#ifndef LUMA_PROTOCOL_ERROR_RECOVERY_HPP
#define LUMA_PROTOCOL_ERROR_RECOVERY_HPP

// ═══════════════════════════════════════════════════════════════════════
// Protocol Error Recovery Strategy
// ═══════════════════════════════════════════════════════════════════════
//
// Shared error recovery policy for the LSP and DAP message loops.
//
// Both servers sit on the same Content-Length-framed JSON transport
// (protocol::Transport) and face the same failure modes during the
// read → dispatch → respond cycle.  This header codifies the recovery
// rules that both implementations follow.
//
// ─── Error classification ────────────────────────────────────────────
//
// Every exception that reaches the message loop falls into one of two
// categories:
//
//   FATAL (break out of the message loop)
//     • protocol::ConnectionClosed — the pipe/socket was lost or EOF
//       occurred mid-message.  No further communication is possible.
//     • protocol::TransportError (non-parse) — an I/O-level failure
//       that is not a simple malformed message.
//     • Resync failure — the transport scanned max_resync_iterations
//       lines without finding a Content-Length header, meaning the
//       stream is unrecoverably corrupt.
//
//   TRANSIENT (log and continue to the next message)
//     • protocol::ParseError — a single message had bad framing or
//       invalid JSON.  The transport layer resyncs to the next
//       Content-Length boundary automatically (up to 1 000 lines).
//     • Handler/dispatch exceptions — a bug or unexpected input in
//       request processing.  The server sends an error response
//       (DAP) or logs (LSP) and continues.
//     • Unknown std::exception during read — treated as a skippable
//       anomaly; the loop continues.
//
// ─── Recovery mechanisms ─────────────────────────────────────────────
//
// 1. Transport-level resync (Transport::resync_to_next_message)
//    After a parse error, the transport scans forward discarding
//    bytes until a valid Content-Length header is found, then buffers
//    it for the next read_message() call.  This is capped at
//    k_default_max_resync_iterations (1 000 lines) to prevent
//    runaway scanning on non-protocol streams.
//
// 2. Consecutive-error tracking (this header)
//    While a single transient error is harmless, a sustained burst
//    suggests the stream is corrupt beyond what resync can fix (e.g.
//    a binary stream mistakenly piped in).  The ErrorRecoveryState
//    helper counts consecutive transient errors and recommends
//    shutdown once the threshold is reached.
//
// 3. Dispatch-level isolation
//    Both servers catch all exceptions from request/notification
//    handlers so that a buggy handler never crashes the loop.  DAP
//    additionally classifies the exception and sends a structured
//    error response.  LSP logs the error via window/logMessage.
//
// ─── Retry policy ────────────────────────────────────────────────────
//
// Neither server retries a failed message read or handler invocation.
// The protocols are request/response — the client owns retry logic.
// The server's job is to survive transient errors and exit cleanly
// on fatal ones.
//
// ─── Shutdown policy ─────────────────────────────────────────────────
//
//   LSP:  Exits on ConnectionClosed, TransportError, EOF, or an
//         explicit "exit" notification from the client.
//   DAP:  Sets an atomic `disconnected_` flag on ConnectionClosed,
//         TransportError, EOF, or write failure (broken pipe).  The
//         flag is checked at the top of every loop iteration.
//
// Both servers perform orderly cleanup on exit (joining worker
// threads, terminating debug sessions, etc.).
//
// ═══════════════════════════════════════════════════════════════════════

#include <cstddef>

#include "protocol/transport_exceptions.hpp"

namespace luma::protocol {

// ─── Tuning constants ────────────────────────────────────────────────

// Maximum number of consecutive transient errors before the message
// loop should treat the stream as unrecoverable and shut down.
// Prevents infinite looping on a completely corrupt input stream.
inline constexpr std::size_t k_default_max_consecutive_errors = 50;

// ─── Error severity ──────────────────────────────────────────────────

enum class ErrorSeverity {
    // The error is scoped to a single message.  Log it, skip the
    // message, and continue.  The transport layer may resync.
    transient,

    // The transport is broken beyond repair.  Exit the message loop
    // and begin orderly shutdown.
    fatal,
};

// ─── Recovery action ─────────────────────────────────────────────────

enum class RecoveryAction {
    // Skip the current message and read the next one.
    skip_and_continue,

    // Shut down the server cleanly.
    shutdown,
};

// ─── Exception classification ────────────────────────────────────────

// Classify a read-phase exception into a severity.
//
// Call this from the catch blocks in the message loop to decide
// whether to continue or break:
//
//   } catch (const protocol::ConnectionClosed&) {
//       // classify_read_error knows this is fatal
//   } catch (const std::exception& e) {
//       auto severity = classify_read_error(e);
//       ...
//   }
//
[[nodiscard]] ErrorSeverity classify_read_error(const std::exception& e);

// ─── Consecutive-error tracker ───────────────────────────────────────

// Lightweight state object that counts consecutive transient errors
// and recommends shutdown when the threshold is exceeded.
//
// Usage:
//
//   ErrorRecoveryState recovery;
//
//   while (running) {
//       try {
//           message = transport.read_message();
//           recovery.on_success();            // reset counter
//           dispatch(message);
//       } catch (const std::exception& e) {
//           auto action = recovery.on_error(classify_read_error(e));
//           if (action == RecoveryAction::shutdown) break;
//           // else: continue
//       }
//   }
//
class ErrorRecoveryState {
public:
    explicit ErrorRecoveryState(
        std::size_t max_consecutive_errors = k_default_max_consecutive_errors)
        : max_consecutive_errors_{max_consecutive_errors} {}

    // Call after a successful message read+dispatch to reset the
    // consecutive error counter.
    void on_success() noexcept {
        consecutive_errors_ = 0;
    }

    // Call when a read or dispatch error occurs.  Returns the
    // recommended recovery action.  Fatal errors always produce
    // shutdown.  Transient errors produce skip_and_continue until
    // the consecutive-error threshold is reached.
    [[nodiscard]] RecoveryAction on_error(ErrorSeverity severity) noexcept {
        if (severity == ErrorSeverity::fatal) {
            return RecoveryAction::shutdown;
        }

        ++consecutive_errors_;

        if (consecutive_errors_ >= max_consecutive_errors_) {
            return RecoveryAction::shutdown;
        }

        return RecoveryAction::skip_and_continue;
    }

    // Current consecutive transient error count.
    [[nodiscard]] std::size_t consecutive_errors() const noexcept {
        return consecutive_errors_;
    }

    // Configured threshold.
    [[nodiscard]] std::size_t max_consecutive_errors() const noexcept {
        return max_consecutive_errors_;
    }

private:
    std::size_t max_consecutive_errors_;
    std::size_t consecutive_errors_{0};
};

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_ERROR_RECOVERY_HPP
