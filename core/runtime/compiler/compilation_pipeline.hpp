#ifndef LUMA_COMPILER_COMPILATION_PIPELINE_HPP
#define LUMA_COMPILER_COMPILATION_PIPELINE_HPP

// Full analysis-to-bytecode pipeline (parse → type-check → lint → compile →
// optional optimize/verify).  Extracted from cli_internal.hpp so that
// non-CLI components can compile programs without depending on the CLI layer.

#include <optional>
#include <string>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/pipeline/pipeline.hpp"
#include "analysis/source/source_manager.hpp"
#include "runtime/compiler/compile_result.hpp"
#include "runtime/compiler/compiler_config.hpp"

namespace luma {

/// Configuration for the compilation pipeline.
///
/// Controls which passes run and how they behave.  Callers construct a
/// profile and pass it to compile_program().
struct CompilerProfile {
    OptimizationLevel optimize_level{OptimizationLevel::None};
    bool type_check{true};   ///< Run the type checker pass.
    bool lint{true};         ///< Run the linter pass.
    bool compile{true};      ///< Run the compiler (AST → bytecode). When false,
                             ///< the pipeline stops after type-check/lint — the
                             ///< check-only profile used by `luma check`.
    bool strict{false};      ///< Treat warnings as errors.
    bool verify{false};      ///< Run the bytecode verifier.
    bool require_main{true}; ///< Require a @main function.
};

// Result of compile_program(). On success (with compilation enabled), `artifact`
// holds the compiled bytecode, also reachable via extract_compile_result(). On
// failure, `exit_code` indicates the error category.
struct CompilationOutcome {
    PipelineResult pipeline_result;
    std::optional<CompileArtifact> artifact;
    int exit_code{0};
    bool success{false};
};

// Render diagnostics and print a summary line with the given header.
void report_diagnostics(const std::vector<Diagnostic>& diagnostics,
                        const SourceManager& source_manager, std::string_view header);

// Convenience overload for PipelineResult: reports both errors and warnings.
void report_diagnostics(const PipelineResult& result, const SourceManager& source_manager);

// Determine the exit code from the first error diagnostic in a pipeline result.
[[nodiscard]] int exit_code_for(const PipelineResult& result);

// Check strict-mode warnings and return the appropriate exit code. Returns 0 if no violation.
[[nodiscard]] int check_strict_warnings(const PipelineResult& result, bool strict);

// Extract the CompileArtifact from the compilation's artifact slot, printing an
// error and returning nullptr when it is empty.
[[nodiscard]] const CompileArtifact*
extract_compile_result(const std::optional<CompileArtifact>& artifact);

// Compile a program through the full analysis pipeline (parse → type-check → lint → compile →
// optional optimize/verify). On failure, diagnostics are reported and an appropriate exit code
// is returned in the outcome. On success, the compiled bytecode is available in the outcome's
// `artifact` slot (empty when the profile disables compilation).
[[nodiscard]] CompilationOutcome compile_program(const std::string& path,
                                                 SourceManager& source_manager,
                                                 const CompilerProfile& profile);

} // namespace luma

#endif // LUMA_COMPILER_COMPILATION_PIPELINE_HPP
