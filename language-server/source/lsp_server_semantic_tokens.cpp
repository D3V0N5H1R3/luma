#include <algorithm>
#include <cstddef>
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
constexpr auto k_accept_all = [](const Token&) { return true; };

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

    // Append one single-line token segment (UTF-16 columns) in delta form.
    const auto emit = [&](int line, int col_utf16, int length, int type_idx, int mods) {
        if (length <= 0) {
            return;
        }
        const int delta_line = line - prev_line;
        const int delta_col = (delta_line == 0) ? col_utf16 - prev_col : col_utf16;

        raw_data.push_back(static_cast<int64_t>(delta_line));
        raw_data.push_back(static_cast<int64_t>(delta_col));
        raw_data.push_back(static_cast<int64_t>(length));
        raw_data.push_back(static_cast<int64_t>(type_idx));
        raw_data.push_back(static_cast<int64_t>(mods));

        prev_line = line;
        prev_col = col_utf16;
    };

    for (const auto& tok : tokens) {
        if (!accept(tok)) {
            continue;
        }

        const auto [type_idx, mods] = classify(tok);

        if (type_idx < 0) {
            continue;
        }

        const auto ext = token_extents(tok);
        const int start_line = ext.start_line_0based;
        const int end_line = ext.end_line_0based;
        const int start_cp = std::max(0, ext.start_col_0based);
        const int end_cp = ext.end_col_0based;

        if (start_line == end_line) {
            // Convert start column and length to UTF-16 code units for the wire.
            const int tok_col_s = encoder.to_utf16(start_line, start_cp);
            emit(start_line, tok_col_s, encoder.to_utf16(start_line, end_cp) - tok_col_s, type_idx,
                 mods);
            continue;
        }

        // Semantic tokens cannot span lines (the client does not negotiate
        // multilineTokenSupport), so a multi-line token such as a triple-quoted
        // string is emitted as one segment per line it covers. Without the
        // source text the line lengths are unknown, so the token is skipped.
        if (end_line < start_line || !encoder.valid()) {
            continue;
        }
        for (int line = start_line; line <= end_line; ++line) {
            const int col_s = line == start_line ? encoder.to_utf16(line, start_cp) : 0;
            const int col_e = line == end_line ? encoder.to_utf16(line, end_cp)
                                               : encoder.utf16_line_length(line).value_or(0);
            emit(line, col_s, col_e - col_s, type_idx, mods);
        }
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

std::vector<int64_t> encode_document_semantic_tokens(const AnalysisResult& result,
                                                     const StdlibRegistry& stdlib_registry) {
    const auto& tokens = result.semantic.tokens;
    return encode_semantic_tokens(
        tokens, result.encoder(),
        [&](const Token& tok) {
            // `tok` is an element of `tokens`, so its neighbours are adjacent.
            const auto index = static_cast<std::size_t>(&tok - tokens.data());
            const TokenNeighbours neighbours{.previous = index > 0 ? &tokens[index - 1] : nullptr,
                                             .next = index + 1 < tokens.size() ? &tokens[index + 1]
                                                                               : nullptr};
            return lsp::classify_token(tok, result, stdlib_registry, neighbours);
        },
        k_accept_all);
}

std::vector<int64_t>
LspSemanticTokensHandler::compute_semantic_token_data(const AnalysisResult& result) const {
    return encode_document_semantic_tokens(result, ctx_.stdlib_registry);
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
        // An empty token list (empty or token-less document) is a valid hit
        // too; requiring data made every such request recompute and hand out
        // a new resultId.
        if (entry && entry->source_hash != 0) {
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

} // namespace luma::lsp
