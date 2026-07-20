#ifndef LUMA_LSP_CODE_ACTION_HANDLER_HPP
#define LUMA_LSP_CODE_ACTION_HANDLER_HPP

#include <string>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

struct AnalysisResult;

class LspCodeActionHandler {
public:
    explicit LspCodeActionHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_code_action(const JsonValue& params);
    [[nodiscard]] JsonValue handle_code_lens(const JsonValue& params);
    [[nodiscard]] JsonValue handle_execute_command(const JsonValue& params);

    void collect_quick_fixes(const std::string& uri, const AnalysisResult& cached,
                             const LockToken& lock_token,
                             const std::vector<Diagnostic>& range_diags,
                             JsonValue::ArrayType& actions) const;
    void collect_refactorings(const std::string& uri, const AnalysisResult& cached,
                              const LockToken& lock_token, const JsonValue& params,
                              JsonValue::ArrayType& actions) const;

private:
    [[nodiscard]] JsonValue execute_show_references(const JsonValue& params);

    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_CODE_ACTION_HANDLER_HPP
