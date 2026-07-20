#ifndef LUMA_LSP_PARAM_EXTRACTION_HPP
#define LUMA_LSP_PARAM_EXTRACTION_HPP

// Parameter extraction helpers for LSP request/notification handlers.
//
// Many LSP handlers share the same boilerplate for validating a JSON params
// object and pulling out the textDocument URI, position, or range.  These
// free-function helpers centralise that logic so handlers can focus on
// feature-specific behaviour.
//
// ═══════════════════════════════════════════════════════════
// Response convention (empty vs error)
// ═══════════════════════════════════════════════════════════
//
// LSP handlers in this server follow a two-tier error propagation policy:
//
//   1. Structurally invalid params (missing required fields, wrong types)
//      → throw InvalidParamsError.
//      The dispatch layer catches this and returns a JSON-RPC error response
//      with code -32602 (InvalidParams).
//
//   2. Feature not applicable (no symbol at cursor, no analysis available)
//      → return a null JsonValue or an empty array, depending on the
//      response type declared by the LSP specification for that method.
//      This is NOT an error — the client interprets null/empty as "nothing
//      to show" and does not display an error to the user.
//
// See also: lsp_types.hpp (InvalidParamsError), lsp_server.hpp (dispatch).
// ═══════════════════════════════════════════════════════════

#include <format>
#include <optional>
#include <string>
#include <vector>

#include "json/json_helpers.hpp"
#include "lsp_constants.hpp"
#include "lsp_params.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_types.hpp"
#include "protocol/uri_utils.hpp"

