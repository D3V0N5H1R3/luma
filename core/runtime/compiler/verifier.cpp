#include "runtime/compiler/verifier.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
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

StackEffect instruction_stack_effect(const std::vector<std::uint8_t>& code, std::size_t offset) {
    const auto u8_operand = [&](std::size_t at) { return static_cast<int>(code[offset + at]); };
    const auto u16_operand = [&](std::size_t at) {
        return static_cast<int>(read_u16_be(&code[offset + at]));
    };

    switch (static_cast<Op>(code[offset])) {
        case Op::Constant:
        case Op::ConstantLong:
        case Op::GetLocal:
        case Op::GetUpvalue:
        case Op::GetGlobal:
        case Op::None:
        case Op::True:
        case Op::False:
        case Op::Zero:
        case Op::One:
        case Op::MakeClosure:
        case Op::TaskScopeEnd: // Pushes the array of task results.
            return {.pops = 0, .pushes = 1};

        case Op::Pop:
        case Op::SetLocalPop:
        case Op::Return:
        case Op::Rethrow:
            return {.pops = 1, .pushes = 0};

        case Op::Dup:
            return {.pops = 1, .pushes = 2};

        case Op::Dup2:
            return {.pops = 2, .pushes = 4};

        case Op::Swap:
            return {.pops = 2, .pushes = 2};

        // Replace the top value (or only inspect it).
        case Op::SetLocal:
        case Op::SetUpvalue:
        case Op::SetGlobal:
        case Op::Negate:
        case Op::Increment:
        case Op::Decrement:
        case Op::Not:
        case Op::MakeSuccess:
        case Op::MakeFailure:
        case Op::MakeSome:
        case Op::Unwrap:
        case Op::ResultInner:
        case Op::IsSuccess:
        case Op::IsSome:
        case Op::EnsureSuccess:
        case Op::Downcast:
        case Op::TrustedDowncast:
        case Op::IsType:
        case Op::JumpIfFalse:
        case Op::JumpIfTrue:
        case Op::NullCoalesce:
        case Op::GetField:
        case Op::GetFieldOpt:
        case Op::GetFieldUnique:
        case Op::Await:
        case Op::ForIterInit:
        case Op::TypeOf:
        case Op::IntToNumber:
        case Op::WidenNumber:
        case Op::Clone:
            return {.pops = 1, .pushes = 1};

        // Combine two values into one.
        case Op::Add:
        case Op::Subtract:
        case Op::Multiply:
        case Op::Divide:
        case Op::IntDivide:
        case Op::Modulo:
        case Op::Equal:
        case Op::NotEqual:
        case Op::Less:
        case Op::LessEqual:
        case Op::Greater:
        case Op::GreaterEqual:
        case Op::And:
        case Op::Or:
        case Op::Concatenate:
        case Op::MakeRange:
        case Op::MakeRangeInc:
        case Op::IndexGet:
        case Op::IndexGetOpt:
        case Op::IndexGetUnique:
        case Op::SetField: // Pops the record and value, pushes the updated record.
        case Op::MakeChoice:
        case Op::MakeChoiceConstructor:
        case Op::Pipe:
        case Op::ErrorPipe:
        case Op::Contains:
            return {.pops = 2, .pushes = 1};

        case Op::IndexSet:
            return {.pops = 3, .pushes = 1};

        case Op::Interpolate:
        case Op::Print:
        case Op::Assert:
            return {.pops = u8_operand(1), .pushes = 1};

        case Op::MakeArray:
        case Op::MakeTuple:
            return {.pops = u16_operand(1), .pushes = 1};

        case Op::MakeDict:
            return {.pops = 2 * u16_operand(1), .pushes = 1};

        case Op::MakeRecord: // u16 type name, u8 field count, field names.
            return {.pops = u8_operand(3), .pushes = 1};

        case Op::RecordWith: // u8 override count, override names; also pops the base record.
            return {.pops = u8_operand(1) + 1, .pushes = 1};

        // The callee (below its arguments) is replaced by the result / task.
        case Op::Call:
        case Op::TailCall:
        case Op::Spawn:
            return {.pops = u8_operand(1) + 1, .pushes = 1};

        case Op::CallNamed: // Each named argument is a (name, value) pair.
            return {.pops = u8_operand(1) + (2 * u8_operand(2)) + 1, .pushes = 1};

        // Pops the iterator state; pushes (element, true) or (value, key, true).
        case Op::ForIterStep:
            return {.pops = 1, .pushes = 2};

        case Op::ForIterStepKV:
            return {.pops = 1, .pushes = 3};

        case Op::Jump:
        case Op::Loop:
        case Op::TryCatch:
        case Op::TryEnd:
        case Op::MatchStart:
        case Op::MatchArm:
        case Op::MatchEnd:
        case Op::TaskScopeBegin:
        case Op::IncrementLocal:
        case Op::DecrementLocal:
        case Op::GetLocalReturn:
        case Op::UniqueLocal:
        case Op::UniqueUpvalue:
        case Op::UniqueGlobal:
        case Op::EndModule:
            return {.pops = 0, .pushes = 0};
    }

    return {.pops = 0, .pushes = 0};
}

