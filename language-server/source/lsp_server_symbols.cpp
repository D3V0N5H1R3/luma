#include <format>
#include <string>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"
#include "json/json.hpp"
#include "lsp_brace_matcher.hpp"
#include "lsp_lock_utils.hpp"
#include "lsp_param_extraction.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_handler.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Document Symbol helpers
// ═══════════════════════════════════════════════════════════

// Find the range of a block declaration by scanning for the opening {
// and the matching closing } in the token stream.  Falls back to a
// single-line range if no braces are found.
// Free function — the actual implementation.
// Stateless: only inspects the provided tokens and location.
Range find_block_range(const std::vector<Token>& tokens, const SourceLocation& decl_loc) {
    // 0-based start position.
    const int start_line = decl_loc.line - 1;
    const int start_col = decl_loc.column - 1;

    // Scan forward from the declaration location, matching both line and column
    // to avoid picking up a brace from a different declaration on the same line.
    std::size_t begin_idx{0};
    bool found_start{false};

    for (std::size_t i{0}; i < tokens.size(); ++i) {
        if (tokens[i].location.line > decl_loc.line ||
            (tokens[i].location.line == decl_loc.line &&
             tokens[i].location.column >= decl_loc.column)) {
            begin_idx = i;
            found_start = true;

            break;
        }
    }

    if (!found_start) {
        return Range{.start = Position{.line = start_line, .character = start_col},
                     .end = Position{.line = start_line, .character = start_col}};
    }

    // Find the opening brace.
    std::size_t brace_idx{tokens.size()};

    for (std::size_t i = begin_idx; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::LeftBrace) {
            brace_idx = i;

            break;
        }
    }

    if (brace_idx == tokens.size()) {
        // No block — single-line range.
        return Range{.start = Position{.line = start_line, .character = start_col},
                     .end = Position{.line = start_line, .character = start_col}};
    }

    // Match braces to find the closing one using the shared helper.
    auto close_opt = find_matching_close_brace(tokens, brace_idx);

    if (!close_opt.has_value()) {
        return Range{.start = Position{.line = start_line, .character = start_col},
                     .end = Position{.line = start_line, .character = start_col}};
    }

    const auto close_idx = *close_opt;

    const auto end_ext = token_extents(tokens[close_idx]);

    return Range{
        .start = Position{.line = start_line, .character = start_col},
        .end = Position{.line = end_ext.end_line_0based, .character = end_ext.end_col_0based}};
}

// Handler method delegates to the free function.
Range LspSymbolHandler::find_block_range(const std::vector<Token>& tokens,
                                         const SourceLocation& decl_loc) const {
    return luma::lsp::find_block_range(tokens, decl_loc);
}

namespace {

// Builds child DocumentSymbol entries from a collection of named members.
// Each Member must expose a `name` data member of string type.
// Used for choice variant children (SymbolKind::Constant) and
// record field children (SymbolKind::Field).
template <typename Member>
[[nodiscard]] std::vector<DocumentSymbol>
build_named_member_children(const std::vector<Member>& members, luma::SymbolKind kind,
                            const Range& parent_range, const Range& parent_selection,
                            const std::vector<Token>& tokens) {
    std::vector<DocumentSymbol> children;
    children.reserve(members.size());

    for (const auto& member : members) {
        DocumentSymbol child;
        child.name = member.name;
        child.kind = kind;
        child.range = parent_range;
        child.selection_range = find_identifier_range_bounded(
            tokens, member.name, parent_range.start.line, parent_range.end.line, parent_selection);
        children.push_back(std::move(child));
    }

    return children;
}

} // namespace

