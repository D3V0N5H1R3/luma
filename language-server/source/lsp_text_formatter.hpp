#ifndef LUMA_LSP_TEXT_FORMATTER_HPP
#define LUMA_LSP_TEXT_FORMATTER_HPP

#include <string>

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Line/token-based Luma source formatter engine.
//
// This is a deliberately line-oriented formatter that degrades
// gracefully on incomplete code while typing, rather than requiring
// a complete, parseable program the way an AST-based formatter would.
//
// The engine is pure text-in / text-out: it has no dependency on the
// LSP protocol, the document store, or any locking, so it can be unit
// tested directly.
// ═══════════════════════════════════════════════════════════

// Format a whole Luma source document. Returns the formatted text.
// Rules:
//   - Indent with spaces (tab_size per level).
//   - Normalize trailing whitespace per line.
//   - Ensure single trailing newline at end of file.
//   - Normalize blank lines: max 2 consecutive.
//   - Normalize spacing around binary operators.
//   - Ensure blank line between top-level declarations.
[[nodiscard]] std::string format_luma_source(const std::string& source, int tab_size);

// Format a sub-range of a document, preserving base indentation.
// `range_text` is the already-extracted text for the line range.
[[nodiscard]] std::string format_range_text(const std::string& range_text, int tab_size);

} // namespace luma::lsp

#endif // LUMA_LSP_TEXT_FORMATTER_HPP
