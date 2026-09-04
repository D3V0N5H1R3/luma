#ifndef LUMA_LSP_ANALYSIS_SERVICE_IMPL_HPP
#define LUMA_LSP_ANALYSIS_SERVICE_IMPL_HPP

// ─────────────────────────────────────────────────────────────────────────────
// LspAnalysisService — concrete implementation of AnalysisService
// ─────────────────────────────────────────────────────────────────────────────
// Owns and executes the full analysis pipeline:
//   lex → parse → include → symbol collection → type-check → lint
//
// LspServer holds a std::unique_ptr<AnalysisService> pointing to this class
// and delegates analysis work through the virtual interface.  All
// server-level concerns (caching, notification dispatch, file watching)
// remain in LspServer.
// ─────────────────────────────────────────────────────────────────────────────

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/lexer/token.hpp"
#include "analysis/source/source_location.hpp"
#include "common/lru_cache.hpp"
#include "json/json.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_analysis_service.hpp"
#include "lsp_config.hpp"

namespace luma {

// Forward declarations — full definitions are only needed in the .cpp file.
struct Declaration;
struct Statement;
struct Program;

} // namespace luma

namespace luma::lsp {

using luma::json::JsonValue;

// Callback interface for analysis-to-server communication.
// Decouples LspAnalysisService from LspServer: the service only sees these
// two callbacks, not the full server API.  Constructed by LspServer and
// passed to the service at creation time.
struct AnalysisCallbacks {
    // Log a diagnostic or informational message to the client.
    std::function<void(const std::string&)> log;
    // Send an LSP notification (method + JSON params) to the client.
    std::function<void(std::string_view, const JsonValue&)> notify;
};

class LspAnalysisService final : public AnalysisService {
public:
    // Construct with the server's configuration, cancellation flag, and
    // notification callbacks.
    //
    // All references and callbacks must remain valid for the lifetime of
    // this object (i.e. the lifetime of the owning LspServer).
    LspAnalysisService(const LspConfig& config, const std::atomic<bool>& cancel_flag,
                       AnalysisCallbacks callbacks);

    // Run the full analysis pipeline for the given source and URI.
    // See AnalysisService::analyze() for the deadline contract.
    [[nodiscard]] AnalysisResult analyze(const std::string& uri, const std::string& source,
                                         std::chrono::steady_clock::time_point deadline =
                                             std::chrono::steady_clock::time_point::max()) override;

    // Invalidate the include token cache for a file at `path`.
    // Called by LspServer when a watched file changes on disk.
    void erase_cached_file(const std::string& path) override;

private:
    // ─── Phase result type ───
    struct LexResult {
        std::vector<Token> tokens;
        std::vector<luma::Diagnostic> warnings;
    };

    // ─── Pipeline phases ───
    [[nodiscard]] LexResult lex_phase(const std::string& source);

    void include_phase(Program& program, const std::string& uri, AnalysisResult& result);

    void symbol_phase(const Program& program, AnalysisResult& result);

    // Type-checks `program`, appending errors and warnings (and any phase
    // failure) to the result's diagnostics.  Diagnostics whose primary location
    // belongs to `prelude_file_id` are dropped: an injected prelude is not part
    // of the user's document, so its locations cannot be rendered against
    // `source`.  Pass 0 when no prelude was injected.
    void type_check_phase(Program& program, AnalysisResult& result, const std::string& source,
                          const std::string& uri, FileId prelude_file_id);

    void doc_comment_phase(AnalysisResult& result, const std::string& source);

    void call_graph_phase(const Program& program, AnalysisResult& result);

    // Runs the linter and appends its warnings (and any phase failure) to the
    // result's diagnostics.  Diagnostics whose primary location belongs to
    // `prelude_file_id` are dropped for the same reason as in type_check_phase.
    // Pass 0 when no prelude was injected.
    void lint_phase(const Program& program, AnalysisResult& result, const std::string& source,
                    const std::string& uri, const std::vector<std::size_t>& line_starts,
                    FileId prelude_file_id);

    // Runs the lex → parse → include → symbol → doc → call-graph → type-check →
    // lint sequence.  Returns true when the caller should return `result`
    // immediately (parse errors produced a best-effort partial result); false
    // when the full pipeline completed and post-processing should continue.
    [[nodiscard]] bool run_pipeline_phases(const std::string& uri, const std::string& source,
                                           std::chrono::steady_clock::time_point deadline,
                                           AnalysisResult& result,
                                           const std::vector<std::size_t>& line_starts);

    // Computes the byte offset at which each source line begins (line 0 at 0).
    [[nodiscard]] static std::vector<std::size_t> compute_line_starts(const std::string& source);

    // ─── Symbol collection helpers ───
    void collect_ast_symbols(const std::vector<std::unique_ptr<Declaration>>& decls,
                             AnalysisResult& result, std::string_view prefix = "");

    void collect_local_vars(const std::vector<std::unique_ptr<Statement>>& stmts,
                            AnalysisResult& result, const std::string& enclosing_function = "",
                            int scope_start = 0, int scope_end = 0);

    void collect_call_graph(const std::vector<std::unique_ptr<Declaration>>& decls,
                            AnalysisResult& result, std::string_view prefix = "");

    // Matches records against interfaces and populates interface_implementations.
    void build_interface_implementations(AnalysisResult& result);

    // Records, for each top-level symbol that originates from an included file,
    // the source path it came from (used for cross-file navigation).
    void build_symbol_origins(AnalysisResult& result);

    // ─── Cancellation / deadline ───

    // Run a named analysis phase, checking for cancellation and deadline before execution.
    // Throws if cancelled or past deadline.
    void check_cancellation_and_deadline(const std::chrono::steady_clock::time_point& deadline,
                                         std::string_view phase_name) const;

    // ─── Index builders ───
    void build_token_index(AnalysisResult& result);
    void build_identifier_index(AnalysisResult& result);

    // ─── Configuration and callbacks ───
    const LspConfig& config_;
    const std::atomic<bool>& cancel_flag_;
    AnalysisCallbacks callbacks_;

    // ─── Include file token cache ───
    // Accessed from the analysis worker thread (via include_phase) and from
    // the main thread (via erase_cached_file), protected by cache_mutex_.
    mutable std::shared_mutex cache_mutex_;
    LruCache<std::string, IncludeCachePtr> include_cache_{1000};
};

} // namespace luma::lsp

#endif // LUMA_LSP_ANALYSIS_SERVICE_IMPL_HPP
