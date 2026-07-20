#ifndef LUMA_LSP_ANALYSIS_SERVICE_HPP
#define LUMA_LSP_ANALYSIS_SERVICE_HPP

// ─────────────────────────────────────────────────────────────────────────────
// AnalysisService — abstract interface for the analysis pipeline
// ─────────────────────────────────────────────────────────────────────────────
// Encapsulates the analysis pipeline (lex → parse → include → type-check →
// lint) independently of the LSP server, so it can be reused by the
// debugger, REPL, and CLI.
//
// Concrete implementation: LspAnalysisService (lsp_analysis_service_impl.hpp)
// Owner: LspServer holds a std::unique_ptr<AnalysisService> and delegates
// all pipeline execution through this interface.
//
// Testability: inject a mock AnalysisService into LspServer (or any future
// consumer) to unit-test request handlers without running the real pipeline.
// ─────────────────────────────────────────────────────────────────────────────

#include <chrono>
#include <string>
#include <vector>

#include "lsp_analysis_result.hpp"

namespace luma::lsp {

// Abstract analysis pipeline interface for dependency injection.
// See LspAnalysisService for the production implementation.
class AnalysisService {
public:
    virtual ~AnalysisService() = default;

    // Run the full analysis pipeline on the given source code.
    // `deadline` bounds how long analysis may run; when the deadline is
    // reached between pipeline phases the method returns a partial result
    // with a user-visible warning diagnostic.  Pass time_point::max() (the
    // default) to run without a timeout.
    [[nodiscard]] virtual AnalysisResult
    analyze(const std::string& uri, const std::string& source,
            std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::time_point::max()) = 0;

    // Invalidate any cached state for the file at `path`.
    // Called when a watched file changes on disk.
    virtual void erase_cached_file(const std::string& path) = 0;
};

} // namespace luma::lsp

#endif // LUMA_LSP_ANALYSIS_SERVICE_HPP