std::vector<DocumentSymbol>
LspSymbolHandler::build_document_symbols(const std::vector<DeclarationPtr>& decls,
                                         const std::vector<Token>& tokens) const {
    std::vector<DocumentSymbol> symbols;

    for (const auto& decl_ptr : decls) {
        if (!decl_ptr) {
            continue;
        }

        DocumentSymbol sym;

        switch (decl_ptr->kind) {
            case DeclarationKind::Choice: {
                const auto& ch = static_cast<const ChoiceDeclaration&>(*decl_ptr);

                sym.name = ch.name;
                sym.kind = SymbolKind::Enum;
                sym.range = find_block_range(tokens, ch.location);

                sym.selection_range = find_identifier_range(tokens, ch.location, ch.name);

                // Add variants as children.
                sym.children = build_named_member_children(ch.variants, SymbolKind::Constant,
                                                           sym.range, sym.selection_range, tokens);

                symbols.push_back(std::move(sym));

                break;
            }
            case DeclarationKind::Function: {
                const auto& func = static_cast<const FunctionDeclaration&>(*decl_ptr);

                sym.name = func.name;
                sym.kind = SymbolKind::Function;
                sym.range = find_block_range(tokens, func.location);

                // selectionRange: just the name token.
                sym.selection_range = find_identifier_range(tokens, func.location, func.name);

                symbols.push_back(std::move(sym));

                break;
            }
            case DeclarationKind::Interface: {
                const auto& iface = static_cast<const InterfaceDeclaration&>(*decl_ptr);

                sym.name = iface.name;
                sym.kind = SymbolKind::Interface;
                sym.range = find_block_range(tokens, iface.location);

                sym.selection_range = find_identifier_range(tokens, iface.location, iface.name);

                symbols.push_back(std::move(sym));

                break;
            }
            case DeclarationKind::Namespace: {
                const auto& ns = static_cast<const NamespaceDeclaration&>(*decl_ptr);

                sym.name = ns.name;
                sym.kind = SymbolKind::Namespace;
                sym.range = find_block_range(tokens, ns.location);

                sym.selection_range = find_identifier_range(tokens, ns.location, ns.name);
                sym.children = build_document_symbols(ns.declarations, tokens);

                symbols.push_back(std::move(sym));

                break;
            }
            case DeclarationKind::Record: {
                const auto& rec = static_cast<const RecordDeclaration&>(*decl_ptr);

                sym.name = rec.name;
                sym.kind = SymbolKind::Struct;
                sym.range = find_block_range(tokens, rec.location);

                sym.selection_range = find_identifier_range(tokens, rec.location, rec.name);

                // Add fields as children.
                sym.children = build_named_member_children(rec.fields, SymbolKind::Field, sym.range,
                                                           sym.selection_range, tokens);

                symbols.push_back(std::move(sym));

                break;
            }
            case DeclarationKind::TypeAlias: {
                const auto& alias = static_cast<const TypeAliasDeclaration&>(*decl_ptr);

                sym.name = alias.name;
                sym.kind = SymbolKind::TypeAlias;

                sym.range = find_identifier_range(tokens, alias.location, alias.name);
                sym.selection_range = sym.range;

                symbols.push_back(std::move(sym));

                break;
            }
            default:
                break;
        }
    }
    return symbols;
}

// ═══════════════════════════════════════════════════════════
// Document Symbol
// ═══════════════════════════════════════════════════════════

// Recursively convert a DocumentSymbol tree from the lexer's codepoint columns
// to the client's UTF-16 columns for the wire response.
namespace {
void document_symbol_to_wire(DocumentSymbol& sym, const PositionEncoder& encoder) {
    sym.range = encoder.to_utf16(sym.range);
    sym.selection_range = encoder.to_utf16(sym.selection_range);
    for (auto& child : sym.children) {
        document_symbol_to_wire(child, encoder);
    }
}
} // namespace

JsonValue LspSymbolHandler::handle_document_symbol(const JsonValue& params) {
    const auto uri = extraction::extract_text_document_uri(params);
    if (!uri) {
        return JsonValue(JsonValue::ArrayType{});
    }

    return with_shared_state(ctx_.state_mutex, ctx_.doc_store, ctx_.analysis_cache,
                             ctx_.pending_uris, [&](ReadStateLock&) -> JsonValue {
                                 const auto cached = ctx_.find_analysis(*uri);

                                 if (!cached) {
                                     return JsonValue(JsonValue::ArrayType{});
                                 }

                                 // Use the cached AST (always populated after a successful parse).
                                 if (!cached->metadata.cached_program.has_value()) {
                                     return JsonValue(JsonValue::ArrayType{});
                                 }

                                 const auto& decls = cached->metadata.cached_program->declarations;
                                 auto symbols =
                                     build_document_symbols(decls, cached->semantic.tokens);

                                 // Convert codepoint columns to the client's
                                 // UTF-16 columns for the wire.
                                 const auto enc = cached->encoder();
                                 for (auto& sym : symbols) {
                                     document_symbol_to_wire(sym, enc);
                                 }

                                 JsonValue::ArrayType arr;
                                 arr.reserve(symbols.size());

                                 for (const auto& sym : symbols) {
                                     arr.push_back(serialise_document_symbol(sym));
                                 }

                                 return JsonValue(std::move(arr));
                             });
}

} // namespace luma::lsp
