#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Compiler Configuration
// ─────────────────────────────────────────────────────────────────────────────
// Shared definitions used by the compilation pipeline and the CLI layer.
// Extracted from cli.hpp so that compiler-layer components do not depend on
// the CLI module.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/source/source_manager.hpp"

namespace luma {

// Bytecode optimization level.
enum class OptimizationLevel : int {
    None = 0,
    Peephole = 1,
    Full = 2
};

// Standardised exit codes.
//
// Error-reporting convention: all user-facing error messages are emitted
// via the diagnostic renderer (DiagnosticRenderer) or std::cerr.  The CLI
// command handlers (e.g. cli_runner.cpp, cli_tester.cpp) return one of the exit
// codes below to indicate success or the category of failure.  Do not use ad-hoc
// exit values — always map through exit_code::from_diagnostic_category() so that
// callers (scripts, CI) can rely on a stable, documented set of codes.
namespace exit_code {

inline constexpr int success = 0;
inline constexpr int runtime_error = 1;
inline constexpr int type_error = 2;
inline constexpr int syntax_error = 3;
inline constexpr int compile_error = 4;
inline constexpr int usage_error = 5;

// Map a DiagnosticCategory to its exit code.
[[nodiscard]] constexpr int from_diagnostic_category(DiagnosticCategory category) noexcept {
    switch (category) {
        case DiagnosticCategory::Compile:
            return compile_error;
        case DiagnosticCategory::Runtime:
            return runtime_error;
        case DiagnosticCategory::Syntax:
            return syntax_error;
        case DiagnosticCategory::Type:
            return type_error;
        case DiagnosticCategory::Warning:
            return success;
    }
    return runtime_error;
}

} // namespace exit_code

// Result of loading and parsing a Luma source file.
struct ProgramLoadResult {
    Program program;
    std::vector<Diagnostic> errors;

    [[nodiscard]] bool ok() const {
        return errors.empty();
    }
};

[[nodiscard]] ProgramLoadResult load_program(const std::string& path,
                                             SourceManager& source_manager);

} // namespace luma
