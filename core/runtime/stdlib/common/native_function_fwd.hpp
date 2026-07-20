#ifndef LUMA_STDLIB_NATIVE_FUNCTION_FWD_HPP
#define LUMA_STDLIB_NATIVE_FUNCTION_FWD_HPP

// ═══════════════════════════════════════════════════════════
// Forward declarations and type aliases for native_function.hpp
// ═══════════════════════════════════════════════════════════
//
// Include this header when you only need the NativeCallable type alias,
// the NativeCallableScope RAII guard, or the Args shorthand — but NOT
// the inline expect_*, make_failure_value(), clone_array(), or
// container_* helpers defined in native_function.hpp.
//
// Typical consumers: vm.hpp, graphicalui_internal.hpp, or any header
// that stores a NativeCallable but never calls stdlib helpers directly.

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

class CancellationToken;

// Callable support — allows stdlib higher-order functions to call
// user-defined functions through the interpreter.
using NativeCallable =
    std::function<Value(const Value&, std::vector<Value>&, const SourceLocation&)>;

// Shorthand for the args span type used in all lambda signatures.
using Args = std::span<const Value>;

namespace detail {

// Thread-local pointer to the active interpreter's NativeCallable.
// Set via NativeCallableScope RAII guard at each interpreter entry point.
inline thread_local const NativeCallable* active_native_callable = nullptr;

// Thread-local pointer to the active cancellation token.
// Set by evaluate_spawn when a task runs inside a task_scope.
inline thread_local const std::shared_ptr<CancellationToken>* active_cancel_token = nullptr;

} // namespace detail

// RAII guard — pushes a NativeCallable as the active callback for the current
// thread and restores the previous one on destruction.  Supports
// multiple Interpreter instances per thread (nested/re-entrant).
class NativeCallableScope {
public:
    explicit NativeCallableScope(const NativeCallable& fn) : prev_{detail::active_native_callable} {
        detail::active_native_callable = &fn;
    }

    ~NativeCallableScope() noexcept {
        detail::active_native_callable = prev_;
    }

    NativeCallableScope(const NativeCallableScope&) = delete;
    NativeCallableScope& operator=(const NativeCallableScope&) = delete;
    NativeCallableScope(NativeCallableScope&&) = delete;
    NativeCallableScope& operator=(NativeCallableScope&&) = delete;

private:
    const NativeCallable* const prev_;
};

} // namespace luma

#endif // LUMA_STDLIB_NATIVE_FUNCTION_FWD_HPP
