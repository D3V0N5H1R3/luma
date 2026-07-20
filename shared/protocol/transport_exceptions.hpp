#ifndef LUMA_PROTOCOL_TRANSPORT_EXCEPTIONS_HPP
#define LUMA_PROTOCOL_TRANSPORT_EXCEPTIONS_HPP

#include <stdexcept>
#include <string>

#include "parse_error.hpp"

namespace luma::protocol {

// ═══════════════════════════════════════════════════════════
// Transport exception hierarchy
// ═══════════════════════════════════════════════════════════
//
// Hierarchy:
//   std::runtime_error
//   ├── luma::ParseError         (shared base — see parse_error.hpp)
//   │   ├── luma::JsonParseError (JSON syntax errors)
//   │   └── protocol::ParseError (framing / header errors)
//   └── TransportError           (I/O and connection errors)
//       ├── ConnectionClosed
//       └── TimeoutError
//
// Callers can catch luma::ParseError to handle all parsing
// failures uniformly, or catch protocol::ParseError /
// luma::JsonParseError individually for layer-specific handling.
// ═══════════════════════════════════════════════════════════

// Base class for all transport-level I/O errors in the LSP/DAP protocol layer.
class TransportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The underlying connection was lost, broken, or could not be established.
// Covers read/write failures, unexpected EOF, and socket errors.
class ConnectionClosed : public TransportError {
public:
    using TransportError::TransportError;
};

// A message could not be parsed: malformed headers, invalid Content-Length,
// or resync failure.  Inherits from luma::ParseError so callers can catch
// all parse failures uniformly.
class ParseError : public luma::ParseError {
public:
    using luma::ParseError::ParseError;
};

// A read or connection attempt exceeded its time limit.
class TimeoutError : public TransportError {
public:
    using TransportError::TransportError;
};

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_TRANSPORT_EXCEPTIONS_HPP
