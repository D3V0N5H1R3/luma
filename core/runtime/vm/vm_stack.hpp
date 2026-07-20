// ─────────────────────────────────────────────────────────────────────────────
// VMStack — Stack and call frame storage for the VM.
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Hold the pre-allocated raw value stack, stack pointer
// triple (base/top/limit), and call frame vector.
//
// Extracted from VM via composition so that stack/frame concerns are
// separated from dispatch logic.  All data members are public so that
// the VM's member-function implementations (in separate translation units)
// can access them directly without syntactic overhead.
//
// Design notes:
//   - The stack is a fixed-size heap allocation (k_max Values).  Push/pop
//     are O(1) raw pointer operations; no reallocation ever occurs.
//   - Overflow/underflow are signalled by StackError (caught by the
//     VM dispatch loop alongside RuntimeError).
//   - The helper methods (push, pop, peek, ensure_space, ensure_depth, restore)
//     are provided for contexts where VMStack is used standalone.  The VM's
//     own push()/pop()/peek() wrappers call runtime_error() instead so that
//     stack faults include a source location in the diagnostic.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_RUNTIME_VM_VM_STACK_HPP
#define LUMA_RUNTIME_VM_VM_STACK_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "common/resource_limits.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/interpreter/runtime_exceptions.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

struct FunctionValue;
struct Binding;

// One active function invocation on the call stack.
struct CallFrame {
    const CompiledFunction* function{nullptr};
    FunctionValue* closure{nullptr}; // The FunctionValue (for upvalue access).
    const std::uint8_t* ip{nullptr}; // Instruction pointer into the function's chunk.
    std::size_t slot_offset{0};      // Offset of this frame's first slot in the stack.

    // Lazily-resolved per-function global inline cache.  Points into the owning
    // VM's global_index_cache_ entry for `function`; nullptr until the first
    // GetGlobal/SetGlobal executed in this frame.  Once set, global accesses
    // index it directly by the bytecode's u16 name handle, avoiding the
    // per-access string hash in VMGlobalCache.  The pointee is owned by the VM
    // and outlives every frame, so this raw pointer never dangles.
    std::vector<Binding*>* global_bindings{nullptr};
};

/// Compute the absolute stack index for a local variable slot in the given frame.
/// `slot` is a zero-based index relative to the frame's base (slot 0 = the callee).
[[nodiscard]] inline std::size_t frame_slot_index(const CallFrame& frame,
                                                  std::size_t slot) noexcept {
    return frame.slot_offset + slot;
}

// Stack and call frame storage for the VM.
struct VMStack {
    // ─── Capacity limits ────────────────────────────────────────────────────
    static constexpr std::size_t k_max{CompileTimeLimits::max_vm_stack_depth};
    static constexpr std::size_t k_frame_max{CompileTimeLimits::max_call_frames};

    // Hint appended to internal error diagnostics.
    static constexpr std::string_view k_internal_error_message{
        "this is an internal error \xe2\x80\x94 please report it"};

    // ─── Data ───────────────────────────────────────────────────────────────
    // Invariant: base <= top <= limit (maintained by push/pop/restore).
    // All three pointers refer into storage[0..k_max].
    std::unique_ptr<Value[]> storage;
    Value* base{nullptr};  // Points to storage[0]; never moves after construction.
    Value* top{nullptr};   // Next free slot; grows toward limit on push.
    Value* limit{nullptr}; // One-past-end sentinel: base + k_max.
    std::vector<CallFrame> frames;

    // ─── Construction ────────────────────────────────────────────────────────

    VMStack() {
        storage = std::make_unique<Value[]>(k_max);
        base = storage.get();
        top = base;
        limit = base + k_max;
    }

    VMStack(const VMStack&) = delete;
    VMStack& operator=(const VMStack&) = delete;

    VMStack(VMStack&& other) noexcept
        : storage{std::move(other.storage)},
          base{std::exchange(other.base, nullptr)},
          top{std::exchange(other.top, nullptr)},
          limit{std::exchange(other.limit, nullptr)},
          frames{std::move(other.frames)} {}

    VMStack& operator=(VMStack&& other) noexcept {
        if (this != &other) {
            storage = std::move(other.storage);
            base = std::exchange(other.base, nullptr);
            top = std::exchange(other.top, nullptr);
            limit = std::exchange(other.limit, nullptr);
            frames = std::move(other.frames);
        }
        return *this;
    }

