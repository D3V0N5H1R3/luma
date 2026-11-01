#ifndef LUMA_LSP_SEMANTIC_TOKENS_HANDLER_HPP
#define LUMA_LSP_SEMANTIC_TOKENS_HANDLER_HPP

#include <cstdint>
#include <utility>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

struct AnalysisResult;
class StdlibRegistry;

// The document's semantic tokens in LSP delta encoding (5 integers per token,
// UTF-16 columns; a multi-line token is emitted once per line it covers).
[[nodiscard]] std::vector<int64_t>
encode_document_semantic_tokens(const AnalysisResult& result,
                                const StdlibRegistry& stdlib_registry);

class LspSemanticTokensHandler {
public:
    explicit LspSemanticTokensHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_semantic_tokens_full(const JsonValue& params);

    // Token classification and encoding used by the analysis pipeline.
    [[nodiscard]] std::pair<int, int> classify_token(const Token& tok,
                                                     const AnalysisResult& result) const;
    [[nodiscard]] std::vector<int64_t>
    compute_semantic_token_data(const AnalysisResult& result) const;

private:
    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_SEMANTIC_TOKENS_HANDLER_HPP
