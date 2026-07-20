#ifndef LUMA_LSP_PARAMS_HPP
#define LUMA_LSP_PARAMS_HPP

// Typed LSP request parameter classes.
//
// These structs mirror the parameter shapes defined by the Language Server
// Protocol specification and provide static `from_json()` factory methods
// that convert raw JsonValue objects into strongly typed C++ values.
//
// Using these classes instead of ad-hoc JSON field extraction in handlers
// centralises validation, improves readability, and makes it easier to
// extend parameter handling (e.g. adding optional fields) in one place.
//
// The types here are *parameter-side* counterparts to the *response-side*
// types already defined in lsp_types.hpp (Position, Range, etc.).  Where a
// struct has the same shape as one in lsp_types.hpp (e.g. Position, Range)
// the `from_json()` method deserialises directly into the existing type to
// avoid duplication.

#include <optional>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "json/json_helpers.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_types.hpp"
#include "protocol/uri_utils.hpp"

namespace luma::lsp::params {

using luma::json::JsonValue;
using luma::protocol::canonicalize_uri;

// ─────────────────────── Position ───────────────────────
//
// Deserialise an LSP Position object { "line": int, "character": int }
// into the existing luma::lsp::Position struct.

struct PositionParams {
    // Deserialise from a JSON Position object.
    // Returns std::nullopt when required fields are missing or malformed.
    [[nodiscard]] static std::optional<Position> from_json(const JsonValue& value) {
        if (!value.is_object() || !value.has("line") || !value.has("character")) {
            return std::nullopt;
        }

        const auto& line_val = value["line"];
        const auto& char_val = value["character"];

        if (!line_val.is_integer() || !char_val.is_integer()) {
            return std::nullopt;
        }

        return Position{
            util::clamp_to_int(line_val.as_integer()),
            util::clamp_to_int(char_val.as_integer()),
        };
    }
};

// ─────────────────────── Range ───────────────────────
//
// Deserialise an LSP Range object { "start": Position, "end": Position }
// into the existing luma::lsp::Range struct.

struct RangeParams {
    // Deserialise from a JSON Range object.
    // Returns std::nullopt when required fields are missing or malformed.
    [[nodiscard]] static std::optional<Range> from_json(const JsonValue& value) {
        if (!value.is_object() || !value.has("start") || !value.has("end")) {
            return std::nullopt;
        }

        auto start = PositionParams::from_json(value["start"]);
        auto end = PositionParams::from_json(value["end"]);

        if (!start || !end) {
            return std::nullopt;
        }

        return Range{*start, *end};
    }
};

// ─────────────────────── TextDocumentIdentifier ───────────────────────
//
// LSP TextDocumentIdentifier: { "uri": string }
// Used by requests that refer to a document without a position.

struct TextDocumentIdentifier {
    std::string uri;

    // Deserialise from a JSON TextDocumentIdentifier object.
    // The URI is canonicalized on extraction.
    // Returns std::nullopt when the "uri" field is missing or not a string.
    [[nodiscard]] static std::optional<TextDocumentIdentifier> from_json(const JsonValue& value) {
        auto uri = luma::json::try_extract_field<std::string>(value, "uri");
        if (!uri) {
            return std::nullopt;
        }

        return TextDocumentIdentifier{canonicalize_uri(std::move(*uri))};
    }
};

// ─────────────────────── TextDocumentPositionParams ───────────────────────
//
// LSP TextDocumentPositionParams:
//   { "textDocument": TextDocumentIdentifier, "position": Position }
//
// Used by textDocument/hover, textDocument/completion,
// textDocument/definition, textDocument/references, and many others.

// Parses the LSP TextDocumentPositionParams shape and yields the shared
// luma::lsp::TextDocumentPosition value (defined in lsp_types.hpp).  Keeping a
// single data struct avoids a second, structurally identical type.

struct TextDocumentPositionParams {
    // Deserialise from a JSON-RPC params object.
    // Returns std::nullopt when required fields are missing or malformed.
    [[nodiscard]] static std::optional<TextDocumentPosition> from_json(const JsonValue& params) {
        if (!params.is_object() || !params.has("textDocument") || !params.has("position")) {
            return std::nullopt;
        }

        auto td = TextDocumentIdentifier::from_json(params["textDocument"]);
        auto pos = PositionParams::from_json(params["position"]);

        if (!td || !pos) {
            return std::nullopt;
        }

        return TextDocumentPosition{
            std::move(td->uri),
            pos->line,
            pos->character,
        };
    }
};

// ─────────────────────── DidOpenTextDocumentParams ───────────────────────
//
// LSP textDocument/didOpen notification params:
//   { "textDocument": { "uri": string, "languageId": string,
//                        "version": int, "text": string } }

struct DidOpenTextDocumentParams {
    std::string uri;
    std::string language_id;
    int version{0};
    std::string text;

    [[nodiscard]] static std::optional<DidOpenTextDocumentParams>
    from_json(const JsonValue& params) {
        if (!params.is_object() || !params.has("textDocument")) {
            return std::nullopt;
        }

        const auto& td = params["textDocument"];
        auto uri = luma::json::try_extract_field<std::string>(td, "uri");
        auto text = luma::json::try_extract_field<std::string>(td, "text");

        if (!uri || !text) {
            return std::nullopt;
        }

        auto language_id = luma::json::try_extract_field<std::string>(td, "languageId");
        auto version = luma::json::try_extract_field<int>(td, "version");

        return DidOpenTextDocumentParams{
            canonicalize_uri(std::move(*uri)),
            language_id.value_or("luma"),
            version.value_or(0),
            std::move(*text),
        };
    }
};

// ─────────────────────── DidCloseTextDocumentParams ───────────────────────
//
// LSP textDocument/didClose notification params:
//   { "textDocument": TextDocumentIdentifier }

struct DidCloseTextDocumentParams {
    std::string uri;

