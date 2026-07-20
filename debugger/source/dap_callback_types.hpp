#ifndef LUMA_DAP_CALLBACK_TYPES_HPP
#define LUMA_DAP_CALLBACK_TYPES_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Shared callback type aliases used across the DAP debugger subsystem.
//
// EventCallback  — sends a DAP event (e.g. "stopped", "output") to the client.
// OutputCallback — sends categorised text output (stdout, stderr, console).
//
// Defined once here so that DebugSession, DebugExecutionEngine, and
// VariableInspector all share the same definitions.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <string>

#include "dap_types.hpp"

namespace luma::dap {

using EventCallback = std::function<void(const std::string& event, const JsonValue& body)>;
using OutputCallback = std::function<void(const std::string& category, const std::string& text)>;

} // namespace luma::dap

#endif // LUMA_DAP_CALLBACK_TYPES_HPP
