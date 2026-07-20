#include "runtime/vm/vm.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "common/unreachable.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/concurrency/task_scope.hpp"
#include "runtime/interpreter/runtime_exceptions.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

// Hint for the compiler to optimise the dispatch loop aggressively.
// Only active on GCC, which honours per-function optimisation attributes.
// Clang ignores `optimize(...)` and warns about it, and MSVC uses
// profile-guided optimisation instead.
#if defined(__GNUC__) && !defined(__clang__) && !defined(_MSC_VER)
#define VM_OPTIMIZE __attribute__((optimize("O3")))
#else
#define VM_OPTIMIZE
#endif

thread_local TaskScope* VMTaskManager::current_scope{nullptr};

// ─────────── Execution loop ───────────

// Reads one opcode byte per iteration and calls the matching VM::op_*()
// handler via k_dispatch_table.  op_return(), op_get_local_return(), and
// op_end_module() signal the loop to exit by setting dispatch_return_value_.

VM_OPTIMIZE Value VM::run() {
    dispatch_return_value_.reset();

    for (;;) {
        try {
            // Per-opcode debug-hook check.  The gating atomic is tested inline
            // here so the common no-debugger path pays a single relaxed-ordered
            // load and never makes the check_debug_hooks() call (which takes a
            // shared_lock and copies the hook only once a pause is armed).
            if (debug_.pause_requested.load(std::memory_order_acquire)) [[unlikely]] {
                if (check_debug_hooks()) {
                    return Value{};
                }
            }

            // Bounds-check the instruction pointer before fetching the next
            // opcode.  Well-formed bytecode always exits the loop via a
            // terminator (Return / GetLocalReturn / EndModule) that sets
            // dispatch_return_value_, so this never trips in practice.  For
            // unverified or crafted bytecode that falls through past the last
            // instruction — or jumps to one-past-the-end — this turns an
            // out-of-bounds read into a catchable runtime error.
            ensure_bytecode_available(1);

            const auto opcode = *stack_.frames.back().ip++;
            (this->*k_dispatch_table[opcode])();

            if (dispatch_return_value_.has_value()) [[unlikely]] {
                Value result = std::move(*dispatch_return_value_);
                dispatch_return_value_.reset();
                return result;
            }

        } catch (const RuntimeError& e) {
            if (!handle_exception(e)) {
                throw;
            }
        } catch (const std::runtime_error& e) {
            if (!handle_exception(e)) {
                throw;
            }
        }
    }
    return Value{};
}

Value VM::run_to_return() {
    // The caller has already pushed the frame to execute.  Drive the shared
    // dispatch loop until that frame returns by temporarily lowering
    // base_depth_ to the surrounding frame depth: complete_return() sets
    // dispatch_return_value_ (ending run()) as soon as the pushed frame pops
    // back to base_depth_.  The previous base_depth_ is always restored — even
    // on exception — so nested run_to_return() calls stay balanced.
    const auto frame_base = stack_.frames.size() - 1;
    const auto prev_base = base_depth_;
    base_depth_ = frame_base;

    try {
        auto result = run();
        base_depth_ = prev_base;
        return result;
    } catch (...) {
        base_depth_ = prev_base;
        throw;
    }
}

// ─────────── Stack operations ───────────

#if defined(__clang__) && !defined(_MSC_VER)
#define VM_HOT __attribute__((hot))
#elif defined(__GNUC__)
#define VM_HOT __attribute__((hot))
#else
#define VM_HOT
#endif

VM_HOT void VM::push(Value value) {
    if (stack_.top >= stack_.limit) [[unlikely]] {
        runtime_error(vm_errors::stack_overflow);
    }

    *stack_.top++ = std::move(value);
}

void VM::validate_stack_space(std::size_t needed) {
    const auto used = stack_size();
    if (used + needed > VMStack::k_max) [[unlikely]] {
        runtime_error(
            vm_errors::stack_overflow_detail(needed, VMStack::k_max - used, VMStack::k_max));
    }
}

void VM::validate_stack_depth(std::size_t required, std::string_view context) {
    if (stack_size() < required) [[unlikely]] {
        runtime_error(vm_errors::stack_underflow_depth(context, required, stack_size()),
                      VMStack::k_internal_error_message);
    }
}

VM_HOT Value VM::pop() {
    if (stack_.top == stack_.base) [[unlikely]] {
        runtime_error(vm_errors::stack_underflow);
    }

    return std::move(*--stack_.top);
}

