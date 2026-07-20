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
} // namespace variable

// Expression evaluator errors.
namespace expression {
constexpr std::string_view evaluation_failed = "<evaluation failed>";
constexpr std::string_view too_long = "<expression too long>";
constexpr std::string_view invalid = "<invalid expression>";
constexpr std::string_view error = "<evaluation error>";
constexpr std::string_view timeout = "<timeout>";
} // namespace expression

// Session errors.
namespace session {
constexpr std::string_view no_active_session = "No active debug session";
} // namespace session

// Request-handler errors — invalid or missing arguments in DAP requests
// (launch, restart, initialize, stackTrace, setVariable, setDataBreakpoints).
namespace request {
constexpr std::string_view initialize_auth_failed =
    "Authentication failed: invalid or missing lumaAuthToken";
constexpr std::string_view launch_missing_program = "Missing 'program' argument in launch request";
constexpr std::string_view restart_missing_program = "Missing 'program' in restart arguments";
constexpr std::string_view invalid_thread_id = "Invalid thread ID";
constexpr std::string_view set_variable_missing_fields =
    "setVariable requires variablesReference, name, and value";
constexpr std::string_view set_data_breakpoints_missing_array =
    "setDataBreakpoints: missing 'breakpoints' array";
} // namespace request

} // namespace luma::dap::messages

#endif // LUMA_DAP_DEBUGGER_MESSAGES_HPP
