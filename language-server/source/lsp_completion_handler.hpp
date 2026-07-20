#ifndef LUMA_LSP_COMPLETION_HANDLER_HPP
#define LUMA_LSP_COMPLETION_HANDLER_HPP

#include <optional>
#include <string>
#include <string_view>

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

class LspCompletionHandler {
public:
    explicit LspCompletionHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_completion(const JsonValue& params);
    [[nodiscard]] JsonValue handle_completion_resolve(const JsonValue& params);
    [[nodiscard]] JsonValue handle_signature_help(const JsonValue& params);

    // Common context for completion sub-handlers and strategies.
    struct CompletionContext {
        const std::string& uri;
        int line;
        std::size_t cursor_offset;
        std::size_t line_start;
        std::string_view line_prefix;
        const std::string& text;
    };

private:
    // Completion sub-handlers.
    [[nodiscard]] JsonValue handle_generic_completions(const CompletionContext& ctx);
    [[nodiscard]] std::optional<JsonValue>
    try_module_completion(const std::string& module_name) const;
    [[nodiscard]] std::optional<JsonValue> try_dot_completion(const CompletionContext& ctx,
                                                              const std::string& identifier);

    // Completion helpers.
    [[nodiscard]] std::optional<std::string>
    parse_module_name(const std::string& text, std::size_t line_start, std::size_t dot_pos);
    [[nodiscard]] JsonValue build_completion_item(std::string_view name, int kind,
                                                  std::string_view detail,
                                                  std::string_view insert_text, int format,
                                                  std::string_view sort_text,
                                                  const JsonValue& data);
    [[nodiscard]] CompletionContext build_completion_context(const LockToken& token,
                                                             const std::string& uri,
                                                             const std::string& text, int line,
                                                             int character) const;

    // Dot/generic completion (fallback after strategy chain).
    [[nodiscard]] JsonValue handle_dot_completions(const CompletionContext& ctx);

    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_COMPLETION_HANDLER_HPP
