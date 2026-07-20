#include "lsp_identifier_collector.hpp"

#include "lsp_scope_stack.hpp"

namespace luma::lsp {

std::vector<Range> collect_identifier_ranges(const std::vector<Token>& tokens,
                                             std::string_view name, int scope_start_1based,
                                             int scope_end_1based, const PositionEncoder* encoder) {
    std::vector<Range> ranges;
    for (const auto& tok : tokens) {
        if (tok.type == TokenType::Identifier && tok.lexeme == name &&
            tok.location.line >= scope_start_1based && tok.location.line <= scope_end_1based) {
            const Range r = token_range(tok);
            ranges.push_back(encoder != nullptr ? encoder->to_utf16(r) : r);
        }
    }
    return ranges;
}

std::vector<std::size_t> collect_scoped_occurrences(const AnalysisResult& result,
                                                    const std::string& name,
                                                    std::string_view doc_uri,
                                                    const ScopedOccurrenceFilter& filter) {
    std::vector<std::size_t> indices;
    auto idx_it = result.metadata.identifier_index.find(name);
    if (idx_it == result.metadata.identifier_index.end()) {
        return indices;
    }

    // Predicate: token must be within the enclosing function's body range.
    auto is_in_local_scope = [&](const Token& tok) -> bool {
        if (!filter.is_local || !filter.enclosing_function.has_value()) {
            return true;
        }
        auto fn_range_it =
            result.semantic.functions.function_body_ranges.find(*filter.enclosing_function);
        return doc_uri == filter.origin_uri &&
               fn_range_it != result.semantic.functions.function_body_ranges.end() &&
               tok.location.line >= fn_range_it->second.first &&
               tok.location.line <= fn_range_it->second.second;
    };

    // Predicate: token must have the expected namespace prefix (if any).
    // When no namespace prefix is expected, an occurrence preceded by
    // `Identifier Dot` (a receiver, e.g. `point.value`) denotes an unrelated
    // member/field, never the bare global or local we are resolving, so it is
    // rejected.
    auto has_valid_namespace = [&](std::size_t tok_idx) -> bool {
        if (filter.namespace_prefix.empty()) {
            return !extract_namespace_prefix(result.semantic.tokens, tok_idx).has_value();
        }
        return has_namespace_prefix(result.semantic.tokens, tok_idx, filter.namespace_prefix);
    };

    // Predicate: for a global (non-local) target, reject occurrences that a
    // local variable or parameter shadows in their own function scope. Those
    // tokens merely share the lexeme; they bind to a different symbol, so
    // renaming/listing the global must leave them untouched.
    auto binds_to_shadowing_local = [&](const Token& tok) -> bool {
        if (filter.is_local) {
            return false;
        }
        const ScopeStack scopes{result, tok.location.line};
        const auto sym = scopes.find_symbol(name);
        return sym.has_value() &&
               (sym->origin == ScopeKind::Block || sym->origin == ScopeKind::Function);
    };

    // Precompute the declaration's NAME range once — it depends only on `name`
    // and the definition location, not on the token being scanned. The stored
    // definition location is anchored at the declaration keyword, not the name,
    // so includeDeclaration=false must compare occurrences against the resolved
    // name range. Hoisted out of the token loop so the scan runs once per query
    // rather than once per occurrence. Uses the by-reference token vector (not
    // the raw-pointer line index, which can dangle on a copied result).
    std::optional<Range> decl_name_range;
    if (!filter.include_declaration && doc_uri == filter.origin_uri) {
        const auto& defs = result.semantic.symbols.definitions;
        if (auto def_it = defs.find(name); def_it != defs.end()) {
            decl_name_range =
                find_declaration_name_range(result.semantic.tokens, def_it->second.location, name);
        }
    }

    for (const std::size_t tok_idx : idx_it->second) {
        const auto& tok = result.semantic.tokens[tok_idx];

        if (!is_in_local_scope(tok)) {
            continue;
        }

        if (!has_valid_namespace(tok_idx)) {
            continue;
        }

        if (binds_to_shadowing_local(tok)) {
            continue;
        }

        // Declaration-site exclusion (see decl_name_range above): skip the token
        // that sits exactly at the definition's name when includeDeclaration is
        // false.
        if (decl_name_range.has_value()) {
            const Range occ_range = token_range(tok);
            if (occ_range.start.line == decl_name_range->start.line &&
                occ_range.start.character == decl_name_range->start.character) {
                continue;
            }
        }

        indices.push_back(tok_idx);
    }
    return indices;
}

} // namespace luma::lsp
