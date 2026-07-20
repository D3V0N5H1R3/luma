#ifndef LUMA_LSP_TRANSPORT_HPP
#define LUMA_LSP_TRANSPORT_HPP

// LSP transport — thin wrapper over the shared stdio transport.
//
// The Content-Length framed stdin/stdout transport is shared with the
// DAP debugger (see shared/protocol/stdio_transport.hpp).  This header
// re-exports the shared types under the luma::lsp namespace so that
// existing LSP code and tests continue to compile unchanged.

#include "protocol/stdio_transport.hpp"

namespace luma::lsp {

// Abstract base used by LspServer and mock transports in tests.
using Transport = protocol::Transport;

// Concrete stdin/stdout transport.
using StdioTransport = protocol::StdioTransport;

} // namespace luma::lsp

#endif // LUMA_LSP_TRANSPORT_HPP
