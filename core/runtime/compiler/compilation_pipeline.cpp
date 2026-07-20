#include "runtime/compiler/compilation_pipeline.hpp"

#include <format>
#include <iostream>
#include <optional>
#include <string>

#include "analysis/diagnostics/renderer.hpp"
#include "analysis/pipeline/lint_pass.hpp"
#include "analysis/pipeline/type_checker_pass.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/compiler_pass.hpp"
#include "runtime/compiler/optimizer_pass.hpp"
#include "runtime/compiler/verifier_pass.hpp"

namespace luma {

void report_diagnostics(const std::vector<Diagnostic>& diagnostics,
                        const SourceManager& source_manager, std::string_view header) {
    if (diagnostics.empty()) {
        return;
    }

    const DiagnosticRenderer renderer{source_manager};
    renderer.render_all(diagnostics);

    std::cerr << "\n" << header << "\n";
}

void report_diagnostics(const PipelineResult& result, const SourceManager& source_manager) {
    if (result.diagnostics.empty()) {
        return;
    }

    std::string header;

    if (result.has_errors()) {
        header = std::format("{} error(s) found.", result.error_count());
    }

    if (result.has_warnings()) {
        if (!header.empty()) {
            header += "\n";
        }

        header += std::format("{} warning(s).", result.warning_count());
    }

    report_diagnostics(result.diagnostics, source_manager, header);
}

int exit_code_for(const PipelineResult& result) {
    for (const auto& d : result.diagnostics) {
        if (d.severity == Severity::Error) {
            return exit_code::from_diagnostic_category(d.category);
        }
    }

    return exit_code::success;
}

int check_strict_warnings(const PipelineResult& result, bool strict) {
    if (strict && result.has_warnings()) {
        std::cerr << "warnings treated as errors (--strict)\n";

        return exit_code::type_error;
    }

    return 0;
}

const CompileArtifact* extract_compile_result(const std::optional<CompileArtifact>& artifact) {
    if (!artifact.has_value()) [[unlikely]] {
        std::cerr << "internal error: compilation produced no result\n";
        return nullptr;
    }

    return &*artifact;
}

CompilationOutcome compile_program(const std::string& path, SourceManager& source_manager,
                                   const CompilerProfile& profile) {
    auto [program, parse_errors] = load_program(path, source_manager);

    if (!parse_errors.empty()) {
        report_diagnostics(parse_errors, source_manager,
                           std::format("{} syntax error(s) found.", parse_errors.size()));
        return {.exit_code = exit_code::syntax_error, .success = false};
    }

    // The compiled bytecode lives here — CompilerPass writes it and the
    // optimizer/verifier read it, so the generic PipelineResult never needs to
    // know about the back-end artifact type.
    std::optional<CompileArtifact> artifact;

    auto builder = Pipeline::builder();

    if (profile.type_check) {
        (void)builder.add<TypeCheckerPass>(profile.require_main);
    }

    // LintPass requires TypeCheckerPass (it uses type-annotated AST nodes).
    // Only add lint when type_check is also enabled.
    if (profile.type_check && profile.lint) {
        (void)builder.add<LintPass>();
    }

    if (profile.compile) {
        (void)builder.add<CompilerPass>(artifact);

        if (profile.optimize_level != OptimizationLevel::None) {
            (void)builder.add<OptimizerPass>(artifact, static_cast<int>(profile.optimize_level));
        }

        if (profile.verify) {
            (void)builder.add<VerifierPass>(artifact);
        }
    }

    auto pipeline = builder.build();
    auto result = pipeline.run(program);

    report_diagnostics(result, source_manager);

    if (result.has_errors()) {
        const int code = exit_code_for(result);
        return {.pipeline_result = std::move(result), .exit_code = code, .success = false};
    }

    if (const int code = check_strict_warnings(result, profile.strict)) {
        return {.pipeline_result = std::move(result), .exit_code = code, .success = false};
    }

    // Check-only profile: success once analysis passes, with no artifact produced.
    if (!profile.compile) {
        return {
            .pipeline_result = std::move(result), .exit_code = exit_code::success, .success = true};
    }

    if (extract_compile_result(artifact) == nullptr) {
        return {.pipeline_result = std::move(result),
                .exit_code = exit_code::compile_error,
                .success = false};
    }

    return {.pipeline_result = std::move(result),
            .artifact = std::move(artifact),
            .exit_code = exit_code::success,
            .success = true};
}

} // namespace luma
