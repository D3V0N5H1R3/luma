#ifndef LUMA_PROTOCOL_ERROR_CODES_HPP
#define LUMA_PROTOCOL_ERROR_CODES_HPP

// ═══════════════════════════════════════════════════════════
// Shared JSON-RPC / protocol error codes
//
// Standard JSON-RPC 2.0 error codes used by both the Language
// Server Protocol (LSP) and Debug Adapter Protocol (DAP)
// transport layers.  Defined once here so that neither server
// needs its own copy.
//
// Reference: https://www.jsonrpc.org/specification#error_object
// LSP addenda: https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/
// ═══════════════════════════════════════════════════════════

namespace luma::protocol {

// ─── Standard JSON-RPC 2.0 error codes ───

// Invalid JSON was received by the server.
inline constexpr int k_json_rpc_parse_error = -32700;

// The JSON sent is not a valid Request object.
inline constexpr int k_json_rpc_invalid_request = -32600;

// The method does not exist / is not available.
inline constexpr int k_json_rpc_method_not_found = -32601;

// Invalid method parameter(s).
inline constexpr int k_json_rpc_invalid_params = -32602;

// Internal JSON-RPC error.
inline constexpr int k_json_rpc_internal_error = -32603;

// ─── LSP-specific error codes ───

// The server has not been initialized yet.
inline constexpr int k_json_rpc_server_not_initialized = -32002;

// The request was cancelled by the client.
inline constexpr int k_json_rpc_request_cancelled = -32800;

} // namespace luma::protocol

#endif // LUMA_PROTOCOL_ERROR_CODES_HPP
