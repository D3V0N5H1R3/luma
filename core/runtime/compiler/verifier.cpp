#include "runtime/compiler/verifier.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "common/byte_utils.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/opcode_metadata.hpp"

namespace luma {

// ─── Deferred jump record ───
// Collected during the main iteration and validated after all instruction
// boundaries are known.

struct JumpRecord {
    std::size_t offset; // Offset of the jump/loop instruction.
    std::size_t target; // Resolved target offset.
    bool is_loop;       // True for Op::Loop, false for forward jumps.
};

// ─── Verifier error factory functions ───

namespace {

[[nodiscard]] VerifyError constant_long_out_of_bounds(std::size_t offset, std::uint32_t idx,
                                                      std::size_t pool_size) {
    return {.offset = offset,
            .message = std::format("constant long index {} at offset {} exceeds pool size {}", idx,
                                   offset, pool_size)};
}

[[nodiscard]] VerifyError constant_out_of_bounds(std::size_t offset, std::uint16_t idx,
                                                 std::size_t pool_size) {
    return {.offset = offset,
            .message = std::format("constant index {} at offset {} exceeds pool size {}", idx,
                                   offset, pool_size)};
}

[[nodiscard]] VerifyError name_out_of_bounds(std::size_t offset, Op op, std::uint16_t idx,
                                             std::size_t names_size) {
    return {.offset = offset,
            .message = std::format("name index {} at offset {} for {} exceeds name table size {}",
                                   idx, offset, opcode_name(op), names_size)};
}

[[nodiscard]] VerifyError upvalue_out_of_bounds(std::size_t offset, Op op, std::uint16_t idx,
                                                int upvalue_count) {
    return {.offset = offset,
            .message = std::format("upvalue index {} at offset {} for {} exceeds upvalue count {}",
                                   idx, offset, opcode_name(op), upvalue_count)};
}

[[nodiscard]] VerifyError local_out_of_bounds(std::size_t offset, Op op, std::uint16_t idx) {
    return {.offset = offset,
            .message =
                std::format("local slot {} at offset {} for {} exceeds maximum slot index {}", idx,
                            offset, opcode_name(op), CompilerLimits::k_max_locals - 1)};
}

[[nodiscard]] VerifyError jump_target_beyond_code(std::size_t offset, std::size_t target,
                                                  std::size_t code_size) {
    return {.offset = offset,
            .message = std::format("jump at offset {} targets offset {} "
                                   "which is beyond code size {}",
                                   offset, target, code_size)};
}

[[nodiscard]] VerifyError loop_underflow(std::size_t offset, std::size_t loop_offset) {
    return {.offset = offset,
            .message = std::format("loop at offset {} jumps back {} bytes "
                                   "which underflows to before code start",
                                   offset, loop_offset)};
}

[[nodiscard]] VerifyError stack_underflow_error(std::size_t offset, Op op) {
    return {.offset = offset,
            .message =
                std::format("stack underflow at offset {} after {}", offset, opcode_name(op))};
}

[[nodiscard]] VerifyError stack_overflow_error(std::size_t offset, int max_depth) {
    return {.offset = offset,
            .message = std::format("stack depth {} exceeds maximum allowed ({})", max_depth,
                                   CompilerLimits::k_max_stack_depth)};
}

[[nodiscard]] VerifyError empty_bytecode_error() {
    return {.offset = 0, .message = "empty bytecode chunk"};
}

[[nodiscard]] VerifyError invalid_opcode_error(std::size_t offset, std::uint8_t byte) {
    return {.offset = offset, .message = std::format("invalid opcode byte: 0x{:02X}", byte)};
}

[[nodiscard]] VerifyError instruction_truncated_error(std::size_t offset, Op op, std::size_t size,
                                                      std::size_t remaining) {
    return {.offset = offset,
            .message = std::format("instruction {} at offset {} truncated: "
                                   "needs {} bytes but only {} remain",
                                   opcode_name(op), offset, size, remaining)};
}

[[nodiscard]] VerifyError jump_not_on_boundary(std::size_t offset, std::size_t target,
                                               bool is_loop) {
    const char* kind = is_loop ? "loop" : "jump";
    return {.offset = offset,
            .message = std::format("{} at offset {} targets offset {} "
                                   "which is not an instruction boundary",
                                   kind, offset, target)};
}

} // namespace

// ─── Per-category operand bounds checks ───

static void check_constant_long_bounds(const Chunk& chunk, std::size_t offset,
                                       const std::vector<std::uint8_t>& code,
                                       std::vector<VerifyError>& errors) {
    if (!in_bounds(code, offset, InstructionLayout::k_u32_operand_size + 1)) {
        return;
    }

    const auto idx = read_u32_be(&code[offset + 1]);

    if (idx >= chunk.constants.size()) {
        errors.push_back(constant_long_out_of_bounds(offset, idx, chunk.constants.size()));
    }
}

static void check_constant_bounds(const Chunk& chunk, Op /*op*/, std::size_t offset,
                                  std::uint16_t idx, std::vector<VerifyError>& errors) {
    const auto pool_size = chunk.constants.size();
    if (idx >= pool_size) {
        errors.push_back(constant_out_of_bounds(offset, idx, pool_size));
    }
}

static void check_name_bounds(const Chunk& chunk, Op op, std::size_t offset, std::uint16_t idx,
                              std::vector<VerifyError>& errors) {
    const auto names_size = chunk.names.size();
    if (idx >= names_size) {
        errors.push_back(name_out_of_bounds(offset, op, idx, names_size));
    }
}

static void check_upvalue_bounds(Op op, std::size_t offset, std::uint16_t idx, int upvalue_count,
                                 std::vector<VerifyError>& errors) {
    if (idx >= static_cast<std::uint16_t>(upvalue_count)) {
        errors.push_back(upvalue_out_of_bounds(offset, op, idx, upvalue_count));
    }
}

static void check_local_bounds(Op op, std::size_t offset, std::uint16_t idx,
                               std::vector<VerifyError>& errors) {
    // Valid slot indices are 0..k_max_locals-1 (k_max_locals is the maximum
    // local *count*), so k_max_locals itself is the first out-of-range index.
    // The comparison must be >=, not >: idx is a std::uint16_t whose maximum
    // value equals k_max_locals (65535), so `idx > k_max_locals` can never be
    // true and the check would never fire.
    if (idx >= CompilerLimits::k_max_locals) {
        errors.push_back(local_out_of_bounds(offset, op, idx));
    }
}

// ─── Unified operand-bounds check ───
//
// A single parameterised function validates u16 operand bounds for all four
// categories (constant, name, upvalue, local).  The OperandCategory metadata
// from opcode_metadata.hpp drives the dispatch.

static void check_operand_bounds(const Chunk& chunk, Op op, std::size_t offset, int upvalue_count,
                                 const std::vector<std::uint8_t>& code,
                                 std::vector<VerifyError>& errors) {
    const auto category = operand_category(op);

    if (category == OperandCategory::None) {
        return;
    }

    // ConstantLong uses a u32 operand — handle it separately.
    if (op == Op::ConstantLong) {
        check_constant_long_bounds(chunk, offset, code, errors);
        return;
    }

    // All other categorised opcodes use a u16 operand at offset+1..offset+2.
    if (!in_bounds(code, offset, 3)) {
        return;
    }

    const auto idx = read_u16_be(&code[offset + 1]);

    switch (category) {
        case OperandCategory::Constant:
            check_constant_bounds(chunk, op, offset, idx, errors);
            break;

        case OperandCategory::Name:
            check_name_bounds(chunk, op, offset, idx, errors);
            break;

        case OperandCategory::Upvalue:
            check_upvalue_bounds(op, offset, idx, upvalue_count, errors);
            break;

        case OperandCategory::Local:
            check_local_bounds(op, offset, idx, errors);
            break;

        case OperandCategory::None:
            break;
    }
}

static void collect_jumps(Op op, std::size_t offset, const std::vector<std::uint8_t>& code,
                          std::vector<JumpRecord>& jumps, std::vector<VerifyError>& errors) {
    if (is_forward_jump(op)) {
        if (!in_bounds(code, offset, InstructionLayout::k_jump_instruction_size)) {
            return;
        }

        const auto jump_offset = static_cast<std::size_t>(read_u32_be(&code[offset + 1]));
        const auto target = Chunk::resolve_forward_jump_target(offset, jump_offset);

        if (target > code.size()) {
            errors.push_back(jump_target_beyond_code(offset, target, code.size()));
        } else {
            jumps.push_back({.offset = offset, .target = target, .is_loop = false});
        }
    }

    if (op == Op::Loop) {
        if (!in_bounds(code, offset, InstructionLayout::k_jump_instruction_size)) {
            return;
        }

        const auto loop_offset = static_cast<std::size_t>(read_u32_be(&code[offset + 1]));

        if (loop_offset > offset + InstructionLayout::k_jump_instruction_size) {
            errors.push_back(loop_underflow(offset, loop_offset));
        } else {
            jumps.push_back({.offset = offset,
                             .target = Chunk::resolve_loop_jump_target(offset, loop_offset),
                             .is_loop = true});
        }
    }
}

static bool track_stack_depth(Op op, std::size_t offset, const std::vector<std::uint8_t>& code,
                              int& depth, int& max_depth, std::vector<VerifyError>& errors) {
    // Try the fixed-effect lookup first.
    if (const auto effect = fixed_stack_effect(op)) {
        depth += *effect;
    } else {
        // Operand-dependent stack effects.
        switch (op) {
            case Op::Interpolate:
                if (offset + 1 < code.size()) {
                    depth += 1 - static_cast<int>(code[offset + 1]);
                }
                break;

            case Op::MakeArray:
            case Op::MakeTuple:
                if (in_bounds(code, offset, 3)) {
                    const int count = static_cast<int>(read_u16_be(&code[offset + 1]));
                    depth += 1 - count;
                }
                break;

            case Op::MakeDict:
                if (in_bounds(code, offset, 3)) {
                    const int count = static_cast<int>(read_u16_be(&code[offset + 1]));
                    depth += 1 - (count * 2);
                }
                break;

            case Op::Call:
            case Op::TailCall:
            case Op::Spawn:
                if (offset + 1 < code.size()) {
                    depth -= static_cast<int>(code[offset + 1]);
                }
                break;

            case Op::Print:
            case Op::Assert:
                if (offset + 1 < code.size()) {
                    depth += 1 - static_cast<int>(code[offset + 1]);
                }
                break;

            case Op::CallNamed:
                if (offset + 2 < code.size()) {
                    const int positional = static_cast<int>(code[offset + 1]);
                    const int named = static_cast<int>(code[offset + 2]);
                    depth -= positional + (named * 2);
                }
                break;

            default:
                break;
        }
    }

    if (depth < 0) {
        errors.push_back(stack_underflow_error(offset, op));
        return false;
    }

    max_depth = std::max(depth, max_depth);

    if (max_depth > CompilerLimits::k_max_stack_depth) {
        errors.push_back(stack_overflow_error(offset, max_depth));
        return false;
    }

    return true;
}

// ─── Single-pass verification ───

std::vector<BytecodeVerifier::VerifyError> BytecodeVerifier::verify(const CompiledFunction& func) {
    std::vector<VerifyError> errors;
    const auto& chunk = func.chunk();
    const auto& code = chunk.code;

    if (code.empty()) {
        errors.push_back(empty_bytecode_error());
        return errors;
    }

    // State accumulated during the single iteration.
    std::vector<std::size_t> boundaries;
    std::vector<JumpRecord> jumps;
    int stack_depth = func.arity;
    int max_stack_depth = stack_depth;
    bool track_stack = true;
    std::size_t offset = 0;

    while (offset < code.size()) {
        boundaries.push_back(offset);

        // ── Opcode validity (InstructionPass) ──
        const auto byte = code[offset];

        if (byte > static_cast<std::uint8_t>(Op::EndModule)) {
            errors.push_back(invalid_opcode_error(offset, byte));
            break; // Cannot determine instruction boundaries past an invalid opcode.
        }

        const auto op = static_cast<Op>(byte);
        const std::size_t size = instruction_size(code, offset);

        if (offset + size > code.size()) {
            errors.push_back(instruction_truncated_error(offset, op, size, code.size() - offset));
            break;
        }

        // ── Operand bounds (constant pool, name table, upvalues, locals) ──
        check_operand_bounds(chunk, op, offset, func.upvalue_count, code, errors);

        // ── Collect jumps for deferred boundary validation ──
        collect_jumps(op, offset, code, jumps, errors);

        // ── Stack depth tracking ──
        if (track_stack) {
            track_stack = track_stack_depth(op, offset, code, stack_depth, max_stack_depth, errors);
        }

        offset += size;
    }

    // ── Deferred jump target validation ──
    // Now that all instruction boundaries are known, verify that every
    // jump/loop targets an instruction boundary.
    for (const auto& jump : jumps) {
        if (jump.target < code.size() && !std::ranges::binary_search(boundaries, jump.target)) {
            errors.push_back(jump_not_on_boundary(jump.offset, jump.target, jump.is_loop));
        }
    }

    return errors;
}

} // namespace luma
