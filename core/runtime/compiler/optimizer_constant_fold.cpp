#include "runtime/compiler/optimizer_internal.hpp"

namespace luma {

namespace {

// Evaluate a constant comparison between two like-typed operands. Shared by the
// integer and string branches of comparison_fold_pass to avoid duplicating the
// six-way comparison switch.
template <typename T>
[[nodiscard]] std::optional<bool> fold_comparison(Op cmp_op, const T& a, const T& b) {
    switch (cmp_op) {
        case Op::Equal:
            return a == b;
        case Op::NotEqual:
            return a != b;
        case Op::Less:
            return a < b;
        case Op::LessEqual:
            return a <= b;
        case Op::Greater:
            return a > b;
        case Op::GreaterEqual:
            return a >= b;
        default:
            return std::nullopt;
    }
}

} // namespace

// ─── Constant folding helpers ───

std::optional<Optimizer::FoldCandidate> Optimizer::find_foldable_pair(const Chunk& chunk,
                                                                      std::size_t from) {
    const auto& code = chunk.code;

    for (std::size_t i = from; i + InstructionLayout::k_constant_instruction_size < code.size();) {
        const auto op1 = static_cast<Op>(code[i]);

        if (op1 != Op::Constant) {
            i += instruction_size(code, i);
            continue;
        }

        const auto idx1 = optimizer_util::read_u16(code, i + 1);
        const auto op2_pos = i + InstructionLayout::k_constant_instruction_size;

        if (!optimizer_util::in_bounds(code, op2_pos,
                                       InstructionLayout::k_constant_instruction_size)) {
            break;
        }

        const auto op2 = static_cast<Op>(code[op2_pos]);

        if (op2 != Op::Constant) {
            i += InstructionLayout::k_constant_instruction_size;
            continue;
        }

        const auto idx2 = optimizer_util::read_u16(code, op2_pos + 1);
        const auto arith_pos = op2_pos + InstructionLayout::k_constant_instruction_size;

        if (arith_pos >= code.size()) {
            break;
        }

        return FoldCandidate{.i = i,
                             .op2_pos = op2_pos,
                             .arith_pos = arith_pos,
                             .idx1 = idx1,
                             .idx2 = idx2,
                             .arith_op = static_cast<Op>(code[arith_pos])};
    }

    return std::nullopt;
}

std::optional<Value> Optimizer::try_fold_operation(const Value& val1, const Value& val2, Op op) {
    if (val1.is_integer() && val2.is_integer()) {
        const auto a = val1.as_integer();
        const auto b = val2.as_integer();

        switch (op) {
            case Op::Add:
                if (!would_overflow_add(a, b)) {
                    return Value{a + b};
                }
                break;
            case Op::Subtract:
                if (!would_overflow_sub(a, b)) {
                    return Value{a - b};
                }
                break;
            case Op::Multiply:
                if (!would_overflow_mul(a, b)) {
                    return Value{a * b};
                }
                break;
            case Op::IntDivide:
                if (b != 0 && !would_overflow_div(a, b)) {
                    return Value{a / b};
                }
                break;
            case Op::Modulo:
                if (b != 0 && !would_overflow_div(a, b)) {
                    return Value{a % b};
                }
                break;
            default:
                break;
        }

        return std::nullopt;
    }

    // Fold when at least one operand is a number (double), or both are numeric.
    // Skip if the result is non-finite to avoid introducing NaN/Infinity at compile time.
    const bool v1_numeric = val1.is_integer() || val1.is_number();
    const bool v2_numeric = val2.is_integer() || val2.is_number();

    if (v1_numeric && v2_numeric) {
        const double a =
            val1.is_integer() ? static_cast<double>(val1.as_integer()) : val1.as_number();
        const double b =
            val2.is_integer() ? static_cast<double>(val2.as_integer()) : val2.as_number();
        std::optional<double> result;

        switch (op) {
            case Op::Add:
                result = a + b;
                break;
            case Op::Subtract:
                result = a - b;
                break;
            case Op::Multiply:
                result = a * b;
                break;
            case Op::Divide:
                if (b != 0.0) {
                    result = a / b;
                }
                break;
            default:
                break;
        }

        if (result.has_value() && std::isfinite(*result)) {
            return Value{*result};
        }

        return std::nullopt;
    }

    if (val1.is_string() && val2.is_string() && op == Op::Concatenate) {
        const auto& sa = val1.as_string();
        const auto& sb = val2.as_string();

        if (sa.size() + sb.size() <= OptimizerLimits::k_max_folded_string_length) {
            return Value{sa + sb};
        }
    }

    return std::nullopt;
}

bool Optimizer::apply_fold_optimization(Chunk& chunk, const FoldCandidate& candidate) {
    if (candidate.idx1 >= chunk.constants.size() || candidate.idx2 >= chunk.constants.size()) {
        return false;
    }

    const auto& val1 = chunk.constants[candidate.idx1];
    const auto& val2 = chunk.constants[candidate.idx2];
    const auto result = try_fold_operation(val1, val2, candidate.arith_op);

    if (!result.has_value()) {
        return false;
    }

    // The constant pool is capped at CompilerLimits::k_max_constants (its index
    // is serialised as a u16 in the .lumc format). add_constant() throws
    // std::overflow_error once full; skip the fold rather than let that escape
    // from the optimizer, which must never make compilation worse.
    if (chunk.constants.size() >= CompilerLimits::k_max_constants) {
        return false;
    }

    // Nop out the second Constant instruction and the arithmetic opcode.
    auto& code = chunk.code;
    const auto new_idx = chunk.add_constant(*result);
    code[candidate.i] = static_cast<std::uint8_t>(Op::Constant);
    optimizer_util::write_u16(code, candidate.i + 1, new_idx);
    nop_out(code, candidate.op2_pos,
            InstructionLayout::k_constant_instruction_size + InstructionLayout::k_opcode_size);
    return true;
}

// ─── Constant folding pass ───

std::size_t Optimizer::constant_fold_pass(Chunk& chunk) const {
    if (chunk.code.size() <
        InstructionLayout::k_constant_instruction_size + InstructionLayout::k_opcode_size) {
        return 0;
    }

    // A jump landing on the second Constant or the operator makes that byte a
    // basic-block entry: folding it away (and letting compaction redirect the
    // jump past it) would corrupt the branch-merge path — e.g. the merge point of
    // an if/match expression or the exit jump of a && / || / ?? short-circuit.
    // Collect the targets once; folding never moves code or rewrites jumps, so the
    // set stays valid for the whole pass (compaction runs only afterwards).
    const auto jump_targets = collect_jump_targets(chunk.code);

    std::size_t eliminated = 0;
    std::size_t from = 0;

    while (true) {
        const auto candidate = find_foldable_pair(chunk, from);
        if (!candidate.has_value()) {
            break;
        }

        const bool crosses_block_boundary = jump_targets.contains(candidate->op2_pos) ||
                                            jump_targets.contains(candidate->arith_pos);

        if (!crosses_block_boundary && apply_fold_optimization(chunk, *candidate)) {
            eliminated +=
                InstructionLayout::k_constant_instruction_size + InstructionLayout::k_opcode_size;
            from = candidate->i; // restart at same position — the new Constant may chain-fold
        } else {
            from = candidate->i +
                   InstructionLayout::
                       k_constant_instruction_size; // advance past the unfolded Constant
        }
    }

    return eliminated;
}

// ─── Unary constant folding pass ───

std::size_t Optimizer::unary_fold_pass(Chunk& chunk) const {
    auto& code = chunk.code;
    std::size_t eliminated = 0;

    if (code.size() <
        InstructionLayout::k_constant_instruction_size + InstructionLayout::k_opcode_size) {
        return 0;
    }

    // See constant_fold_pass: never fold a unary operator that a jump can land on
    // directly, or the branch jumping to it would skip the operator entirely.
    const auto jump_targets = collect_jump_targets(code);

    for (std::size_t i = 0; i + InstructionLayout::k_constant_instruction_size < code.size();) {
        const auto op = static_cast<Op>(code[i]);

        if (op != Op::Constant) {
            i += instruction_size(code, i);
            continue;
        }

        const auto idx = optimizer_util::read_u16(code, i + 1);
        const auto unary_offset = i + InstructionLayout::k_constant_instruction_size;

        if (unary_offset >= code.size()) {
            break;
        }

        const auto unary_op = static_cast<Op>(code[unary_offset]);

        if (idx >= chunk.constants.size() || jump_targets.contains(unary_offset)) {
            i += InstructionLayout::k_constant_instruction_size;
            continue;
        }

        const auto& val = chunk.constants[idx];

        // The constant pool is capped at CompilerLimits::k_max_constants (its
        // index is serialised as a u16). add_constant() throws
        // std::overflow_error once full; skip folding rather than let that
        // escape from the optimizer, which must never make compilation worse.
        if (chunk.constants.size() >= CompilerLimits::k_max_constants) {
            i += InstructionLayout::k_constant_instruction_size;
            continue;
        }

        // Constant(n) + Negate → Constant(-n)
        if (unary_op == Op::Negate && val.is_integer()) {
            const auto n = val.as_integer();

            if (n != std::numeric_limits<std::int64_t>::min()) {
                const auto new_idx = chunk.add_constant(Value{-n});
                optimizer_util::write_u16(code, i + 1, new_idx);
                nop_out(code, unary_offset, 1);
                eliminated += 1;
                continue;
            }
        }

        if (unary_op == Op::Negate && val.is_number()) {
            const auto new_idx = chunk.add_constant(Value{-val.as_number()});
            optimizer_util::write_u16(code, i + 1, new_idx);
            nop_out(code, unary_offset, 1);
            eliminated += 1;
            continue;
        }

        // Constant(b) + Not → True/False
        if (unary_op == Op::Not && val.is_bool()) {
            const auto b = val.as_bool();
            code[i] = static_cast<std::uint8_t>(b ? Op::False : Op::True);
            // Remove constant operand bytes + Not.
            nop_out(code, i + 1,
                    InstructionLayout::k_u16_operand_size + InstructionLayout::k_opcode_size);
            eliminated += InstructionLayout::k_u16_operand_size + InstructionLayout::k_opcode_size;
            continue;
        }

        i += InstructionLayout::k_constant_instruction_size;
    }

    return eliminated;
}

// ─── Comparison constant folding pass ───

std::size_t Optimizer::comparison_fold_pass(Chunk& chunk) const {
    auto& code = chunk.code;
    std::size_t eliminated = 0;

    // Minimum size: Constant(3) + Constant(3) + comparison(1) = 7.
    constexpr auto k_cmp_fold_min_size =
        (InstructionLayout::k_constant_instruction_size * 2) + InstructionLayout::k_opcode_size;
    // Bytes eliminated when folding: u16 operand(2) + Constant(3) + cmp(1) = 6.
    constexpr auto k_cmp_fold_eliminated = InstructionLayout::k_u16_operand_size +
                                           InstructionLayout::k_constant_instruction_size +
                                           InstructionLayout::k_opcode_size;

    if (code.size() < k_cmp_fold_min_size) {
        return 0;
    }

    // See constant_fold_pass: never fold when a jump targets the second Constant
    // or the comparison operator, or the branch-merge path would skip it.
    const auto jump_targets = collect_jump_targets(code);

    for (std::size_t i = 0; i + k_cmp_fold_eliminated < code.size();) {
        const auto op1 = static_cast<Op>(code[i]);

        if (op1 != Op::Constant) {
            i += instruction_size(code, i);
            continue;
        }

        const auto idx1 = optimizer_util::read_u16(code, i + 1);
        const auto op2_offset = i + InstructionLayout::k_constant_instruction_size;

        if (!optimizer_util::in_bounds(code, op2_offset,
                                       InstructionLayout::k_constant_instruction_size) ||
            static_cast<Op>(code[op2_offset]) != Op::Constant) {
            i += InstructionLayout::k_constant_instruction_size;
            continue;
        }

        const auto idx2 = optimizer_util::read_u16(code, op2_offset + 1);
        const auto cmp_offset = op2_offset + InstructionLayout::k_constant_instruction_size;

        if (!optimizer_util::in_bounds(code, cmp_offset, 1)) {
            break;
        }

        const auto cmp_op = static_cast<Op>(code[cmp_offset]);

        if (!is_comparison(cmp_op)) {
            i += InstructionLayout::k_constant_instruction_size;
            continue;
        }

        if (jump_targets.contains(op2_offset) || jump_targets.contains(cmp_offset)) {
            i += InstructionLayout::k_constant_instruction_size;
            continue;
        }

        if (idx1 >= chunk.constants.size() || idx2 >= chunk.constants.size()) {
            i += InstructionLayout::k_constant_instruction_size;
            continue;
        }

        const auto& val1 = chunk.constants[idx1];
        const auto& val2 = chunk.constants[idx2];

        std::optional<bool> result;

        if (val1.is_integer() && val2.is_integer()) {
            result = fold_comparison(cmp_op, val1.as_integer(), val2.as_integer());
        } else if (val1.is_string() && val2.is_string()) {
            // Uses C++ std::string::operator< (byte-wise lexicographic), which
            // must match the VM's runtime Less/Greater string comparison
            // (compare_values in the VM) or a compile-time fold would disagree
            // with the -O0 result. If that runtime ordering ever changes
            // (e.g. to Unicode-aware or locale-dependent collation), this fold
            // must be updated in lockstep.
            result = fold_comparison(cmp_op, val1.as_string(), val2.as_string());
        }

        if (result.has_value()) {
            code[i] = static_cast<std::uint8_t>(*result ? Op::True : Op::False);
            nop_out(code, i + 1, k_cmp_fold_eliminated);
            eliminated += k_cmp_fold_eliminated;
            continue;
        }

        i += InstructionLayout::k_constant_instruction_size;
    }

    return eliminated;
}

} // namespace luma
