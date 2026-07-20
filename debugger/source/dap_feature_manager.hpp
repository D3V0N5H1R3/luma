#ifndef LUMA_DAP_FEATURE_MANAGER_HPP
#define LUMA_DAP_FEATURE_MANAGER_HPP

// ─────────────────────────────────────────────────────────────────────────────
// FeatureManager — centralised DAP capability negotiation and querying.
//
// During DAP initialisation the client sends its capabilities and the server
// responds with its own.  FeatureManager owns both sides of that negotiation:
//
//   1. Records client capabilities from the initialize request arguments.
//   2. Builds the server capabilities response, enabling only features that
//      both the server implements and the client can consume.
//   3. Provides fast, type-safe queries so handlers can check whether a
//      feature is available without re-parsing JSON or scattering boolean
//      flags across multiple classes.
//
// This header is intentionally self-contained: it depends only on dap_types.hpp
// (for JsonValue and exception-filter constants) to avoid circular includes
// with dap_server.hpp.
//
// Thread safety: FeatureManager is written once during initialize (single
// DAP protocol thread) and read-only thereafter.  No synchronisation needed.
// ─────────────────────────────────────────────────────────────────────────────

#include <array>
#include <string>
#include <string_view>

#include "dap_types.hpp" // JsonValue, kFilterCaught, kFilterUncaught

namespace luma::dap {

// ─── Feature identifiers ───
// Strongly-typed enum for every DAP feature the Luma debugger knows about.
// Keeps call sites readable:  `if (features.is_enabled(Feature::StepBack))`

enum class Feature : int {
    ConfigurationDone,
    FunctionBreakpoints,
    ConditionalBreakpoints,
    HitConditionalBreakpoints,
    EvaluateForHovers,
    StepBack,
    SetVariable,
    RestartFrame,
    RestartRequest,
    GotoTargets,
    Completions,
    Modules,
    ExceptionOptions,
    ExceptionInfo,
    LogPoints,
    LoadedSources,
    Terminate,
    BreakpointLocations,
    InvalidatedEvent,
    ValueFormattingOptions,
    SingleThreadExecution,
    DataBreakpoints,
    InstructionBreakpoints,
    StepInTargets,
    Count_, // sentinel — must be last
};

inline constexpr int kFeatureCount = static_cast<int>(Feature::Count_);

// ─── Client capabilities ───
// Subset of the initialize-request arguments that describe what the
// client (editor) supports.  Extended as needed.

struct ClientCapabilities {
    bool lines_start_at_1{true};
    bool columns_start_at_1{true};
    bool supports_variable_type{false};
    bool supports_variable_paging{false};
    bool supports_run_in_terminal_request{false};
    bool supports_memory_references{false};
    bool supports_progress_reporting{false};
    bool supports_invalidated_event{false};
    bool supports_memory_event{false};
    bool supports_start_debugging_request{false};
    std::string path_format{"path"}; // "path" or "uri"
};

// ─── FeatureManager ───

class FeatureManager {
public:
    // ── Initialisation ──────────────────────────────────────────────────

    // Parse client capabilities from the DAP initialize request arguments
    // and negotiate which server features to enable.
    void receive_client_capabilities(const JsonValue& args) {
        if (args.is_object()) {
            client_.lines_start_at_1 = args.get_or<bool>("linesStartAt1", true);
            client_.columns_start_at_1 = args.get_or<bool>("columnsStartAt1", true);
            client_.supports_variable_type = args.get_or<bool>("supportsVariableType", false);
            client_.supports_variable_paging = args.get_or<bool>("supportsVariablePaging", false);
            client_.supports_run_in_terminal_request =
                args.get_or<bool>("supportsRunInTerminalRequest", false);
            client_.supports_memory_references =
                args.get_or<bool>("supportsMemoryReferences", false);
            client_.supports_progress_reporting =
                args.get_or<bool>("supportsProgressReporting", false);
            client_.supports_invalidated_event =
                args.get_or<bool>("supportsInvalidatedEvent", false);
            client_.supports_memory_event = args.get_or<bool>("supportsMemoryEvent", false);
            client_.supports_start_debugging_request =
                args.get_or<bool>("supportsStartDebuggingRequest", false);
            client_.path_format = args.get_or<std::string>("pathFormat", "path");
        }

        negotiate_features();
        initialized_ = true;
    }

    // ── Server capability JSON ──────────────────────────────────────────

