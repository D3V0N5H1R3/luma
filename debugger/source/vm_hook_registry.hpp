#ifndef LUMA_DAP_VM_HOOK_REGISTRY_HPP
#define LUMA_DAP_VM_HOOK_REGISTRY_HPP

#include <functional>
#include <memory>
#include <string>

#include "json/json.hpp"

namespace luma {
class VM;
} // namespace luma

namespace luma::dap {

class TimeTravelRecorder;
class DebugExecutionEngine;
class ThreadStateManager;
class BreakpointManager;
class ExpressionEvaluator;

// ═══════════════════════════════════════════════════════════
// HookInstallationContext — narrow interface for VM hook setup.
//
// Provides exactly the members that install_debug_hooks needs,
// instead of granting friend access to the entire DebugSession.
// Pointers are non-owning and must outlive the installed hooks.
// ═══════════════════════════════════════════════════════════

struct HookInstallationContext {
    std::unique_ptr<TimeTravelRecorder>* time_travel_recorder{nullptr};
    DebugExecutionEngine* execution_engine{nullptr};
    ThreadStateManager* thread_state_manager{nullptr};
    BreakpointManager* breakpoint_manager{nullptr};
    ExpressionEvaluator* expression_evaluator{nullptr};
    std::function<void(const std::string&, const json::JsonValue&)> event_callback;
};

// Sets up all debug hooks on a VM using the provided context.
// The context must remain valid for the lifetime of the VM.
void install_debug_hooks(VM& vm, const HookInstallationContext& ctx);

} // namespace luma::dap

#endif // LUMA_DAP_VM_HOOK_REGISTRY_HPP
