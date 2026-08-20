#include <format>
#include <string>
#include <vector>

#include "analysis/lexer/token_type.hpp"
#include "json/json.hpp"
#include "lsp_analysis_cache.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_identifier_collector.hpp"
#include "lsp_keyword_catalog.hpp"
#include "lsp_params.hpp"
#include "lsp_rename_handler.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Rename — validation and conflict detection helpers
// ═══════════════════════════════════════════════════════════

// Check whether `new_name` conflicts with an existing name in the
// same scope. Returns true if a conflict is detected. `target_name` is the
// symbol's current (pre-rename) name: renaming a symbol to its own name is
// always a no-op, never a conflict, but the definition/locals maps queried
// below necessarily already contain that name (it names the very symbol
// being renamed), so that case must be excluded up front or every rename
// to an unchanged name would be misreported as colliding with itself.
bool has_rename_conflict(const AnalysisResult& result, const std::string& new_name,
                         const std::string& target_name, bool is_local,
                         const std::optional<std::string>& enclosing_fn,
                         const std::string& rename_ns, const LspAnalysisCache& cache) {
    if (new_name == target_name) {
        return false;
    }
    if (is_local && enclosing_fn.has_value()) {
        auto fl_it = result.semantic.locals.function_locals.find(*enclosing_fn);
        if (fl_it != result.semantic.locals.function_locals.end() &&
            fl_it->second.contains(new_name)) {
            return true;
        }
        auto fi = result.semantic.symbols.user_functions.find(*enclosing_fn);
        if (fi != result.semantic.symbols.user_functions.end()) {
            for (const auto& p : fi->second.parameters) {
                if (p.name == new_name) {
                    return true;
                }
            }
        }
    } else {
        const std::string qualified_new_name =
            rename_ns.empty() ? new_name : rename_ns + "." + new_name;
        bool conflict_found = false;
        cache.for_each([&](const std::string&, const AnalysisResult& doc_result) {
            if (doc_result.semantic.symbols.definitions.contains(new_name) ||
                doc_result.semantic.symbols.definitions.contains(qualified_new_name) ||
                doc_result.semantic.symbols.user_functions.contains(new_name) ||
                doc_result.semantic.symbols.user_functions.contains(qualified_new_name)) {
                conflict_found = true;
            }
        });
        return conflict_found;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════
// Rename symbol
// ═══════════════════════════════════════════════════════════

JsonValue LspRenameHandler::handle_rename(const JsonValue& params) {
    // Use typed RenameParams for structured extraction of
    // textDocument/position and the newName field.
    auto rename_params = params::RenameParams::from_json(params);
    if (!rename_params || rename_params->new_name.empty()) {
        return {};
    }
    const std::string new_name = std::move(rename_params->new_name);

    // Validate that the new name is a legal Luma identifier.
    if (!util::is_valid_identifier(new_name)) {
        return {};
    }

    // Reject Luma reserved keywords as new names.
    if (is_reserved_keyword_name(new_name)) {
        return {};
    }

    return ctx_.resolve_token_context(params, [&](const TokenContext& ctx) -> JsonValue {
        const auto& uri = ctx.uri;

        const auto& result_ref = *ctx.result;
        const auto& tokens = result_ref.semantic.tokens;
        const auto& target_token = *ctx.token;

        // Do not rename stdlib modules.
        if (ctx_.stdlib_registry.has_module(target_token.lexeme)) {
            return {};
        }

        const std::string target_name = target_token.lexeme;

        // Detect namespace prefix at cursor position for namespace-aware rename.
        std::string rename_ns;
        {
            const std::size_t ti = ctx.token_idx;
            if (auto ns = extract_namespace_prefix(tokens, ti)) {
                rename_ns = std::move(*ns);
            }
        }

        // Scope-aware rename.
        // For local variables: restrict to same function scope.
        // For top-level names: rename across all cached documents.
        const bool is_local = is_local_variable(result_ref, target_name);
        const auto enclosing_fn =
            is_local ? find_enclosing_function(result_ref, target_token.location.line)
                     : std::nullopt;

        // Conflict detection: reject rename if the new name already exists
        // in the same scope (would create shadowing or duplicate definitions).
        if (has_rename_conflict(result_ref, new_name, target_name, is_local, enclosing_fn,
                                rename_ns, *ctx.cache)) {
            return {};
        }

        WorkspaceEdit edit;

        auto rename_in = [&](const std::string& doc_uri, const AnalysisResult& doc_result) {
            auto indices = collect_scoped_occurrences(
                doc_result, target_name, doc_uri,
                ScopedOccurrenceFilter{.namespace_prefix = rename_ns,
                                       .is_local = is_local,
                                       .enclosing_function = enclosing_fn,
                                       .origin_uri = uri});

            for (const std::size_t tok_idx : indices) {
                edit.changes[doc_uri].push_back(TextEdit{
                    .range = doc_result.to_wire(token_range(doc_result.semantic.tokens[tok_idx])),
                    .new_text = new_name});
            }
        };

        if (is_local) {
            rename_in(uri, result_ref);
        } else {
            for (const auto& [doc_uri, doc_result] : ctx.cache->entries()) {
                rename_in(doc_uri, doc_result);
            }
        }

        if (edit.changes.empty()) {
            return {};
        }

        return serialise_workspace_edit(edit);
    });
}

// ═══════════════════════════════════════════════════════════
// Prepare rename
// ═══════════════════════════════════════════════════════════

JsonValue LspRenameHandler::handle_prepare_rename(const JsonValue& params) {
    return ctx_.resolve_token_context(params, [&](const TokenContext& ctx) -> JsonValue {
        const auto& token = *ctx.token;

        // Only allow renaming identifiers.
        if (token.type != TokenType::Identifier) {
            return {};
        }

        // Do not rename stdlib modules.
        if (ctx_.stdlib_registry.has_module(token.lexeme)) {
            return {};
        }

        return JsonValue(JsonValue::ObjectType{
            {"range", serialise_range(ctx.result->to_wire(token_range(token)))},
            {"placeholder", JsonValue(token.lexeme)},
        });
    });
}

// ═══════════════════════════════════════════════════════════
// Linked editing ranges
// ═══════════════════════════════════════════════════════════

JsonValue LspRenameHandler::handle_linked_editing_range(const JsonValue& params) {
    return ctx_.resolve_token_context(params, [&](const TokenContext& ctx) -> JsonValue {
        const auto& result = *ctx.result;
        const auto& tokens = result.semantic.tokens;
        const auto& target = *ctx.token;
        if (target.type != TokenType::Identifier) {
            return {};
        }

        // For record field definitions: find all uses of the same field name
        // in record creation expressions within the same document.
        // For other identifiers: find matching definition + usages in same scope.
        const auto& id_index = result.metadata.identifier_index;
        auto idx_it = id_index.find(target.lexeme);
        if (idx_it == id_index.end() || idx_it->second.size() < 2) {
            return {};
        }

        // Only provide linked editing for identifiers that are local to a function.
        if (!is_local_variable(result, target.lexeme)) {
            return {};
        }

        const auto enclosing_fn = find_enclosing_function(result, target.location.line);
        if (!enclosing_fn.has_value()) {
            return {};
        }

        auto fn_range_it = result.semantic.functions.function_body_ranges.find(*enclosing_fn);
        if (fn_range_it == result.semantic.functions.function_body_ranges.end()) {
            return {};
        }

        // Collect occurrences within the enclosing function using scoped filtering.
        auto indices =
            collect_scoped_occurrences(result, target.lexeme, /*doc_uri=*/"",
                                       ScopedOccurrenceFilter{.namespace_prefix = "",
                                                              .is_local = true,
                                                              .enclosing_function = enclosing_fn,
                                                              .origin_uri = ""});

        JsonValue::ArrayType ranges;
        for (const std::size_t i : indices) {
            ranges.push_back(serialise_range(result.to_wire(token_range(tokens[i]))));
        }

        if (ranges.size() < 2) {
            return {};
        }

        return JsonValue(JsonValue::ObjectType{
            {"ranges", JsonValue(std::move(ranges))},
        });
    });
}

} // namespace luma::lsp
