#ifndef LUMA_LSP_SYMBOL_RESOLVER_HPP
#define LUMA_LSP_SYMBOL_RESOLVER_HPP

#include <optional>
#include <string>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/string_hash.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// SymbolResolver — unified symbol lookup for LSP features.
//
// Provides common queries used by hover, go-to-definition,
// rename, references, and completion. Operates on read-only
// references to AnalysisResult; does not manage locks or state.
//
// Design note: symbol resolution here is *static/AST-based*,
// querying AnalysisResult maps produced by the type checker
// (definitions, user_functions, scoped_locals, token streams).
// This is fundamentally different from the DAP debugger's
// *runtime-based* symbol resolution (expression_evaluator.hpp),
// which queries live VM state via VMIntrospector (stack locals,
// closure upvalues, global environment bindings). Unifying
// these approaches is not practical — they operate on disjoint
// data domains. Shared vocabulary types live in shared/symbols/.
// ═══════════════════════════════════════════════════════════

// Return type conventions for find_* functions:
// - find_definition: returns const SymbolDefinition* (non-owning pointer, nullptr if not found)
// - find_function: returns const UserFunctionInfo* (non-owning pointer, nullptr if not found)
// - find_record: returns const RecordInfo* (non-owning pointer, nullptr if not found)
// - find_token_at: returns optional<size_t> (index into token vector)
// - find_enclosing_function: returns optional<string> (function name)
// - find_analysis: returns const AnalysisResult* (non-owning pointer, nullptr if not found)
// - find_identifier_range: returns Range (value semantics, always valid)
// - find_all_references: returns vector<IdentifierLocation> (collected matches)
// - find_file_id: returns optional<int> (nullopt if not found)
// All find_* functions return a "not found" sentinel (nullopt/nullptr)
// rather than throwing on missing items.

// Result of resolving a variable's type at a position.
struct ResolvedVariableType {
    std::string type_name;
    bool is_mutable{false};
};

// Result of resolving a symbol definition.
struct ResolvedDefinition {
    std::string uri;
    SourceLocation location;
    std::string type_string;
    std::string qualified_name;
};

// Resolve the type of a variable at a given source position.
// Uses scope-aware lookup: enclosing function locals → global local_variable_types.
[[nodiscard]] std::optional<ResolvedVariableType>
resolve_variable_type(const AnalysisResult& result, const std::string& name, int line_1based);

// Find the enclosing function for a given 1-based line number.
// Binary search on sorted_function_ranges.
[[nodiscard]] std::optional<std::string> find_enclosing_function(const AnalysisResult& result,
                                                                 int line_1based);

// Get the enclosing function's body range for a given 1-based line number.
// Returns {0, INT_MAX} when no enclosing function is found (i.e. top-level scope).
[[nodiscard]] std::pair<int, int> enclosing_scope_range(const AnalysisResult& result,
                                                        int line_1based);

// Check whether a name is a local variable (in local_variable_types
// but not in definitions or user_functions).
[[nodiscard]] bool is_local_variable(const AnalysisResult& result, const std::string& name);

// Check whether a local variable is in scope at a given 1-based line.
// Uses block-level scoped_locals when available; falls back to true
// (function-level scope) when no block scope data exists.
[[nodiscard]] bool is_in_scope(const AnalysisResult& result, const std::string& name,
                               int line_1based);

// Check whether a symbol is mutable, considering both unqualified and
// function-qualified keys in the mutable_locals set.
[[nodiscard]] bool
is_mutable_symbol(const AnalysisResult& result, const std::string& name,
                  const std::optional<std::string>& enclosing_function = std::nullopt);

// Find the definition of a symbol in the current document's analysis result.
// Checks definitions, then user_functions.
[[nodiscard]] std::optional<ResolvedDefinition>
resolve_definition_local(const std::string& name, const std::string& uri,
                         const AnalysisResult& result);

