#include "runtime/compiler/optimizer_internal.hpp"

namespace luma {

// ─── Pass registry ───

const std::array<Optimizer::OptimizationPass, 8>& Optimizer::passes() {
    static const std::array<OptimizationPass, 8> k_passes{{
        {.name = "peephole", .fn = &Optimizer::peephole_pass, .min_level = 1},
        {.name = "constant_fold", .fn = &Optimizer::constant_fold_pass, .min_level = 2},
        {.name = "unary_fold", .fn = &Optimizer::unary_fold_pass, .min_level = 2},
        {.name = "comparison_fold", .fn = &Optimizer::comparison_fold_pass, .min_level = 2},
        {.name = "dead_code", .fn = &Optimizer::dead_code_pass, .min_level = 2},
        {.name = "jump_threading", .fn = &Optimizer::jump_threading_pass, .min_level = 2},
        {.name = "dead_store", .fn = &Optimizer::dead_store_pass, .min_level = 2},
        {.name = "tail_call", .fn = &Optimizer::tail_call_pass, .min_level = 2},
    }};
    return k_passes;
}

// ─── Public ───

std::size_t Optimizer::optimize(Chunk& chunk) const {
    if (level_ == 0 || chunk.code.empty()) {
        return 0;
    }

    std::size_t total_eliminated = 0;

    // Run passes iteratively until no more optimizations apply.
    // A higher limit allows cascading optimizations to converge fully.
    for (int iteration = 0; iteration < OptimizerLimits::k_max_optimizer_iterations; ++iteration) {
        std::size_t eliminated = 0;

        for (const auto& pass : passes()) {
            if (level_ >= pass.min_level) {
                eliminated += (this->*pass.fn)(chunk);
            }
        }

        if (eliminated == 0) {
            break;
        }

        total_eliminated += eliminated;
    }

    if (total_eliminated > 0) {
        total_eliminated = compact(chunk);
    }

    return total_eliminated;
}

// ─── Cross-function inlining ───

std::size_t
Optimizer::inline_small_functions(Chunk& caller_chunk,
                                  const std::vector<CompiledFunction>& all_functions) const {
    if (level_ < 2 || caller_chunk.code.empty()) {
        return 0;
    }

    auto& code = caller_chunk.code;
    std::size_t eliminated = 0;

    for (std::size_t i = 0; i + 4 < code.size();) {
        const auto op = static_cast<Op>(code[i]);
        const auto inst_size = instruction_size(code, i);

        if (op != Op::MakeClosure) {
            i += inst_size;
            continue;
        }

        // MakeClosure <u16 func_index> <u8 upvalue_count>
        const auto func_idx = optimizer_util::read_u16(code, i + 1);
        const auto upvalue_count =
            read_u8_checked(code, i + InstructionLayout::k_make_closure_upvalue_offset);

        // Only inline functions with no upvalues.
        if (upvalue_count != 0 || func_idx >= all_functions.size()) {
            i += inst_size;
            continue;
        }

        const auto& callee = all_functions[func_idx];
        const auto& callee_code = callee.chunk().code;

        // Callee must be small, have no upvalues, and end with Return.
        if (callee_code.size() > OptimizerLimits::k_max_inline_body || callee_code.empty() ||
            !callee.upvalues.empty()) {
            i += inst_size;
            continue;
        }

        // Check that callee ends with Return.
        const auto last_op = static_cast<Op>(callee_code.back());
        if (last_op != Op::Return) {
            i += inst_size;
            continue;
        }

        // Skip past MakeClosure to find the Call instruction.
        const auto call_offset = i + inst_size;

        if (call_offset + 1 >= code.size()) {
            i += inst_size;
            continue;
        }

        const auto call_op = static_cast<Op>(code[call_offset]);

        // We only handle: MakeClosure immediately followed by Call(0).
        // This covers constant-returning lambdas and zero-arg factory calls.
        if (call_op != Op::Call || read_u8_checked(code, call_offset + 1) != 0) {
            i += inst_size;
            continue;
        }

        // Body = everything before the final Return.
        const auto body_size = callee_code.size() - 1; // exclude Return byte

        if (body_size == 0) {
            // Empty body — function returns null. Replace MakeClosure+Call with None.
            const auto total_old =
                inst_size + InstructionLayout::k_u8_instruction_size; // MakeClosure + Call
            code[i] = static_cast<std::uint8_t>(Op::None);
            nop_out(code, i + 1, total_old - 1);
            eliminated += total_old - 1;
            i += total_old;
            continue;
        }

        // The callee body is copied verbatim into the caller chunk, so any
        // operand that indexes a chunk-local table becomes meaningless in the
        // caller context.  Reject the whole body if it contains one.  Deriving
        // the check from the central operand-category metadata (rather than a
        // hand-maintained opcode list) keeps it in lock-step with the opcode
        // table: it uniformly covers the constant pool (Constant), the name
        // table (GetGlobal/GetField/IsType/…), local slots (GetLocal/
        // IncrementLocal/…) and upvalue indices.  The record-building ops embed
        // name-table indices in variable-length operands (category None), and
        // MakeClosure references the function table, so block those explicitly.
        bool safe_to_inline = true;
        for (std::size_t j = 0; j < body_size;) {
            const auto body_op = static_cast<Op>(callee_code[j]);
            const auto category = operand_category(body_op);
            if (category == OperandCategory::Constant || category == OperandCategory::Name ||
                category == OperandCategory::Local || category == OperandCategory::Upvalue ||
                body_op == Op::MakeRecord || body_op == Op::RecordWith ||
                body_op == Op::MakeClosure) {
                safe_to_inline = false;
                break;
            }
            j += instruction_size(callee_code, j);
        }

        if (!safe_to_inline) {
            i += inst_size;
            continue;
        }

        // Safe to inline: the callee body uses only stack-relative operations
        // whose meaning is independent of the enclosing chunk's tables.
        const auto total_old =
            inst_size + InstructionLayout::k_u8_instruction_size; // MakeClosure + Call

        if (body_size <= total_old) {
            // Body fits — copy callee body and nop-out the rest.
            std::copy(callee_code.begin(),
                      callee_code.begin() + static_cast<std::ptrdiff_t>(body_size),
                      code.begin() + static_cast<std::ptrdiff_t>(i));
            if (body_size < total_old) {
                nop_out(code, i + body_size, total_old - body_size);
            }
            eliminated += total_old - body_size;
            i += total_old;
        } else {
            // Body is larger than the replaced sequence — skip (no expansion support).
            i += inst_size;
        }
    }

    if (eliminated > 0) {
        eliminated = compact(caller_chunk);
    }

    return eliminated;
}

// ─── Helpers ───

std::size_t Optimizer::compact(Chunk& chunk) const {
    auto& code = chunk.code;
    auto& source_map = chunk.source_map;

    // Build a dead-byte mask by walking the instruction stream.
    std::vector<bool> dead(code.size(), false);

    for (std::size_t i = 0; i < code.size();) {
        if (is_dead_byte(code[i])) {
            dead[i] = true;
            ++i;
        } else {
            i += instruction_size(code, i);
        }
    }

    // Build a mapping from old offsets to new offsets.
    std::vector<std::size_t> offset_map(code.size() + 1, 0);
    std::size_t new_offset = 0;

    for (std::size_t i = 0; i < code.size(); ++i) {
        offset_map[i] = new_offset;

        if (!dead[i]) {
            ++new_offset;
        }
    }

    offset_map[code.size()] = new_offset;

    const std::size_t eliminated = code.size() - new_offset;

    if (eliminated == 0) {
        return 0;
    }

    // Fix up jump offsets before compacting.
    for (std::size_t i = 0; i < code.size();) {
        if (dead[i]) {
            ++i;
            continue;
        }

        const auto op = static_cast<Op>(code[i]);

        if (is_forward_jump(op)) {
            const auto old_offset = static_cast<std::size_t>(read_u32_be(&code[i + 1]));
            const auto old_after = i + InstructionLayout::k_jump_instruction_size;
            const auto old_target = old_after + old_offset;

            if (old_target <= code.size() && old_after < offset_map.size() &&
                old_target < offset_map.size()) {
                const auto new_after = offset_map[old_after];
                const auto new_target = offset_map[old_target];
                const auto new_jump = static_cast<std::uint32_t>(new_target - new_after);
                patch_u32_be(&code[i + 1], new_jump);
            }
        } else if (op == Op::Loop) {
            const auto old_offset = static_cast<std::size_t>(read_u32_be(&code[i + 1]));
            const auto old_after = i + InstructionLayout::k_jump_instruction_size;
            const auto old_target = old_after - old_offset;

            if (old_after < offset_map.size() && old_target < offset_map.size()) {
                const auto new_after = offset_map[old_after];
                const auto new_target = offset_map[old_target];
                const auto new_loop = static_cast<std::uint32_t>(new_after - new_target);
                patch_u32_be(&code[i + 1], new_loop);
            }
        }

        i += instruction_size(code, i);
    }

    // Compact the code (remove dead bytes).
    std::vector<std::uint8_t> new_code;
    new_code.reserve(new_offset);

    for (std::size_t i = 0; i < code.size(); ++i) {
        if (!dead[i]) {
            new_code.push_back(code[i]);
        }
    }

    // Update source map.
    std::vector<std::pair<std::size_t, SourceLocation>> new_source_map;
    new_source_map.reserve(source_map.size());

    for (const auto& [offset, loc] : source_map) {
        if (offset < offset_map.size() && offset_map[offset] < new_offset) {
            if (offset < code.size() && !dead[offset]) {
                new_source_map.emplace_back(offset_map[offset], loc);
            }
        }
    }

    code = std::move(new_code);
    source_map = SourceMap{std::move(new_source_map)};

    return eliminated;
}

} // namespace luma
