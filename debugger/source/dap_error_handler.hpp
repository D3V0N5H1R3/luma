#ifndef LUMA_DAP_ERROR_HANDLER_HPP
#define LUMA_DAP_ERROR_HANDLER_HPP

#include <exception>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <typeinfo>

#include "diagnostic_log.hpp"

namespace luma::dap {

// ═══════════════════════════════════════════════════════════
// Error handling conventions for the DAP debugger
//
// The debugger uses four complementary error-reporting strategies,
// chosen by the nature of the operation:
//
//   1. Exceptions — unrecoverable errors that prevent an operation
//      from even starting (missing program, invalid session state).
//      Caught at the DAP protocol boundary via classify_exception().
//
//   2. std::optional<T> — lookup results where absence is expected
//      and the reason is obvious (nullopt = not found).
//      Example: ExpressionEvaluator::try_local_lookup().
//
//   3. ExecutionResult — fallible operations that either
//      succeed or fail with a human-readable message.
//      Example: DebugExecutionEngine::continue_execution().
//
//   4. Domain-specific enums — multi-outcome operations where the
//      caller must distinguish between several failure modes.
//      Example: an enum result with more than two distinct outcomes.
//
// When adding new error-producing code, prefer (3) for operations
// invoked by DAP request handlers, (2) for pure lookups, (4) when
// there are more than two distinct outcomes, and (1) only for truly
// unrecoverable situations.
// ═══════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════
// DAP Error Codes
//
// Numeric identifiers for structured error responses.  The
// DAP specification allows adapters to define their own error
// codes; these cover the most common failure categories.
// ═══════════════════════════════════════════════════════════

namespace error_code {

constexpr int k_invalid_argument = 2;
constexpr int k_internal_error = 4;

} // namespace error_code

// ═══════════════════════════════════════════════════════════
// DAP Error Messages
//
// Centralised execution-control error strings, mirroring the core's
// luma::vm_errors catalogue, so shared wording (e.g. "Unknown thread
// ID") stays consistent across every throw/response site and a change
// propagates from one place.  Inline functions format messages that
// embed runtime values.
// ═══════════════════════════════════════════════════════════

namespace error_messages {

[[nodiscard]] inline std::string unknown_thread_id(int thread_id) {
    return std::format("Unknown thread ID {}", thread_id);
}

[[nodiscard]] inline std::string thread_not_paused(int thread_id) {
    return std::format("Thread {} is not paused", thread_id);
}

} // namespace error_messages

// ═══════════════════════════════════════════════════════════
// Exception classification
//
// Rethrows a captured exception_ptr, classifies it, logs a
// diagnostic to stderr, and returns a formatted error message
// paired with an appropriate error code.  The DAP protocol
// boundary sends the message on a failed response.
// ═══════════════════════════════════════════════════════════

struct ClassifiedError {
    std::string message;
    int code{error_code::k_internal_error};
};

// Classify a captured exception into an error message and code.
// Logs the error to the adapter's stderr for server-side diagnostics.
[[nodiscard]] inline ClassifiedError classify_exception(const std::string& command,
                                                        std::exception_ptr ep) {
    try {
        std::rethrow_exception(ep);
    } catch (const std::system_error& e) {
        write_adapter_diagnostic(
            std::format("DAP system error in '{}': {} {}", command, e.code().value(), e.what()));
        return {std::format("error: {}", e.what()), error_code::k_internal_error};
    } catch (const std::invalid_argument& e) {
        write_adapter_diagnostic(
            std::format("DAP invalid argument in '{}': {}", command, e.what()));
        return {std::format("error: {}", e.what()), error_code::k_invalid_argument};
    } catch (const std::exception& e) {
        write_adapter_diagnostic(
            std::format("DAP error in '{}': {}: {}", command, typeid(e).name(), e.what()));
        return {std::format("error: {}", e.what()), error_code::k_internal_error};
    } catch (...) {
        write_adapter_diagnostic(std::format("DAP: unknown exception type in '{}'", command));
        return {std::string("Unknown internal error"), error_code::k_internal_error};
    }
}

} // namespace luma::dap

#endif // LUMA_DAP_ERROR_HANDLER_HPP
