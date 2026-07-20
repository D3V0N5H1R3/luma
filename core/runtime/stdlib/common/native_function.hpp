#ifndef LUMA_STDLIB_NATIVE_FUNCTION_HPP
#define LUMA_STDLIB_NATIVE_FUNCTION_HPP

// ═══════════════════════════════════════════════════════════
// Standard library error handling convention
// ═══════════════════════════════════════════════════════════
//
// Three patterns, chosen by the nature of the error:
//
// 1. Throw RuntimeError — for programming errors (bugs in the
//    Luma program).  The caller cannot reasonably recover;
//    these must be fixed in the source code.
//    ● Wrong argument count → use expect_args() / expect_min_args()
//    ● Wrong argument type  → use expect_string() / expect_integer()
//                              / expect_array() / expect_boolean()
//                              / expect_dict() / expect_numeric() etc.
//    ● Invalid enum/constant → throw RuntimeError with descriptive
//      message (e.g. unrecognised mode string)
//    ● Inner element type violations (e.g. array element is not a
//      task, row element is not an array) — still a programming
//      error even though the outer container type is correct.
//    ● Resource-limit violations (max_array_size, max_dictionary_size,
//      max_string_size) — these are safety rails, not user-recoverable.
//
// 2. Return Result<Value> — for expected failures that are part
//    of normal operation.  The program should handle them with
//    match/unwrap.
//    ● I/O errors   (file not found, network timeout)
//    ● Parse errors  (invalid JSON, bad number format)
//    ● Domain errors (singular matrix, division by zero)
//    Use make_success_value() / make_failure_value() helpers below.
//    Functions returning result<T> must NEVER throw for conditions
//    they can report as failure — be consistent within a module.
//
// 3. Return optional<Value> / std::nullopt — for lookups where
//    absence is a normal, expected outcome.
//    ● Dictionary key lookup, array find, regex first match
//
// Always use the centralised expect_* helpers for type validation.
// Do NOT write inline `if (!v.is_string()) throw …` checks — the
// helpers give uniform error messages and hints.
//
// See also: documents/Luma_Error_Handling.md §6 (stdlib rules).
//
// ── Header organisation ──
//
// This header is the main include point for stdlib code.  It
// transitively includes the sub-headers:
//
//   native_function_validation.hpp  — expect_* type-validation helpers
//   native_function_containers.hpp  — clone, mutation, result, and
//                                      functional container helpers
//
// Existing code that includes native_function.hpp continues to work
// unchanged — all symbols remain available through this header.
// ═══════════════════════════════════════════════════════════

#include "runtime/interpreter/environment.hpp"
#include "runtime/stdlib/common/native_function_containers.hpp"
#include "runtime/stdlib/common/native_function_fwd.hpp"
#include "runtime/stdlib/common/native_function_validation.hpp"

namespace luma {

/// @deprecated Use ModuleBuilder (function_builder.hpp) for new modules.
/// define_native is retained for backward compatibility with existing
/// module code and is used internally by define_method and ModuleBuilder.
/// New modules should use the builder pattern:
///
///   ModuleBuilder{"ModuleName", env}
///       .func("function_name", 1)
///           .extract_body(expect_string, [](const auto& s, const Args&, SourceLocation) {
///               return Value{static_cast<int64_t>(s.size())};
///           });
inline void define_native(const EnvPtr& env, std::string_view name, NativeFunction fn) {
    auto nfv = std::make_shared<NativeFunctionValue>();
    nfv->name = name;
    nfv->function = std::move(fn);

    env->define(std::string{name}, Value{std::move(nfv)}, false);
}

// ─── Method Registration Macros ───
// These macros reduce boilerplate for stdlib module definitions.
//
// Tradeoffs vs. a builder pattern:
//   + Macros: More concise, familiar pattern across 50+ modules
//   + Macros: Zero runtime overhead (expanded at compile time)
//   - Macros: Can't set breakpoints on macro expansions directly
//   - Macros: Harder to refactor with IDE tooling
//   + Builder: More debuggable, better IDE support
//   - Builder: More verbose, runtime overhead from std::function construction
//
// Decision: Keep macros. The stdlib is stable and rarely debugged at the
// registration level. If debugging is needed, expand the macro manually.

// ─── Method-style definition helpers ───
// These reduce the per-function boilerplate in stdlib modules.  A typical
// method definition goes from:
//
//   define_native(env, "Queue.length",
//       [](std::span<const Value> args, SourceLocation loc) -> Value {
//           expect_args("Queue.length", args, 1, loc);
//           auto src = expect_queue(args[0], "Queue.length", loc);
//           return Value{static_cast<std::int64_t>(src->elements.size())};
//       });
//
// To:
//
//   define_method(env, "Queue.length", 1, expect_queue,
//       [](const auto& src, const Args&, SourceLocation) -> Value {
//           return Value{static_cast<std::int64_t>(src->elements.size())};
//       });

// Shorthand for the args span type — defined in native_function_fwd.hpp,
// re-exported here for backward compatibility.

// define_method: Combines define_native + expect_args + self-extraction.
// ExtractFn must be callable as extract(args[0], name, loc) — i.e. one of
// the expect_* helpers above.  BodyFn receives the extracted self, the
// full args vector (for extra arguments beyond self), and the location.
template <typename ExtractFn, typename BodyFn>
void define_method(const EnvPtr& env, std::string_view name, std::size_t arity, ExtractFn extract,
                   BodyFn body) {
    define_native(env, name,
                  [n = std::string{name}, arity, extract, body = std::move(body)](
                      std::span<const Value> args, SourceLocation loc) -> Value {
                      expect_args(n, args, arity, loc);
                      auto self = extract(args[0], n, loc);
                      return body(self, args, loc);
                  });
}

} // namespace luma

#endif // LUMA_STDLIB_NATIVE_FUNCTION_HPP
