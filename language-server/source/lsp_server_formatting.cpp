#include <algorithm>
#include <string>
#include <string_view>

#include "json/json.hpp"
#include "lsp_formatting_handler.hpp"
#include "lsp_param_extraction.hpp"
#include "lsp_params.hpp"
#include "lsp_position_utils.hpp"
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

    // Return a single whole-document text edit ending exactly at the end of
    // the document: the last line (after the final newline, possibly empty)
    // and its length in UTF-16 code units. A position past the end is not a
    // valid LSP position and strict clients reject the edit.
    const auto last_newline = source.rfind('\n');
    const std::string_view last_line = last_newline == std::string::npos
                                           ? std::string_view{source}
                                           : std::string_view{source}.substr(last_newline + 1);
    const auto line_count = static_cast<int>(std::ranges::count(source, '\n'));
    const Position end{.line = line_count,
                       .character = byte_offset_to_utf16_column(last_line, last_line.size())};

    JsonValue::ArrayType edits;
    edits.emplace_back(JsonValue::ObjectType{
        {"range", serialise_range(Range{.start = Position{.line = 0, .character = 0}, .end = end})},
        {"newText", JsonValue(formatted)},
    });

    return JsonValue(std::move(edits));
}

} // namespace luma::lsp
