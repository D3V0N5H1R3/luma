#ifndef LUMA_LSP_DEFINITION_RESOLVER_HPP
#define LUMA_LSP_DEFINITION_RESOLVER_HPP

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include "lsp_analysis_cache.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_optional_ref.hpp"
#include "lsp_symbol_lookup.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_types.hpp"
#include "protocol/uri_utils.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════════════════════
// DefinitionResolver — translates symbol lookup results into LSP Locations.
//
// Takes a symbol name (plain or qualified) and resolves it to the Location
// of its definition, handling:
//   • Variable / constant definitions (top-level and local)
//   • User-defined functions
//   • Record types and record fields (via variable type → record → field)
//   • Symbols originating from included files (cross-file navigation)
//
// The resolver is stateless — it borrows the analysis data it needs through
// constructor parameters and callback functions.  Thread-safety is the
// caller's responsibility (hold appropriate locks before constructing).
//
// Usage:
//   DefinitionResolver resolver(uri, *result, tokens, stdlib_registry,
//                                find_analysis_fn, find_block_fn, all_results);
//   auto loc = resolver.resolve(qualified_name, token);
//   if (loc) { return serialise_location(*loc); }
// ═══════════════════════════════════════════════════════════════════════════

class DefinitionResolver {
public:
    // Callback: look up an AnalysisResult by URI (for cross-file resolution).
    using FindAnalysisFn = std::function<optional_ref<const AnalysisResult>(const std::string&)>;

    // Callback: find the brace-delimited block range around a source location
    // (used for narrowing record field searches).
    using FindBlockFn = std::function<Range(const std::vector<Token>&, const SourceLocation&)>;

    // Construct a resolver bound to a specific document context.
    //
    // Parameters:
    //   uri            — canonical URI of the document under the cursor
    //   result         — analysis result for that document
    //   tokens         — reference to result.semantic.tokens (convenience)
    //   find_analysis  — callback to locate analysis results for other URIs
    //   find_block     — callback to compute a block range around a location
    //   all_results    — all cached analysis results (for cross-file fallback)
    //   cache          — optional analysis cache; when it carries a populated
    //                    cross-file symbol index, cross-file resolution uses an
    //                    O(1) index lookup instead of scanning all_results.
    //
    // LIFETIME: all reference parameters (result, tokens, all_results) must
    // remain valid for the lifetime of this object.  In practice this means
    // the caller must hold the ReadStateLock that protects the analysis
    // cache for the entire lifetime of the DefinitionResolver instance.
    DefinitionResolver(const std::string& uri, const AnalysisResult& result,
                       const std::vector<Token>& tokens, FindAnalysisFn find_analysis,
                       FindBlockFn find_block,
                       const std::unordered_map<std::string, AnalysisResult>& all_results,
                       const LspAnalysisCache* cache = nullptr)
        : uri_{uri},
          result_{result},
          tokens_{tokens},
          lookup_{result},
          find_analysis_{std::move(find_analysis)},
          find_block_{std::move(find_block)},
          all_results_{all_results},
          cache_{cache} {}

    // ─── Primary API ─────────────────────────────────────────────────

    // Resolve a symbol to its definition location.
    //
    // Parameters:
    //   qualified_name — fully qualified name (e.g. "Ns.func"), or plain name
    //   token          — the token under the cursor
    //   token_index    — index of `token` in the token vector
    //
    // Returns the definition Location, or std::nullopt if unresolvable.
    [[nodiscard]] std::optional<Location>
    resolve(const std::string& qualified_name, const Token& token, std::size_t token_index) const {
        // Step 1: record field navigation (e.g. cursor on `obj.field`).
        if (auto field_loc = try_resolve_field(token, token_index)) {
            return field_loc;
        }

        // Step 2: local file definition / function lookup.
        if (auto local_loc = try_resolve_local(qualified_name, token)) {
            return local_loc;
        }

        // Step 3: cross-file fallback search.
        return try_resolve_cross_file(qualified_name, token);
    }

    // ─── Targeted resolvers ──────────────────────────────────────────
    //
    // Exposed individually so callers can invoke a specific resolution
    // strategy when the symbol kind is already known.

    // Resolve a record field definition (cursor on `field` in `obj.field`).
    [[nodiscard]] std::optional<Location> try_resolve_field(const Token& token,
                                                            std::size_t token_index) const {
        if (token_index < 2 || tokens_[token_index - 1].type != TokenType::Dot ||
            tokens_[token_index - 2].type != TokenType::Identifier) {
            return std::nullopt;
        }

        const auto& var_name = tokens_[token_index - 2].lexeme;
        const auto& field_name = token.lexeme;

        // Skip stdlib modules and choice types — those are handled elsewhere.
        if (result_.semantic.symbols.choice_variants.contains(var_name)) {
            return std::nullopt;
        }

        const auto resolved = resolve_variable_type(result_, var_name, token.location.line);
        if (!resolved.has_value()) {
            return std::nullopt;
        }

        const auto& var_type = resolved->type_name;
        auto rec_ref = lookup_.find_record(var_type);
        if (!rec_ref) {
            return std::nullopt;
        }

        for (const auto& [fname, ftype] : rec_ref->fields) {
            if (fname == field_name) {
                auto rdef_ref = lookup_.find_definition(var_type);
                if (rdef_ref) {
                    const auto block_range = find_block_(tokens_, rdef_ref->location);
                    auto field_range = find_identifier_range_bounded(
                        tokens_, field_name, block_range.start.line, block_range.end.line,
                        find_identifier_range(tokens_, rdef_ref->location, var_type));
                    return Location{uri_, field_range};
                }
                break;
            }
        }

        return std::nullopt;
    }

