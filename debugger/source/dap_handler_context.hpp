#ifndef LUMA_DAP_HANDLER_CONTEXT_HPP
#define LUMA_DAP_HANDLER_CONTEXT_HPP

// ─────────────────────────────────────────────────────────────────────────────
// DapHandlerContext — shared state injected into each handler group.
//
// Instead of each handler class befriending DapServer or duplicating state,
// all mutable session state lives here.  DapServer owns the context and
// passes a reference to each handler at construction time.
//
// Design note — intentional aggregation:
// This struct deliberately aggregates session state, watch cache, compiled
// breakpoints, pending breakpoints, feature negotiation, and auth state.
// Splitting these into separate objects would require each handler to accept
// 5–6 additional constructor arguments, increasing coupling without improving
// testability.  The tradeoff is accepted: DapHandlerContext is a named,
// documented aggregate whose members each have a clear purpose and lifetime.
//
// Fields are grouped and labelled below.  All mutable access is single-
// threaded (DAP protocol thread); session and breakpoint manager use their
// own internal synchronisation for cross-thread access.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "compiled_breakpoint.hpp"
#include "dap_feature_manager.hpp"
#include "dap_handler_types.hpp"
#include "dap_helpers.hpp"
#include "dap_protocol_handler.hpp"
#include "dap_types.hpp"
#include "debug_session_state.hpp"

namespace luma::dap {

class DebugSession;

// Shared mutable state accessed by all handler groups via reference.
struct DapHandlerContext {
    // ─── Session state ───
    std::unique_ptr<DebugSession> session;
    WatchCache watch_cache;
    CompiledBreakpointCache compiled_bp_cache;
    std::unordered_map<std::string, std::vector<BreakpointRequest>> pending_breakpoints;
    std::vector<std::string> pending_exception_filters;
    std::vector<BreakpointRequest> pending_function_bp_requests;
    std::vector<DataBreakpointRequest> pending_data_breakpoints;
    LaunchConfig last_launch_config;

    // ─── Feature negotiation ───
    FeatureManager feature_manager;

    // ─── Authentication state ───
    std::string auth_token;
    bool auth_failed{false};

    // ─── Protocol layer (non-owning reference) ───
    DapProtocolHandler& protocol_handler;

    // Constructor and destructor defined in dap_handler_context.cpp
    // where DebugSession is complete (required for unique_ptr<DebugSession>).
    explicit DapHandlerContext(DapProtocolHandler& handler);
    ~DapHandlerContext();

    // ─── Session helpers ───

    // No-session policy (three tiers; see also require_session in dap_helpers.hpp):
    //   1. Throw     — handlers that fundamentally need a running session
    //                  (execution control, evaluate, state modification) call
    //                  require_session(); the dispatch loop converts the throw
    //                  into a DAP error response.
    //   2. Pending   — breakpoint handlers store the request and return
    //                  unverified breakpoints, because DAP clients legitimately
    //                  set breakpoints before launch; pending state is applied
    //                  at session creation.
    //   3. Graceful  — inspection reads (threads/scopes/variables/stackTrace)
    //                  and lifecycle no-ops (disconnect/terminate/
    //                  configurationDone) return empty/ok results, since a
    //                  client may poll them with no active session.
    // Guard tiers 2 and 3 with has_session(); use require_session() for tier 1.
    [[nodiscard]] bool has_session() const noexcept {
        return session != nullptr;
    }

    void invalidate_watches() {
        watch_cache.invalidate();
    }

    void create_session(std::function<void(const std::string&, const JsonValue&)> event_cb,
                        std::function<void(const std::string&, const std::string&)> output_cb);

    void reset_session();

    void apply_pending_breakpoints();

    // Launch (or re-launch) a program with the given config.
    [[nodiscard]] HandlerResult launch_with_config(const LaunchConfig& config);
};

} // namespace luma::dap

#endif // LUMA_DAP_HANDLER_CONTEXT_HPP
