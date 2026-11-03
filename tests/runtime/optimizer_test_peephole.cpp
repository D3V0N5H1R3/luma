// Bytecode optimizer unit tests: optimisation level 0 and peephole patterns.

#include <array>
#include <cstddef>
#include <cstdint>

#include "analysis/source/source_location.hpp"
#include "compiler_test_helpers.hpp"
#include "optimizer_test_helpers.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/optimizer.hpp"
#include "runtime/interpreter/value.hpp"
#include "test_framework.hpp"

using luma::Chunk;
using luma::CompilerLimits;
using luma::Op;
using luma::Optimizer;
using luma::SourceLocation;
using luma::Value;
using luma::test::count_opcode;
using luma::test::has_opcode;
using luma::test::make_chunk;
using luma::test::opcode_offsets;
using luma::test::read_u16_operand;

// ─── Level 0: no optimisation ───

static void test_level0_noop() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::Not),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{0};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_EQ(eliminated, 0U);
    ASSERT_EQ(chunk.code.size(), 3U);
}

// ─── Peephole: True + Not → False ───

static void test_peephole_true_not_to_false() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::Not),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_TRUE(has_opcode(chunk, Op::False));
    ASSERT_FALSE(has_opcode(chunk, Op::Not));
}

// ─── Peephole: False + Not → True ───

static void test_peephole_false_not_to_true() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::False),
        static_cast<std::uint8_t>(Op::Not),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_TRUE(has_opcode(chunk, Op::True));
    ASSERT_FALSE(has_opcode(chunk, Op::Not));
}

// ─── Peephole: Not + Not is preserved (`!!x` truthiness coercion) ───
//
// `!!x` is NOT the identity: `!` coerces its operand to a boolean via truthiness,
// so `!!v` is a boolean even when `v` is not.  A permissive `any` operand (e.g. an
// untyped dictionary element) can hold a string/number at runtime, and cancelling
// the pair would leave that original non-boolean value in a slot the unoptimised
// program fills with a boolean.  The pair must survive.
static void test_peephole_double_not_preserved() {
    // Use GetLocal to avoid True+Not being matched first.
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::GetLocal),
        0x00,
        0x00,
        static_cast<std::uint8_t>(Op::Not),
        static_cast<std::uint8_t>(Op::Not),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_EQ(count_opcode(chunk, Op::Not), 2U);
}

// ─── Peephole: Negate + Negate is preserved (INT64_MIN overflow) ───

static void test_peephole_double_negate_preserved() {
    // checked_negate raises "integer overflow in negation" for INT64_MIN, so
    // `-(-x)` must still raise when x is INT64_MIN.  Cancelling the pair would
    // skip that RuntimeError, so both Negates must survive.
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Negate),
        static_cast<std::uint8_t>(Op::Negate),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_EQ(eliminated, 0U);
    ASSERT_EQ(count_opcode(chunk, Op::Negate), 2U);
}

// ─── Peephole: Dup + Pop → eliminated ───

static void test_peephole_dup_pop_eliminated() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::Dup),
        static_cast<std::uint8_t>(Op::Pop),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Dup));
}

// ─── Peephole: Zero + Add is preserved (signed-zero correctness) ───
//
// `x + 0` must NOT be folded away: for a `number` operand the VM performs an
// IEEE-754 add, and `-0.0 + 0.0` normalises to `+0.0`.  Cancelling the
// `Zero; Add` pair would leave `-0.0` on that path, silently flipping the sign
// of a zero result at -O1.  (`Zero; Subtract` is likewise preserved — see
// test_peephole_zero_subtract_preserved — because a permissive `any` operand can
// hold a non-numeric runtime value that `-` rejects.)
static void test_peephole_zero_add_preserved() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Zero),
        static_cast<std::uint8_t>(Op::Add),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_TRUE(has_opcode(chunk, Op::Zero));
    ASSERT_TRUE(has_opcode(chunk, Op::Add));
}

