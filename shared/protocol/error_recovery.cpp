#include "protocol/error_recovery.hpp"

#include "protocol/transport_exceptions.hpp"

namespace luma::protocol {

ErrorSeverity classify_read_error(const std::exception& e) {
    // ConnectionClosed — pipe/socket gone.
    if (dynamic_cast<const ConnectionClosed*>(&e) != nullptr) {
        return ErrorSeverity::fatal;
    }

    // ParseError — malformed message, resync possible.
    if (dynamic_cast<const ParseError*>(&e) != nullptr) {
        return ErrorSeverity::transient;
    }

    // TransportError (non-parse, non-connection) — assume fatal.
    if (dynamic_cast<const TransportError*>(&e) != nullptr) {
        return ErrorSeverity::fatal;
    }

    // Everything else (unexpected) — treat as transient so one
    // rogue message does not bring down the server.
    return ErrorSeverity::transient;
}

} // namespace luma::protocol
