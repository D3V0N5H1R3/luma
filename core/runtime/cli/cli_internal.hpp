#ifndef LUMA_CLI_CLI_INTERNAL_HPP
#define LUMA_CLI_CLI_INTERNAL_HPP

// Internal helpers shared by cli_runner.cpp, cli_tester.cpp, and cli_commands.cpp.
// Not part of the public CLI API — include only from cli implementation files.

#include <iostream>
#include <stdexcept>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/renderer.hpp"
#include "analysis/errors/error.hpp"
#include "analysis/source/source_manager.hpp"
#include "runtime/cli/cli.hpp"
#include "runtime/compiler/compilation_pipeline.hpp"

namespace luma {

// Policy: Compilation/analysis errors → DiagnosticRenderer for rich formatting
//         (see report_diagnostics() in compilation_pipeline.hpp).
//         CLI argument errors and runtime exceptions → std::cerr for simple one-line messages.

// Print a one-line CLI error to stderr and return the given exit code.
[[nodiscard]] inline int report_cli_error(std::string_view message, int code) {
    std::cerr << "error: " << message << "\n";
    return code;
}

// Execute action(source_manager) and map exceptions to exit codes.
template <typename Action> [[nodiscard]] inline int with_error_handling(Action action) {
    SourceManager source_manager;

    try {
        return action(source_manager);
    } catch (const RuntimeError& error) {
        auto diag = DiagnosticBuilder{Severity::Error, std::string{error.what()}}
                        .category(DiagnosticCategory::Runtime)
                        .primary(error.location())
                        .build();

        if (const auto& hint = error.hint()) {
            diag.hint = *hint;
        }

        const DiagnosticRenderer renderer{source_manager};
        renderer.render(diag);

        return exit_code::runtime_error;
    } catch (const std::runtime_error& error) {
        std::cerr << "error: " << error.what() << "\n";
        return exit_code::runtime_error;
    } catch (const std::exception& error) {
        std::cerr << "internal error: " << error.what() << "\n";
        return exit_code::runtime_error;
    }
}

} // namespace luma

#endif // LUMA_CLI_CLI_INTERNAL_HPP
