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
    // One-past-the-end pointer into `function`'s chunk, cached at frame-push
    // time (and refreshed on tail-call, which reassigns `function` in place)
    // so the jump/loop opcode handlers can read it directly instead of
    // recomputing `function->chunk().code.data() + chunk.code.size()` via two
    // pointer chases on every jump.
    const std::uint8_t* code_end{nullptr};
    std::size_t slot_offset{0}; // Offset of this frame's first slot in the stack.

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

    // The k_max-slot storage (several MiB) is recycled through a small
    // per-thread pool: every spawned task constructs a fresh VM, and
    // allocating + value-initialising a full stack per spawn dominated the
    // cost of `spawn`.  A recycled block holds only empty / moved-from Values
    // (see release_storage), so it is ready for reuse without re-initialising.
    VMStack() : storage{acquire_storage()} {
        base = storage.get();
        top = base;
        limit = base + k_max;
    }

    ~VMStack() {
        release_storage();
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
            release_storage();
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

    // Move the stack top to `new_top`, destroying every value above it so the
    // resources those values hold (locals of a returned function, temporaries
    // abandoned by exception unwinding, …) are released deterministically
    // instead of lingering until the slot is overwritten — a lingering copy
    // would also keep reference counts high and force copy-on-write copies.
    // Every path that lowers the top other than pop() must use this.
    //
    // Defensive: if crafted bytecode left the top *below* `new_top` (it popped
    // past its own frame), the slots in between are reset to `none` rather
    // than exposing stale values.  Precondition: base <= new_top <= limit.
    void truncate_to(Value* new_top) noexcept {
        assert(new_top >= base && new_top <= limit && "truncate_to: target outside the stack");

        while (top > new_top) {
            *--top = Value{};
        }

        while (top < new_top) {
            *top++ = Value{};
        }
    }

    // Convenience overload taking a slot count from the stack base.
    void truncate_to(std::size_t new_size) noexcept {
        truncate_to(base + new_size);
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
                stack.truncate_to(stack.base);
                stack.frames.clear();
            }
        }

        ResetGuard(const ResetGuard&) = delete;
        ResetGuard& operator=(const ResetGuard&) = delete;
    };

private:
    // Per-thread free list of stack blocks; see the constructor comment.
    struct StoragePool {
        // At most this many idle blocks are kept per thread — enough for the
        // one task VM a worker runs at a time plus a nested VM.
        static constexpr std::size_t k_capacity{2};

        std::vector<std::unique_ptr<Value[]>> blocks;

        StoragePool() = default;
        StoragePool(const StoragePool&) = delete;
        StoragePool& operator=(const StoragePool&) = delete;
        StoragePool(StoragePool&&) = delete;
        StoragePool& operator=(StoragePool&&) = delete;

        ~StoragePool() {
            destroyed() = true;
        }

        // Set once this thread's pool has been destroyed (thread exit), so a
        // VMStack destroyed later on the same thread frees its block instead
        // of touching the dead pool.  Trivially destructible, so it stays
        // readable for the rest of the thread's lifetime.
        [[nodiscard]] static bool& destroyed() noexcept {
            thread_local bool flag = false;
            return flag;
        }

        [[nodiscard]] static StoragePool& instance() {
            thread_local StoragePool pool;
            return pool;
        }
    };

    [[nodiscard]] static std::unique_ptr<Value[]> acquire_storage() {
        if (!StoragePool::destroyed()) {
            auto& blocks = StoragePool::instance().blocks;

            if (!blocks.empty()) {
                auto block = std::move(blocks.back());
                blocks.pop_back();
                return block;
            }
        }

        return std::make_unique<Value[]>(k_max);
    }

    // Destroy the live values and hand the block back to this thread's pool.
    // Slots above the top are empty or moved-from (truncate_to and pop() leave
    // nothing alive there), so the block can be reused as-is.
    void release_storage() noexcept {
        if (!storage) {
            return;
        }

        truncate_to(base);

        if (!StoragePool::destroyed()) {
            try {
                auto& blocks = StoragePool::instance().blocks;

                if (blocks.size() < StoragePool::k_capacity) {
                    blocks.push_back(std::move(storage));
                }
            } catch (...) { // NOLINT(bugprone-empty-catch)
                // Pool bookkeeping failed (allocation); just free the block.
            }
        }

        storage.reset();
        base = nullptr;
        top = nullptr;
        limit = nullptr;
    }
};

} // namespace luma

#endif // LUMA_RUNTIME_VM_VM_STACK_HPP