// Find the definition of a symbol across all cached analysis results.
// Falls back to cross-file search if not found locally.
[[nodiscard]] std::optional<ResolvedDefinition>
resolve_definition(const std::string& name, const std::string& current_uri,
                   const AnalysisResult& current_result,
                   const StringMap<AnalysisResult>& all_results);

// Find the token at a given 0-based (line, character) position.
// Returns the index into result.semantic.tokens, or nullopt if no token found.
[[nodiscard]] std::optional<std::size_t> find_token_at(const AnalysisResult& result, int line,
                                                       int character);

// Build a qualified name from a token and its preceding context.
// E.g., if cursor is on "func" after "Ns.", returns "Ns.func".
[[nodiscard]] std::string build_qualified_name(const std::vector<Token>& tokens,
                                               std::size_t token_idx);

// Collect all locations of an identifier, optionally scoped to a function.
// Returns a list of (uri, Range) pairs.
struct IdentifierLocation {
    std::string uri;
    Range range;
};

// Bundles the parameters for a reference search query.
// Constructed at the call site and consumed immediately — reference members
// are intentional (the struct is never stored or copied).
struct ReferenceQuery {
    const std::string& name;
    const std::string& namespace_prefix;
    bool is_local;
    const std::optional<std::string>& enclosing_function;
    const std::string& origin_uri;
    const AnalysisResult& origin_result;
    const StringMap<AnalysisResult>& all_results;
    bool include_declaration{true};
};

[[nodiscard]] std::vector<IdentifierLocation> find_all_references(const ReferenceQuery& query);

// Result of looking up a symbol by name in a file's definitions and
// user_functions maps. Used by go-to-definition and cross-file navigation.
struct SymbolResolution {
    enum class Kind {
        Definition,
        UserFunction,
        NotFound
    };
    Kind kind{Kind::NotFound};
    const SymbolDefinition* def_info{nullptr};
    const UserFunctionInfo* func_info{nullptr};
};

// Resolve a symbol by name in a single file's definitions and user_functions.
// Tries the qualified name first, then the plain name, in both maps.
[[nodiscard]] SymbolResolution resolve_symbol_in_file(const StringMap<SymbolDefinition>& defs,
                                                      const StringMap<UserFunctionInfo>& user_funcs,
                                                      const std::string& qualified_name,
                                                      const std::string& plain_name);

// ═══════════════════════════════════════════════════════════
// SymbolIndex — cross-file definition lookup cache.
//
// Maintains a name → {uri, definition_info} index built from
// all AnalysisResults. Turns the O(N) cross-file search in
// resolve_definition() into an O(1) hash lookup.
//
// Rebuild the index whenever analysis results change (e.g.
// after document edits or workspace indexing).
// ═══════════════════════════════════════════════════════════

struct SymbolIndexEntry {
    std::string uri;
    SourceLocation location;
    std::string type_string;
    std::string qualified_name;
};

class SymbolIndex {
public:
    // Rebuild the index from all analysis results.
    void rebuild(const StringMap<AnalysisResult>& all_results);

    // Look up a symbol by name. Returns the first matching entry, or nullopt.
    [[nodiscard]] std::optional<ResolvedDefinition> lookup(const std::string& name) const;

    // Clear the index.
    void clear();

    // Whether the index has been populated.
    [[nodiscard]] bool empty() const {
        return index_.empty();
    }

private:
    StringMap<std::vector<SymbolIndexEntry>> index_;
};

// Cached variant of resolve_definition() — uses a SymbolIndex for
// O(1) cross-file lookups. Falls back to the non-cached linear scan
// when the index is empty (not yet built).
[[nodiscard]] std::optional<ResolvedDefinition>
resolve_definition_cached(const std::string& name, const std::string& current_uri,
                          const AnalysisResult& current_result, const SymbolIndex& index,
                          const StringMap<AnalysisResult>& all_results);

} // namespace luma::lsp

#endif // LUMA_LSP_SYMBOL_RESOLVER_HPP
