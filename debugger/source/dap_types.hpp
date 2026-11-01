#ifndef LUMA_DAP_TYPES_HPP
#define LUMA_DAP_TYPES_HPP

#include <string>
#include <string_view>
#include <vector>

#include "json/json.hpp"

namespace luma::dap {

using luma::json::JsonValue;

// ─── DAP event names ───
constexpr std::string_view k_event_initialized = "initialized";
constexpr std::string_view k_event_stopped = "stopped";
constexpr std::string_view k_event_continued = "continued";
constexpr std::string_view k_event_terminated = "terminated";
constexpr std::string_view k_event_exited = "exited";
constexpr std::string_view k_event_thread = "thread";
constexpr std::string_view k_event_output = "output";
constexpr std::string_view k_event_invalidated = "invalidated";
constexpr std::string_view k_event_breakpoint = "breakpoint";

// ─── Breakpoint event reasons ───
constexpr std::string_view k_breakpoint_reason_changed = "changed";

// ─── Thread event reasons ───
constexpr std::string_view k_thread_reason_started = "started";
constexpr std::string_view k_thread_reason_exited = "exited";

// ─── Stop reasons ───
// DAP protocol string values — these exact strings are required by the
// Debug Adapter Protocol specification for stopped-event reasons.
constexpr std::string_view k_stop_reason_breakpoint = "breakpoint";
constexpr std::string_view k_stop_reason_step = "step";
constexpr std::string_view k_stop_reason_exception = "exception";
constexpr std::string_view k_stop_reason_pause = "pause";
constexpr std::string_view k_stop_reason_entry = "entry";

// Type-safe enum for stop reasons.  Use stop_reason_string() to convert
// to the DAP protocol string when serialising stopped events.
enum class StopReason {
    Step,
    Breakpoint,
    Exception,
    Pause,
    Entry
};

[[nodiscard]] constexpr std::string_view stop_reason_string(StopReason reason) noexcept {
    switch (reason) {
        case StopReason::Step:
            return k_stop_reason_step;
        case StopReason::Breakpoint:
            return k_stop_reason_breakpoint;
        case StopReason::Exception:
            return k_stop_reason_exception;
        case StopReason::Pause:
            return k_stop_reason_pause;
        case StopReason::Entry:
            return k_stop_reason_entry;
    }
    return "unknown";
}

// Sentinel value indicating "no file" or "file not found".
constexpr int k_file_id_not_found = -1;

// ─── Exception filter IDs ───
constexpr std::string_view k_filter_caught = "caught";
constexpr std::string_view k_filter_uncaught = "uncaught";

// ─── Output categories ───
constexpr std::string_view k_output_console = "console";
constexpr std::string_view k_output_stdout = "stdout";
constexpr std::string_view k_output_stderr = "stderr";

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
    std::string condition;
    std::string hit_condition;
    std::string log_message;

    // Identifier already reported to the client for this breakpoint (assigned
    // before the program was compiled), or 0 to let the breakpoint manager
    // allocate one.  Carrying it through resolution keeps the ID the editor
    // knows identical to the one in later `breakpoint` and `stopped` events.
    int id{0};
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
