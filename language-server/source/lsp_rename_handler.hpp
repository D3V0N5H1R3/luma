#ifndef LUMA_LSP_RENAME_HANDLER_HPP
#define LUMA_LSP_RENAME_HANDLER_HPP

#include <optional>
#include <string>

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

struct AnalysisResult;
class LspAnalysisCache;

// Check whether `new_name` conflicts with an existing name in the same scope
// (an existing local, parameter, or top-level definition/function). Returns
// true if a conflict is detected. `target_name` is the symbol's current
// (pre-rename) name: renaming a symbol to its own name is always a no-op,
// never a conflict, and is excluded up front (the definition/locals maps
// necessarily already contain that name, since it names the very symbol
// being renamed). Declared here (rather than file-local) so it is directly
// unit-testable without racing the asynchronous analysis pipeline.
[[nodiscard]] bool has_rename_conflict(const AnalysisResult& result, const std::string& new_name,
                                       const std::string& target_name, bool is_local,
                                       const std::optional<std::string>& enclosing_fn,
                                       const std::string& rename_ns, const LspAnalysisCache& cache);

class LspRenameHandler {
public:
    explicit LspRenameHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_rename(const JsonValue& params);
    [[nodiscard]] JsonValue handle_prepare_rename(const JsonValue& params);
    [[nodiscard]] JsonValue handle_linked_editing_range(const JsonValue& params);

private:
    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_RENAME_HANDLER_HPP
