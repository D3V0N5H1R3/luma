#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/source/source_location.hpp"

namespace luma {

// Per-phase diagnostic buffer for analysis passes (shared base class).
//
// Provides a uniform API for emitting errors, warnings, and warnings
// with attached fix suggestions.  Each subclass specifies the
// DiagnosticCategory used for errors (warnings always use
// DiagnosticCategory::Warning) and the DiagnosticSource that
// identifies the compilation phase.
//
// Diagnostics are accumulated in an internal vector.  Subclasses
// retrieve them via diagnostics() / take_diagnostics().
//
// Phases that use this base class:
//   Lexer          — category(Syntax),  source(Syntax)
//   Parser         — category(Syntax),  source(Syntax)
//   TypeChecker    — category(Type),    source(Type)
//   Linter         — category(Warning), source(Lint)
//
// Diagnostic buffering design (CA-9):
//   Each pass inherits from this class and therefore buffers diagnostics
//   in the same way — via the diagnostics_ vector defined here.  This is
//   intentional: per-phase buffering allows each pass to tag diagnostics
//   with its own category/source before flushing to the aggregate
//   DiagnosticCollector.  If a future pass needs custom buffering, it
//   should still inherit from this class and override as needed rather
//   than maintaining an independent vector.
//
// Diagnostic code convention (CA-27):
//   All diagnostic emission methods accept DiagnosticCode (defaulting to
//   DiagnosticCode::None).  Every analysis phase — lexer, parser, type
//   checker, linter, and compiler — should use the centralised
//   DiagnosticCode enum defined in diagnostic.hpp for machine-readable
//   error identification.  Raw strings are used only for the human-
//   readable message and hint; never as a substitute for a diagnostic
//   code.  When adding a new diagnostic, always assign a DiagnosticCode
//   entry rather than relying solely on the message text.
//
// Relationship to DiagnosticCollector:
//   DiagnosticEmitter = per-phase buffer with category/source defaults.
//   DiagnosticCollector = aggregate sink receiving from all phases.
//   Both satisfy the HasDiagnostics concept (defined in diagnostic.hpp)
//   for generic read-only access to collected diagnostics.
class DiagnosticEmitter {
public:
    // Emit an error diagnostic at the given location.
    void emit_error(const SourceLocation& loc, std::string message, std::string_view hint = {},
                    DiagnosticCode code = DiagnosticCode::None,
                    std::optional<Fix> fix = std::nullopt);

    // Emit a warning diagnostic at the given location.
    void emit_warning(const SourceLocation& loc, std::string message, std::string_view hint = {},
                      DiagnosticCode code = DiagnosticCode::None);

    // Emit a warning with an attached fix suggestion.
    void warning_with_fix(const SourceLocation& loc, std::string message, Fix fix,
                          std::string_view hint = {}, DiagnosticCode code = DiagnosticCode::None);

    // Returns true if any emitted diagnostic has Error severity.
    [[nodiscard]] bool has_errors() const;

    // Number of diagnostics with Error severity.
    [[nodiscard]] std::size_t error_count() const;

    // Number of diagnostics with Warning severity.
    [[nodiscard]] std::size_t warning_count() const;

    // Read-only access to all collected diagnostics.
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const;

    // Move all collected diagnostics out and reset the internal state.
    [[nodiscard]] std::vector<Diagnostic> take_diagnostics();

protected:
    explicit DiagnosticEmitter(DiagnosticCategory error_category, DiagnosticSource source);

    // Non-polymorphic base: a protected, non-virtual destructor prevents
    // deletion through a DiagnosticEmitter* while avoiding vtable overhead.
    ~DiagnosticEmitter() = default;

    // Push a pre-built diagnostic directly into the collection.
    // Use when the caller needs fine-grained control beyond what
    // emit_error / emit_warning provide.
    void emit(Diagnostic diagnostic);

    // Build a Diagnostic without storing it.
    //
    // Use when the caller needs to route the diagnostic to a
    // container other than diagnostics_ (e.g. the TypeChecker's
    // separate warnings_ vector) while still reusing the standard
    // builder sequence.
    [[nodiscard]] static Diagnostic build_diagnostic(Severity severity, DiagnosticCategory category,
                                                     DiagnosticSource source,
                                                     const SourceLocation& loc, std::string message,
                                                     std::string_view hint = {},
                                                     DiagnosticCode code = DiagnosticCode::None,
                                                     std::optional<Fix> fix = std::nullopt);

    // Clear all collected diagnostics (for reuse across multiple runs).
    void clear_diagnostics();

private:
    void emit_impl(Severity severity, DiagnosticCategory category, const SourceLocation& loc,
                   std::string message, std::string_view hint, DiagnosticCode code,
                   std::optional<Fix> fix);

    DiagnosticCategory error_category_;
    DiagnosticSource source_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t error_count_{0};
    std::size_t warning_count_{0};
};

} // namespace luma
