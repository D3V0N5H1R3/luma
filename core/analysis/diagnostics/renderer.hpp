#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/source/source_manager.hpp"

namespace luma {

// Formats diagnostics into human-readable messages for terminal output.
// Supports multi-span display with source context, carets, and hints.
class DiagnosticRenderer {
public:
    explicit DiagnosticRenderer(const SourceManager& source_manager)
        : source_manager_{source_manager} {}

    // Format a single diagnostic to a string.
    [[nodiscard]] std::string format(const Diagnostic& diag) const;

    // Format the summary line ("N error(s) and M warning(s) emitted").
    [[nodiscard]] static std::string format_summary(const std::vector<Diagnostic>& diagnostics);

    // Print a single diagnostic to stderr.
    void render(const Diagnostic& diag) const;

    // Print multiple diagnostics to stderr with a summary line.
    void render_all(const std::vector<Diagnostic>& diagnostics) const;

private:
    // Render the severity/category header with file location.
    [[nodiscard]] std::string render_header(const Diagnostic& diag) const;

    // Render a single source span with context lines and carets.
    [[nodiscard]] std::string render_span(const DiagnosticSpan& span) const;

    // Render the caret/underline line for a span, coloured by primary/secondary.
    [[nodiscard]] static std::string render_caret_line(const DiagnosticSpan& span,
                                                       std::string_view padding);

    // Render the hint section (explicit or auto-generated from error code).
    [[nodiscard]] static std::string render_hint(const Diagnostic& diag);

    // Render the suggested-fix section.
    [[nodiscard]] static std::string render_fixes(const Diagnostic& diag);

    // Format a single context line (before/after the error line) with gutter and optional dimming.
    [[nodiscard]] static std::string format_context_line(int line_num, std::string_view text,
                                                         std::size_t gutter_width, bool dimmed);

    // Format a hint line with colour: "hint: <text>\n".
    [[nodiscard]] static std::string format_hint_line(std::string_view text);

    const SourceManager& source_manager_;
};

} // namespace luma