    // ─── Stack helpers ───────────────────────────────────────────────────────

    [[nodiscard]] std::size_t size() const noexcept {
        assert(top >= base && "stack corruption: top below base");
        return static_cast<std::size_t>(top - base);
    }

    static_assert(sizeof(Value) * k_max < std::numeric_limits<std::size_t>::max() / 2,
                  "stack allocation would risk size_t overflow");

    // Push a value; throws StackError on overflow.
    void push(Value value) {
        if (top >= limit) [[unlikely]] {
            throw StackError{std::string{vm_errors::stack_overflow}};
        }
        *top++ = std::move(value);
    }

    // Pop the top value; throws StackError on underflow.
    [[nodiscard]] Value pop() {
        if (top == base) [[unlikely]] {
            throw StackError{std::string{vm_errors::stack_underflow}};
        }
        return std::move(*--top);
    }

    // Return a reference to the value at `distance` below the top.
    [[nodiscard]] Value& peek(std::size_t distance = 0) {
        if (distance >= size()) [[unlikely]] {
            throw StackError{vm_errors::stack_underflow_on_peek(distance, size())};
        }
        return *(top - 1 - distance);
    }

    [[nodiscard]] const Value& peek(std::size_t distance = 0) const {
        if (distance >= size()) [[unlikely]] {
            throw StackError{vm_errors::stack_underflow_on_peek(distance, size())};
        }
        return *(top - 1 - distance);
    }

    // Ensure that `needed` additional slots are available; throws on overflow.
    // TODO: Accept SourceLocation parameter once callers have location info,
    // so that stack-fault diagnostics can include the originating source line.
    void ensure_space(std::size_t needed) {
        const auto used = size();
        if (used + needed > k_max) [[unlikely]] {
            throw StackError{vm_errors::stack_overflow_detail(needed, k_max - used, k_max)};
        }
    }

    // Ensure the stack holds at least `required` values; throws on underflow.
    // TODO: Accept SourceLocation parameter once callers have location info,
    // so that stack-fault diagnostics can include the originating source line.
    void ensure_depth(std::size_t required, std::string_view context) {
        if (size() < required) [[unlikely]] {
            throw StackError{
                std::format("{}: stack underflow (expected {} values, got {}) \xe2\x80\x94 {}",
                            context, required, size(), k_internal_error_message)};
        }
    }

    // Read-only view of the live stack slots.
    [[nodiscard]] std::span<const Value> span() const {
        return {base, top};
    }

    // Mutable view of the live stack slots (debugger use only).
    [[nodiscard]] std::span<Value> span_mut() {
        return {base, top};
    }

#ifndef NDEBUG
    // Verify the base/top/limit invariant.  Call at the start or end of
    // any method that manipulates the stack pointers directly.
    void check_invariants() const noexcept {
        assert(base != nullptr && "VMStack not initialised");
        assert(top >= base && "stack corruption: top below base");
        assert(top <= limit && "stack corruption: top beyond limit");
        assert(limit == base + k_max && "stack corruption: limit mismatch");
    }
#endif

    // RAII guard that resets the stack to empty and clears all call frames
    // on destruction unless explicitly dismissed.  Used by execution entry
    // points (execute_function, execute_tests) to guarantee consistent
    // VM state after an exception.
    struct ResetGuard {
        VMStack& stack;
        bool dismissed{false};

        explicit ResetGuard(VMStack& s) : stack(s) {}

        ~ResetGuard() {
            if (!dismissed) {
                stack.top = stack.base;
                stack.frames.clear();
            }
        }

        ResetGuard(const ResetGuard&) = delete;
        ResetGuard& operator=(const ResetGuard&) = delete;
    };

    // Replace the entire stack contents (time-travel debugger); throws if too large.
    void restore(std::vector<Value> s) {
        const auto n = s.size();
        if (n > k_max) [[unlikely]] {
            throw StackError{vm_errors::stack_restore_too_large(n, k_max)};
        }
        for (std::size_t i = 0; i < n; ++i) {
            base[i] = std::move(s[i]);
        }
        top = base + n;
    }
};

} // namespace luma

#endif // LUMA_RUNTIME_VM_VM_STACK_HPP