    [[nodiscard]] static std::optional<DidCloseTextDocumentParams>
    from_json(const JsonValue& params) {
        if (!params.is_object() || !params.has("textDocument")) {
            return std::nullopt;
        }

        auto td = TextDocumentIdentifier::from_json(params["textDocument"]);
        if (!td) {
            return std::nullopt;
        }

        return DidCloseTextDocumentParams{std::move(td->uri)};
    }
};

// ─────────────────────── ReferenceParams ───────────────────────
//
// LSP textDocument/references params — extends TextDocumentPositionParams
// with an optional context.includeDeclaration flag.

struct ReferenceParams {
    std::string uri;
    int line{0};
    int character{0};
    bool include_declaration{false};

    [[nodiscard]] static std::optional<ReferenceParams> from_json(const JsonValue& params) {
        auto base = TextDocumentPositionParams::from_json(params);
        if (!base) {
            return std::nullopt;
        }

        bool include_decl = false;
        if (params.has("context") && params["context"].is_object()) {
            auto val = luma::json::try_extract_field<bool>(params["context"], "includeDeclaration");
            if (val) {
                include_decl = *val;
            }
        }

        return ReferenceParams{
            std::move(base->uri),
            base->line,
            base->character,
            include_decl,
        };
    }
};

// ─────────────────────── RenameParams ───────────────────────
//
// LSP textDocument/rename params — extends TextDocumentPositionParams
// with a required newName field.

struct RenameParams {
    std::string uri;
    int line{0};
    int character{0};
    std::string new_name;

    [[nodiscard]] static std::optional<RenameParams> from_json(const JsonValue& params) {
        auto base = TextDocumentPositionParams::from_json(params);
        if (!base) {
            return std::nullopt;
        }

        auto name = luma::json::try_extract_field<std::string>(params, "newName");
        if (!name) {
            return std::nullopt;
        }

        return RenameParams{
            std::move(base->uri),
            base->line,
            base->character,
            std::move(*name),
        };
    }
};

// ─────────────────────── CodeActionParams ───────────────────────
//
// LSP textDocument/codeAction params:
//   { "textDocument": TextDocumentIdentifier, "range": Range,
//     "context": { "diagnostics": Diagnostic[] } }

struct CodeActionParams {
    std::string uri;
    Range range;
    std::optional<JsonValue> context; // kept as raw JSON for diagnostic parsing

    [[nodiscard]] static std::optional<CodeActionParams> from_json(const JsonValue& params) {
        if (!params.is_object() || !params.has("textDocument") || !params.has("range")) {
            return std::nullopt;
        }

        auto td = TextDocumentIdentifier::from_json(params["textDocument"]);
        auto rng = RangeParams::from_json(params["range"]);

        if (!td || !rng) {
            return std::nullopt;
        }

        std::optional<JsonValue> ctx;
        if (params.has("context")) {
            ctx = params["context"];
        }

        return CodeActionParams{
            std::move(td->uri),
            *rng,
            std::move(ctx),
        };
    }
};

// ─────────────────────── FormattingOptions ───────────────────────
//
// LSP FormattingOptions: { "tabSize": int, "insertSpaces": bool, ... }
// Only the fields used by the Luma formatter are extracted.

struct FormattingOptions {
    int tab_size{4};
    bool insert_spaces{true};

    [[nodiscard]] static FormattingOptions from_json(const JsonValue& value) {
        FormattingOptions opts;
        if (!value.is_object()) {
            return opts;
        }

        auto ts = luma::json::try_extract_field<int>(value, "tabSize");
        if (ts) {
            opts.tab_size = *ts;
        }

        auto is = luma::json::try_extract_field<bool>(value, "insertSpaces");
        if (is) {
            opts.insert_spaces = *is;
        }

        return opts;
    }
};

// ─────────────────────── FileEvent ───────────────────────
//
// LSP FileEvent: { "uri": string, "type": int }
// Represents a single file change in a didChangeWatchedFiles notification.

struct FileEvent {
    std::string uri;
    int type{2}; // 1 = created, 2 = changed, 3 = deleted

    [[nodiscard]] static std::optional<FileEvent> from_json(const JsonValue& value) {
        if (!value.is_object()) {
            return std::nullopt;
        }

        auto uri = luma::json::try_extract_field<std::string>(value, "uri");
        if (!uri) {
            return std::nullopt;
        }

        auto change_type = luma::json::try_extract_field<int>(value, "type");

        return FileEvent{
            canonicalize_uri(std::move(*uri)),
            change_type.value_or(2),
        };
    }
};

// ─────────────────────── DidChangeWatchedFilesParams ───────────────────────
//
// LSP workspace/didChangeWatchedFiles notification params:
//   { "changes": FileEvent[] }

struct DidChangeWatchedFilesParams {
    std::vector<FileEvent> changes;

    [[nodiscard]] static std::optional<DidChangeWatchedFilesParams>
    from_json(const JsonValue& params) {
        if (!params.is_object() || !params.has("changes")) {
            return std::nullopt;
        }

        const auto& changes_val = params["changes"];
        if (!changes_val.is_array()) {
            return std::nullopt;
        }

        std::vector<FileEvent> events;
        for (const auto& item : changes_val.as_array()) {
            auto event = FileEvent::from_json(item);
            if (event) {
                events.push_back(std::move(*event));
            }
        }

        return DidChangeWatchedFilesParams{std::move(events)};
    }
};

} // namespace luma::lsp::params

#endif // LUMA_LSP_PARAMS_HPP
