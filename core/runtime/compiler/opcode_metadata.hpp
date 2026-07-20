#ifndef LUMA_RUNTIME_COMPILER_OPCODE_METADATA_HPP
#define LUMA_RUNTIME_COMPILER_OPCODE_METADATA_HPP

// Centralised opcode metadata utilities.
//
// Per-opcode facts (operand category, operand layout, semantic-group flags,
// and fixed stack effect) live in a single constexpr table in opcode.hpp.
// The helpers here are thin projections of that table, so adding or changing
// an opcode means editing one table row -- there are no parallel switch
// statements to keep in sync.
//
// This header also provides instruction_size(), which computes the full size
// of an instruction (including variable-length operands) from the bytecode
// stream.  Both the bytecode verifier and the optimizer use it.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "runtime/compiler/opcode.hpp"

namespace luma {

// Map an opcode to the category of its u16 operand (verifier bounds
// checking).  The OperandCategory enum itself lives in opcode.hpp next to
// the table this projects from.
[[nodiscard]] constexpr OperandCategory operand_category(Op op) noexcept {
    return opcode_operand_category(op);
}

// Returns the total size in bytes of the instruction at `offset` in `code`,
// including the opcode byte itself.  Handles variable-size instructions
// (MakeRecord, RecordWith) by reading their embedded operand counts.
//
// For truncated bytecode (offset + required bytes >= code.size()), returns
// the base size to avoid out-of-bounds reads.
[[nodiscard]] inline std::size_t instruction_size(const std::vector<std::uint8_t>& code,
                                                  std::size_t offset) {
    const auto op = static_cast<Op>(code[offset]);

    switch (op) {
        case Op::MakeRecord: {
            // MakeRecord <u16 type_name> <u8 field_count> [<u16 field_name> * field_count]
            if (offset + 3 >= code.size()) {
                return 4; // Base size when truncated.
            }
            const auto field_count = static_cast<std::size_t>(code[offset + 3]);
            return 4 + (field_count * 2);
        }

        case Op::RecordWith: {
            // RecordWith <u8 override_count> [<u16 field_name> * override_count]
            if (offset + 1 >= code.size()) {
                return 2; // Base size when truncated.
            }
            const auto override_count = static_cast<std::size_t>(code[offset + 1]);
            return 2 + (override_count * 2);
        }

        default:
            return opcode_base_size(op);
    }
}

// Returns true if the opcode has at least one operand (i.e. instruction size > 1).
[[nodiscard]] constexpr bool has_operand(Op op) noexcept {
    return opcode_base_size(op) > 1;
}

// --- Semantic-group classification ---
//
// Each helper projects the op_flag bitmask stored in the opcode table
// (opcode.hpp).  Group membership is defined there, in one place, so these
// helpers stay in sync automatically -- there is no parallel switch.

// Returns true if the opcode is a forward-jump instruction with a u32 offset
// operand.  These encode a forward offset: instruction_end + offset = target.
[[nodiscard]] constexpr bool is_forward_jump(Op op) noexcept {
    return (opcode_flags(op) & op_flag::k_forward_jump) != 0;
}

// Returns true if the opcode is any jump instruction (forward or backward Loop).
[[nodiscard]] constexpr bool is_jump(Op op) noexcept {
    return (opcode_flags(op) & op_flag::k_jump) != 0;
}

// Returns true if the opcode unconditionally terminates the current basic
// block.  Code immediately after a terminator (with no incoming jump) is
// unreachable.
[[nodiscard]] constexpr bool is_terminator(Op op) noexcept {
    return (opcode_flags(op) & op_flag::k_terminator) != 0;
}

// Returns true if the opcode is a comparison operator (produces a boolean).
[[nodiscard]] constexpr bool is_comparison(Op op) noexcept {
    return (opcode_flags(op) & op_flag::k_comparison) != 0;
}

// Returns true if the opcode is a binary or unary arithmetic operator.
[[nodiscard]] constexpr bool is_arithmetic(Op op) noexcept {
    return (opcode_flags(op) & op_flag::k_arithmetic) != 0;
}

// Returns true if the opcode affects the program counter or call stack.  This
// is a superset of is_jump() and is_terminator() -- it also covers function
// calls, exception handling, match dispatch, and pipe operators.
[[nodiscard]] constexpr bool is_control_flow(Op op) noexcept {
    return (opcode_flags(op) & op_flag::k_control_flow) != 0;
}

// Returns true if the opcode manipulates the stack without performing
// computation (constant/literal pushes, pop, dup, and swap).
[[nodiscard]] constexpr bool is_stack_op(Op op) noexcept {
    return (opcode_flags(op) & op_flag::k_stack_op) != 0;
}

// Returns true if the opcode loads from or stores to a named variable slot
// (local, upvalue, or global).
[[nodiscard]] constexpr bool is_load_store(Op op) noexcept {
    return (opcode_flags(op) & op_flag::k_load_store) != 0;
}

// Returns the fixed stack-effect delta for opcodes whose effect is known
// statically (independent of operand values).  Returns std::nullopt for
// opcodes whose effect depends on operands (Interpolate, MakeArray, Call,
// ...) or that have no net effect on depth tracking (Return, jumps, ...).
[[nodiscard]] constexpr std::optional<int> fixed_stack_effect(Op op) noexcept {
    return opcode_fixed_stack_effect(op);
}

} // namespace luma

#endif // LUMA_RUNTIME_COMPILER_OPCODE_METADATA_HPP
