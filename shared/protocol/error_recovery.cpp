#include "protocol/error_recovery.hpp"

#include "protocol/transport_exceptions.hpp"

namespace luma::protocol {

ErrorSeverity classify_read_error(const std::exception& e) {
    // ConnectionClosed — pipe/socket gone.
    if (dynamic_cast<const ConnectionClosed*>(&e) != nullptr) {
        return ErrorSeverity::Fatal;
    }

    // ResyncError — the transport exhausted its resync scan without finding a
    // header, so the stream is unrecoverably corrupt.  Checked before the
    // ParseError branch below because ResyncError derives from ParseError.
    if (dynamic_cast<const ResyncError*>(&e) != nullptr) {
        return ErrorSeverity::Fatal;
    }

    // ParseError — malformed message, resync possible.
    if (dynamic_cast<const ParseError*>(&e) != nullptr) {
        return ErrorSeverity::Transient;
    }

    // TransportError (non-parse, non-connection) — assume fatal.
    if (dynamic_cast<const TransportError*>(&e) != nullptr) {
        return ErrorSeverity::Fatal;
    }

    // Everything else (unexpected) — treat as transient so one
    // rogue message does not bring down the server.
    return ErrorSeverity::Transient;
}

} // namespace luma::protocol
