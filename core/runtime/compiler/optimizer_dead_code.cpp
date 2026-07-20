#include "runtime/compiler/optimizer_internal.hpp"

namespace luma {

// ─── Dead code elimination pass ───

std::size_t Optimizer::dead_code_pass(Chunk& chunk) const {
    auto& code = chunk.code;
    std::size_t eliminated = 0;

    // First pass: collect all jump target offsets so we don't eliminate
    // reachable code that is the target of a branch.
    std::unordered_set<std::size_t> jump_targets;
    for (std::size_t i = 0; i < code.size();) {
        const auto op = static_cast<Op>(code[i]);

        if (is_forward_jump(op)) {
            if (i + InstructionLayout::k_u32_operand_size < code.size()) {
                const auto offset = static_cast<std::size_t>(optimizer_util::read_u32(code, i + 1));
                jump_targets.insert(i + InstructionLayout::k_jump_instruction_size + offset);
            }
        } else if (op == Op::Loop) {
            if (i + InstructionLayout::k_u32_operand_size < code.size()) {
                const auto offset = static_cast<std::size_t>(optimizer_util::read_u32(code, i + 1));
                if (offset <= i + InstructionLayout::k_jump_instruction_size) {
                    jump_targets.insert(i + InstructionLayout::k_jump_instruction_size - offset);
                }
            }
        }

        i += instruction_size(code, i);
    }

    // Second pass: eliminate dead code after terminators, stopping at
    // jump targets and EndModule.
    for (std::size_t i = 0; i < code.size();) {
        const auto op = static_cast<Op>(code[i]);
        const auto size = instruction_size(code, i);
        const auto next = i + size;

        if (next >= code.size()) {
            break;
        }

        // After a Return or unconditional Jump, subsequent non-jump-target
        // instructions are dead until the next jump target.
        if (is_terminator(op)) {
            const std::size_t dead_start = next;
            std::size_t dead_end = dead_start;

            while (dead_end < code.size()) {
                const auto dead_op = static_cast<Op>(code[dead_end]);

                // EndModule is a valid sentinel — don't eliminate.
                if (dead_op == Op::EndModule) {
                    break;
                }

                // Stop if this offset is a jump target — it's reachable.
                if (jump_targets.contains(dead_end)) {
                    break;
                }

                dead_end += instruction_size(code, dead_end);
            }

            if (dead_end > dead_start) {
                const auto dead_size = dead_end - dead_start;
                nop_out(code, dead_start, dead_size);
                eliminated += dead_size;
            }

            i = dead_end;
            continue;
        }

        i = next;
    }

    return eliminated;
}

// ─── Dead store elimination pass ───

std::size_t Optimizer::dead_store_pass(Chunk& chunk) const {
    auto& code = chunk.code;
    std::size_t eliminated = 0;

    for (std::size_t i = 0; i < code.size();) {
        if (is_dead_byte(code[i])) {
            ++i;
            continue;
        }

        const auto op = static_cast<Op>(code[i]);
        const auto size = instruction_size(code, i);
        const auto next = i + size;

        // Pattern 1: SetLocal <s> immediately followed by SetLocal <s> (same slot).
        // SetLocal peeks TOS (stack effect 0), so the first write is dead.
        if (op == Op::SetLocal && next + 2 < code.size() && !is_dead_byte(code[next])) {
            const auto next_op = static_cast<Op>(code[next]);

            if (next_op == Op::SetLocal) {
                const auto slot1 = optimizer_util::read_u16(code, i + 1);
                const auto slot2 = optimizer_util::read_u16(code, next + 1);

                if (slot1 == slot2) {
                    nop_out(code, i, size);
                    eliminated += size;
                    i = next;
                    continue;
                }
            }
        }

        // Pattern 2: SetLocalPop <s> immediately followed by SetLocalPop <s> (same slot).
        // The first pops+sets, but the set is dead. Replace with Pop to keep the pop effect.
        if (op == Op::SetLocalPop && next + 2 < code.size() && !is_dead_byte(code[next])) {
            const auto next_op = static_cast<Op>(code[next]);

            if (next_op == Op::SetLocalPop) {
                const auto slot1 = optimizer_util::read_u16(code, i + 1);
                const auto slot2 = optimizer_util::read_u16(code, next + 1);

                if (slot1 == slot2) {
                    // Replace first SetLocalPop with Pop (keep the pop, remove the set).
                    code[i] = static_cast<std::uint8_t>(Op::Pop);
                    nop_out(code, i + 1, size - 1);
                    eliminated += size - 1;
                    i = next;
                    continue;
                }
            }
        }

        i += size;
    }

    return eliminated;
}

} // namespace luma
