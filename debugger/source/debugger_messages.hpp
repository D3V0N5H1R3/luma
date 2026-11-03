#ifndef LUMA_DAP_DEBUGGER_MESSAGES_HPP
#define LUMA_DAP_DEBUGGER_MESSAGES_HPP

#include <string_view>

namespace luma::dap::messages {

// Variable inspector errors.
namespace variable {
constexpr std::string_view stale_reference = "<stale reference>";
constexpr std::string_view immutable_variable = "<immutable variable>";
constexpr std::string_view not_found_format = "Variable '{}' not found";
constexpr std::string_view type_mismatch_format = "Cannot assign '{}' to variable of type '{}'";
constexpr std::string_view frame_not_paused =
    "Cannot set a variable: the frame is no longer paused";
constexpr std::string_view nested_not_supported =
    "Cannot set '{}': only variables in the Local, Closure, and Global scopes can be modified";
} // namespace variable

// Expression evaluator errors.
namespace expression {
constexpr std::string_view evaluation_failed = "<evaluation failed>";
constexpr std::string_view too_long = "<expression too long>";
constexpr std::string_view invalid = "<invalid expression>";
constexpr std::string_view error = "<evaluation error>";
constexpr std::string_view timeout = "<timeout>";
} // namespace expression

// Breakpoint status messages shown next to a breakpoint in the editor.
namespace breakpoint {
constexpr std::string_view pending_launch = "Breakpoint will be verified when program launches";
constexpr std::string_view moved_format = "Breakpoint moved to executable line {}";
constexpr std::string_view unresolved_format =
    "Breakpoint in '{}' could not be resolved: file not found in compiled program";
} // namespace breakpoint

// Session errors.
namespace session {
constexpr std::string_view no_active_session = "No active debug session";
} // namespace session

// Request-handler errors — invalid or missing arguments in DAP requests
// (launch, restart, initialize, stackTrace, setVariable).
namespace request {
constexpr std::string_view launch_missing_program = "Missing 'program' argument in launch request";
constexpr std::string_view restart_missing_program = "Missing 'program' in restart arguments";
constexpr std::string_view invalid_thread_id = "Invalid thread ID";
constexpr std::string_view set_variable_missing_fields =
    "setVariable requires variablesReference, name, and value";
constexpr std::string_view unsupported_command = "Unsupported command";
} // namespace request

} // namespace luma::dap::messages

#endif // LUMA_DAP_DEBUGGER_MESSAGES_HPP
