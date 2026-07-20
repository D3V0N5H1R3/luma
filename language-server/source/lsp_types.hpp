#ifndef LUMA_LSP_TYPES_HPP
#define LUMA_LSP_TYPES_HPP

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "json/json.hpp"
#include "lsp_constants.hpp"
#include "protocol/error_codes.hpp"
#include "symbols/symbol_kind.hpp"

namespace luma::lsp {

using luma::json::JsonValue;

// ─────────────────────── LSP Protocol Types ───────────────────────

// LSP positions are 0-based (line and character).
struct Position {
    int line{0};
    int character{0};
};

// Parsed textDocument/position parameters common to many LSP requests.
struct TextDocumentPosition {
    std::string uri;
    int line{0};
    int character{0};

    // Extract URI and position from a JSON-RPC params object.
    // Returns nullopt if required fields are missing or malformed.
    [[nodiscard]] static std::optional<TextDocumentPosition> from_params(const JsonValue& params);
};

struct Range {
    Position start;
    Position end;
};

// ─────────────────────── Location ───────────────────────

struct Location {
    std::string uri;
    Range range;
};

// A related location attached to a diagnostic (e.g. "first declared here").
struct DiagnosticRelatedInformation {
    Location location;
    std::string message;
};

struct Diagnostic {
    Range range;
    int severity{constants::severity::error};
    std::string source;
    std::string message;
    std::string code;             // e.g. "E0006", "W0001" — empty if no code
    std::string code_description; // URL for documentation — empty if none
    std::vector<DiagnosticRelatedInformation> related_information;
    std::vector<int> tags; // DiagnosticTag values (e.g. Unnecessary, Deprecated)
};

// CompletionItemKind enum values are defined in lsp_constants.hpp
// (constants::completion_kind::*). InsertTextFormat values live in
// constants::insert_text_format::*.

// JSON-RPC standard error codes — shared with the DAP server.
// Re-exported into the luma::lsp namespace for backward compatibility.
inline constexpr int k_json_rpc_parse_error = protocol::k_json_rpc_parse_error;
inline constexpr int k_json_rpc_invalid_request = protocol::k_json_rpc_invalid_request;
inline constexpr int k_json_rpc_method_not_found = protocol::k_json_rpc_method_not_found;
inline constexpr int k_json_rpc_invalid_params = protocol::k_json_rpc_invalid_params;
inline constexpr int k_json_rpc_internal_error = protocol::k_json_rpc_internal_error;
inline constexpr int k_json_rpc_server_not_initialized =
    protocol::k_json_rpc_server_not_initialized;
inline constexpr int k_json_rpc_request_cancelled = protocol::k_json_rpc_request_cancelled;

// Error propagation policy (see also lsp_server.hpp):
// - Invalid client requests (malformed params) → throw InvalidParamsError
// - Expected absence of data (no symbol at position) → return std::nullopt / null JsonValue
// - Internal errors → log via log_message() + send_error() response
//
// Exception thrown by handlers when request params are structurally invalid.
// The dispatch loop catches this and returns an InvalidParams (-32602) error
// response, distinguishing it from "feature not applicable" (null result).
class InvalidParamsError : public std::runtime_error {
public:
    explicit InvalidParamsError(const std::string& message) : std::runtime_error(message) {}
};

struct CompletionItem {
    std::string label;
    int kind{constants::completion_kind::function};
    std::string detail;
    std::string documentation; // optional, shown in expanded view
    std::string insert_text;   // empty = use label
    int insert_text_format{constants::insert_text_format::plaintext};
    std::string sort_text;   // empty = use label for sorting
    std::string data;        // opaque payload returned to completionItem/resolve
    std::string filter_text; // empty = use label for filtering
};

// Forward declaration — defined later, implementation in lsp_types.cpp.
[[nodiscard]] JsonValue serialise_completion_item(const CompletionItem& item);

// Build the insert text for a callable completion item. When the client
// supports snippets, returns "name($0)" so the cursor lands between the
// parentheses; otherwise returns an empty string (the label is inserted
// verbatim). Centralises the snippet ternary shared across the completion
// providers.
[[nodiscard]] inline std::string call_snippet_insert_text(std::string_view name,
                                                          bool snippet_support) {
    return snippet_support ? std::string(name) + "($0)" : std::string{};
}

// Fluent builder for CompletionItem construction.
// Avoids long positional argument lists and makes optional fields explicit.
//
// Usage:
//   items.push_back(CompletionItemBuilder()
//       .label("Math")
//       .kind(constants::completion_kind::module_)
//       .detail("stdlib module")
//       .build());
class CompletionItemBuilder {
public:
    CompletionItemBuilder& label(std::string_view l) {
        item_.label = std::string(l);
        return *this;
    }

