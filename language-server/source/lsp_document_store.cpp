#include "lsp_document_store.hpp"

#include <algorithm>
#include <string>

#include "common/utf8.hpp"
#include "lsp_position_utils.hpp"

namespace luma::lsp {

std::size_t DocumentStore::position_to_offset(const LockToken& /*unused*/, const std::string& uri,
                                              const std::string& text, int line,
                                              int character) const {
    // Retrieve cached line-start offsets.
    const auto* doc = find_document(uri);

    // Temporary storage for the fallback (non-cached) path.
    std::vector<std::size_t> tmp_line_starts;

    if (doc == nullptr || doc->line_starts.empty()) {
        tmp_line_starts.reserve(128);
        tmp_line_starts.push_back(0);
        for (std::size_t i{0}; i < text.size(); ++i) {
            if (text[i] == '\n') {
                tmp_line_starts.push_back(i + 1);
            }
        }
    }

    const auto& line_starts =
        (doc != nullptr && !doc->line_starts.empty()) ? doc->line_starts : tmp_line_starts;

    if (line < 0 || static_cast<std::size_t>(line) >= line_starts.size()) {
        return text.size();
    }

    // Extract the line content (without newline) for UTF-16 conversion.
    const std::size_t line_offset = line_starts[static_cast<std::size_t>(line)];
    auto line_end = text.size();
    if (static_cast<std::size_t>(line) + 1 < line_starts.size()) {
        line_end = line_starts[static_cast<std::size_t>(line) + 1];
    }
    if (line_end > line_offset && text[line_end - 1] == '\n') {
        --line_end;
    }
    if (line_end > line_offset && text[line_end - 1] == '\r') {
        --line_end;
    }

    const std::string_view line_text(text.data() + line_offset, line_end - line_offset);
    const auto byte_off = utf16_column_to_byte_offset(line_text, character);

    return std::min(line_offset + byte_off, text.size());
}

} // namespace luma::lsp
