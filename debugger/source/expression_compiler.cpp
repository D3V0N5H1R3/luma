#include "expression_compiler.hpp"

#include <format>

#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/parser/parser.hpp"
#include "analysis/pipeline/pipeline.hpp"
#include "analysis/pipeline/type_checker_pass.hpp"
#include "analysis/source/source_manager.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/compiler_pass.hpp"
#include "runtime/include/include_resolver.hpp"

namespace luma::dap {

namespace {

constexpr std::string_view k_bp_eval_wrapper = "__bp_eval__";

// Wraps a compilation lambda in a try/catch that sets error_out on failure.
template <typename Fn>
auto compile_with_error_handling(Fn&& compile_fn,
                                 std::string& error_out) -> decltype(compile_fn()) {
    try {
        return compile_fn();
    } catch (const std::exception& e) {
        error_out = e.what();
        return std::nullopt;
    }
}

// Collect the first error message from diagnostics into error_out,
// or all error messages into detailed_errors if provided.
void collect_error_messages(const std::vector<Diagnostic>& diagnostics, std::string& error_out,
                            const std::string& fallback_label,
                            std::vector<std::string>* detailed_errors) {
    if (detailed_errors != nullptr) {
        for (const auto& diag : diagnostics) {
            if (diag.severity == Severity::Error) {
                const int line = diag.spans.empty() ? 0 : diag.spans[0].start.line;
                detailed_errors->push_back(std::format("Line {}: {}", line, diag.message));
            }
        }
        error_out = fallback_label;
    } else {
        error_out = fallback_label;
        for (const auto& diag : diagnostics) {
            if (diag.severity == Severity::Error) {
                error_out = diag.message;
                break;
            }
        }
    }
}

// Run the TypeChecker + Compiler pipeline on an already-parsed program.
// On success, returns the PipelineCompileResult. On failure, sets error_out
// and optionally populates detailed_errors with per-diagnostic messages.
[[nodiscard]] std::optional<PipelineCompileResult>
run_pipeline(Program& program, std::string& error_out, const std::string& error_label,
             std::vector<std::string>* detailed_errors = nullptr) {
    std::optional<CompileArtifact> artifact;
    auto pipeline = Pipeline::builder().add<TypeCheckerPass>().add<CompilerPass>(artifact).build();
    auto result = pipeline.run(program);

    if (result.has_errors()) {
        collect_error_messages(result.diagnostics, error_out, error_label, detailed_errors);
        return std::nullopt;
    }

    if (!artifact.has_value()) {
        error_out = "Internal error: compilation produced no result";
        return std::nullopt;
    }

    PipelineCompileResult out;
    out.functions = std::move(artifact->functions);
    out.top_level = std::move(artifact->top_level);
    return out;
}

} // namespace

std::optional<CompiledFunction> compile_expression_direct(const std::string& expression,
                                                          std::string& error_out) {
    return compile_with_error_handling(
        [&]() -> std::optional<CompiledFunction> {
            // Wrap the expression in a function body so the parser accepts it.
            // The declared return type is cosmetic: this path bypasses the type
            // checker. The breakpoint-condition cache uses the result only to
            // validate that the expression compiles; the expression evaluator
            // executes it via VM::execute_function to obtain the value.
            const std::string source = std::format("function boolean {}() {{ return {} }}\n",
                                                   k_bp_eval_wrapper, expression);

            DiagnosticCollector diagnostics;
            Lexer lexer{source, diagnostics, 0};
            auto tokens = lexer.tokenize();

            Parser parser{std::move(tokens)};
            auto program = parser.parse();

            if (!parser.get_errors().empty()) {
                error_out = parser.get_errors()[0].message;
                return std::nullopt;
            }

            Compiler compiler;
            auto result = compiler.compile(program, true /* repl_mode */);

            if (!result.success || result.functions.empty()) {
                error_out = "Compilation failed";
                if (!result.diagnostics.empty()) {
                    error_out = result.diagnostics[0].message;
                }
                return std::nullopt;
            }

            return std::move(result.functions[0]);
        },
        error_out);
}

std::optional<PipelineCompileResult>
compile_program_pipeline(SourceManager& source_manager, const std::string& path,
                         std::string& error_out, std::vector<std::string>* detailed_errors) {
    return compile_with_error_handling(
        [&]() -> std::optional<PipelineCompileResult> {
            const auto& source_file = source_manager.load(path);

            DiagnosticCollector diagnostics;
            Lexer lexer{source_file.text, diagnostics, source_file.file_id};
            auto tokens = lexer.tokenize();

            Parser parser{std::move(tokens)};
            auto program = parser.parse();

            const auto& parse_errors = parser.get_errors();

            if (!parse_errors.empty()) {
                collect_error_messages(parse_errors, error_out, "Syntax errors", detailed_errors);
                return std::nullopt;
            }

            IncludeResolver include_resolver{source_manager};

            if (!include_resolver.resolve(program)) {
                collect_error_messages(include_resolver.get_diagnostics(), error_out,
                                       "Include resolution errors", detailed_errors);
                return std::nullopt;
            }

            return run_pipeline(program, error_out, "Compilation errors", detailed_errors);
        },
        error_out);
}

} // namespace luma::dap
