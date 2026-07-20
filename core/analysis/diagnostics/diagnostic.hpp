#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analysis/diagnostics/diagnostic_codes.hpp"
#include "analysis/source/source_location.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostic — the core diagnostic value types
// ─────────────────────────────────────────────────────────────────────────────
// Declares the data types that carry a diagnostic through the pipeline:
// DiagnosticText, DiagnosticSpan, Fix, Diagnostic, and the HasDiagnostics
// concept.  The diagnostic vocabulary (severity, category, source, and the
// DiagnosticCode enum) lives in diagnostic_codes.hpp; the fluent construction
// API lives in diagnostic_builder.hpp.
//
// Both companion headers are re-included here (codes at the top, the builder at
// the bottom) so that every existing translation unit that includes only
// diagnostic.hpp keeps seeing the full surface unchanged.

namespace luma {

// A message/hint pair returned by diagnostic builder functions.
// Used by both the type checker (diag_builders) and compiler
// (compiler_errors) to produce consistent diagnostic text.
struct DiagnosticText {
    std::string message;
    std::string hint;
};

// A labelled source span — points to a specific location with a message.
struct DiagnosticSpan {
    SourceLocation start;
    SourceLocation end;
    std::string label;
    bool is_primary{true}; // Primary spans get carets; secondary get dashes.
};

// A suggested edit that can be applied automatically.
struct Fix {
    SourceLocation start;
    SourceLocation end;
    std::string replacement;
    std::string description;

    [[nodiscard]] static Fix replace(SourceLocation start, SourceLocation end, std::string text,
                                     std::string desc = "") {
        return Fix{start, end, std::move(text), std::move(desc)};
    }

    [[nodiscard]] static Fix insert(SourceLocation loc, std::string text, std::string desc = "") {
        return Fix{loc, loc, std::move(text), std::move(desc)};
    }
};

// A complete diagnostic message with optional multi-span support
// and suggested fixes.
struct Diagnostic {
    Severity severity{Severity::Error};
    DiagnosticCategory category{DiagnosticCategory::Compile};
    DiagnosticSource source{DiagnosticSource::Unknown};
    DiagnosticCode code{DiagnosticCode::None};
    std::string message;
    std::vector<DiagnosticSpan> spans;
    std::optional<std::string> hint;
    std::vector<Fix> fixes;

    // Human-readable error code string (e.g. "E0001", "W0005").
    [[nodiscard]] std::string code_string() const;

    // Convenience: primary source location (from the first primary span).
    [[nodiscard]] SourceLocation primary_location() const;
};

// ─── HasDiagnostics Concept ───

// Common read interface satisfied by both DiagnosticCollector (the
// per-compilation aggregate sink) and DiagnosticEmitter (the per-phase
// diagnostic buffer).  Use in templates that need to inspect collected
// diagnostics without caring which concrete container holds them.
//
// Design note: DiagnosticCollector and DiagnosticEmitter intentionally
// remain separate types.  DiagnosticEmitter is a base class for
// analysis phases — it tags every emitted diagnostic with a
// phase-specific category and source.  DiagnosticCollector is a
// standalone aggregate that receives pre-built diagnostics from one
// or more phases.  The HasDiagnostics concept captures the read-only
// surface they share without forcing an inheritance relationship.
template <typename T>
concept HasDiagnostics = requires(const T& t) {
    { t.diagnostics() } -> std::same_as<const std::vector<Diagnostic>&>;
    { t.has_errors() } -> std::convertible_to<bool>;
    { t.error_count() } -> std::convertible_to<std::size_t>;
};

} // namespace luma

// The DiagnosticBuilder class and the diag:: factories used to live in this
// header.  They now live in diagnostic_builder.hpp; re-include it (after the
// value types above, which it depends on) so existing includers of
// diagnostic.hpp keep seeing them unchanged.
#include "analysis/diagnostics/diagnostic_builder.hpp"
