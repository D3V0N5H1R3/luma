#include "analysis/diagnostics/diagnostic_emitter.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luma {

DiagnosticEmitter::DiagnosticEmitter(DiagnosticCategory error_category, DiagnosticSource source)
    : error_category_{error_category}, source_{source} {}

Diagnostic DiagnosticEmitter::build_diagnostic(Severity severity, DiagnosticCategory category,
                                               DiagnosticSource source, const SourceLocation& loc,
                                               std::string message, std::string_view hint,
                                               DiagnosticCode code, std::optional<Fix> fix) {
    auto builder =
        DiagnosticBuilder{severity, std::move(message), source}.category(category).primary(loc);

    if (!hint.empty()) {
        builder.hint(std::string{hint});
    }

    if (code != DiagnosticCode::None) {
        builder.error_code(code);
    }

    if (fix.has_value()) {
        builder.fix(std::move(*fix));
    }

    return builder.build();
}

void DiagnosticEmitter::emit_impl(Severity severity, DiagnosticCategory category,
                                  const SourceLocation& loc, std::string message,
                                  std::string_view hint, DiagnosticCode code,
                                  std::optional<Fix> fix) {
    emit(build_diagnostic(severity, category, source_, loc, std::move(message), hint, code,
                          std::move(fix)));
}

// API convenience: each method delegates to emit_impl() with the appropriate severity/category.
void DiagnosticEmitter::emit_error(const SourceLocation& loc, std::string message,
                                   std::string_view hint, DiagnosticCode code,
                                   std::optional<Fix> fix) {
    emit_impl(Severity::Error, error_category_, loc, std::move(message), hint, code,
              std::move(fix));
}

void DiagnosticEmitter::emit_warning(const SourceLocation& loc, std::string message,
                                     std::string_view hint, DiagnosticCode code) {
    emit_impl(Severity::Warning, DiagnosticCategory::Warning, loc, std::move(message), hint, code,
              std::nullopt);
}

void DiagnosticEmitter::warning_with_fix(const SourceLocation& loc, std::string message, Fix fix,
                                         std::string_view hint, DiagnosticCode code) {
    emit_impl(Severity::Warning, DiagnosticCategory::Warning, loc, std::move(message), hint, code,
              std::move(fix));
}

bool DiagnosticEmitter::has_errors() const {
    return error_count_ > 0;
}

std::size_t DiagnosticEmitter::error_count() const {
    return error_count_;
}

std::size_t DiagnosticEmitter::warning_count() const {
    return warning_count_;
}

const std::vector<Diagnostic>& DiagnosticEmitter::diagnostics() const {
    return diagnostics_;
}

std::vector<Diagnostic> DiagnosticEmitter::take_diagnostics() {
    error_count_ = 0;
    warning_count_ = 0;
    return std::move(diagnostics_);
}

void DiagnosticEmitter::emit(Diagnostic diagnostic) {
    if (diagnostic.severity == Severity::Error) {
        ++error_count_;
    } else if (diagnostic.severity == Severity::Warning) {
        ++warning_count_;
    }

    diagnostics_.push_back(std::move(diagnostic));
}

void DiagnosticEmitter::clear_diagnostics() {
    diagnostics_.clear();
    error_count_ = 0;
    warning_count_ = 0;
}

} // namespace luma
