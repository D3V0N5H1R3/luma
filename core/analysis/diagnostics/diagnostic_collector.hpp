#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"

namespace luma {

// Aggregate diagnostic sink for a compilation run.
//
// Collects pre-built Diagnostic values from one or more analysis
// phases.  Each phase (Lexer, Parser, TypeChecker, Linter) inherits
// from DiagnosticEmitter — a per-phase buffer that tags
// diagnostics with the appropriate category and source.  At the end
// of a phase, diagnostics are flushed into this collector via emit().
//
// Relationship to DiagnosticEmitter:
//   DiagnosticEmitter = per-phase buffer with category/source defaults.
//   DiagnosticCollector = aggregate sink receiving from all phases.
//   Both satisfy the HasDiagnostics concept (defined in diagnostic.hpp)
//   for generic read-only access to collected diagnostics.
class DiagnosticCollector {
public:
    // Push a fully-constructed diagnostic directly into the collection.
    // Use this when the caller needs fine-grained control over category,
    // source, error code, or multi-span annotations.
    void emit(Diagnostic d) {
        if (d.severity == Severity::Error) {
            ++error_count_;
        } else if (d.severity == Severity::Warning) {
            ++warning_count_;
        }

        diagnostics_.push_back(std::move(d));
    }

    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const {
        return diagnostics_;
    }

    [[nodiscard]] std::vector<Diagnostic> take_diagnostics() {
        error_count_ = 0;
        warning_count_ = 0;
        return std::move(diagnostics_);
    }

    [[nodiscard]] bool has_errors() const {
        return error_count_ > 0;
    }

    [[nodiscard]] std::size_t error_count() const {
        return error_count_;
    }

    [[nodiscard]] std::size_t warning_count() const {
        return warning_count_;
    }

private:
    std::vector<Diagnostic> diagnostics_;
    std::size_t error_count_{0};
    std::size_t warning_count_{0};
};

} // namespace luma
