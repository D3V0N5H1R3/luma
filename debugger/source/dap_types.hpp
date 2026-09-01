#ifndef LUMA_DAP_TYPES_HPP
#define LUMA_DAP_TYPES_HPP

#include <string>
#include <string_view>
#include <vector>

#include "json/json.hpp"

namespace luma::dap {

using luma::json::JsonValue;

// ─── DAP event names ───
constexpr std::string_view kEventInitialized = "initialized";
constexpr std::string_view kEventStopped = "stopped";
constexpr std::string_view kEventContinued = "continued";
constexpr std::string_view kEventTerminated = "terminated";
constexpr std::string_view kEventExited = "exited";
constexpr std::string_view kEventThread = "thread";
constexpr std::string_view kEventOutput = "output";
constexpr std::string_view kEventInvalidated = "invalidated";

// ─── Thread event reasons ───
constexpr std::string_view kThreadReasonStarted = "started";
constexpr std::string_view kThreadReasonExited = "exited";

// ─── Stop reasons ───
// DAP protocol string values — these exact strings are required by the
// Debug Adapter Protocol specification for stopped-event reasons.
constexpr std::string_view kStopReasonBreakpoint = "breakpoint";
constexpr std::string_view kStopReasonStep = "step";
constexpr std::string_view kStopReasonException = "exception";
constexpr std::string_view kStopReasonPause = "pause";
constexpr std::string_view kStopReasonEntry = "entry";
constexpr std::string_view kStopReasonDataBreakpoint = "data breakpoint";

// Type-safe enum for stop reasons.  Use stop_reason_string() to convert
// to the DAP protocol string when serialising stopped events.
enum class StopReason {
    Step,
    Breakpoint,
    Exception,
    Pause,
    Entry,
    DataBreakpoint
};

[[nodiscard]] constexpr std::string_view stop_reason_string(StopReason reason) noexcept {
    switch (reason) {
        case StopReason::Step:
            return kStopReasonStep;
        case StopReason::Breakpoint:
            return kStopReasonBreakpoint;
        case StopReason::Exception:
            return kStopReasonException;
        case StopReason::Pause:
            return kStopReasonPause;
        case StopReason::Entry:
            return kStopReasonEntry;
        case StopReason::DataBreakpoint:
            return kStopReasonDataBreakpoint;
    }
    return "unknown";
}

// Sentinel value indicating "no file" or "file not found".
constexpr int kFileIdNotFound = -1;

// ─── Exception filter IDs ───
constexpr std::string_view kFilterCaught = "caught";
constexpr std::string_view kFilterUncaught = "uncaught";

// ─── Output categories ───
constexpr std::string_view kOutputConsole = "console";
constexpr std::string_view kOutputStdout = "stdout";
constexpr std::string_view kOutputStderr = "stderr";

// ─── Error messages ───
constexpr std::string_view kErrorAuthFailed = "Authentication failed — connection rejected";
constexpr std::string_view kErrorUnknownInternal = "Unknown internal error";

// ─── Step modes ───
enum class StepMode {
    None,
    Over,
    Into,
    Out
};

// ─── Protocol types ───

// Incoming breakpoint request from the editor.
struct BreakpointRequest {
    int line{0};
    std::string name; // For function breakpoints.
    std::string condition;
    std::string hit_condition;
    std::string log_message;
};

// Incoming data (watchpoint) breakpoint request from the editor.  Mirrored into
// DapHandlerContext so it can be re-applied when the session is recreated (e.g.
// luma/hotReload), matching the pending line/function/exception breakpoints.
struct DataBreakpointRequest {
    std::string data_id;
    std::string access_type;
    std::string condition;
};

struct Source {
    std::string name; // Display name shown in the editor's breadcrumb/tab.
    std::string path; // Absolute filesystem path to the source file.
};

struct Breakpoint {
    int id{0};            // Unique breakpoint identifier assigned by the debug adapter.
    bool verified{false}; // true if the breakpoint could be resolved to a source location.
    Source source;
    int line{0};
    std::string message; // Optional human-readable message explaining the breakpoint state.
    std::string reason;  // DAP reason field for unverified breakpoints

    // For conditional breakpoints.
    std::string condition;
    std::string hit_condition;
    std::string log_message;
};

struct StackFrame {
    int id{0};
    std::string name;
    Source source;
    int line{0};
    int column{0};
    std::string presentation_hint; // DAP presentationHint: "normal", "label", "subtle".
};

struct Scope {
    std::string name;
    int variables_reference{0};
    bool expensive{false};
    std::string presentation_hint; // DAP presentationHint: "arguments", "locals", or "registers".
};

struct Variable {
    std::string name;
    std::string value;
    std::string type;
    int variables_reference{0};
    int named_variables{0};
    int indexed_variables{0};
    std::string evaluate_name; // DAP evaluateName: expression to re-evaluate this variable.

    // Optional presentation hint attributes.
    // Uses Luma-language polarity (true = writable); serialised as DAP
    // "readOnly" presentationHint attribute when false.
    bool is_mutable{false}; // true if the variable can be modified via setVariable.
};

// ─── Serialisation helpers ───
[[nodiscard]] JsonValue serialise_source(const Source& src);
[[nodiscard]] JsonValue serialise_breakpoint(const Breakpoint& bp);
[[nodiscard]] JsonValue serialise_stack_frame(const StackFrame& frame);
[[nodiscard]] JsonValue serialise_scope(const Scope& scope);
[[nodiscard]] JsonValue serialise_variable(const Variable& var, bool include_type = true);

} // namespace luma::dap

#endif // LUMA_DAP_TYPES_HPP
