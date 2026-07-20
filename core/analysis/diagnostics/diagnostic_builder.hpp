#pragma once

#include <string>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_codes.hpp"
#include "analysis/source/source_location.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostic Builder — fluent construction of rich Diagnostic values
// ─────────────────────────────────────────────────────────────────────────────
// Declares the DiagnosticBuilder class and the diag:: factory functions used to
// assemble a full Diagnostic — severity, category, source, error code, one or
// more source spans, a hint, and suggested fixes — with a fluent chain.
//
// Do not confuse this with diagnostic_builders.hpp (plural): that header holds
// the diag_builders:: functions, which produce only (message, hint) *text*
// pairs for recurring error patterns.  The DiagnosticBuilder here produces the
// complete Diagnostic value.
//
// diagnostic.hpp re-includes this header for source compatibility, so existing
// translation units that include diagnostic.hpp keep seeing DiagnosticBuilder
// and diag:: unchanged.
namespace luma {

// ─── Diagnostic Builder ───

class DiagnosticBuilder {
public:
    // Construct a builder with severity, message, and source.
    // Every diagnostic must identify its originating phase.
    DiagnosticBuilder(Severity severity, std::string message, DiagnosticSource source);

    // 2-argument constructor for runtime error catch sites. Sets source to Runtime.
    DiagnosticBuilder(Severity severity, std::string message);

    DiagnosticBuilder& category(DiagnosticCategory category);
    DiagnosticBuilder& source(DiagnosticSource source);
    DiagnosticBuilder& error_code(DiagnosticCode code);
    DiagnosticBuilder& primary(SourceLocation start, SourceLocation end, std::string label = "");
    DiagnosticBuilder& primary(SourceLocation loc, std::string label = "");
    DiagnosticBuilder& secondary(SourceLocation start, SourceLocation end, std::string label = "");
    DiagnosticBuilder& secondary(SourceLocation loc, std::string label = "");
    DiagnosticBuilder& hint(std::string hint);
    DiagnosticBuilder& fix(Fix fix);
    // Consume the builder and return the finished diagnostic.
    // After calling build(), the builder is left in a moved-from state
    // and must not be reused.
    [[nodiscard]] Diagnostic build();

private:
    DiagnosticBuilder& add_span(SourceLocation start, SourceLocation end, std::string label,
                                bool is_primary);

    Diagnostic diag_;
};

// Factory functions for common diagnostic types.
//
// Naming convention (consistent throughout the codebase):
//   diag::error()   — severity Error, use for hard failures
//   diag::warning() — severity Warning, use for non-fatal issues
//   diag::hint()    — severity Hint, use for editorial suggestions
//
// These return a DiagnosticBuilder so callers can chain .category(),
// .source(), .primary(), .hint(), .error_code(), .fix() etc. before
// calling .build().
//
// Convention: every analysis pass must set both .category() and .source()
// to identify the phase that produced the diagnostic:
//   Lexer/Parser  → category(Syntax),  source(Syntax)
//   TypeChecker   → category(Type),    source(Type)
//   Linter        → category(Warning), source(Lint)
//   Compiler      → category(Compile), source(Compile)
//   Verifier      → category(Compile), source(Verify)
//
// Each pass wraps the builder in thin helpers (e.g. TypeChecker::error(),
// Linter::warn()) that apply the correct defaults automatically.
//
// Example usage:
//
//   auto d = diag::error("type mismatch")
//                .category(DiagnosticCategory::Type)
//                .source(DiagnosticSource::Type)
//                .error_code(DiagnosticCode::TypeMismatch)
//                .primary(loc, "expected 'integer', got 'string'")
//                .hint("consider converting with Converter.to_integer()")
//                .build();
//   collector.add(std::move(d));
namespace diag {

[[nodiscard]] DiagnosticBuilder error(std::string message);
[[nodiscard]] DiagnosticBuilder warning(std::string message);
[[nodiscard]] DiagnosticBuilder hint(std::string message);

} // namespace diag

} // namespace luma
