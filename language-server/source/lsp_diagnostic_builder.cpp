#include "lsp_diagnostic_builder.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "common/utf8.hpp"
#include "lsp_constants.hpp"
#include "lsp_diagnostic_codes.hpp"
#include "lsp_position_utils.hpp"

namespace luma::lsp::diagnostic_builder {

int byte_col_to_utf16(std::string_view line_text, int byte_col_0) {
    return byte_offset_to_utf16_column(line_text, static_cast<std::size_t>(byte_col_0));
}

// Convert a 0-based CODEPOINT column within `line_text` to a byte offset. The
// lexer advances columns once per Unicode codepoint, so a diagnostic column is
// a codepoint index and must be walked through the UTF-8 line to find the
// matching byte position before any byte→UTF-16 conversion.
[[nodiscard]] static std::size_t codepoint_col_to_byte_offset(std::string_view line_text,
                                                              int codepoint_col_0) {
    std::size_t byte_off{0};
    int cols{0};
    while (byte_off < line_text.size() && cols < codepoint_col_0) {
        byte_off += static_cast<std::size_t>(
            luma::utf8_codepoint_len(static_cast<std::uint8_t>(line_text[byte_off])));
        ++cols;
    }
    return std::min(byte_off, line_text.size());
}

// Maps a Luma diagnostic severity to the corresponding LSP severity integer.
[[nodiscard]] static int severity_to_lsp(luma::Severity sev) noexcept {
    switch (sev) {
        case luma::Severity::Error:
            return constants::severity::error;
        case luma::Severity::Warning:
            return constants::severity::warning;
        case luma::Severity::Hint:
            return constants::severity::hint;
        default:
            return constants::severity::information;
    }
}

// Returns true if the diagnostic code should be tagged as "unnecessary" in LSP.
[[nodiscard]] static bool is_unnecessary_code_tag(const std::string& code) noexcept {
    return code == diagnostic_code::unused_variable || code == diagnostic_code::unused_function ||
           code == diagnostic_code::unused_parameter || code == diagnostic_code::unreachable_code;
}

// Returns the byte offset of the start of `line_1` (1-based) within `source`,
// using the precomputed `line_starts` cache when available, or walking the
// source text as a fallback.
static std::size_t find_line_start_offset(const std::string& source,
                                          const std::vector<std::size_t>& line_starts, int line_1) {
    if (!line_starts.empty()) {
        const auto idx = static_cast<std::size_t>(line_1 - 1);
        if (idx < line_starts.size()) {
            return line_starts[idx];
        }
    } else {
        int current_line{1};
        std::size_t offset{0};
        for (std::size_t i{0}; i < source.size() && current_line < line_1; ++i) {
            if (source[i] == '\n') {
                ++current_line;
                offset = i + 1;
            }
        }
        return offset;
    }
    return 0;
}

std::string_view get_line_text(const std::string& source,
                               const std::vector<std::size_t>& line_starts, int line_1) {
    if (line_starts.empty() || line_1 < 1) {
        return {};
    }

    const auto idx = static_cast<std::size_t>(line_1 - 1);

    if (idx >= line_starts.size()) {
        return {};
    }

    const auto start = line_starts[idx];
    auto end = source.size();

    if (idx + 1 < line_starts.size()) {
        end = line_starts[idx + 1];
    }

    // Strip trailing newline.
    if (end > start && source[end - 1] == '\n') {
        --end;
    }

    if (end > start && source[end - 1] == '\r') {
        --end;
    }

    return std::string_view{source}.substr(start, end - start);
}

int word_end_column(const std::string& source, int luma_line, int luma_col,
                    const std::vector<std::size_t>& line_starts) {
    const int col_0 = luma_col - 1; // 0-based codepoint column
    const std::size_t line_start = find_line_start_offset(source, line_starts, luma_line);

    // Walk to the byte offset of the starting codepoint column.
    std::size_t pos{line_start};
    for (int cp{0}; pos < source.size() && source[pos] != '\n' && cp < col_0; ++cp) {
        pos += static_cast<std::size_t>(utf8_codepoint_len(static_cast<std::uint8_t>(source[pos])));
    }

    if (pos >= source.size() || source[pos] == '\n') {
        return col_0 + 1; // at least one character wide
    }

    // Scan forward over identifier codepoints, counting columns. Bytes >= 0x80
    // are UTF-8 continuation/lead bytes of a Unicode letter, which the lexer
    // admits in identifiers, so they count as part of the word.
    int end_col_0{col_0};
    while (pos < source.size()) {
        const auto c = static_cast<unsigned char>(source[pos]);
        const bool is_ident = (std::isalnum(c) != 0) || c == '_' || c >= 0x80;
        if (!is_ident) {
            break;
        }
        pos += static_cast<std::size_t>(utf8_codepoint_len(static_cast<std::uint8_t>(c)));
        ++end_col_0;
    }

    return std::max(end_col_0, col_0 + 1); // at least one character wide
}

std::string diagnostic_doc_url(const std::string& code) {
    if (code.empty()) {
        return {};
    }
    // Link to the Luma user manual's error reference section.
    return "https://github.com/cschladetsch/luma/blob/main/documents/Luma_User_Manual.md#" + code;
}

Diagnostic make_whole_file_diagnostic(std::string message, int severity) {
    return Diagnostic{
        .range = Range{.start = Position{.line = 0, .character = 0},
                       .end = Position{.line = 0, .character = 0}},
        .severity = severity,
        .source = std::string(constants::diagnostic::source),
        .message = std::move(message),
    };
}

Diagnostic make_diagnostic(const luma::Diagnostic& diag, const std::string& source,
                           const std::string& uri, const std::vector<std::size_t>& line_starts) {
    const int severity = severity_to_lsp(diag.severity);

    const auto loc = diag.primary_location();
    const int start_line = loc.line > 0 ? loc.line - 1 : 0;
    const int start_col_cp = loc.column > 0 ? loc.column - 1 : 0; // 0-based codepoint column
    const int end_col_cp = word_end_column(source, loc.line, loc.column, line_starts);

    // Map codepoint columns → byte offsets → UTF-16 code units for LSP.
    const auto line_text = get_line_text(source, line_starts, loc.line);
    const int start_col = byte_offset_to_utf16_column(
        line_text, codepoint_col_to_byte_offset(line_text, start_col_cp));
    const int end_col =
        byte_offset_to_utf16_column(line_text, codepoint_col_to_byte_offset(line_text, end_col_cp));

    std::string message = diag.message;

    if (diag.hint.has_value()) {
        message += "\n\nHint: ";
        message += *diag.hint;
    }

    const auto code = diag.code_string();

    Diagnostic result{
        .range =
            Range{
                .start = Position{.line = start_line, .character = start_col},
                .end = Position{.line = start_line, .character = end_col},
            },
        .severity = severity,
        .source = std::string(constants::diagnostic::source),
        .message = std::move(message),
        .code = code,
        .code_description = diagnostic_doc_url(code),
        .related_information = {},
        // Diagnostic tags: mark unused symbols and unreachable code.
        .tags = is_unnecessary_code_tag(code)
                    ? std::vector<int>{constants::diagnostic_tag::unnecessary}
                    : std::vector<int>{},
    };

    // Include secondary spans as relatedInformation when URI is available.
    if (!uri.empty()) {
        for (const auto& span : diag.spans) {
            if (!span.is_primary && !span.label.empty()) {
                const int sl = span.start.line > 0 ? span.start.line - 1 : 0;
                const int sc_cp = span.start.column > 0 ? span.start.column - 1 : 0;
                const auto span_line = get_line_text(source, line_starts, span.start.line);
                const int sc = byte_offset_to_utf16_column(
                    span_line, codepoint_col_to_byte_offset(span_line, sc_cp));
                const int sc_end = byte_offset_to_utf16_column(
                    span_line, codepoint_col_to_byte_offset(span_line, sc_cp + 1));
                result.related_information.push_back(DiagnosticRelatedInformation{
                    .location =
                        Location{.uri = uri,
                                 .range = Range{.start = Position{.line = sl, .character = sc},
                                                .end = Position{.line = sl, .character = sc_end}}},
                    .message = span.label,
                });
            }
        }
    }

    return result;
}

} // namespace luma::lsp::diagnostic_builder
