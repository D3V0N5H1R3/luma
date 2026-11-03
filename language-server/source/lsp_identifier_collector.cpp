#include "lsp_identifier_collector.hpp"

#include <utility>

#include "lsp_scope_stack.hpp"
#include "symbols/qualified_name.hpp"

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

namespace {

// The last segment of the namespace holding `qualified` ("A.B.f" → "B"), the
// form a receiver in source (`B.f`) is compared against.
[[nodiscard]] std::string_view namespace_segment(std::string_view qualified) {
    const auto dot = qualified.rfind('.');
    return dot == std::string_view::npos ? std::string_view{}
                                         : symbols::qualified_member(qualified.substr(0, dot));
}

} // namespace

const std::string* namespace_member_declared_at(const AnalysisResult& result,
                                                std::size_t token_index) {
    const auto start = token_range(result.semantic.tokens[token_index]).start;
    for (const auto& member : result.semantic.symbols.namespace_member_names) {
        if (member.line == start.line && member.character == start.character) {
            return &member.qualified_name;
        }
    }
    return nullptr;
}

std::string unqualified_binding_namespace(const AnalysisResult& result, std::size_t token_index) {
    if (const auto* member = namespace_member_declared_at(result, token_index)) {
        return std::string{namespace_segment(*member)};
    }
    const auto& imported = result.semantic.symbols.imported_names;
    if (const auto it = imported.find(result.semantic.tokens[token_index].lexeme);
        it != imported.end()) {
        return std::string{namespace_segment(it->second)};
    }
    return {};
}

std::string target_namespace_prefix(const AnalysisResult& result, std::size_t token_index) {
    if (auto ns = extract_namespace_prefix(result.semantic.tokens, token_index)) {
        return std::move(*ns);
    }
    return unqualified_binding_namespace(result, token_index);
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

    // Predicate: the token must bind to the target's namespace. A qualified
    // occurrence binds to the namespace named by its receiver. An unqualified
    // one binds to a namespace only when it is a member's own declaration name
    // or a name imported with `use`; any other bare use — including one inside
    // the namespace's block — refers to a top-level symbol, as in the type
    // checker. When no namespace prefix is expected, an occurrence preceded by
    // `Identifier Dot` (a receiver, e.g. `point.value`) denotes an unrelated
    // member/field and is rejected.
    auto has_valid_namespace = [&](std::size_t tok_idx) -> bool {
        const auto& tokens = result.semantic.tokens;
        if (extract_namespace_prefix(tokens, tok_idx).has_value()) {
            return !filter.namespace_prefix.empty() &&
                   has_namespace_prefix(tokens, tok_idx, filter.namespace_prefix);
        }
        return unqualified_binding_namespace(result, tok_idx) == filter.namespace_prefix;
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
    const bool exclude_declaration = !filter.include_declaration && doc_uri == filter.origin_uri;
    if (exclude_declaration && filter.namespace_prefix.empty()) {
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

        // Declaration-site exclusion: skip the token that sits exactly at the
        // definition's name when includeDeclaration is false. A namespace
        // member's declaration is the bare name recorded for it.
        if (decl_name_range.has_value()) {
            const Range occ_range = token_range(tok);
            if (occ_range.start.line == decl_name_range->start.line &&
                occ_range.start.character == decl_name_range->start.character) {
                continue;
            }
        }
        if (exclude_declaration && !filter.namespace_prefix.empty() &&
            namespace_member_declared_at(result, tok_idx) != nullptr) {
            continue;
        }

        indices.push_back(tok_idx);
    }
    return indices;
}

} // namespace luma::lsp
