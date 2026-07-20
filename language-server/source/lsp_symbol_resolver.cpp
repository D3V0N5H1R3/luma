#include "lsp_symbol_resolver.hpp"

#include <algorithm>
#include <limits>

#include "analysis/lexer/token_type.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_identifier_collector.hpp"
#include "lsp_scope_stack.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_type_formatter.hpp"

namespace luma::lsp {

std::optional<ResolvedVariableType>
resolve_variable_type(const AnalysisResult& result, const std::string& name, int line_1based) {
    const ScopeStack scopes{result, line_1based};
    auto sym = scopes.find_symbol(name);
    if (!sym.has_value() || !util::is_known_type(sym->type_name)) {
        return std::nullopt;
    }
    return ResolvedVariableType{.type_name = sym->type_name, .is_mutable = sym->is_mutable};
}

std::optional<std::string> find_enclosing_function(const AnalysisResult& result, int line_1based) {
    const auto& ranges = result.semantic.functions.sorted_function_ranges;
    if (ranges.empty()) {
        return std::nullopt;
    }

    // Find the last range whose start_line <= line_1based.
    auto it = std::upper_bound(
        ranges.begin(), ranges.end(), line_1based,
        [](int line, const AnalysisResult::FunctionRange& r) { return line < r.start_line; });

    // Walk backward through all candidates.
    while (it != ranges.begin()) {
        --it;
        if (it->start_line <= line_1based && line_1based <= it->end_line) {
            return it->name;
        }
    }
    return std::nullopt;
}

std::pair<int, int> enclosing_scope_range(const AnalysisResult& result, int line_1based) {
    const auto enclosing = find_enclosing_function(result, line_1based);
    if (enclosing.has_value()) {
        auto br_it = result.semantic.functions.function_body_ranges.find(*enclosing);
        if (br_it != result.semantic.functions.function_body_ranges.end()) {
            return br_it->second;
        }
    }
    return {0, std::numeric_limits<int>::max()};
}

bool is_local_variable(const AnalysisResult& result, const std::string& name) {
    return result.semantic.locals.local_variable_types.contains(name) &&
           !result.semantic.symbols.definitions.contains(name) &&
           !result.semantic.symbols.user_functions.contains(name);
}

bool is_in_scope(const AnalysisResult& result, const std::string& name, int line_1based) {
    // If no scoped_locals data exists, fall back to "always in scope".
    const auto enclosing = find_enclosing_function(result, line_1based);
    if (!enclosing.has_value()) {
        return true;
    }
    auto fn_it = result.semantic.locals.scoped_locals.find(*enclosing);
    if (fn_it == result.semantic.locals.scoped_locals.end()) {
        // No block scope data — treat as function-scoped (always in scope within function).
        return true;
    }
    auto sl_it = fn_it->second.find(name);
    if (sl_it == fn_it->second.end()) {
        // No block scope data — treat as function-scoped (always in scope within function).
        return true;
    }
    for (const auto& entry : sl_it->second) {
        if (line_1based >= entry.scope_start_line && line_1based <= entry.scope_end_line) {
            return true;
        }
    }
    return false;
}

bool is_mutable_symbol(const AnalysisResult& result, const std::string& name,
                       const std::optional<std::string>& enclosing_function) {
    if (result.semantic.locals.mutable_locals.contains(name)) {
        return true;
    }
    if (enclosing_function.has_value()) {
        return result.semantic.locals.mutable_locals.contains(*enclosing_function + ":" + name);
    }
    return false;
}

std::optional<ResolvedDefinition> resolve_definition_local(const std::string& name,
                                                           const std::string& uri,
                                                           const AnalysisResult& result) {
    // Check definitions.
    if (auto def = result.find_definition(name)) {
        return ResolvedDefinition{.uri = uri,
                                  .location = def->location,
                                  .type_string = def->type_string,
                                  .qualified_name = name};
    }

    // Check user functions.
    auto fn_it = result.semantic.symbols.user_functions.find(name);
    if (fn_it != result.semantic.symbols.user_functions.end()) {
        return ResolvedDefinition{.uri = uri,
                                  .location = fn_it->second.location,
                                  .type_string = fn_it->second.return_type,
                                  .qualified_name = name};
    }

    return std::nullopt;
}

std::optional<ResolvedDefinition>
resolve_definition(const std::string& name, const std::string& current_uri,
                   const AnalysisResult& current_result,
                   const std::unordered_map<std::string, AnalysisResult>& all_results) {
    // Try local first.
    auto local = resolve_definition_local(name, current_uri, current_result);
    if (local.has_value()) {
        return local;
    }

    // Cross-file search.
    for (const auto& [doc_uri, doc_result] : all_results) {
        if (doc_uri == current_uri) {
            continue;
        }

        auto found = resolve_definition_local(name, doc_uri, doc_result);
        if (found.has_value()) {
            return found;
        }
    }

    return std::nullopt;
}

std::optional<std::size_t> find_token_at(const AnalysisResult& result, int line, int character) {
    // LSP positions are 0-based, Luma SourceLocation is 1-based. The incoming
    // `character` is a UTF-16 code-unit column (default LSP position encoding);
    // convert it to the codepoint column the lexer uses before comparing against
    // token columns, so positions on lines with supplementary-plane characters
    // (emoji/astral text) resolve to the correct token.
    const int codepoint_char = result.to_codepoint_col(line, character);
    const int luma_line{line + 1};
    const int luma_col{codepoint_char + 1};

    // Use the per-line index for O(1) line lookup + small linear scan.
    const auto& line_index = result.metadata.line_index;
    const auto& tokens = result.semantic.tokens;
    const auto [start, end] = line_index.index_range(static_cast<std::size_t>(luma_line));

    for (std::size_t i = start; i < end; ++i) {
        const auto& tok = tokens[i];
        const int col_end = tok.location.column;
        const int col_start = token_start_column_1based(tok);
        if (luma_col >= col_start && luma_col < col_end) {
            return i;
        }
    }

    return std::nullopt;
}

std::string build_qualified_name(const std::vector<Token>& tokens, std::size_t token_idx) {
    std::string name = tokens[token_idx].lexeme;

    if (token_idx >= 2 && tokens[token_idx - 1].type == TokenType::Dot &&
        tokens[token_idx - 2].type == TokenType::Identifier) {
        name = tokens[token_idx - 2].lexeme + "." + name;
    }

    return name;
}

std::vector<IdentifierLocation> find_all_references(const ReferenceQuery& query) {
    std::vector<IdentifierLocation> locations;

    auto collect_from = [&](const std::string& doc_uri, const AnalysisResult& doc_result) {
        auto indices = collect_scoped_occurrences(
            doc_result, query.name, doc_uri,
            ScopedOccurrenceFilter{.namespace_prefix = query.namespace_prefix,
                                   .is_local = query.is_local,
                                   .enclosing_function = query.enclosing_function,
                                   .origin_uri = query.origin_uri,
                                   .include_declaration = query.include_declaration});

        for (const std::size_t tok_idx : indices) {
            locations.push_back(IdentifierLocation{
                .uri = doc_uri,
                .range = doc_result.to_wire(token_range(doc_result.semantic.tokens[tok_idx]))});
        }
    };

    if (query.is_local) {
        collect_from(query.origin_uri, query.origin_result);
    } else {
        for (const auto& [doc_uri, doc_result] : query.all_results) {
            collect_from(doc_uri, doc_result);
        }
    }

    return locations;
}

SymbolResolution
resolve_symbol_in_file(const std::unordered_map<std::string, SymbolDefinition>& defs,
                       const std::unordered_map<std::string, UserFunctionInfo>& user_funcs,
                       const std::string& qualified_name, const std::string& plain_name) {
    // Try qualified name first in definitions.
    auto def_it = defs.find(qualified_name);
    if (def_it != defs.end()) {
        return {.kind = SymbolResolution::Kind::Definition,
                .def_info = &def_it->second,
                .func_info = nullptr};
    }

    // Try plain name in definitions.
    def_it = defs.find(plain_name);
    if (def_it != defs.end()) {
        return {.kind = SymbolResolution::Kind::Definition,
                .def_info = &def_it->second,
                .func_info = nullptr};
    }

    // Try qualified name in user_functions.
    auto uf_it = user_funcs.find(qualified_name);
    if (uf_it != user_funcs.end()) {
        return {.kind = SymbolResolution::Kind::UserFunction,
                .def_info = nullptr,
                .func_info = &uf_it->second};
    }

    // Try plain name in user_functions.
    uf_it = user_funcs.find(plain_name);
    if (uf_it != user_funcs.end()) {
        return {.kind = SymbolResolution::Kind::UserFunction,
                .def_info = nullptr,
                .func_info = &uf_it->second};
    }

    return {.kind = SymbolResolution::Kind::NotFound, .def_info = nullptr, .func_info = nullptr};
}

// ═══════════════════════════════════════════════════════════
// SymbolIndex implementation
// ═══════════════════════════════════════════════════════════

void SymbolIndex::rebuild(const std::unordered_map<std::string, AnalysisResult>& all_results) {
    index_.clear();
    for (const auto& [uri, result] : all_results) {
        // Index definitions.
        for (const auto& [name, def] : result.semantic.symbols.definitions) {
            index_[name].push_back(SymbolIndexEntry{.uri = uri,
                                                    .location = def.location,
                                                    .type_string = def.type_string,
                                                    .qualified_name = name});
        }
        // Index user functions.
        for (const auto& [name, info] : result.semantic.symbols.user_functions) {
            index_[name].push_back(SymbolIndexEntry{.uri = uri,
                                                    .location = info.location,
                                                    .type_string = info.return_type,
                                                    .qualified_name = name});
        }
    }
}

std::optional<ResolvedDefinition> SymbolIndex::lookup(const std::string& name) const {
    auto it = index_.find(name);
    if (it == index_.end() || it->second.empty()) {
        return std::nullopt;
    }
    const auto& entry = it->second.front();
    return ResolvedDefinition{.uri = entry.uri,
                              .location = entry.location,
                              .type_string = entry.type_string,
                              .qualified_name = entry.qualified_name};
}

void SymbolIndex::clear() {
    index_.clear();
}

std::optional<ResolvedDefinition>
resolve_definition_cached(const std::string& name, const std::string& current_uri,
                          const AnalysisResult& current_result, const SymbolIndex& index,
                          const std::unordered_map<std::string, AnalysisResult>& all_results) {
    // Always try local first (same-file definitions take priority).
    auto local = resolve_definition_local(name, current_uri, current_result);
    if (local.has_value()) {
        return local;
    }

    // Use the index for cross-file lookup when available.
    if (!index.empty()) {
        auto cached = index.lookup(name);
        if (cached.has_value() && cached->uri != current_uri) {
            return cached;
        }
    }

    // Fallback: linear scan (index not built or symbol not in index).
    return resolve_definition(name, current_uri, current_result, all_results);
}

} // namespace luma::lsp
