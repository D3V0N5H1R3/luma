// ─────────────────────────────────────────────────────────────────────────────
// VM Dispatch Helpers — Shared inline utilities for dispatch files.
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Provide lightweight, inline helper types and utilities
// used across the vm_dispatch_*.cpp translation units to reduce repeated
// stack manipulation and frame introspection patterns.
//
// All helpers are designed to stay on the VM's hot path: they are either
// trivial inline functions or small structs with no heap allocation.
//
// Usage: Included by vm.hpp so that the helper types are visible to all
// dispatch files through the existing #include "runtime/vm/vm.hpp".
//
// ─── Design boundary ─────────────────────────────────────────────────────────
// Helpers in THIS file must not depend on VM internals (no VM* / VM& parameter,
// no access to VMStack, VMTaskManager, etc.).  They operate solely on
// primitive C++ types, Value, or opcode-stream pointers.
//
// Helpers that need VM state (e.g., access to the call frame or the value
// stack) belong as private VM member functions in vm.hpp / vm_helpers.cpp,
// not here.
//
// See also:
//   common/overflow.hpp  — compile-time overflow predicates
//                          (would_overflow_add, would_overflow_sub, etc.)
//                          Prefer these over hand-rolled overflow checks in
//                          dispatch files; they are constexpr and template-safe.
//   common/index_validator.hpp — bounds checking and Python-style index
//                          normalisation for collection access.
//
// ─── VM validation helper naming conventions ─────────────────────────────────
//   validate_*() — pre-condition assertion; throws on failure,
//                  returns void (or the validated value/reference).
//                  Preferred convention for all new helpers.
//   check_*()   — returns bool, no throw
//   try_*()     — returns std::optional<T>, no throw
//
// Note: require_*() was a historical synonym for validate_*(). All helpers
// have been renamed to validate_*() for consistency.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_RUNTIME_VM_VM_DISPATCH_HELPERS_HPP
#define LUMA_RUNTIME_VM_VM_DISPATCH_HELPERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "common/resource_limits.hpp"
#include "runtime/interpreter/runtime_exceptions.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm_constants.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

// Result of popping the right operand from a binary operation while
// keeping the left operand on the stack as a mutable reference.
//
// The typical binary-op sequence:
//     auto [a, b] = pop_binary_ref();
//     a = some_operation(a, b);
//
// replaces the repeated pattern:
//     auto b = pop();
//     auto& a = *(stack_.top - 1);
//     a = some_operation(a, b);
struct BinaryOperands {
    Value& a_ref; // Left operand — remains on the stack for in-place update.
    Value b;      // Right operand — popped from the stack.
};

/// A vector that uses stack storage for small counts, falling back to
/// heap allocation for larger sizes.  Used in VM hot paths to avoid
/// unnecessary heap allocations for common small-arity function calls.
template <typename T, std::size_t StackCapacity = VMConstants::k_small_vector_capacity>
class SmallVector {
public:
    explicit SmallVector(std::size_t count) : size_(count) {
        if (count > StackCapacity) {
            heap_.resize(count);
        }
    }

    SmallVector(const SmallVector&) = delete;
    SmallVector& operator=(const SmallVector&) = delete;
    SmallVector(SmallVector&&) noexcept = default;
    SmallVector& operator=(SmallVector&&) noexcept = default;

    T& operator[](std::size_t i) noexcept {
        return size_ <= StackCapacity ? stack_[i] : heap_[i];
    }

    const T& operator[](std::size_t i) const noexcept {
        return size_ <= StackCapacity ? stack_[i] : heap_[i];
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    T* data() noexcept {
        return size_ <= StackCapacity ? stack_.data() : heap_.data();
    }

    const T* data() const noexcept {
        return size_ <= StackCapacity ? stack_.data() : heap_.data();
    }

    T* begin() noexcept {
        return data();
    }

    T* end() noexcept {
        return data() + size_;
    }

    const T* begin() const noexcept {
        return data();
    }

    const T* end() const noexcept {
        return data() + size_;
    }

private:
    std::size_t size_;
    std::array<T, StackCapacity> stack_{};
    std::vector<T> heap_;
};

// ─────────── Collection type dispatch ───────────

/// Dispatches on collection type, calling the appropriate handler.
/// Returns true if handled, false if the value is not a supported collection.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702) // unreachable when on_default() does not return (e.g. throws)
#endif
template <typename ArrayFn, typename DictFn, typename StringFn, typename DefaultFn>
bool dispatch_collection(const Value& val, ArrayFn&& on_array, DictFn&& on_dict,
                         StringFn&& on_string, DefaultFn&& on_default) {
    if (val.is_array()) {
        on_array();
        return true;
    }
    if (val.is_dictionary()) {
        on_dict();
        return true;
    }
    if (val.is_string()) {
        on_string();
        return true;
    }
    on_default();
    return false;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// ─────────── Overflow-safe unary integer operations ───────────

/// Negate an integer, promoting to double on INT64_MIN overflow.
[[nodiscard]] inline Value safe_negate(std::int64_t n) {
    if (n == std::numeric_limits<std::int64_t>::min()) [[unlikely]] {
        return Value{static_cast<double>(n) * -1.0};
    }
    return Value{-n};
}

/// Increment an integer, promoting to double on INT64_MAX overflow.
[[nodiscard]] inline Value safe_increment(std::int64_t n) {
    if (n == std::numeric_limits<std::int64_t>::max()) [[unlikely]] {
        return Value{static_cast<double>(n) + 1.0};
    }
    return Value{n + 1};
}

/// Decrement an integer, promoting to double on INT64_MIN overflow.
[[nodiscard]] inline Value safe_decrement(std::int64_t n) {
    if (n == std::numeric_limits<std::int64_t>::min()) [[unlikely]] {
        return Value{static_cast<double>(n) - 1.0};
    }
    return Value{n - 1};
}

// ─────────── String size validation ───────────

/// Returns true when concatenating two strings of the given sizes would
/// stay within ResourceLimits::max_string_size.  This is a pure predicate
/// (check_* convention) — the caller decides how to report the failure.
[[nodiscard]] inline bool check_string_concat_size(std::size_t a_size,
                                                   std::size_t b_size) noexcept {
    return a_size <= ResourceLimits::max_string_size &&
           b_size <= ResourceLimits::max_string_size - a_size;
}

// ─────────── Jump bounds validation ───────────

/// Validate that a jump destination IP is within the current bytecode buffer.
/// Throws BytecodeError for malformed bytecode.  Source location is intentionally
/// omitted: by the time this check fires the IP has already been advanced past the
/// buffer end, making any location derived from it meaningless.
///
/// Used by op_jump, op_jump_if_false, and op_jump_if_true (the three identical
/// callers that motivated extraction).
inline void validate_jump_target(const std::uint8_t* ip, const std::uint8_t* code_end) {
    if (ip > code_end) [[unlikely]] {
        throw BytecodeError{vm_errors::jump_target_beyond_bounds};
    }
}

} // namespace luma

#endif // LUMA_RUNTIME_VM_VM_DISPATCH_HELPERS_HPP