VM_HOT Value& VM::peek(std::size_t distance) {
    if (distance >= stack_size()) [[unlikely]] {
        runtime_error(vm_errors::stack_underflow_on_peek(distance, stack_size()));
    }
    return *(stack_.top - 1 - distance);
}

VM_HOT const Value& VM::peek(std::size_t distance) const {
    if (distance >= stack_size()) [[unlikely]] {
        throw StackError(vm_errors::stack_underflow_on_peek(distance, stack_size()));
    }
    return *(stack_.top - 1 - distance);
}

void VM::restore_stack(std::vector<Value> s) {
    const auto n = s.size();
    if (n > VMStack::k_max) [[unlikely]] {
        runtime_error(vm_errors::stack_restore_too_large(n, VMStack::k_max));
    }
    for (std::size_t i = 0; i < n; ++i) {
        stack_.base[i] = std::move(s[i]);
    }
    stack_.top = stack_.base + n;
}

// ─────────── Instruction helpers ───────────

// The operand readers skip the per-read truncation check for verified
// bytecode.  The bytecode verifier (verifier.cpp) proves that every
// instruction — opcode plus all operands — fits within the chunk, so once the
// always-on fetch guard in run() has admitted an opcode at a valid instruction
// boundary, its operand bytes are guaranteed to be present.  Unverified or
// crafted bytecode keeps the full check.  Resolving the frame once also avoids
// recomputing frames.back() for the pointer bump.

VM_HOT std::uint8_t VM::read_byte() {
    auto& frame = stack_.frames.back();
    if (!frame.function->is_verified()) {
        ensure_bytecode_available(frame, 1);
    }
    return *frame.ip++;
}

VM_HOT std::uint16_t VM::read_u16() {
    auto& frame = stack_.frames.back();
    if (!frame.function->is_verified()) {
        ensure_bytecode_available(frame, 2);
    }
    auto& ip = frame.ip;
    auto hi =
        static_cast<std::uint16_t>(static_cast<unsigned>(*ip++) << VMConstants::k_byte_shift_8);
    auto lo = static_cast<std::uint16_t>(*ip++);
    return static_cast<std::uint16_t>(hi | lo);
}

VM_HOT std::uint32_t VM::read_u32() {
    auto& frame = stack_.frames.back();
    if (!frame.function->is_verified()) {
        ensure_bytecode_available(frame, 4);
    }
    auto& ip = frame.ip;
    const auto b0 = static_cast<std::uint32_t>(*ip++) << VMConstants::k_byte_shift_24;
    const auto b1 = static_cast<std::uint32_t>(*ip++) << VMConstants::k_byte_shift_16;
    const auto b2 = static_cast<std::uint32_t>(*ip++) << VMConstants::k_byte_shift_8;
    const auto b3 = static_cast<std::uint32_t>(*ip++);
    return b0 | b1 | b2 | b3;
}

Value VM::read_constant() {
    auto index = read_u16();
    const auto& constants = stack_.frames.back().function->chunk().constants;

    if (index >= constants.size()) [[unlikely]] {
        runtime_error(vm_errors::constant_index_out_of_bounds(index, constants.size()));
    }

    return constants[index];
}

std::string_view VM::read_name() {
    auto index = read_u16();

    return checked_name(index);
}

std::string_view VM::checked_name(std::uint16_t index) const {
    const auto& names = stack_.frames.back().function->chunk().names;

    if (index >= names.size()) [[unlikely]] {
        runtime_error(vm_errors::name_index_out_of_bounds(index, names.size()));
    }

    return names[index];
}

// ─────────── Frame slot validation ───────────

Value& VM::get_local_slot(const CallFrame& cf, std::uint16_t slot) {
    auto index = frame_slot_index(cf, slot);

    // Bounds-check local slots unconditionally, even for verified functions: a
    // frame's slot_offset is a runtime quantity, so the static verifier cannot
    // prove that slot_offset + slot stays within the value stack.  Without this
    // a crafted or corrupt .lumc with the verified bit set could drive the VM
    // into an out-of-bounds stack read/write.  Legitimate bytecode never trips
    // it because every accessed local lives below the current stack top.
    if (index >= stack_size()) [[unlikely]] {
        runtime_error(vm_errors::local_variable_out_of_bounds(slot, cf.slot_offset),
                      VMStack::k_internal_error_message);
    }

    return stack_.base[index];
}

} // namespace luma
