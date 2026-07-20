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

class LspSemanticTokensHandler {
public:
    explicit LspSemanticTokensHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_semantic_tokens_full(const JsonValue& params);
    [[nodiscard]] JsonValue handle_semantic_tokens_full_delta(const JsonValue& params);
    [[nodiscard]] JsonValue handle_semantic_tokens_range(const JsonValue& params);

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