namespace {

// Stack-depth state reaching an instruction along some control-flow edge.
struct PendingInstruction {
    std::size_t offset;
    int depth;
};

// Step budget of the stack-depth walk, per instruction (see check_stack_depths).
constexpr std::size_t k_max_visits_per_instruction = 64;

[[nodiscard]] VerifyError loop_stack_growth_error(std::size_t offset, int growth) {
    return {.offset = offset,
            .message = std::format("stack grows by {} on every iteration of the loop at offset {}",
                                   growth, offset)};
}

[[nodiscard]] VerifyError stack_analysis_budget_error(std::size_t offset) {
    return {.offset = offset,
            .message =
                std::format("stack depth analysis exceeded its step budget at offset {}", offset)};
}

// Values a ForIterStep / ForIterStepKV does not push when the iterator is
// exhausted: it then pushes only `false`, which the JumpIfFalse right after it
// takes to the loop exit.
[[nodiscard]] int values_missing_when_exhausted(Op op) {
    if (op == Op::ForIterStep) {
        return 1;
    }

    if (op == Op::ForIterStepKV) {
        return 2;
    }

    return 0;
}

// Walk the control flow graph from the entry point, simulating each reachable
// instruction with the deepest stack that reaches it.  Simulating the deepest
// stack avoids false underflow reports at merge points while still catching
// any instruction that pops more than every path into it provides.
//
// The walk revisits an instruction only when a deeper stack reaches it.  A
// loop whose body leaves extra values behind would otherwise be re-walked once
// per extra stack slot, so stack growth across a back edge is reported
// directly, and a step budget bounds the work on crafted bytecode (this runs
// on every deserialized .lumc file).
void check_stack_depths(const CompiledFunction& func, const std::vector<std::size_t>& boundaries,
                        std::vector<VerifyError>& errors) {
    const auto& code = func.chunk().code;
    std::vector<int> deepest(code.size(), -1);
    // Every frame starts with the callee in slot 0 followed by its arguments.
    std::vector<PendingInstruction> pending{
        {.offset = 0, .depth = func.arity + CompilerLimits::k_first_parameter_slot}};

    // Compiled code reaches each instruction with at most a few distinct
    // depths; this budget leaves ample headroom for that.
    const std::size_t step_budget = (boundaries.size() * k_max_visits_per_instruction) + 1;
    std::size_t steps = 0;

    const auto is_boundary = [&](std::size_t offset) {
        return std::ranges::binary_search(boundaries, offset);
    };

    const auto previous_op = [&](std::size_t offset) -> std::optional<Op> {
        const auto it = std::ranges::lower_bound(boundaries, offset);

        if (it == boundaries.begin()) {
            return std::nullopt;
        }

        return static_cast<Op>(code[*std::prev(it)]);
    };

    while (!pending.empty()) {
        const auto [offset, depth] = pending.back();
        pending.pop_back();

        if (offset >= code.size() || !is_boundary(offset) || depth <= deepest[offset]) {
            continue;
        }

        // Invalid or truncated instructions are reported by the linear pass.
        if (code[offset] > static_cast<std::uint8_t>(Op::EndModule) ||
            offset + instruction_size(code, offset) > code.size()) {
            continue;
        }

        if (++steps > step_budget) {
            errors.push_back(stack_analysis_budget_error(offset));
            return;
        }

        deepest[offset] = depth;

        const auto op = static_cast<Op>(code[offset]);
        const auto effect = instruction_stack_effect(code, offset);

        if (depth < effect.pops) {
            errors.push_back(stack_underflow_error(offset, op));
            return;
        }

        const int after = depth - effect.pops + effect.pushes;

        if (after > CompilerLimits::k_max_stack_depth) {
            errors.push_back(stack_overflow_error(offset, after));
            return;
        }

        const auto next = offset + instruction_size(code, offset);

        switch (op) {
            case Op::Return:
            case Op::GetLocalReturn:
            case Op::Rethrow:
            case Op::EndModule:
                break;

            case Op::Jump:
                pending.push_back({.offset = Chunk::resolve_forward_jump_target(
                                       offset, read_u32_be(&code[offset + 1])),
                                   .depth = after});
                break;

            case Op::Loop: {
                const auto back = static_cast<std::size_t>(read_u32_be(&code[offset + 1]));

                if (back <= next) {
                    const auto target = Chunk::resolve_loop_jump_target(offset, back);

                    // A loop header already simulated with a shallower stack
                    // means each iteration leaves values behind.
                    if (deepest[target] >= 0 && after > deepest[target]) {
                        errors.push_back(loop_stack_growth_error(offset, after - deepest[target]));
                        return;
                    }

                    pending.push_back({.offset = target, .depth = after});
                }

                break;
            }

            case Op::JumpIfFalse:
            case Op::JumpIfTrue:
            case Op::NullCoalesce: {
                const auto previous = previous_op(offset);
                const int missing = op == Op::JumpIfFalse && previous
                                        ? values_missing_when_exhausted(*previous)
                                        : 0;

                if (after < missing) {
                    errors.push_back(stack_underflow_error(offset, op));
                    return;
                }

                pending.push_back({.offset = next, .depth = after});
                pending.push_back({.offset = Chunk::resolve_forward_jump_target(
                                       offset, read_u32_be(&code[offset + 1])),
                                   .depth = after - missing});
                break;
            }

            case Op::TryCatch:
                // The handler restores this depth and pushes the error value.
                pending.push_back({.offset = next, .depth = after});
                pending.push_back({.offset = Chunk::resolve_forward_jump_target(
                                       offset, read_u32_be(&code[offset + 1])),
                                   .depth = after + 1});
                break;

            default:
                pending.push_back({.offset = next, .depth = after});
                break;
        }
    }
}

} // namespace

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

    // ── Stack depth along every control-flow path ──
    check_stack_depths(func, boundaries, errors);

    return errors;
}

} // namespace luma
