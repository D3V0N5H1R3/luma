#ifndef LUMA_DAP_INSPECTION_HANDLER_HPP
#define LUMA_DAP_INSPECTION_HANDLER_HPP

#include <optional>
#include <string>
#include <vector>

#include "dap_handler_base.hpp"
#include "dap_handler_context.hpp"
#include "dap_handler_types.hpp"
#include "dap_types.hpp"
#include "expression_evaluator.hpp"

namespace luma::dap {

// ─── Inspection Handler ───
// Handles DAP state inspection requests: threads, stackTrace, scopes,
// variables, evaluate, setVariable, completions, stepInTargets,
// loadedSources, source, and exceptionInfo.

class DapInspectionHandler : public DapHandler {
public:
    explicit DapInspectionHandler(DapHandlerContext& ctx) : DapHandler(ctx) {}

    // ─── State inspection ───
    [[nodiscard]] HandlerResult handle_threads(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_stack_trace(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_scopes(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_variables(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_evaluate(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_set_variable(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_completions(const JsonValue& args);
    // Return the list of candidate step-in targets at the current source location.
    // Clients use this to offer "Step into X()" choices when multiple function
    // calls appear on the same line.  Returns an empty targets array when
    // the current frame has no resolvable call targets.
    [[nodiscard]] HandlerResult handle_step_in_targets(const JsonValue& args);

    // ─── Sources ───
    [[nodiscard]] HandlerResult handle_loaded_sources(const JsonValue& args);
    [[nodiscard]] HandlerResult handle_source(const JsonValue& args);

    // ─── Exception info ───
    [[nodiscard]] HandlerResult handle_exception_info(const JsonValue& args);

private:
    // ─── handle_variables helpers ───

    struct VariablesRequest {
        int reference{0};
        int start{0};
        int count{0};
        std::string filter;
        bool format_hex{false};
    };

    [[nodiscard]] static VariablesRequest parse_variables_request(const JsonValue& args);
    static void apply_variable_formatting(std::vector<Variable>& vars, bool format_hex);

    // ─── handle_evaluate helpers ───

    [[nodiscard]] std::optional<HandlerResult> try_cached_evaluate(int frame_id,
                                                                   const std::string& expression,
                                                                   EvaluationContext context) const;

    void cache_evaluate_result(int frame_id, const std::string& expression,
                               EvaluationContext context, const std::string& display,
                               const std::string& type, int variables_reference);
};

} // namespace luma::dap

#endif // LUMA_DAP_INSPECTION_HANDLER_HPP
