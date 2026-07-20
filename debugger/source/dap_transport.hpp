#ifndef LUMA_DAP_TRANSPORT_HPP
#define LUMA_DAP_TRANSPORT_HPP

// DAP stdio transport — thin wrapper over the shared stdio transport.
//
// The Content-Length framed stdin/stdout transport is shared with the
// LSP language server (see shared/protocol/stdio_transport.hpp).  This
// header re-exports the shared type under the luma::dap namespace so
// that existing DAP code continues to compile unchanged.

#include "protocol/stdio_transport.hpp"

namespace luma::dap {

// Concrete stdin/stdout transport with thread-safe writes and
// optional read timeout.
using Transport = protocol::StdioTransport;

} // namespace luma::dap

#endif // LUMA_DAP_TRANSPORT_HPP
