#include "runtime/compiler/optimizer_internal.hpp"

namespace luma {

// ─── Jump threading pass ───

std::size_t Optimizer::jump_threading_pass(Chunk& chunk) const {
    auto& code = chunk.code;
    std::size_t changed = 0;

    for (std::size_t i = 0; i < code.size();) {
        if (is_dead_byte(code[i])) {
            ++i;
            continue;
        }

        const auto op = static_cast<Op>(code[i]);

        if (op != Op::Jump && op != Op::JumpIfFalse && op != Op::JumpIfTrue) {
            i += instruction_size(code, i);
            continue;
        }

        if (i + InstructionLayout::k_u32_operand_size >= code.size()) {
            break;
        }

        // Read the current forward-jump offset (u32, big-endian).
        const auto offset = optimizer_util::read_u32(code, i + 1);
        auto target =
            i + InstructionLayout::k_jump_instruction_size + static_cast<std::size_t>(offset);

        // Follow chains of unconditional jumps, up to a depth limit.
        constexpr int max_chain = OptimizerLimits::k_max_jump_chain_depth;
        int hops = 0;

        while (hops < max_chain && target + InstructionLayout::k_u32_operand_size < code.size() &&
               static_cast<Op>(code[target]) == Op::Jump) {
            const auto next_offset = optimizer_util::read_u32(code, target + 1);
            const auto next_target = target + InstructionLayout::k_jump_instruction_size +
                                     static_cast<std::size_t>(next_offset);
            if (next_target >= code.size()) {
                break; // Out-of-bounds target — stop following the chain.
            }
            target = next_target;
            ++hops;
        }

        if (hops > 0) {
            // Rewrite the offset to point directly to the final target.
            const auto new_offset = static_cast<std::uint32_t>(
                target - (i + InstructionLayout::k_jump_instruction_size));
            optimizer_util::write_u32(code, i + 1, new_offset);
            ++changed;
        }

        i += InstructionLayout::k_jump_instruction_size;
    }

    return changed;
}

// ─── Tail call optimization pass ───

std::size_t Optimizer::tail_call_pass(Chunk& chunk) const {
    auto& code = chunk.code;
    // TailCall is the same size as Call (only the opcode byte changes), so this
    // pass never removes bytes and never needs compaction. It still returns the
    // rewrite count so the outer optimizer loop's convergence check stays
    // meaningful if a future change makes rewrites size-changing.
    std::size_t rewrites = 0;

    // Tail call optimisation is unsafe when the function captures or is
    // captured by closures, because TailCall tears down the current frame
    // before the callee runs. If the chunk uses upvalues (GetUpvalue/
    // SetUpvalue) or creates closures that capture locals (MakeClosure with
    // upvalue_count > 0), skip TCO entirely.
    for (std::size_t i = 0; i < code.size();) {
        if (is_dead_byte(code[i])) {
            ++i;
            continue;
        }
        const auto op = static_cast<Op>(code[i]);
        if (op == Op::GetUpvalue || op == Op::SetUpvalue) {
            return 0; // Uses upvalues — unsafe to tail-call.
        }
        if (op == Op::MakeClosure) {
            // MakeClosure <u16 func_index> <u8 upvalue_count>
            if (i + 3 < code.size() && code[i + 3] > 0) {
                return 0; // Creates a closure that captures locals — unsafe.
            }
        }
        i += instruction_size(code, i);
    }

    for (std::size_t i = 0; i < code.size();) {
        if (is_dead_byte(code[i])) {
            ++i;
            continue;
        }

        const auto op = static_cast<Op>(code[i]);
        const auto size = instruction_size(code, i);
        const auto next = i + size;

        // Pattern: Call <n> immediately followed by Return → TailCall <n>.
        // Keep the Return byte: for compiled callees TailCall reuses the frame
        // so Return is never reached, but for native callees TailCall falls
        // through to call_value and needs Return to end the frame properly.
        if (op == Op::Call && next < code.size() && !is_dead_byte(code[next])) {
            const auto next_op = static_cast<Op>(code[next]);

            if (next_op == Op::Return) {
                code[i] = static_cast<std::uint8_t>(Op::TailCall);
                // Do NOT nop out Return — it's needed for native callee fallback.
                ++rewrites;
                i = next + 1;
                continue;
            }
        }

        i += size;
    }

    return rewrites;
}

} // namespace luma
