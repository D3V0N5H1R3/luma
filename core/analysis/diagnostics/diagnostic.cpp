#include "analysis/diagnostics/diagnostic.hpp"

#include <cassert>
#include <format>
#include <string>
#include <utility>

namespace luma {

// ─── Diagnostic ───

std::string Diagnostic::code_string() const {
    if (code == DiagnosticCode::None) {
        return {};
    }

    const auto code_number = static_cast<int>(code);
    const auto cat = category_of(code_number).value_or(DiagnosticCategory::Compile);

    return std::format("{}{:04d}", category_prefix(cat), display_number(cat, code_number));
}

SourceLocation Diagnostic::primary_location() const {
    for (const auto& span : spans) {
        if (span.is_primary) {
            return span.start;
        }
    }

    return {};
}

// ─── DiagnosticBuilder ───

DiagnosticBuilder::DiagnosticBuilder(Severity severity, std::string message,
                                     DiagnosticSource source)
    : diag_{.severity = severity,
            .category = DiagnosticCategory::Compile,
            .source = source,
            .code = DiagnosticCode::None,
            .message = std::move(message),
            .spans = {},
            .hint = std::nullopt,
            .fixes = {}} {}

DiagnosticBuilder::DiagnosticBuilder(Severity severity, std::string message)
    : DiagnosticBuilder{severity, std::move(message), DiagnosticSource::Runtime} {}

DiagnosticBuilder& DiagnosticBuilder::category(DiagnosticCategory category) {
    diag_.category = category;

    return *this;
}

DiagnosticBuilder& DiagnosticBuilder::source(DiagnosticSource source) {
    diag_.source = source;

    return *this;
}

DiagnosticBuilder& DiagnosticBuilder::error_code(DiagnosticCode code) {
    diag_.code = code;

    return *this;
}

DiagnosticBuilder& DiagnosticBuilder::add_span(SourceLocation start, SourceLocation end,
                                               std::string label, bool is_primary) {
    diag_.spans.push_back(
        {.start = start, .end = end, .label = std::move(label), .is_primary = is_primary});

    return *this;
}

DiagnosticBuilder& DiagnosticBuilder::primary(SourceLocation start, SourceLocation end,
                                              std::string label) {
    return add_span(start, end, std::move(label), true);
}

DiagnosticBuilder& DiagnosticBuilder::primary(SourceLocation loc, std::string label) {
    return primary(loc, loc, std::move(label));
}

DiagnosticBuilder& DiagnosticBuilder::secondary(SourceLocation start, SourceLocation end,
                                                std::string label) {
    return add_span(start, end, std::move(label), false);
}

DiagnosticBuilder& DiagnosticBuilder::secondary(SourceLocation loc, std::string label) {
    return secondary(loc, loc, std::move(label));
}

DiagnosticBuilder& DiagnosticBuilder::hint(std::string hint) {
    diag_.hint = std::move(hint);

    return *this;
}

DiagnosticBuilder& DiagnosticBuilder::fix(Fix fix) {
    diag_.fixes.push_back(std::move(fix));

    return *this;
}

Diagnostic DiagnosticBuilder::build() {
    assert(diag_.source != DiagnosticSource::Unknown &&
           "DiagnosticBuilder::build() reached Unknown source; "
           "use the 3-argument constructor or call .source() explicitly");

    return std::move(diag_);
}

// ─── Factory Functions ───

namespace diag {

DiagnosticBuilder error(std::string message) {
    return DiagnosticBuilder{Severity::Error, std::move(message)};
}

DiagnosticBuilder warning(std::string message) {
    return DiagnosticBuilder{Severity::Warning, std::move(message)};
}

DiagnosticBuilder hint(std::string message) {
    return DiagnosticBuilder{Severity::Hint, std::move(message)};
}

} // namespace diag

} // namespace luma
