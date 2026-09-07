#include <string>

#include "json/json.hpp"
#include "lsp_formatting_handler.hpp"
#include "lsp_param_extraction.hpp"
#include "lsp_params.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_text_formatter.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Document formatting
// ═══════════════════════════════════════════════════════════

JsonValue LspFormattingHandler::handle_formatting(const JsonValue& params) {
    const auto uri = extraction::extract_text_document_uri(params);
    if (!uri) {
        return JsonValue(JsonValue::ArrayType{});
    }

    auto state = ctx_.acquire_read_lock();
    const auto* doc_ptr = ctx_.doc_store.get_content(state.token(), *uri);
    if (doc_ptr == nullptr) {
        return JsonValue(JsonValue::ArrayType{});
    }

    // Read formatting options.
    const auto fmt_opts = params::FormattingOptions::from_json(params.get("options"));

    const auto& source = *doc_ptr;
    const auto formatted = format_luma_source(source, fmt_opts.tab_size);

    if (formatted == source) {
        return JsonValue(JsonValue::ArrayType{}); // no changes
    }

    // Return a single whole-document text edit.
    // Count lines for the end position.
    int line_count = 0;
    for (const char c : source) {
        if (c == '\n') {
            ++line_count;
        }
    }

    JsonValue::ArrayType edits;
    edits.emplace_back(JsonValue::ObjectType{
        {"range", serialise_range(Range{.start = Position{.line = 0, .character = 0},
                                        .end = Position{.line = line_count + 1, .character = 0}})},
        {"newText", JsonValue(formatted)},
    });

    return JsonValue(std::move(edits));
}

} // namespace luma::lsp
