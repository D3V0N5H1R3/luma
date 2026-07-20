#ifndef LUMA_STDLIB_ERROR_HELPERS_HPP
#define LUMA_STDLIB_ERROR_HELPERS_HPP

// ═══════════════════════════════════════════════════════════
// Generic error-wrapping templates for stdlib modules
// ═══════════════════════════════════════════════════════════
//
// Many stdlib modules (FileSystem, Json, Http, etc.) follow an
// identical try-catch pattern for operations that return Result
// values:
//
//   try {
//       // perform operation — may return success OR failure
//       return some_result_value;
//   } catch (const std::exception& e) {
//       return make_failure_value(
//           std::format("Module.func: {}", e.what()));
//   }
//
// The templates below eliminate this boilerplate.  Unlike
// safe_call() and apply_with_error_handling() in
// native_function_containers.hpp (which always wrap the return
// value in make_success_value), these templates pass through
// whatever the inner operation returns — the operation itself
// decides whether to return success or failure.  Only unhandled
// exceptions are caught and wrapped in a failure result.
//
// Choosing the right wrapper:
//
//   safe_call(mod, fn, op)
//     → op() returns a raw Value; wrapper wraps in success.
//       Exceptions get module/function metadata.
//
//   apply_with_error_handling(op)
//     → op() returns a raw Value; wrapper wraps in success.
//       No module/function metadata on exceptions.
//
//   wrap_result_operation(mod, fn, op[, code])   ← this header
//     → op() returns success/failure directly; pass-through.
//       Exceptions become prefixed failure results, optionally
//       carrying a machine-readable error_code (e.g. parse_error).
//
// In short:
//   Use safe_call()              when op() returns a raw Value → wrapped in success, exceptions get metadata
//   Use wrap_result_operation()  when op() returns success/failure directly → pass-through, exceptions get prefix
//   Use apply_with_error_handling() when op() returns a raw Value → wrapped in success, no metadata
//
// ═══════════════════════════════════════════════════════════

#include <concepts>
#include <string>
#include <string_view>

#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/native_function_containers.hpp"

namespace luma {

// Generic wrapper for operations that return result<T> Values.
//
// The operation callable must return a Value directly (typically via
// make_success_value / make_failure_value).  If the operation throws
// a std::exception, it is caught and returned as a failure result
// prefixed with "Module.function: <what>".  An optional error_code
// attaches a machine-readable identifier (e.g. error_codes::parse_error)
// to those exception-derived failures; when left empty the failure carries
// the prefixed message only, matching the historical behaviour.
//
// Use this when the inner operation needs to make its own success/failure
// decisions (e.g. checking file existence before reading).
template <typename Func>
    requires std::invocable<Func>
[[nodiscard]] Value wrap_result_operation(std::string_view module, std::string_view function,
                                          Func&& fn, std::string_view error_code = "") {
    try {
        return std::forward<Func>(fn)();
    } catch (const RuntimeError& e) {
        return failure_msg(module, function, e.what(), error_code);
    } catch (const std::exception& e) {
        return failure_msg(module, function, e.what(), error_code);
    }
}

} // namespace luma

#endif // LUMA_STDLIB_ERROR_HELPERS_HPP
