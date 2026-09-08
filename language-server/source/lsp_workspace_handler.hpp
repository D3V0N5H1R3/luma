#ifndef LUMA_LSP_WORKSPACE_HANDLER_HPP
#define LUMA_LSP_WORKSPACE_HANDLER_HPP

#include <string>

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

class AnalysisPipeline;
class AnalysisService;

class LspWorkspaceHandler {
public:
    LspWorkspaceHandler(LspHandlerContext& ctx, AnalysisPipeline& pipeline,
                        AnalysisService& service)
        : ctx_(ctx), analysis_pipeline_(pipeline), analysis_service_(service) {}

    void load_background_file(const std::string& path);
    void handle_did_change_configuration(const JsonValue& params);
    void handle_did_change_watched_files(const JsonValue& params);
    void handle_did_save(const JsonValue& params);

    // Schedule analysis via the pipeline.
    void schedule_analysis(const std::string& uri, bool force_diagnostics = false);

private:
    LspHandlerContext& ctx_;
    AnalysisPipeline& analysis_pipeline_;
    AnalysisService& analysis_service_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_WORKSPACE_HANDLER_HPP
