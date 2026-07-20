#pragma once

#include <cstddef>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"

namespace luma {

// Counts and categorises diagnostics by severity.
//
// Provides a single, reusable utility for tallying errors, warnings,
// hints, and info diagnostics — replacing several ad-hoc counting
// implementations previously scattered across the pipeline, collector,
// and renderer.
//
// The by-severity count() and total() accessors round out this value type's
// tally surface; they are retained as coherent API even where current callers
// read the named members (errors/warnings/…) directly.
struct DiagnosticStats {
    std::size_t errors{0};
    std::size_t warnings{0};
    std::size_t hints{0};
    std::size_t infos{0};

    // Construct stats from a vector of diagnostics in a single pass.
    [[nodiscard]] static DiagnosticStats from(const std::vector<Diagnostic>& diagnostics) noexcept {
        DiagnosticStats stats;

        for (const auto& diagnostic : diagnostics) {
            switch (diagnostic.severity) {
                case Severity::Error:
                    ++stats.errors;
                    break;
                case Severity::Warning:
                    ++stats.warnings;
                    break;
                case Severity::Hint:
                    ++stats.hints;
                    break;
                case Severity::Info:
                    ++stats.infos;
                    break;
            }
        }

        return stats;
    }

    // Count of diagnostics with the given severity.
    [[nodiscard]] constexpr std::size_t count(Severity severity) const noexcept {
        switch (severity) {
            case Severity::Error:
                return errors;
            case Severity::Warning:
                return warnings;
            case Severity::Hint:
                return hints;
            case Severity::Info:
                return infos;
        }

        return 0;
    }

    [[nodiscard]] constexpr bool has_errors() const noexcept {
        return errors > 0;
    }

    [[nodiscard]] constexpr bool has_warnings() const noexcept {
        return warnings > 0;
    }

    [[nodiscard]] constexpr std::size_t total() const noexcept {
        return errors + warnings + hints + infos;
    }
};

} // namespace luma
