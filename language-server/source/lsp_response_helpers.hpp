#ifndef LUMA_LSP_RESPONSE_HELPERS_HPP
#define LUMA_LSP_RESPONSE_HELPERS_HPP

// ═══════════════════════════════════════════════════════════════════════
// LSP Response Helpers — consistent JSON-RPC response construction
// ═══════════════════════════════════════════════════════════════════════
//
// This header provides free functions for building complete JSON-RPC 2.0
// response envelopes (success, error, null-result) and common LSP result
// patterns (single location, location array).
//
// These complement the existing infrastructure:
//   - lsp_transport_wrapper.cpp     — LspTransportWrapper::send_response / send_error
//     (transport-level helpers that lock write_mutex_ and write to transport;
//     also surfaced to handlers via LspHandlerContext)
//   - lsp_types.hpp / lsp_builders   — result *payload* builders (hover,
//     location, range, etc.)
//   - lsp_param_extraction.hpp       — parameter extraction and small
//     response-value conveniences (make_location, make_range, make_text_edit)
//
// Use case: code that needs to construct a JSON-RPC envelope without
// access to the LspServer instance (e.g. standalone tests, dispatch
// helpers, or code that builds a response before sending).
//
// Header-only — no .cpp companion needed.
// ═══════════════════════════════════════════════════════════════════════

#include <string>
#include <string_view>
#include <vector>

#include "json/json.hpp"
#include "lsp_types.hpp"

namespace luma::lsp::response {

using luma::json::JsonValue;

// ─── Standard LSP / JSON-RPC error codes ─────────────────────────────
//
// Re-exported as response-level constants for callers that only include
// this header.  The canonical definitions live in protocol/error_codes.hpp
// (shared with the DAP server) and are re-exported in lsp_types.hpp.

inline constexpr int k_parse_error = k_json_rpc_parse_error;                       // -32700
inline constexpr int k_invalid_request = k_json_rpc_invalid_request;               // -32600
inline constexpr int k_method_not_found = k_json_rpc_method_not_found;             // -32601
inline constexpr int k_invalid_params = k_json_rpc_invalid_params;                 // -32602
inline constexpr int k_internal_error = k_json_rpc_internal_error;                 // -32603
inline constexpr int k_server_not_initialized = k_json_rpc_server_not_initialized; // -32002
inline constexpr int k_request_cancelled = k_json_rpc_request_cancelled;           // -32800

// LSP-specific error codes (LSP 3.17 §error-codes).
inline constexpr int k_content_modified = -32801;
inline constexpr int k_server_cancelled = -32802;
inline constexpr int k_request_failed = -32803;

// ─── JSON-RPC envelope builders ──────────────────────────────────────

// Build a complete JSON-RPC 2.0 success response envelope.
//
//   { "jsonrpc": "2.0", "id": <id>, "result": <result> }
//
[[nodiscard]] inline JsonValue make_success_response(const JsonValue& id, const JsonValue& result) {
    return JsonValue(JsonValue::ObjectType{
        {"jsonrpc", JsonValue("2.0")},
        {"id", id},
        {"result", result},
    });
}

// Build a complete JSON-RPC 2.0 error response envelope.
//
//   { "jsonrpc": "2.0", "id": <id>, "error": { "code": <code>, "message": <msg> } }
//
[[nodiscard]] inline JsonValue make_error_response(const JsonValue& id, int code,
                                                   std::string_view message) {
    return JsonValue(JsonValue::ObjectType{
        {"jsonrpc", JsonValue("2.0")},
        {"id", id},
        {"error", JsonValue(JsonValue::ObjectType{
                      {"code", JsonValue(static_cast<int64_t>(code))},
                      {"message", JsonValue(std::string(message))},
                  })},
    });
}

// Build a JSON-RPC 2.0 success response with a null result.
// Many LSP methods return null to signal "nothing found" (hover, definition,
// type definition, etc.).
//
//   { "jsonrpc": "2.0", "id": <id>, "result": null }
//
[[nodiscard]] inline JsonValue make_null_result(const JsonValue& id) {
    return make_success_response(id, JsonValue{});
}

// ─── Common LSP result payload builders ──────────────────────────────
//
// These build result *values* (not full envelopes).  Wrap with
// make_success_response() when a full envelope is needed.

// Build a single Location result from a URI and Range.
// Suitable for textDocument/definition, textDocument/typeDefinition, etc.
[[nodiscard]] inline JsonValue make_location_result(const std::string& uri, const Range& range) {
    return serialise_location(Location{uri, range});
}

// Build a Location result from explicit position coordinates.
[[nodiscard]] inline JsonValue make_location_result(const std::string& uri, int start_line,
                                                    int start_char, int end_line, int end_char) {
    return serialise_location(Location{
        uri,
        Range{Position{start_line, start_char}, Position{end_line, end_char}},
    });
}

// Build an array-of-locations result from a vector of Location structs.
// Suitable for textDocument/references, textDocument/implementation, etc.
[[nodiscard]] inline JsonValue make_locations_result(const std::vector<Location>& locations) {
    JsonValue::ArrayType arr;
    arr.reserve(locations.size());
    for (const auto& loc : locations) {
        arr.push_back(serialise_location(loc));
    }
    return JsonValue(std::move(arr));
}

// Build an array-of-locations result from a pre-built JSON array.
// Useful when locations have already been serialised individually.
[[nodiscard]] inline JsonValue make_locations_result(JsonValue::ArrayType locations) {
    return JsonValue(std::move(locations));
}

// Build an empty array result — the "nothing found" equivalent for
// methods that return arrays (references, highlights, implementations).
[[nodiscard]] inline JsonValue make_empty_array_result() {
    return JsonValue(JsonValue::ArrayType{});
}

} // namespace luma::lsp::response

#endif // LUMA_LSP_RESPONSE_HELPERS_HPP
