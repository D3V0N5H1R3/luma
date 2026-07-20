#include "analysis/diagnostics/renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_builders.hpp"
#include "analysis/diagnostics/diagnostic_stats.hpp"
#include "runtime/cli/terminal.hpp"

namespace luma {

// Minimum underline width for zero-width spans (e.g. insertion points).
static constexpr std::size_t k_min_span_width = 1;

// Upper bound on the caret indent and underline width.  Span columns are
// display-only, but a column can originate from a corrupt or adversarial
// bytecode cache (deserialized as a raw u32 with no range check), where a
// negative or enormous value would otherwise drive an unbounded native
// allocation here.  4096 is far beyond any readable source line.
static constexpr std::size_t k_max_render_width = 4096;

// Reserve estimates for the rendered-string buffers, defined once so that
// format()'s aggregate reservation stays in step with the per-section
// renderers.  A span block is up to ~5 lines (context before, blank gutter,
// error line, caret, context after) of ~120 chars plus colour escapes; the
// trailer covers the main message, hint, and fixes.
static constexpr std::size_t k_header_reserve = 128;
static constexpr std::size_t k_span_reserve = 512;
static constexpr std::size_t k_trailer_reserve = 256;

// Map a Severity to the corresponding term:: colour function output.
[[nodiscard]] static std::string_view severity_color(Severity severity) noexcept {
    switch (severity) {
        case Severity::Error:
            return term::red();
        case Severity::Warning:
            return term::yellow();
        case Severity::Hint:
            return term::cyan();
        case Severity::Info:
            return {};
    }

    return term::red();
}

// Append `text` wrapped in `color` and a trailing reset() escape to `out`.
// Everything goes through std::string::append(string_view), so a string_view
// colour or text never forces the throwaway std::string that
// `out += std::string{view}` would allocate. When colour is disabled
// (non-TTY) or `color` is empty, both escapes collapse to nothing and only
// `text` is appended.
static void append_colored(std::string& out, std::string_view color, std::string_view text) {
    out.append(color);
    out.append(text);
    out.append(term::reset());
}

// Convenience wrapper returning `text` wrapped in `color` + reset() as a
// freshly-allocated string.
[[nodiscard]] static std::string colorize(std::string_view color, std::string_view text) {
    std::string out;
    append_colored(out, color, text);

    return out;
}

std::string DiagnosticRenderer::render_header(const Diagnostic& diag) const {
    std::string result;
    result.reserve(k_header_reserve);

    const auto loc = diag.primary_location();
    const auto* file = source_manager_.get_file(loc.file_id);
    const auto filename = (file != nullptr) ? file->path : std::string{"<unknown>"};

    const auto category_label = category_name(diag.category);
    const auto code_str = diag.code_string();

    const auto header = code_str.empty() ? std::format("{} in {}:{}:{}", category_label, filename,
                                                       loc.line, loc.column)
                                         : std::format("{}[{}] in {}:{}:{}", category_label,
                                                       code_str, filename, loc.line, loc.column);

    append_colored(result, severity_color(diag.severity), header);
    result += "\n";

    return result;
}

std::string DiagnosticRenderer::render_span(const DiagnosticSpan& span) const {
    std::string result;
    result.reserve(k_span_reserve);

    const auto span_loc = span.start;
    const auto* span_file = source_manager_.get_file(span_loc.file_id);

    if (span_file == nullptr) {
        return std::format("[unable to retrieve source for file_id {}]", span_loc.file_id);
    }

    const auto line_text = source_manager_.get_line(span_loc.file_id, span_loc.line);
    const auto line_num = std::to_string(span_loc.line);
    const auto padding = std::string(line_num.size(), ' ');
    const auto gutter_width = line_num.size();

    // Context line before the error line (N-1), rendered dimmed.
    // Note: the "before" context needs a bounds check (line > 1) while the
    // "after" context relies on get_line() returning empty for non-existent
    // lines.  Both delegate to format_context_line() for formatting.
    if (span_loc.line > 1) {
        const auto prev_text = source_manager_.get_line(span_loc.file_id, span_loc.line - 1);
        if (!prev_text.empty()) {
            result += format_context_line(span_loc.line - 1, prev_text, gutter_width, true);
        }
    }

    result += std::format("   {} |\n", padding);
    result += std::format("   {} | {}\n", line_num, line_text);

    // Caret / underline line (delegated so render_span stays a short orchestrator).
    result += render_caret_line(span, padding);

    // Context line after the error line (N+1), rendered dimmed.  Guard against
    // span_loc.line == INT_MAX (line/column can come from a corrupt bytecode
    // source-map): line + 1 would be signed overflow.  The "before" context is
    // already guarded by line > 1.
    if (span_loc.line < std::numeric_limits<int>::max()) {
        const auto next_text = source_manager_.get_line(span_loc.file_id, span_loc.line + 1);
        if (!next_text.empty()) {
            result += format_context_line(span_loc.line + 1, next_text, gutter_width, true);
        }
    }

    return result;
}

std::string DiagnosticRenderer::render_caret_line(const DiagnosticSpan& span,
                                                  std::string_view padding) {
    // Column offset for the caret: 1-based column converted to a 0-based indent.
    // Clamp to k_max_render_width so an out-of-range column (negative or huge —
    // e.g. from a corrupt/adversarial bytecode cache) cannot drive an unbounded
    // allocation.  The signed comparison runs first so a negative column yields
    // no indent instead of wrapping to a huge size_t.
    const auto caret_indent =
        span.start.column > 1
            ? std::min(static_cast<std::size_t>(span.start.column - 1), k_max_render_width)
            : std::size_t{0};
    const auto caret_padding = std::string(caret_indent, ' ');

    // Compute underline width from span.  Fall back to k_min_span_width for
    // zero-width spans.  The difference is computed in 64-bit to avoid signed
    // overflow for extreme columns, then clamped to k_max_render_width.
    const auto span_width =
        (span.end.line == span.start.line && span.end.column > span.start.column)
            ? static_cast<std::size_t>(
                  std::min<std::int64_t>(static_cast<std::int64_t>(span.end.column) -
                                             static_cast<std::int64_t>(span.start.column),
                                         static_cast<std::int64_t>(k_max_render_width)))
            : k_min_span_width;

    const auto underline = std::string(span_width, span.is_primary ? '^' : '-');

    std::string result = std::format("   {} | {}", padding, caret_padding);
    append_colored(result, span.is_primary ? term::red() : term::cyan(), underline);

    if (!span.label.empty()) {
        result.append(" ");
        result.append(span.label);
    }

    result += "\n";

    return result;
}

std::string DiagnosticRenderer::format_hint_line(std::string_view text) {
    auto result = colorize(term::cyan(), std::format("hint: {}", text));
    result += "\n";

    return result;
}

std::string DiagnosticRenderer::render_hint(const Diagnostic& diag) {
    if (diag.hint) {
        return format_hint_line(*diag.hint);
    }

    // Fall back to a type-specific hint derived from the error code.
    const auto auto_hint = diag_builders::auto_hint_for_code(diag.code);

    if (!auto_hint.empty()) {
        return format_hint_line(auto_hint);
    }

    return {};
}

std::string DiagnosticRenderer::render_fixes(const Diagnostic& diag) {
    std::string result;

    for (const auto& fix : diag.fixes) {
        std::string body{"fix: "};

        if (!fix.description.empty()) {
            body += fix.description;
        } else if (!fix.replacement.empty()) {
            body += std::format("replace with '{}'", fix.replacement);
        }

        result += colorize(term::green(), body);
        result += "\n";
    }

    return result;
}

std::string DiagnosticRenderer::format(const Diagnostic& diag) const {
    std::string result;
    result.reserve(k_header_reserve + (diag.spans.size() * k_span_reserve) + k_trailer_reserve);

    // Category and location header.
    result += render_header(diag);

    // Source context for each span.
    for (const auto& span : diag.spans) {
        result += render_span(span);
    }

    // Main message.
    result.append(diag.message);
    result.append("\n");

    // Hint (explicit or auto-generated from error code).
    result += render_hint(diag);

    // Suggested fixes.
    result += render_fixes(diag);

    return result;
}

void DiagnosticRenderer::render(const Diagnostic& diag) const {
    std::cerr << format(diag);
}

std::string DiagnosticRenderer::format_summary(const std::vector<Diagnostic>& diagnostics) {
    const auto stats = DiagnosticStats::from(diagnostics);

    if (!stats.has_errors() && !stats.has_warnings()) {
        return {};
    }

    std::string result;
    result.append(term::bold());

    if (stats.has_errors()) {
        result.append(term::red());
        result += std::to_string(stats.errors);
        result += " error";
        if (stats.errors != 1) {
            result += "s";
        }
    }

    if (stats.has_errors() && stats.has_warnings()) {
        result.append(term::reset());
        result.append(term::bold());
        result += " and ";
    }

    if (stats.has_warnings()) {
        result.append(term::yellow());
        result += std::to_string(stats.warnings);
        result += " warning";
        if (stats.warnings != 1) {
            result += "s";
        }
    }

    result += " emitted";
    result.append(term::reset());
    result += "\n";

    return result;
}

void DiagnosticRenderer::render_all(const std::vector<Diagnostic>& diagnostics) const {
    for (const auto& diag : diagnostics) {
        render(diag);
        std::cerr << "\n";
    }

    const auto summary = format_summary(diagnostics);

    if (!summary.empty()) {
        std::cerr << summary;
    }
}

std::string DiagnosticRenderer::format_context_line(int line_num, std::string_view text,
                                                    std::size_t gutter_width, bool dimmed) {
    std::string result;

    const auto line = std::format("   {:>{}} | {}\n", line_num, gutter_width, text);

    if (dimmed) {
        append_colored(result, term::dim(), line);
    } else {
        result.append(line);
    }

    return result;
}

} // namespace luma