    CompletionItemBuilder& kind(int k) {
        item_.kind = k;
        return *this;
    }

    CompletionItemBuilder& detail(std::string_view d) {
        item_.detail = std::string(d);
        return *this;
    }

    CompletionItemBuilder& documentation(std::string_view doc) {
        item_.documentation = std::string(doc);
        return *this;
    }

    CompletionItemBuilder& insert_text(std::string_view text) {
        item_.insert_text = std::string(text);
        return *this;
    }

    CompletionItemBuilder& insert_text_format(int fmt) {
        item_.insert_text_format = fmt;
        return *this;
    }

    CompletionItemBuilder& sort_text(std::string_view s) {
        item_.sort_text = std::string(s);
        return *this;
    }

    CompletionItemBuilder& data(std::string_view d) {
        item_.data = std::string(d);
        return *this;
    }

    CompletionItemBuilder& filter_text(std::string_view f) {
        item_.filter_text = std::string(f);
        return *this;
    }

    [[nodiscard]] JsonValue build() const {
        return serialise_completion_item(item_);
    }

private:
    CompletionItem item_;
};

struct MarkupContent {
    std::string kind; // "markdown" or "plaintext"
    std::string value;
};

// ─────────────────────── Document Symbol ───────────────────────

// SymbolKind values from shared/symbols/symbol_kind.hpp are used directly
// via luma::SymbolKind and luma::to_lsp_symbol_kind().

struct DocumentSymbol {
    std::string name;
    luma::SymbolKind kind{luma::SymbolKind::Function};
    Range range;
    Range selection_range;
    std::vector<DocumentSymbol> children;
};

// ─────────────────────── Workspace Edit ───────────────────────

struct TextEdit {
    Range range;
    std::string new_text;
};

// uri → ordered list of edits.
struct WorkspaceEdit {
    std::map<std::string, std::vector<TextEdit>> changes;
};

// ─────────────────────── Code Action ───────────────────────

struct CodeAction {
    std::string title;
    std::string kind; // "quickfix", "refactor", etc.
    WorkspaceEdit edit;
    std::optional<Diagnostic> diagnostic; // the diagnostic this action fixes
};

// ─────────────────────── Semantic Tokens ───────────────────────

// Semantic token type indices — must match the order advertised in initialize.
enum class SemanticTokenType : int {
    Namespace = 0,
    Type = 1,
    Function = 2,
    Variable = 3,
    Parameter = 4,
    Keyword = 5,
    String = 6,
    Number = 7,
    Operator = 8,
    Decorator = 9, // @main, @test annotations
};

// Semantic token modifier bit flags — must match the order advertised in initialize.
enum class SemanticTokenModifier : int {
    None = 0,
    Definition = 1 << 0,
    Readonly = 1 << 1,
};

[[nodiscard]] constexpr int operator|(SemanticTokenModifier a, SemanticTokenModifier b) noexcept {
    return static_cast<int>(a) | static_cast<int>(b);
}

[[nodiscard]] constexpr int operator|(int a, SemanticTokenModifier b) noexcept {
    return a | static_cast<int>(b);
}

[[nodiscard]] constexpr int operator|(SemanticTokenModifier a, int b) noexcept {
    return static_cast<int>(a) | b;
}

// ─────────────────────── Serialisation ───────────────────────

[[nodiscard]] JsonValue serialise_position(const Position& pos);
[[nodiscard]] JsonValue serialise_range(const Range& range);
[[nodiscard]] JsonValue serialise_diagnostic(const Diagnostic& diag);
[[nodiscard]] JsonValue serialise_completion_item(const CompletionItem& item);
[[nodiscard]] JsonValue serialise_markup_content(const MarkupContent& content);
[[nodiscard]] JsonValue serialise_location(const Location& loc);
[[nodiscard]] JsonValue serialise_document_symbol(const DocumentSymbol& sym);
[[nodiscard]] JsonValue serialise_workspace_edit(const WorkspaceEdit& edit);
[[nodiscard]] JsonValue serialise_code_action(const CodeAction& action);

// ─────────────────────── LSP Response Builders ───────────────────────
//
// Higher-level convenience functions that produce ready-to-return JSON values
// for common LSP response patterns. These avoid the boilerplate of constructing
// intermediate structs and manually assembling ObjectType maps in handlers.

namespace lsp_builders {

// Build a Position object {line, character}.
[[nodiscard]] JsonValue position(int line, int character);

// Build a Range object {start, end}.
[[nodiscard]] JsonValue range(int start_line, int start_char, int end_line, int end_char);

// Build a MarkupContent object {kind, value}.
[[nodiscard]] JsonValue markup_content(std::string_view value, std::string_view kind = "markdown");

// Build a Hover response {contents, range}.
[[nodiscard]] JsonValue hover(std::string_view markdown_content, int start_line, int start_char,
                              int end_line, int end_char);

// Build a Hover response {contents} without a range.
[[nodiscard]] JsonValue hover(std::string_view markdown_content);

// Build a Location response {uri, range}.
[[nodiscard]] JsonValue location(std::string_view uri, int start_line, int start_char, int end_line,
                                 int end_char);

// Build a CompletionItem with common fields.
[[nodiscard]] JsonValue
completion_item(std::string_view label, int kind, std::string_view detail = "",
                std::string_view insert_text = "",
                int insert_text_format = constants::insert_text_format::plaintext);

// Build a Diagnostic object.
[[nodiscard]] JsonValue diagnostic(int start_line, int start_char, int end_line, int end_char,
                                   int severity, std::string_view message,
                                   std::string_view source = constants::diagnostic::source);

// Build a FoldingRange object {startLine, endLine, kind}.
[[nodiscard]] JsonValue folding_range(int start_line, int end_line, std::string_view kind);

// Build an InlayHint object {position, label, kind, paddingLeft, paddingRight}.
// LSP InlayHintKind: 1 = Type, 2 = Parameter.
[[nodiscard]] JsonValue inlay_hint(int line, int character, std::string_view label, int kind,
                                   bool padding_left = false, bool padding_right = false);

// Build a semantic tokens response {data} or {resultId, data}.
[[nodiscard]] JsonValue semantic_tokens_response(JsonValue::ArrayType data,
                                                 std::string_view result_id = "");

// Build a semantic tokens delta response {resultId, edits}.
[[nodiscard]] JsonValue semantic_tokens_delta_response(std::string_view result_id,
                                                       JsonValue::ArrayType edits);

// Build a semantic token edit object {start, deleteCount, data}.
[[nodiscard]] JsonValue semantic_token_edit(int64_t start, int64_t delete_count,
                                            JsonValue::ArrayType data);

// Build a call/type hierarchy item {name, kind, uri, range, selectionRange} with optional data.
[[nodiscard]] JsonValue hierarchy_item(std::string_view name, luma::SymbolKind kind,
                                       std::string_view uri, const Range& range,
                                       std::string_view data = "");

// Build a Location JSON value from a URI and a Range.
[[nodiscard]] inline JsonValue make_location(const std::string& uri, const Range& range) {
    return serialise_location(Location{uri, range});
}

// Build a Range JSON value from start/end positions.
[[nodiscard]] inline JsonValue make_range(const Position& start, const Position& end) {
    return serialise_range(Range{start, end});
}

// Build a TextEdit JSON value {range, newText}.
[[nodiscard]] inline JsonValue make_text_edit(const Range& range, std::string_view new_text) {
    return JsonValue(JsonValue::ObjectType{
        {"range", serialise_range(range)},
        {"newText", JsonValue(std::string(new_text))},
    });
}

// Invoke a sequence of resolver callables, returning the first non-empty
// result.  Each resolver must return std::string.  Short-circuits as soon
// as a resolver returns a non-empty string.
//
// Usage:
//   auto text = try_resolve(
//       [&] { return resolve_stdlib(token); },
//       [&] { return resolve_user_function(token); });
template <typename... Resolvers>
[[nodiscard]] inline std::string try_resolve(Resolvers&&... resolvers) {
    std::string result;
    (void)((result.empty() && ((void)(result = resolvers()), true)) || ...);
    return result;
}

} // namespace lsp_builders

} // namespace luma::lsp

#endif // LUMA_LSP_TYPES_HPP
