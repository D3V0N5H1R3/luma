#ifndef LUMA_LSP_DIAGNOSTIC_BUILDER_HPP
#define LUMA_LSP_DIAGNOSTIC_BUILDER_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "lsp_types.hpp"

namespace luma::lsp::diagnostic_builder {

// Convert a 0-based byte column offset within a line to a 0-based UTF-16
// code unit offset (as required by LSP).
[[nodiscard]] int byte_col_to_utf16(std::string_view line_text, int byte_col_0);

// Extract the text of a line (without newline) given the source and
// precomputed line starts.  `line_1` is 1-based.
[[nodiscard]] std::string_view
get_line_text(const std::string& source, const std::vector<std::size_t>& line_starts, int line_1);

// Return the 0-based end character column of the word starting at
// (luma_line, luma_col) (both 1-based) within source.
[[nodiscard]] int word_end_column(const std::string& source, int luma_line, int luma_col,
                                  const std::vector<std::size_t>& line_starts);

// Build a documentation URL for a diagnostic code (empty if none).
[[nodiscard]] std::string diagnostic_doc_url(const std::string& code);

// Build a diagnostic anchored at the very start of the document (a zero-width
// range at line 0, character 0). Used for whole-file, pipeline-level messages
// (analysis failures, timeouts) that have no meaningful source span.
[[nodiscard]] Diagnostic make_whole_file_diagnostic(std::string message, int severity);

// Convert a luma::Diagnostic (from the type checker / compiler) into an
// LSP Diagnostic.  When `uri` is non-empty, secondary spans are included
// as relatedInformation.
[[nodiscard]] Diagnostic make_diagnostic(const luma::Diagnostic& diag, const std::string& source,
                                         const std::string& uri = {},
                                         const std::vector<std::size_t>& line_starts = {});

} // namespace luma::lsp::diagnostic_builder

#endif // LUMA_LSP_DIAGNOSTIC_BUILDER_HPP