// ─── Peephole: Zero + Subtract is preserved (`any` operand type contract) ───
//
// `x - 0` is the identity for a numeric operand, but a permissive `any` operand
// (e.g. an untyped dictionary element) can hold a string/boolean at runtime that
// `-` rejects with a RuntimeError.  Cancelling the pair would skip that error and
// leave the non-numeric value in a slot the unoptimised program never fills.
static void test_peephole_zero_subtract_preserved() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Zero),
        static_cast<std::uint8_t>(Op::Subtract),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_TRUE(has_opcode(chunk, Op::Zero));
    ASSERT_TRUE(has_opcode(chunk, Op::Subtract));
}

// ─── Peephole: One + Add is preserved (not fused into Increment) ───
//
// Increment reports overflow as "integer overflow in increment", so fusing
// `x + 1` would make its runtime error name the wrong operation.

static void test_peephole_one_add_is_preserved() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True), // placeholder stack value
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Add),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_FALSE(has_opcode(chunk, Op::Increment));
    ASSERT_TRUE(has_opcode(chunk, Op::One));
    ASSERT_TRUE(has_opcode(chunk, Op::Add));
}

// ─── Peephole: One + Subtract is preserved (not fused into Decrement) ───

static void test_peephole_one_subtract_is_preserved() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True), // placeholder stack value
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Subtract),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_FALSE(has_opcode(chunk, Op::Decrement));
    ASSERT_TRUE(has_opcode(chunk, Op::One));
    ASSERT_TRUE(has_opcode(chunk, Op::Subtract));
}

// ─── Peephole: One + Multiply is preserved (`any` operand type contract) ───
//
// `x * 1` is the identity for a numeric operand, but a permissive `any` operand
// can hold a boolean at runtime that `*` rejects with a RuntimeError (and other
// non-numeric types change behaviour).  Cancelling the pair would skip that error,
// so the pair must survive.
static void test_peephole_one_multiply_preserved() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True), // placeholder stack value
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Multiply),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_TRUE(has_opcode(chunk, Op::Multiply));
}

// ─── Peephole: One + Divide is preserved (`any` operand type contract) ───
//
// `x / 1` is the identity for a numeric operand, but a permissive `any` operand
// (e.g. an untyped dictionary element) can hold a string/boolean at runtime that
// `/` rejects with a RuntimeError.  Cancelling the pair would skip that error and
// leave the non-numeric value in a slot the unoptimised program never fills.
static void test_peephole_one_divide_preserved() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True), // placeholder stack value
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Divide),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_TRUE(has_opcode(chunk, Op::Divide));
}

// ─── Peephole: One + IntDivide is NOT folded (x // 1 is unsound for number) ───

// `//` requires integer operands and handle_int_divide raises a RuntimeError on a
// non-integer, but bytecode carries no static type and a slot can hold a `number`
// (e.g. through the permissive `any` type).  The `One; IntDivide` cancel-pair was
// removed (same class as `x % 1`), so IntDivide must survive the optimizer intact.
static void test_peephole_one_int_divide_preserved() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True), // placeholder stack value
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::IntDivide),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_TRUE(has_opcode(chunk, Op::IntDivide));
    ASSERT_TRUE(has_opcode(chunk, Op::One));
}

// ─── Peephole: One + Modulo is NOT folded (x % 1 is unsound for number) ───

// The old `One; Modulo -> Pop; Zero` fold assumed integer semantics (x % 1 == 0),
// but bytecode carries no static type, and for `number` operands `5.5 % 1 == 0.5`.
// The peephole was removed, so Modulo must survive the optimizer intact.
static void test_peephole_one_modulo_preserved() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True), // placeholder stack value
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Modulo),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_TRUE(has_opcode(chunk, Op::Modulo));
    ASSERT_TRUE(has_opcode(chunk, Op::One));
    ASSERT_FALSE(has_opcode(chunk, Op::Zero));
}

// ─── Peephole: IntToNumber + IntToNumber → single IntToNumber ───

static void test_peephole_double_int_to_number() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::IntToNumber),
        static_cast<std::uint8_t>(Op::IntToNumber),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_EQ(count_opcode(chunk, Op::IntToNumber), 1U);
}