    // Build the negotiated capabilities as a DAP-compatible JSON object.
    // This is the body of the initialize response.
    [[nodiscard]] JsonValue build_capabilities_json() const {
        JsonValue::ObjectType obj;

        const auto set = [&](const char* key, Feature f) {
            obj[key] = JsonValue(is_enabled(f));
        };

        // ─── Breakpoints ───
        set("supportsConditionalBreakpoints", Feature::ConditionalBreakpoints);
        set("supportsHitConditionalBreakpoints", Feature::HitConditionalBreakpoints);
        set("supportsFunctionBreakpoints", Feature::FunctionBreakpoints);
        set("supportsLogPoints", Feature::LogPoints);
        set("supportsBreakpointLocationsRequest", Feature::BreakpointLocations);
        set("supportsDataBreakpoints", Feature::DataBreakpoints);
        set("supportsInstructionBreakpoints", Feature::InstructionBreakpoints);

        // ─── Execution control ───
        set("supportsConfigurationDoneRequest", Feature::ConfigurationDone);
        set("supportsRestartRequest", Feature::RestartRequest);
        set("supportsRestartFrame", Feature::RestartFrame);
        set("supportsTerminateRequest", Feature::Terminate);
        set("supportsStepBack", Feature::StepBack);
        set("supportsStepInTargetsRequest", Feature::StepInTargets);
        set("supportsGotoTargetsRequest", Feature::GotoTargets);
        set("supportsSingleThreadExecutionRequests", Feature::SingleThreadExecution);

        // ─── Inspection ───
        set("supportsEvaluateForHovers", Feature::EvaluateForHovers);
        set("supportsSetVariable", Feature::SetVariable);
        set("supportsCompletionsRequest", Feature::Completions);
        set("supportsValueFormattingOptions", Feature::ValueFormattingOptions);

        // ─── Sources and modules ───
        set("supportsLoadedSourcesRequest", Feature::LoadedSources);
        set("supportsModulesRequest", Feature::Modules);

        // ─── Events and exceptions ───
        set("supportsInvalidatedEvent", Feature::InvalidatedEvent);
        set("supportsExceptionOptions", Feature::ExceptionOptions);
        set("supportsExceptionInfoRequest", Feature::ExceptionInfo);

        // Exception breakpoint filters.
        JsonValue::ArrayType filters;
        JsonValue::ObjectType caught;
        caught["filter"] = JsonValue(std::string(kFilterCaught));
        caught["label"] = JsonValue(std::string("Caught Exceptions"));
        caught["default"] = JsonValue(false);
        filters.emplace_back(JsonValue(std::move(caught)));

        JsonValue::ObjectType uncaught;
        uncaught["filter"] = JsonValue(std::string(kFilterUncaught));
        uncaught["label"] = JsonValue(std::string("Uncaught Exceptions"));
        uncaught["default"] = JsonValue(true);
        filters.emplace_back(JsonValue(std::move(uncaught)));

        obj["exceptionBreakpointFilters"] = JsonValue(std::move(filters));

        return JsonValue(std::move(obj));
    }

    // ── Queries ─────────────────────────────────────────────────────────

    // Check whether a specific server feature is enabled after negotiation.
    [[nodiscard]] bool is_enabled(Feature feature) const noexcept {
        const int idx = static_cast<int>(feature);
        if (idx < 0 || idx >= kFeatureCount) {
            return false;
        }
        return features_[idx];
    }

    // ── Client capability accessors ─────────────────────────────────────

    [[nodiscard]] const ClientCapabilities& client() const noexcept {
        return client_;
    }

    [[nodiscard]] bool lines_start_at_1() const noexcept {
        return client_.lines_start_at_1;
    }

    [[nodiscard]] bool columns_start_at_1() const noexcept {
        return client_.columns_start_at_1;
    }

    [[nodiscard]] bool client_supports_variable_type() const noexcept {
        return client_.supports_variable_type;
    }

    [[nodiscard]] bool client_supports_variable_paging() const noexcept {
        return client_.supports_variable_paging;
    }

    [[nodiscard]] bool client_supports_invalidated_event() const noexcept {
        return client_.supports_invalidated_event;
    }

    [[nodiscard]] bool client_supports_progress_reporting() const noexcept {
        return client_.supports_progress_reporting;
    }

    [[nodiscard]] bool client_supports_run_in_terminal() const noexcept {
        return client_.supports_run_in_terminal_request;
    }

    [[nodiscard]] bool client_supports_memory_references() const noexcept {
        return client_.supports_memory_references;
    }

    [[nodiscard]] const std::string& path_format() const noexcept {
        return client_.path_format;
    }

    // Whether receive_client_capabilities() has been called.
    [[nodiscard]] bool is_initialized() const noexcept {
        return initialized_;
    }

private:
    // Populate the features_ array based on what the server implements
    // and what the client can consume.
    void negotiate_features() {
        // Start with all features disabled.
        features_.fill(false);

        const auto enable = [this](Feature f) {
            features_[static_cast<int>(f)] = true;
        };

        // Features the server always provides.
        enable(Feature::ConfigurationDone);
        enable(Feature::FunctionBreakpoints);
        enable(Feature::ConditionalBreakpoints);
        enable(Feature::HitConditionalBreakpoints);
        enable(Feature::EvaluateForHovers);
        enable(Feature::StepBack);
        enable(Feature::SetVariable);
        enable(Feature::RestartRequest);
        enable(Feature::Completions);
        enable(Feature::ExceptionOptions);
        enable(Feature::ExceptionInfo);
        enable(Feature::LogPoints);
        enable(Feature::LoadedSources);
        enable(Feature::Terminate);
        enable(Feature::BreakpointLocations);
        enable(Feature::ValueFormattingOptions);
        enable(Feature::DataBreakpoints);
        enable(Feature::StepInTargets);

        // Features gated on client support.
        if (client_.supports_invalidated_event) {
            enable(Feature::InvalidatedEvent);
        }
    }

    ClientCapabilities client_;
    std::array<bool, kFeatureCount> features_{};
    bool initialized_{false};
};

} // namespace luma::dap

#endif // LUMA_DAP_FEATURE_MANAGER_HPP
