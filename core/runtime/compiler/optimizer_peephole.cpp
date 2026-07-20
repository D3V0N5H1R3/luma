#include "runtime/compiler/optimizer_internal.hpp"

namespace luma {

// ─── Peephole pattern tables ───
// Data-driven pattern definitions for common two-instruction peephole
// optimizations.  Each entry describes a (first_op, second_op) pair and
// the resulting transformation.

namespace {

// Pattern: replace first_op with replacement, NOP out second_op.
// Used for comparison inversion (Equal+Not→NotEqual) and boolean folding.
struct ReplaceFirstPattern {
    Op first;
    Op second;
    Op replacement;
};

constexpr ReplaceFirstPattern k_replace_first_patterns[] = {
    // Boolean folding: True+Not → False, False+Not → True.
    {.first = Op::True, .second = Op::Not, .replacement = Op::False},
    {.first = Op::False, .second = Op::Not, .replacement = Op::True},

    // Equality inversion: Equal+Not → NotEqual, NotEqual+Not → Equal.
    //
    // Only equality is inverted.  The relational forms (Less/LessEqual/Greater/
    // GreaterEqual) are deliberately NOT inverted: under IEEE-754 a NaN operand
    // makes `!(a < b)` and `a >= b` disagree (the negation is true, the inverse
    // comparison false), so fusing `Cmp; Not` into the inverse comparison would
    // silently change results once -O1 runs.  Equality is safe because `!(a==b)`
    // and `a!=b` agree for every operand, NaN included.
    {.first = Op::Equal, .second = Op::Not, .replacement = Op::NotEqual},
    {.first = Op::NotEqual, .second = Op::Not, .replacement = Op::Equal},
};

// Pattern: NOP out both instructions (cancel each other).
struct CancelPairPattern {
    Op first;
    Op second;
};

constexpr CancelPairPattern k_cancel_pair_patterns[] = {
    {.first = Op::Dup, .second = Op::Pop},
    {.first = Op::Swap, .second = Op::Swap},
    {.first = Op::None, .second = Op::Pop},
    // Only the pure stack manipulations above qualify as cancel pairs.  A cancel
    // pair must reproduce its input for EVERY runtime value that can reach it, so
    // every arithmetic/logical "identity" is intentionally absent: its opcode
    // inspects, coerces, or rejects operand TYPES at runtime, and cancelling the
    // pair would skip a RuntimeError or change a value the unoptimised program
    // produces.  Two runtime facts make this reachable even when the source looks
    // integer- or number-typed: integer overflow promotes an `integer` slot to a
    // `number`, and the permissive `any` type lets a slot hold a string/boolean/etc.
    // the operator does not accept.  The absent pairs and why each is unsound:
    //   Negate;Negate `-(-x)`     — safe_negate promotes INT64_MIN to a number, so
    //                               cancelling keeps the raw INT64_MIN integer and
    //                               changes the result's type.
    //   BitwiseNot;BitwiseNot `~~x`,
    //   One;IntDivide `x // 1`    — `~` and `//` require integer operands; an
    //                               overflow-promoted number raises at runtime, but
    //                               the cancel would return it unchanged.
    //   Not;Not `!!x`             — `!` coerces via truthiness, so `!!v` is a
    //                               boolean; for a non-boolean `any` operand that
    //                               differs from the original value.
    //   Zero;Add `x + 0`,
    //   Zero;Subtract `x - 0`,
    //   One;Multiply `x * 1`,
    //   One;Divide `x / 1`        — `+`/`-`/`*`/`/` raise on the non-numeric (for
    //                               `*`, non-numeric/non-string) runtime types an
    //                               `any` operand may hold; `x + 0` additionally
    //                               normalises -0.0 to +0.0.
};

// Pattern: NOP out first_op, replace second_op with replacement.
struct ReplaceSecondPattern {
    Op first;
    Op second;
    Op replacement;
};

constexpr ReplaceSecondPattern k_replace_second_patterns[] = {
    {.first = Op::One, .second = Op::Add, .replacement = Op::Increment},
    {.first = Op::One, .second = Op::Subtract, .replacement = Op::Decrement},
};

} // anonymous namespace

// ─── Peephole pass ───

std::size_t Optimizer::peephole_pass(Chunk& chunk) const {
    auto& code = chunk.code;
    std::size_t eliminated = 0;

    if (code.size() < 2) {
        return 0;
    }

    // Offsets that a jump can land on.  A two-instruction peephole must not
    // fuse/remove the second instruction when control flow can jump directly to
    // it (e.g. the short-circuit exit jump of && / || / ?? lands on the
    // instruction after the right operand), otherwise the fused form is wrong.
    const auto jump_targets = collect_jump_targets(code);

    for (std::size_t i = 0; i + 1 < code.size();) {
        const auto op = static_cast<Op>(code[i]);
        const auto size = instruction_size(code, i);
        const auto next_offset = i + size;

        if (next_offset >= code.size()) {
            break;
        }

        const auto next_op = static_cast<Op>(code[next_offset]);

        // Never fuse across a basic-block boundary: if a jump targets the second
        // instruction, it may execute without the first having run.
        if (jump_targets.contains(next_offset)) {
            i += size;
            continue;
        }

        // ── Table-driven: replace first opcode, NOP second ──
        bool matched = false;
        for (const auto& pat : k_replace_first_patterns) {
            if (op == pat.first && next_op == pat.second) {
                code[i] = static_cast<std::uint8_t>(pat.replacement);
                nop_out(code, next_offset, 1);
                eliminated += 1;
                i += 1;
                matched = true;
                break;
            }
        }
        if (matched) {
            continue;
        }

        // ── Table-driven: cancel both opcodes ──
        for (const auto& pat : k_cancel_pair_patterns) {
            if (op == pat.first && next_op == pat.second) {
                nop_out(code, i, 2);
                eliminated += 2;
                i += 2;
                matched = true;
                break;
            }
        }
        if (matched) {
            continue;
        }

        // ── Table-driven: NOP first, replace second ──
        for (const auto& pat : k_replace_second_patterns) {
            if (op == pat.first && next_op == pat.second) {
                nop_out(code, i, 1);
                code[next_offset] = static_cast<std::uint8_t>(pat.replacement);
                eliminated += 1;
                i = next_offset + 1;
                matched = true;
                break;
            }
        }
        if (matched) {
            continue;
        }

        // ── Structural patterns (operand-dependent) ──

        // Pattern: IntToNumber + IntToNumber → single IntToNumber (idempotent).
        if (op == Op::IntToNumber && next_op == Op::IntToNumber) {
            nop_out(code, next_offset, 1);
            eliminated += 1;
            i = next_offset + 1;
            continue;
        }

        // Pattern: Constant(integer) + IntToNumber → Constant(float).
        if (op == Op::Constant && next_op == Op::IntToNumber && i + 2 < code.size()) {
            const auto idx = optimizer_util::read_u16(code, i + 1);
            if (idx < chunk.constants.size() && chunk.constants[idx].is_integer()) {
                const auto promoted = static_cast<double>(chunk.constants[idx].as_integer());
                const auto new_idx = chunk.add_constant(Value{promoted});
                optimizer_util::write_u16(code, i + 1, new_idx);
                nop_out(code, next_offset, 1);
                eliminated += 1;
                i += 3;
                continue;
            }
        }

        // Pattern: SetLocal <slot> + GetLocal <slot> → SetLocal + Dup.
        if (op == Op::SetLocal && next_op == Op::GetLocal && i + 5 < code.size()) {
            const auto set_slot = optimizer_util::read_u16(code, i + 1);
            const auto get_slot = optimizer_util::read_u16(code, next_offset + 1);

            if (set_slot == get_slot) {
                code[i + 3] = static_cast<std::uint8_t>(Op::Dup);
                nop_out(code, i + 4, 2);
                eliminated += 2;
                i += 4;
                continue;
            }
        }

        // Pattern: GetLocal <slot> + Return → GetLocalReturn <slot>.
        if (op == Op::GetLocal && next_op == Op::Return) {
            code[i] = static_cast<std::uint8_t>(Op::GetLocalReturn);
            nop_out(code, next_offset, 1);
            eliminated += 1;
            i += 3;
            continue;
        }

        // Pattern: Dup + SetLocalPop <s> → SetLocal <s>.
        if (op == Op::Dup && next_op == Op::SetLocalPop && i + 3 < code.size()) {
            code[i] = static_cast<std::uint8_t>(Op::SetLocal);
            code[i + 1] = code[next_offset + 1];
            code[i + 2] = code[next_offset + 2];
            // Dup is one byte, so next_offset == i + 1: the slot operand now lives
            // at i+1..i+2.  Only the single trailing byte at i+3 is dead — nop_out
            // over next_offset would clobber the slot we just wrote.
            nop_out(code, i + 3, 1);
            eliminated += 1;
            i += 3;
            continue;
        }

        i += size;
    }

    return eliminated;
}

} // namespace luma