// ─── Peephole: Zero + Multiply → Pop + Zero (x * 0 = 0) ───

static void test_peephole_zero_multiply_preserved() {
    // Zero + Multiply must NOT be optimized away because Multiply also
    // handles string repetition: "hello" * 0 should produce "" (empty string),
    // not the integer 0.  The optimizer conservatively preserves this pattern.
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True), // placeholder stack value
        static_cast<std::uint8_t>(Op::Zero),
        static_cast<std::uint8_t>(Op::Multiply),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    // Both Zero and Multiply must remain — no folding.
    ASSERT_TRUE(has_opcode(chunk, Op::Zero));
    ASSERT_TRUE(has_opcode(chunk, Op::Multiply));
}

// ─── Peephole: Constant(int) + IntToNumber skips when the pool is full ───
//
// Promoting the integer constant needs a new number entry; once the pool is
// full add_constant() would throw std::overflow_error out of the optimizer.
static void test_peephole_int_to_number_skips_when_pool_full() {
    Chunk chunk;
    const auto loc = SourceLocation{};

    for (std::int64_t i = 0; i < static_cast<std::int64_t>(CompilerLimits::k_max_constants); ++i) {
        (void)chunk.add_constant(Value{i});
    }

    chunk.emit_u16(Op::Constant, 7, loc);
    chunk.emit(Op::IntToNumber, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{1};

    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_TRUE(has_opcode(chunk, Op::IntToNumber));
    ASSERT_EQ(chunk.constants.size(), static_cast<std::size_t>(CompilerLimits::k_max_constants));
}

// ─── Peephole: relational Cmp+Not must NOT fuse (NaN correctness) ───

static void test_peephole_relational_not_not_fused() {
    // For a NaN operand IEEE-754 makes `!(a < b)` (true) and `a >= b` (false)
    // disagree, so the peephole must never fuse a relational comparison with a
    // following Not.  Each pair must survive verbatim: both opcodes remain and
    // no inverse comparison is introduced.
    struct Case {
        Op cmp;
        Op inverse;
    };

    const std::array cases{
        Case{.cmp = Op::Less, .inverse = Op::GreaterEqual},
        Case{.cmp = Op::LessEqual, .inverse = Op::Greater},
        Case{.cmp = Op::Greater, .inverse = Op::LessEqual},
        Case{.cmp = Op::GreaterEqual, .inverse = Op::Less},
    };

    for (const auto& c : cases) {
        auto chunk = make_chunk({
            static_cast<std::uint8_t>(c.cmp),
            static_cast<std::uint8_t>(Op::Not),
            static_cast<std::uint8_t>(Op::EndModule),
        });

        const Optimizer opt{1};
        const auto eliminated = opt.optimize(chunk);

        ASSERT_EQ(eliminated, 0U);
        ASSERT_TRUE(has_opcode(chunk, c.cmp));
        ASSERT_TRUE(has_opcode(chunk, Op::Not));
        ASSERT_FALSE(has_opcode(chunk, c.inverse));
    }
}

// ─── Peephole: equality Cmp+Not still fuses (NaN-safe inversion) ───

static void test_peephole_equality_not_still_fuses() {
    // `!(a == b)` and `a != b` agree for every operand, NaN included, so the
    // equality inversion remains enabled.
    auto eq_chunk = make_chunk({
        static_cast<std::uint8_t>(Op::Equal),
        static_cast<std::uint8_t>(Op::Not),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt_eq{1};
    ASSERT_GT(opt_eq.optimize(eq_chunk), 0U);
    ASSERT_TRUE(has_opcode(eq_chunk, Op::NotEqual));
    ASSERT_FALSE(has_opcode(eq_chunk, Op::Not));

    auto ne_chunk = make_chunk({
        static_cast<std::uint8_t>(Op::NotEqual),
        static_cast<std::uint8_t>(Op::Not),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt_ne{1};
    ASSERT_GT(opt_ne.optimize(ne_chunk), 0U);
    ASSERT_TRUE(has_opcode(ne_chunk, Op::Equal));
    ASSERT_FALSE(has_opcode(ne_chunk, Op::Not));
}

// ─── Peephole: comparison+Not preserved across a jump target ───

static void test_peephole_cmp_not_preserved_across_jump_target() {
    // Mimics the codegen of `!(a == b && c == d)` where the short-circuit path
    // jumps directly onto the Not that negates the whole expression:
    //   Equal                   # left operand result
    //   JumpIfFalse -> Not      # short-circuit exit lands on the Not
    //   Pop
    //   Equal                   # right operand result
    //   Not                     # negates the whole && expression
    // The peephole must NOT fuse the second `Equal; Not` into NotEqual, because
    // the short-circuit path jumps directly to the Not (regression test for the
    // dropped-negation bug).  Equality is used because it is the inversion that
    // remains enabled after relational inversions were removed for NaN safety.
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit(Op::Equal, loc);              // offset 0: left comparison
    chunk.emit_u32(Op::JumpIfFalse, 2, loc); // offset 1-5: target = 6 + 2 = 8 (the Not)
    chunk.emit(Op::Pop, loc);                // offset 6
    chunk.emit(Op::Equal, loc);              // offset 7: right comparison
    chunk.emit(Op::Not, loc);                // offset 8: jump target — must survive
    chunk.emit(Op::EndModule, loc);          // offset 9

    const Optimizer opt{1};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_TRUE(has_opcode(chunk, Op::Not));
    ASSERT_FALSE(has_opcode(chunk, Op::NotEqual));
}

// ─── Chained peephole: multiple patterns in sequence ───

static void test_chained_peephole_patterns() {
    // True + Not, then Dup + Pop — both should be optimized in one pass.
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::Not),
        static_cast<std::uint8_t>(Op::Dup),
        static_cast<std::uint8_t>(Op::Pop),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_TRUE(has_opcode(chunk, Op::False));
    ASSERT_FALSE(has_opcode(chunk, Op::Not));
    ASSERT_FALSE(has_opcode(chunk, Op::Dup));
}

// ─── Peephole: stacked cancel pairs all eliminated in one scan ───

static void test_peephole_stacked_cancel_pairs_all_eliminated() {
    // Two adjacent None+Pop cancel pairs in a single chunk must BOTH be
    // eliminated by one peephole scan — after cancelling the first pair the scan
    // continues forward and cancels the second rather than stopping.  None+Pop is
    // one of the three sound cancel pairs (pure stack manipulation with no operand
    // type contract); the arithmetic/logical "identities" are intentionally no
    // longer cancel pairs (see the k_cancel_pair_patterns rationale and the
    // *_preserved tests).
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::None),
        static_cast<std::uint8_t>(Op::Pop),
        static_cast<std::uint8_t>(Op::None),
        static_cast<std::uint8_t>(Op::Pop),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    const Optimizer opt{1};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_EQ(count_opcode(chunk, Op::None), 0U);
    ASSERT_EQ(count_opcode(chunk, Op::Pop), 0U);
}

// ─── SetLocal + GetLocal → SetLocal + Dup (peephole fusion) ───

static void test_set_local_get_local_fusion() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);             // Push a value.
    chunk.emit_u16(Op::SetLocal, 0, loc); // SetLocal slot 0.
    chunk.emit_u16(Op::GetLocal, 0, loc); // GetLocal slot 0.
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{1};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_TRUE(has_opcode(chunk, Op::Dup));
}

// ─── Peephole Dup+SetLocalPop preserves the slot operand ───

static void test_peephole_dup_set_local_pop_preserves_slot() {
    // `Dup; SetLocalPop <slot>` fuses to `SetLocal <slot>`.  Dup is one byte, so
    // the rewritten slot operand lands at i+1..i+2; nopping from i+1 (rather than
    // the single dead byte at i+3) would clobber the slot just written, producing
    // `SetLocal 0xFFFF`.  Use a distinctive slot so a clobber is unmistakable.
    constexpr std::uint16_t k_slot = 7;
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit(Op::Dup, loc);                     // 0
    chunk.emit_u16(Op::SetLocalPop, k_slot, loc); // 1-3
    chunk.emit(Op::EndModule, loc);               // 4

    const Optimizer opt{1};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_TRUE(has_opcode(chunk, Op::SetLocal));
    ASSERT_FALSE(has_opcode(chunk, Op::Dup));
    ASSERT_FALSE(has_opcode(chunk, Op::SetLocalPop));

    // The slot operand must be intact (big-endian u16 immediately after SetLocal).
    const auto set_local_offsets = opcode_offsets(chunk, Op::SetLocal);
    ASSERT_EQ(set_local_offsets.size(), 1U);
    ASSERT_EQ(read_u16_operand(chunk, set_local_offsets[0] + 1), k_slot);
}

// ─── Strength reduction: multiply by power of two must NOT become a shift ───

static void test_multiply_power_of_two_not_strength_reduced() {
    // x * 2^n must keep the Multiply opcode.  Multiply is overloaded (string
    // repeat, number*int, int overflow raising), so a blind rewrite to
    // ShiftLeft (which demands two integers and wraps on overflow) would
    // miscompile "ab" * 4, 3.5 * 2, and large-integer products.  The optimizer
    // has no operand type or range information at the bytecode level, so the
    // transform was removed entirely.
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_four = chunk.add_constant(Value{static_cast<std::int64_t>(4)});

    chunk.emit(Op::True, loc); // Non-constant placeholder left operand.
    chunk.emit_u16(Op::Constant, idx_four, loc);
    chunk.emit(Op::Multiply, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    // Multiply preserved; no shift introduced.
    ASSERT_TRUE(has_opcode(chunk, Op::Multiply));
}

// ─── Empty chunk ───

static void test_optimize_empty_chunk() {
    Chunk chunk;

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_EQ(eliminated, 0U);
}

// ─── End-to-end: peephole on compiled chunk ───

static void test_end_to_end_peephole_on_compiled() {
    // Build a chunk with True + Not and apply the optimizer.
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit(Op::True, loc);
    chunk.emit(Op::Not, loc);
    chunk.emit(Op::Return, loc);

    const Optimizer opt{1};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_TRUE(has_opcode(chunk, Op::False));
    ASSERT_FALSE(has_opcode(chunk, Op::Not));
}

// ─── main ───

int main() {
    // Level 0.
    RUN(test_level0_noop);

    // Peephole.
    RUN(test_peephole_true_not_to_false);
    RUN(test_peephole_false_not_to_true);
    RUN(test_peephole_double_not_preserved);
    RUN(test_peephole_double_negate_preserved);
    RUN(test_peephole_dup_pop_eliminated);
    RUN(test_peephole_zero_add_preserved);
    RUN(test_peephole_zero_subtract_preserved);
    RUN(test_peephole_one_add_is_preserved);
    RUN(test_peephole_one_subtract_is_preserved);
    RUN(test_peephole_one_multiply_preserved);
    RUN(test_peephole_one_divide_preserved);
    RUN(test_peephole_one_int_divide_preserved);
    RUN(test_peephole_one_modulo_preserved);
    RUN(test_peephole_double_int_to_number);
    RUN(test_peephole_zero_multiply_preserved);
    RUN(test_peephole_int_to_number_skips_when_pool_full);

    // Comparison + Not fusion.
    RUN(test_peephole_relational_not_not_fused);
    RUN(test_peephole_equality_not_still_fuses);
    RUN(test_peephole_cmp_not_preserved_across_jump_target);

    // Chained and fused patterns.
    RUN(test_chained_peephole_patterns);
    RUN(test_peephole_stacked_cancel_pairs_all_eliminated);
    RUN(test_set_local_get_local_fusion);
    RUN(test_multiply_power_of_two_not_strength_reduced);
    RUN(test_peephole_dup_set_local_pop_preserves_slot);

    // Edge cases.
    RUN(test_optimize_empty_chunk);
    RUN(test_end_to_end_peephole_on_compiled);

    return SUMMARY();
}