namespace luma::lsp::extraction {

using luma::protocol::canonicalize_uri;
using luma::protocol::path_to_uri;
using luma::protocol::uri_to_path;

// ─── textDocument.uri extraction ─────────────────────────────────────

// Extract and canonicalize the textDocument.uri from LSP params.
// Returns std::nullopt when the params structure is missing or malformed.
// Delegates to params::TextDocumentIdentifier::from_json for parsing.
[[nodiscard]] inline std::optional<std::string> extract_text_document_uri(const JsonValue& params) {
    const auto& td = params.get("textDocument");
    if (td.is_null()) {
        return std::nullopt;
    }

    auto id = params::TextDocumentIdentifier::from_json(td);
    if (!id) {
        return std::nullopt;
    }

    return std::move(id->uri);
}

// Extract and canonicalize the textDocument.uri, throwing InvalidParamsError
// when required fields are absent.  Use this in handlers where a missing
// textDocument is always a protocol violation.
[[nodiscard]] inline std::string require_text_document_uri(const JsonValue& params) {
    auto uri = extract_text_document_uri(params);
    if (!uri) {
        throw InvalidParamsError("Missing or malformed textDocument.uri");
    }
    return std::move(*uri);
}

// ─── Range extraction ────────────────────────────────────────────────

// Extract an LSP Range from a JSON range object.
// Returns std::nullopt if any required field is missing.
// Delegates to params::RangeParams::from_json for parsing.
[[nodiscard]] inline std::optional<Range> extract_range(const JsonValue& range_value) {
    return params::RangeParams::from_json(range_value);
}

// ─── Combined textDocument + range ───────────────────────────────────

// Parsed textDocument identifier with a range, as sent by requests such as
// textDocument/codeAction, textDocument/rangeFormatting, and
// textDocument/semanticTokens/range.
struct TextDocumentRange {
    std::string uri;
    Range range;
};

// Extract textDocument.uri and a sibling "range" field from params.
// Returns std::nullopt when either is missing or malformed.
[[nodiscard]] inline std::optional<TextDocumentRange>
extract_text_document_range(const JsonValue& params) {
    auto uri = extract_text_document_uri(params);
    if (!uri) {
        return std::nullopt;
    }

    if (!params.has("range")) {
        return std::nullopt;
    }

    auto range = extract_range(params["range"]);
    if (!range) {
        return std::nullopt;
    }

    return TextDocumentRange{std::move(*uri), *range};
}

// ─── textDocument + position validation ──────────────────────────────

// Validate that textDocument/position params are present and well-formed.
// Throws InvalidParamsError if missing — use at handler entry points that
// receive textDocument/position params but do not use resolve_token_context().
inline void require_text_document_position(const JsonValue& params) {
    if (!TextDocumentPosition::from_params(params)) {
        throw InvalidParamsError("Missing or malformed textDocument/position params");
    }
}

// ─── Typed field extraction with error reporting ─────────────────────

// Extract a required field from a JSON object, throwing InvalidParamsError
// when the field is absent or has the wrong type.  Use this instead of
// try_extract_field<T>() when the field is mandatory per the LSP spec.
//
// Usage:
//   auto uri = require_field<std::string>(params, "textDocument.uri");
//   auto line = require_field<int>(params, "position.line");
template <typename T>
[[nodiscard]] inline T require_field(const JsonValue& obj, std::string_view field_name) {
    auto value = luma::json::try_extract_field<T>(obj, field_name);
    if (!value) {
        throw InvalidParamsError(std::format("Missing or invalid required field: {}", field_name));
    }
    return std::move(*value);
}

// ─── Diagnostic deserialization ─────────────────────────────────────
//
// Parse an array of LSP Diagnostic JSON objects (from params.context.diagnostics)
// into a vector of internal Diagnostic structs.  Extracted so that handlers
// that receive diagnostics from the client (e.g. code actions, quick fixes)
// do not need to duplicate the deserialization logic inline (LS-17).
[[nodiscard]] inline std::vector<Diagnostic> parse_diagnostics(const JsonValue& params) {
    std::vector<Diagnostic> diags;

    if (!params.has("context") || !params["context"].has("diagnostics")) {
        return diags;
    }

    for (const auto& d : params["context"]["diagnostics"].as_array()) {
        if (!d.is_object()) {
            continue;
        }

        Range r{};
        if (d.has("range") && d["range"].has("start")) {
            r.start.line = util::clamp_to_int(d["range"]["start"]["line"].as_integer());
            r.start.character = util::clamp_to_int(d["range"]["start"]["character"].as_integer());
            r.end.line = util::clamp_to_int(d["range"]["end"]["line"].as_integer());
            r.end.character = util::clamp_to_int(d["range"]["end"]["character"].as_integer());
        }

        diags.push_back(Diagnostic{r, d.get_or<int>("severity", constants::severity::error),
                                   std::string(constants::diagnostic::source),
                                   d.get_or<std::string>("message", ""),
                                   d.get_or<std::string>("code", "")});
    }

    return diags;
}

// ─── Quoted-name extraction ─────────────────────────────────────────
//
// Extract a name enclosed in single quotes from a diagnostic message.
// Shared by quick-fix handlers and code-action generation.
// E.g., extract_quoted_name("unused variable 'x'", "unused variable '") → "x".
[[nodiscard]] inline std::string extract_quoted_name(const std::string& text,
                                                     std::string_view prefix) {
    if (!text.starts_with(prefix)) {
        return {};
    }
    const auto close = text.find('\'', prefix.size());
    if (close == std::string::npos) {
        return {};
    }
    return text.substr(prefix.size(), close - prefix.size());
}

// Extract the first single-quoted name from anywhere in a diagnostic message,
// returning nullopt when there is no non-empty '...'-quoted span.  Unlike
// extract_quoted_name (which is prefix-anchored), this scans for the first
// quote pair, so it suits messages whose quoted name is not at a fixed offset.
// E.g., extract_first_quoted_name("'foo' shadows an outer variable") → "foo".
[[nodiscard]] inline std::optional<std::string>
extract_first_quoted_name(const std::string& message) {
    const auto open = message.find('\'');
    if (open == std::string::npos) {
        return std::nullopt;
    }
    const auto close = message.find('\'', open + 1);
    if (close == std::string::npos) {
        return std::nullopt;
    }
    auto name = message.substr(open + 1, close - open - 1);
    if (name.empty()) {
        return std::nullopt;
    }
    return name;
}

} // namespace luma::lsp::extraction

#endif // LUMA_LSP_PARAM_EXTRACTION_HPP
