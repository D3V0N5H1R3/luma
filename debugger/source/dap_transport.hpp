#ifndef LUMA_DAP_TRANSPORT_HPP
#define LUMA_DAP_TRANSPORT_HPP

// DAP stdio transport — thin wrapper over the shared stdio transport.
//
// The Content-Length framed stdin/stdout transport is shared with the
// LSP language server (see shared/protocol/stdio_transport.hpp).  This
// header re-exports the shared type under the luma::dap namespace so
// that existing DAP code continues to compile unchanged.

#include <optional>

#include "protocol/stdio_transport.hpp"

namespace luma::dap {

// Concrete stdin/stdout transport with thread-safe writes.
using Transport = protocol::StdioTransport;

// Move the DAP input stream off the process's standard input.
//
// The debuggee runs in this process, so anything it reads from stdin
// (Console.read_line, Terminal input) would otherwise consume protocol bytes
// sent by the editor and corrupt the Content-Length framing.  This duplicates
// stdin to a private descriptor for the transport and points the process's
// stdin at the null device, so the debuggee sees end-of-input instead.
// Returns the private descriptor, or std::nullopt if isolation failed (the
// caller then falls back to reading stdin directly).
[[nodiscard]] std::optional<int> detach_protocol_input_from_stdin();

} // namespace luma::dap

#endif // LUMA_DAP_TRANSPORT_HPP
