// Front-end program loading: lex → parse → resolve includes, collecting
// warnings for a single deferred render. Implements load_program(), declared in
// compiler_config.hpp. Lives in the compiler layer (beside its only caller,
// compilation_pipeline.cpp) so the pipeline no longer reaches into the CLI folder.

#include <algorithm>
#include <format>
#include <iterator>
#include <stdexcept>
#include <vector>

#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/diagnostics/renderer.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/parser/parser.hpp"
#include "analysis/prelude/gui_prelude.hpp"
#include "analysis/source/source_manager.hpp"
#include "common/path_utils.hpp"
#include "runtime/compiler/compiler_config.hpp"
#include "runtime/include/include_resolver.hpp"

namespace luma {

namespace {

// Append the warning-severity diagnostics from `diagnostics` to `warnings`,
// preserving their original order. Errors are handled separately by callers.
void append_warnings(std::vector<Diagnostic>& warnings,
                     const std::vector<Diagnostic>& diagnostics) {
    std::ranges::copy_if(diagnostics, std::back_inserter(warnings),
                         [](const Diagnostic& d) { return d.severity == Severity::Warning; });
}

// Render all accumulated warnings in a single pass.
void render_all_warnings(const std::vector<Diagnostic>& warnings,
                         const SourceManager& source_manager) {
    if (warnings.empty()) {
        return;
    }

    const DiagnosticRenderer renderer{source_manager};

    for (const auto& w : warnings) {
        renderer.render(w);
    }
}

// Lex and parse a source file. Lexer warnings are accumulated into `warnings`
// rather than rendered inline — the caller renders all phases' warnings at once.
// Returns a ProgramLoadResult with errors populated on failure.
[[nodiscard]] ProgramLoadResult parse_source(const SourceFile& source_file,
                                             std::vector<Diagnostic>& warnings) {
    DiagnosticCollector lexer_diagnostics;
    Lexer lexer{source_file.text, lexer_diagnostics, source_file.file_id};

    auto tokens = lexer.tokenize();

    // Surface lexer diagnostics: errors terminate loading; warnings are accumulated.
    if (lexer_diagnostics.has_errors()) {
        return ProgramLoadResult{.program = Program{},
                                 .errors = {lexer_diagnostics.diagnostics().begin(),
                                            lexer_diagnostics.diagnostics().end()}};
    }

    append_warnings(warnings, lexer_diagnostics.diagnostics());

    Parser parser{std::move(tokens)};

    auto program = parser.parse();

    // Collect any syntax errors from the parser.
    const auto& parse_errors = parser.get_errors();

    if (!parse_errors.empty()) {
        return ProgramLoadResult{.program = std::move(program),
                                 .errors = {parse_errors.begin(), parse_errors.end()}};
    }

    return ProgramLoadResult{.program = std::move(program), .errors = {}};
}

// Resolve file includes for an already-parsed program. Include warnings are
// accumulated into `warnings` rather than rendered inline.
// Returns a ProgramLoadResult with errors populated on failure.
[[nodiscard]] ProgramLoadResult resolve_includes(Program program, SourceManager& source_manager,
                                                 std::vector<Diagnostic>& warnings) {
    IncludeResolver include_resolver{source_manager};
    const bool resolved = include_resolver.resolve(program);
    const auto& include_diagnostics = include_resolver.get_diagnostics();

    if (!resolved) {
        return ProgramLoadResult{
            .program = std::move(program),
            .errors = {include_diagnostics.begin(), include_diagnostics.end()}};
    }

    append_warnings(warnings, include_diagnostics);

    return ProgramLoadResult{.program = std::move(program), .errors = {}};
}

} // namespace

ProgramLoadResult load_program(const std::string& path, SourceManager& source_manager) {
    if (!has_luma_extension(path)) {
        throw std::runtime_error{
            std::format("file must have a '{}' extension: '{}'", luma_extension, path)};
    }

    const auto& source_file = source_manager.load(path);

    // Collect warnings from all phases and render them in a single pass at the
    // end, so the user sees a coherent warning list rather than interleaved output.
    std::vector<Diagnostic> warnings;

    auto parsed = parse_source(source_file, warnings);

    if (!parsed.ok()) {
        render_all_warnings(warnings, source_manager);
        return parsed;
    }

    auto resolved = resolve_includes(std::move(parsed.program), source_manager, warnings);

    // Inject the built-in Solaris surface when the program references
    // it, so `namespace Solaris`, the design tokens, and the `View` record
    // are available with no include. Conditional so non-GUI programs are
    // unaffected and unpolluted; guarded internally against double injection.
    if (resolved.ok() && source_manager.any_source_contains_word("Solaris")) {
        prelude::inject_gui_prelude(resolved.program, source_manager);
    }

    render_all_warnings(warnings, source_manager);
    return resolved;
}

} // namespace luma
