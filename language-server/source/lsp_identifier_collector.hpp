#ifndef LUMA_LSP_IDENTIFIER_COLLECTOR_HPP
#define LUMA_LSP_IDENTIFIER_COLLECTOR_HPP

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "analysis/lexer/token_type.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Identifier occurrence collection utilities
//
// Centralise the repeated pattern of scanning tokens for
// identifier matches and returning Range or index results.
// ═══════════════════════════════════════════════════════════

// Collect 0-based Range objects for all Identifier tokens matching 'name'
// within an optional 1-based inclusive line range [scope_start, scope_end].
// Uses a linear token scan — prefer the index-based overload when an
// AnalysisResult with an identifier_index is available.
//
// When `encoder` is non-null the emitted ranges are converted from the lexer's
// codepoint columns to the client's UTF-16 columns (the LSP wire encoding); a
// null encoder leaves ranges in codepoint space (identity), matching the prior
// behaviour for callers that do their own conversion.
[[nodiscard]] std::vector<Range>
collect_identifier_ranges(const std::vector<Token>& tokens, std::string_view name,
                          int scope_start_1based = 0,
                          int scope_end_1based = std::numeric_limits<int>::max(),
                          const PositionEncoder* encoder = nullptr);

// ─── Index-based, scope-aware occurrence collection ───

// Query-scoped filters for collect_scoped_occurrences(). These stay constant
// while scanning multiple documents, so callers build the filter once and pass
// the per-document `result`/`doc_uri` separately.
struct ScopedOccurrenceFilter {
    // Only tokens preceded by this "Namespace." pattern are kept (empty = any).
    std::string_view namespace_prefix;
    // When true (with enclosing_function set), restrict to the function body.
    bool is_local{false};
    // Enclosing function whose body bounds a local-variable search.
    const std::optional<std::string>& enclosing_function;
    // URI of the document that owns the definition site.
    std::string_view origin_uri;
    // When false, the definition-site token in the origin document is excluded.
    bool include_declaration{true};
};

// Collect token indices from the identifier_index that pass scope and
// namespace filtering.  Returns indices into the token vector so each
// caller can map them to its own output type (Location, TextEdit, etc.).
//
// Filtering applied (in order):
//   1. Local-variable scope: if filter.is_local && filter.enclosing_function is
//      set, only tokens inside the enclosing function body range in the origin
//      document are kept.
//   2. Namespace prefix: if non-empty, only tokens preceded by the expected
//      "Namespace." pattern are kept.
//   3. Declaration skip: if filter.include_declaration is false and we are in
//      the origin document, the definition-site token is excluded.
[[nodiscard]] std::vector<std::size_t>
collect_scoped_occurrences(const AnalysisResult& result, const std::string& name,
                           std::string_view doc_uri, const ScopedOccurrenceFilter& filter);

} // namespace luma::lsp

#endif // LUMA_LSP_IDENTIFIER_COLLECTOR_HPP
