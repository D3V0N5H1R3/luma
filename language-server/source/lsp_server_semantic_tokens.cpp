#include <algorithm>
#include <span>
#include <string>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "analysis/lexer/token_type.hpp"
#include "json/json.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_param_extraction.hpp"
#include "lsp_semantic_token_cache.hpp"
#include "lsp_semantic_tokens_handler.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_token_classifier.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

namespace {

// Predicate that accepts every token — used for full semantic token encoding.
constexpr auto k_accept_all = [](const Token&) {
    return true;
};

// Return the document's stored content hash, falling back to hashing the
// current content only when no stored hash is available. Lets freshly computed
// token data be tagged without re-hashing the whole document on the common
// path (the stored hash is maintained by the analysis pipeline).
[[nodiscard]] std::size_t stored_or_computed_hash(const DocumentStore& doc_store,
                                                  const LockToken& token, const std::string& uri) {
    const auto stored = doc_store.get_content_hash(token, uri);
    if (stored != 0) {
        return stored;
    }
    if (const auto* content = doc_store.get_content(token, uri)) {
        return std::hash<std::string>{}(*content);
    }
    return 0;
}

// Encode a sequence of tokens into the LSP semantic token delta format
// (deltaLine, deltaStart, length, tokenType, tokenModifiers), filtered by
// an arbitrary predicate.  Shared by full and range token computations.
//
// Token columns and lengths are converted from the lexer's codepoint columns
// to the client's UTF-16 code units via `encoder`; the two differ only for
// tokens on lines containing supplementary-plane characters (e.g. a string
// literal with an emoji), where a codepoint length would desynchronise the
// delta-encoded stream.
template <typename ClassifyFn, typename Predicate>
[[nodiscard]] std::vector<int64_t>
encode_semantic_tokens(std::span<const Token> tokens, const PositionEncoder& encoder,
                       ClassifyFn&& classify, Predicate&& accept) {
    constexpr std::size_t k_fields_per_semantic_token = 5;
    std::vector<int64_t> raw_data;
    raw_data.reserve(tokens.size() * k_fields_per_semantic_token);

    int prev_line{0};
    int prev_col{0};

    for (const auto& tok : tokens) {
        if (!accept(tok)) {
            continue;
        }

        const auto [type_idx, mods] = classify(tok);

        if (type_idx < 0) {
            continue;
        }

        const auto ext = token_extents(tok);
        const int tok_line = ext.start_line_0based;

        // Semantic tokens cannot span multiple lines.  A multi-line token (e.g.
        // a triple-quoted string) must be skipped rather than encoded, or its
        // oversized/negative length and deltaStart would corrupt every following
        // token in the delta-encoded stream.
        if (ext.start_line_0based != ext.end_line_0based) {
            continue;
        }

        const int start_cp = std::max(0, ext.start_col_0based);
        const int end_cp = ext.end_col_0based;
        if (end_cp - start_cp <= 0) {
            continue;
        }

        // Convert start column and length to UTF-16 code units for the wire.
        const int tok_col_s = encoder.to_utf16(tok_line, start_cp);
        const int length = encoder.to_utf16(tok_line, end_cp) - tok_col_s;

        if (length <= 0) {
            continue;
        }

        const int delta_line = tok_line - prev_line;
        const int delta_col = (delta_line == 0) ? tok_col_s - prev_col : tok_col_s;

        raw_data.push_back(static_cast<int64_t>(delta_line));
        raw_data.push_back(static_cast<int64_t>(delta_col));
        raw_data.push_back(static_cast<int64_t>(length));
        raw_data.push_back(static_cast<int64_t>(type_idx));
        raw_data.push_back(static_cast<int64_t>(mods));

        prev_line = tok_line;
        prev_col = tok_col_s;
    }

    return raw_data;
}

// Convert a vector of int64_t semantic token data into a JSON array.
[[nodiscard]] JsonValue::ArrayType to_json_array(const std::vector<int64_t>& data) {
    JsonValue::ArrayType arr;
    arr.reserve(data.size());
    for (const auto v : data) {
        arr.emplace_back(v);
    }
    return arr;
}

// Compute minimal delta edits between two semantic token data arrays.
// Finds the first and last differing positions and produces a single
// SemanticTokensEdit that replaces the changed region.
[[nodiscard]] JsonValue::ArrayType compute_delta_edits(const std::vector<int64_t>& old_data,
                                                       const std::vector<int64_t>& new_data) {
    JsonValue::ArrayType edits;

    // Find first difference.
    std::size_t start_diff = 0;
    while (start_diff < old_data.size() && start_diff < new_data.size() &&
           old_data[start_diff] == new_data[start_diff]) {
        ++start_diff;
    }

    // Find last difference (from the end).
    std::size_t old_end_match = 0;
    std::size_t new_end_match = 0;
    while (old_end_match < old_data.size() - start_diff &&
           new_end_match < new_data.size() - start_diff &&
           old_data[old_data.size() - 1 - old_end_match] ==
               new_data[new_data.size() - 1 - new_end_match]) {
        ++old_end_match;
        ++new_end_match;
    }

    if (start_diff < old_data.size() || start_diff < new_data.size()) {
        const std::size_t delete_count = old_data.size() - start_diff - old_end_match;
        const std::size_t insert_start = start_diff;
        const std::size_t insert_end = new_data.size() - new_end_match;

        JsonValue::ArrayType insert_data;
        for (std::size_t i = insert_start; i < insert_end; ++i) {
            insert_data.emplace_back(new_data[i]);
        }

        edits.push_back(lsp_builders::semantic_token_edit(static_cast<int64_t>(start_diff),
                                                          static_cast<int64_t>(delete_count),
                                                          std::move(insert_data)));
    }

    return edits;
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Semantic token classification
// ═══════════════════════════════════════════════════════════
// All classification logic now lives in lsp_token_classifier.hpp
// (token_class predicates, SymbolClassifier, and top-level classify_token).

std::pair<int, int> LspSemanticTokensHandler::classify_token(const Token& tok,
                                                             const AnalysisResult& result) const {
    return lsp::classify_token(tok, result, ctx_.stdlib_registry);
}

// ═══════════════════════════════════════════════════════════
// Token data encoding (shared between full, delta, and worker)
// ═══════════════════════════════════════════════════════════

std::vector<int64_t>
LspSemanticTokensHandler::compute_semantic_token_data(const AnalysisResult& result) const {
    const auto& tokens = result.semantic.tokens;
    return encode_semantic_tokens(
        tokens, result.encoder(), [&](const Token& tok) { return classify_token(tok, result); },
        k_accept_all);
}

// ═══════════════════════════════════════════════════════════
// Semantic tokens (full)
// ═══════════════════════════════════════════════════════════

JsonValue LspSemanticTokensHandler::handle_semantic_tokens_full(const JsonValue& params) {
    const auto uri = extraction::extract_text_document_uri(params);
    if (!uri) {
        return lsp_builders::semantic_tokens_response({});
    }

    // Cache hit path: if the document's stored content hash matches the hash
    // captured when tokens were last computed, return the pre-computed data
    // immediately. The stored hash is maintained by the analysis pipeline, so
    // there is no need to re-hash the whole document on this hot path.
    {
        auto state = ctx_.acquire_read_lock();
        const auto entry = ctx_.semantic_token_cache.get(*uri);
        if (entry && entry->source_hash != 0 && !entry->data.empty()) {
            if (ctx_.doc_store.get_content_hash(state.token(), *uri) == entry->source_hash) {
                return lsp_builders::semantic_tokens_response(to_json_array(entry->data),
                                                              entry->result_id);
            }
        }
    }

    // Slow path: compute tokens from the current analysis result.
    // Hold the lock while the analysis result reference is alive.
    std::vector<int64_t> raw_data;
    std::size_t content_hash{0};
    {
        auto state = ctx_.acquire_read_lock();
        auto cached = ctx_.find_analysis(*uri);
        if (!cached) {
            return lsp_builders::semantic_tokens_response({});
        }

        raw_data = compute_semantic_token_data(*cached);
        content_hash = stored_or_computed_hash(ctx_.doc_store, state.token(), *uri);
    }
    const auto result_id = ctx_.semantic_token_cache.update(*uri, raw_data, content_hash);

    return lsp_builders::semantic_tokens_response(to_json_array(raw_data), result_id);
}

// ═══════════════════════════════════════════════════════════
// Semantic tokens (full/delta)
// ═══════════════════════════════════════════════════════════

JsonValue LspSemanticTokensHandler::handle_semantic_tokens_full_delta(const JsonValue& params) {
    const auto uri = extraction::extract_text_document_uri(params);
    if (!uri) {
        return handle_semantic_tokens_full(params);
    }

    // If no previous result ID provided, fall back to full.
    auto prev_id = luma::json::try_extract_field<std::string>(params, "previousResultId");
    if (!prev_id) {
        return handle_semantic_tokens_full(params);
    }

    // All shared-state access is contained in this scope so the lock is
    // released automatically before any call to handle_semantic_tokens_full,
    // which acquires its own lock.
    struct DeltaState {
        std::vector<int64_t> old_data;
        std::vector<int64_t> new_data;
        std::size_t content_hash{0};
    };

    std::optional<DeltaState> delta;
    {
        auto state = ctx_.acquire_read_lock();

        const auto cached = ctx_.find_analysis(*uri);
        if (cached) {
            // Check if the previous result ID matches what we have cached.
            // Also determine whether the pre-computed data is current (source unchanged).
            const auto entry = ctx_.semantic_token_cache.get(*uri);
            if (entry && entry->result_id == *prev_id) {
                const auto old_data_snapshot = entry->data;

                bool new_data_is_cached{false};
                if (entry->source_hash != 0 &&
                    ctx_.doc_store.get_content_hash(state.token(), *uri) == entry->source_hash) {
                    new_data_is_cached = true;
                }

                // Compute new token data — skip recomputation when the cache is current.
                std::vector<int64_t> new_data;
                if (new_data_is_cached) {
                    new_data = old_data_snapshot;
                } else {
                    new_data = compute_semantic_token_data(*cached);
                }

                const std::size_t content_hash =
                    stored_or_computed_hash(ctx_.doc_store, state.token(), *uri);

                delta = DeltaState{.old_data = old_data_snapshot,
                                   .new_data = std::move(new_data),
                                   .content_hash = content_hash};
            }
        }
    } // shared-state lock released here

    if (!delta) {
        return handle_semantic_tokens_full(params);
    }

    // Fast path: if the data is identical, return an empty edit list.
    if (delta->old_data == delta->new_data) {
        const auto new_result_id = ctx_.semantic_token_cache.refresh_result_id(*uri);
        return lsp_builders::semantic_tokens_delta_response(new_result_id.value_or("0"), {});
    }

    // Compute minimal edits between old and new token data.
    auto edits = compute_delta_edits(delta->old_data, delta->new_data);

    // Update stored data and result ID.
    const auto new_result_id =
        ctx_.semantic_token_cache.update(*uri, std::move(delta->new_data), delta->content_hash);

    return lsp_builders::semantic_tokens_delta_response(new_result_id, std::move(edits));
}

// ═══════════════════════════════════════════════════════════
// Semantic tokens (range)
// ═══════════════════════════════════════════════════════════

JsonValue LspSemanticTokensHandler::handle_semantic_tokens_range(const JsonValue& params) {
    const auto tdr = extraction::extract_text_document_range(params);
    if (!tdr) {
        return lsp_builders::semantic_tokens_response({});
    }

    const auto& uri = tdr->uri;

    auto state = ctx_.acquire_read_lock();
    const auto cached = ctx_.find_analysis(uri);

    if (!cached) {
        return lsp_builders::semantic_tokens_response({});
    }

    // Extract the requested range (1-based for token comparison).
    const int range_start_line = tdr->range.start.line + 1;
    // LSP range end is exclusive at character level. Include the end line
    // and filter by character position for tokens on that line.
    const int range_end_line = tdr->range.end.line + 1;
    const int range_end_char = tdr->range.end.character;
    // The token columns compared below are codepoint-based, but the incoming
    // range end character is UTF-16; convert it once for the end-line filter.
    const int range_end_char_cp = cached->to_codepoint_col(tdr->range.end.line, range_end_char);

    const auto& tokens = cached->semantic.tokens;
    const auto& result_ref = *cached;

    // Tokens are emitted in source order (ascending line), so binary-search
    // the [range_start_line, range_end_line] window instead of iterating and
    // filtering every token. Character-level trimming on the end line is still
    // handled by the predicate below.
    const auto line_of = [](const Token& tok) {
        return tok.location.line;
    };
    const auto first = std::ranges::lower_bound(tokens, range_start_line, {}, line_of);
    const auto last = std::ranges::upper_bound(tokens, range_end_line, {}, line_of);

    // A client may send an inverted range (end line before start line). That
    // makes `last` precede `first`, so constructing a span from the pair would
    // yield a negative (huge) size — undefined behaviour. Treat an inverted
    // range as selecting no tokens.
    if (last < first) {
        return lsp_builders::semantic_tokens_response({});
    }

    const std::span<const Token> range_tokens{first, last};

    // Encode tokens in LSP delta format, filtering to the requested range.
    auto raw_data = encode_semantic_tokens(
        range_tokens, result_ref.encoder(),
        [&](const Token& tok) { return classify_token(tok, result_ref); },
        [&](const Token& tok) {
            if (tok.location.line == range_end_line) {
                if (range_end_char_cp == 0) {
                    return false;
                }
                if (token_col_start_0based(tok) >= range_end_char_cp) {
                    return false;
                }
            }
            return true;
        });

    return lsp_builders::semantic_tokens_response(to_json_array(raw_data));
}

} // namespace luma::lsp