    // Resolve a symbol definition in the current file.
    [[nodiscard]] std::optional<Location> try_resolve_local(const std::string& qualified_name,
                                                            const Token& token) const {
        const auto& defs = result_.semantic.symbols.definitions;
        const auto& user_funcs = result_.semantic.symbols.user_functions;
        const auto resolved =
            resolve_symbol_in_file(defs, user_funcs, qualified_name, token.lexeme);

        if (resolved.kind == SymbolResolution::Kind::Definition) {
            return resolve_definition_location(resolved.def_info->location, qualified_name,
                                               token.lexeme);
        }

        if (resolved.kind == SymbolResolution::Kind::UserFunction) {
            return Location{
                uri_, find_identifier_range(tokens_, resolved.func_info->location, token.lexeme)};
        }

        return std::nullopt;
    }

    // Search all cached analysis results for the symbol definition.
    [[nodiscard]] std::optional<Location> try_resolve_cross_file(const std::string& qualified_name,
                                                                 const Token& token) const {
        // Fast path: when the cache exposes a populated cross-file symbol index,
        // resolve in O(1). The index is authoritative — it mirrors every cached
        // file's definitions/user_functions — so a miss means the symbol is not
        // defined in any cached file, and we can return without scanning.
        // (try_resolve_local already failed here, so the current file never holds
        // the symbol; excluding uri_ matches the linear scan's self-skip.)
        if (cache_ != nullptr && cache_->has_symbol_index()) {
            for (const auto& key : {std::cref(qualified_name), std::cref(token.lexeme)}) {
                if (auto hit = cache_->lookup_symbol(key.get(), uri_)) {
                    if (auto other = find_analysis_(hit->uri)) {
                        return Location{hit->uri,
                                        find_identifier_range(other->semantic.tokens, hit->location,
                                                              token.lexeme)};
                    }
                }
            }
            return std::nullopt;
        }

        // Fallback: linear scan over all cached results (index not yet built).
        for (const auto& [other_uri, other_result] : all_results_) {
            if (other_uri == uri_) {
                continue;
            }

            const auto other_resolved = resolve_symbol_in_file(
                other_result.semantic.symbols.definitions,
                other_result.semantic.symbols.user_functions, qualified_name, token.lexeme);

            if (other_resolved.kind == SymbolResolution::Kind::Definition) {
                return Location{other_uri, find_identifier_range(other_result.semantic.tokens,
                                                                 other_resolved.def_info->location,
                                                                 token.lexeme)};
            }

            if (other_resolved.kind == SymbolResolution::Kind::UserFunction) {
                return Location{other_uri, find_identifier_range(other_result.semantic.tokens,
                                                                 other_resolved.func_info->location,
                                                                 token.lexeme)};
            }
        }

        return std::nullopt;
    }

private:
    // Resolve the Location for a SymbolDefinition, following symbol_origins
    // to the included file when the symbol was imported.
    [[nodiscard]] std::optional<Location>
    resolve_definition_location(const SourceLocation& loc, const std::string& qualified_name,
                                const std::string& plain_name) const {
        // Check if this symbol was imported from an included file.
        auto origin_it = result_.semantic.includes.symbol_origins.find(qualified_name);
        if (origin_it == result_.semantic.includes.symbol_origins.end()) {
            origin_it = result_.semantic.includes.symbol_origins.find(plain_name);
        }

        if (origin_it != result_.semantic.includes.symbol_origins.end()) {
            const auto& origin_path = origin_it->second;
            const auto origin_uri = luma::protocol::path_to_uri(origin_path);
            const auto origin_result = find_analysis_(origin_uri);
            if (origin_result) {
                return Location{origin_uri, find_identifier_range(origin_result->semantic.tokens,
                                                                  loc, plain_name)};
            }
            // Fall back to current file's tokens for range computation.
            return Location{origin_uri, find_identifier_range(tokens_, loc, plain_name)};
        }

        // Symbol defined in the current file.
        return Location{uri_, find_identifier_range(tokens_, loc, plain_name)};
    }

    const std::string& uri_;
    const AnalysisResult& result_;
    const std::vector<Token>& tokens_;
    SymbolLookup lookup_;
    FindAnalysisFn find_analysis_;
    FindBlockFn find_block_;
    // Borrowed reference — caller must keep the analysis cache locked for
    // the lifetime of this resolver (see constructor documentation).
    const std::unordered_map<std::string, AnalysisResult>& all_results_;
    // Optional accelerator: cross-file symbol index owned by the cache.
    // Borrowed; nullptr when unavailable, in which case the linear-scan
    // fallback over all_results_ is used.
    const LspAnalysisCache* cache_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_DEFINITION_RESOLVER_HPP
