// Bytecode optimizer unit tests: binary, unary, and comparison constant folding.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include "analysis/source/source_location.hpp"
#include "compiler_test_helpers.hpp"
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
using luma::test::has_opcode;

// ─── Helpers ───

// Returns true when the chunk's constant pool holds the integer `value`.
static bool has_integer_constant(const Chunk& chunk, std::int64_t value) {
    return std::ranges::any_of(chunk.constants, [value](const Value& constant) {
        return constant.is_integer() && constant.as_integer() == value;
    });
}

// Returns true when the chunk's constant pool holds the number `value`.
static bool has_number_constant(const Chunk& chunk, double value) {
    return std::ranges::any_of(chunk.constants, [value](const Value& constant) {
        return constant.is_number() && constant.as_number() == value;
    });
}

// Returns true when the chunk's constant pool holds the string `value`.
static bool has_string_constant(const Chunk& chunk, const std::string& value) {
    return std::ranges::any_of(chunk.constants, [&value](const Value& constant) {
        return constant.is_string() && constant.as_string() == value;
    });
}

// ─── Constant folding: integer addition ───

static void test_constant_fold_integer_add() {
    // Build: Constant(3) Constant(7) Add
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(3)});
    const auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(7)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Add, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    // Should fold to a single Constant(10).
    ASSERT_FALSE(has_opcode(chunk, Op::Add));
}

// ─── Constant folding: integer multiplication ───

static void test_constant_fold_integer_mul() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(6)});
    const auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(7)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Multiply, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Multiply));
    // Result (42) should be in the constant pool.
    ASSERT_TRUE(has_integer_constant(chunk, 42));
}

// ─── Constant folding: skips division by zero ───

static void test_constant_fold_skip_div_zero() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(10)});
    const auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(0)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::IntDivide, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    // Should NOT fold — division by zero must remain a runtime error.
    ASSERT_TRUE(has_opcode(chunk, Op::IntDivide));
}

// ─── Constant folding: floating-point ───

static void test_constant_fold_float_add() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_a = chunk.add_constant(Value{1.5});
    const auto idx_b = chunk.add_constant(Value{2.5});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Add, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Add));
    // Result (4.0) should be in the constant pool.
    ASSERT_TRUE(has_number_constant(chunk, 4.0));
}

// ─── Constant folding: subtraction ───

static void test_constant_fold_subtraction() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(100)});
    const auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(58)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Subtract, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Subtract));
    ASSERT_TRUE(has_integer_constant(chunk, 42));
}

// ─── Constant folding: modulo ───

static void test_constant_fold_modulo() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(17)});
    const auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(5)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Modulo, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Modulo));
    ASSERT_TRUE(has_integer_constant(chunk, 2));
}

// ─── Constant folding: skips INT64_MIN % -1 (signed-overflow UB) ───

static void test_constant_fold_skip_modulo_int_min_neg1() {
    // INT64_MIN % -1 is undefined behaviour in C++ (the matching division
    // overflows).  The optimizer must bail out and leave the Modulo for the VM
    // rather than evaluating it at compile time — pre-fix this crashed the
    // compiler with an integer-overflow trap.
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(INT64_MIN)});
    const auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(-1)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Modulo, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    // Must NOT fold — the operation stays a runtime opcode.
    ASSERT_TRUE(has_opcode(chunk, Op::Modulo));
}

// ─── Constant folding: overflowing integer arithmetic is left for the VM ───
//
// The VM raises "integer overflow in <operation>" rather than widening an
// integer to a number, so INT64_MAX + 1 (and friends) must not be folded into
// any constant — neither a wrapped integer nor a promoted number.
static void test_constant_fold_skips_integer_overflow() {
    for (const Op op : {Op::Add, Op::Subtract, Op::Multiply}) {
        Chunk chunk;
        const auto loc = SourceLocation{};
        const std::int64_t lhs = op == Op::Subtract ? INT64_MIN : INT64_MAX;
        const std::int64_t rhs = op == Op::Multiply ? 2 : 1;
        const auto idx_a = chunk.add_constant(Value{lhs});
        const auto idx_b = chunk.add_constant(Value{rhs});

        chunk.emit_u16(Op::Constant, idx_a, loc);
        chunk.emit_u16(Op::Constant, idx_b, loc);
        chunk.emit(op, loc);
        chunk.emit(Op::EndModule, loc);

        const Optimizer opt{2};
        ASSERT_EQ(opt.optimize(chunk), 0U);

        ASSERT_TRUE(has_opcode(chunk, op));
        ASSERT_EQ(chunk.constants.size(), 2U);
    }
}

// ─── Constant folding: string concatenation ───

static void test_constant_fold_string_concat() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_a = chunk.add_constant(Value{std::string{"hello "}});
    const auto idx_b = chunk.add_constant(Value{std::string{"world"}});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Concatenate, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Concatenate));
    ASSERT_TRUE(has_string_constant(chunk, "hello world"));
}

// ─── Constant folding: skips fold when the constant pool is full ───
//
// The pool is capped at CompilerLimits::k_max_constants (65535) because the
// .lumc format serialises the per-chunk constant count as a u16. Folding must
// skip (not throw std::overflow_error) when adding the folded result would
// overflow the pool — the optimizer must never make compilation worse.
static void test_constant_fold_skips_when_pool_full() {
    Chunk chunk;
    const auto loc = SourceLocation{};

    // Fill the pool to capacity with distinct values 0..65534 so add_constant
    // assigns index == value (no dedup hits) and the pool ends up completely
    // full.
    std::uint16_t idx_a{};
    std::uint16_t idx_b{};

    for (std::int64_t i = 0; i < static_cast<std::int64_t>(CompilerLimits::k_max_constants); ++i) {
        const auto idx = chunk.add_constant(Value{i});

        if (i == static_cast<std::int64_t>(CompilerLimits::k_max_constants) - 2) {
            idx_a = idx;
        }

        if (i == static_cast<std::int64_t>(CompilerLimits::k_max_constants) - 1) {
            idx_b = idx;
        }
    }

    ASSERT_EQ(chunk.constants.size(), static_cast<std::size_t>(CompilerLimits::k_max_constants));

    // Sum of the two largest constants is not already present in the pool, so
    // folding would require a brand-new entry.
    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Add, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};

    // Must not throw std::overflow_error — the fold should simply be skipped.
    ASSERT_EQ(opt.optimize(chunk), 0U);

    ASSERT_TRUE(has_opcode(chunk, Op::Add));
    ASSERT_EQ(chunk.constants.size(), static_cast<std::size_t>(CompilerLimits::k_max_constants));
}

// ─── Unary constant folding: Negate integer ───

static void test_unary_fold_negate_integer() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx = chunk.add_constant(Value{static_cast<std::int64_t>(42)});

    chunk.emit_u16(Op::Constant, idx, loc);
    chunk.emit(Op::Negate, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Negate));
    ASSERT_TRUE(has_integer_constant(chunk, -42));
}

// ─── Unary constant folding: Negate double ───

static void test_unary_fold_negate_double() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx = chunk.add_constant(Value{3.14});

    chunk.emit_u16(Op::Constant, idx, loc);
    chunk.emit(Op::Negate, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Negate));
    ASSERT_TRUE(has_number_constant(chunk, -3.14));
}

// ─── Comparison constant folding: integer less than (true) ───

static void test_comparison_fold_less_true() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(3)});
    const auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(7)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Less, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Less));
    ASSERT_TRUE(has_opcode(chunk, Op::True));
}

// ─── Comparison constant folding: integer equal (false) ───

static void test_comparison_fold_equal_false() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(5)});
    const auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(10)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Equal, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Equal));
    ASSERT_TRUE(has_opcode(chunk, Op::False));
}

// ─── Comparison constant folding: string comparison ───

static void test_comparison_fold_string_equal() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_a = chunk.add_constant(Value{std::string{"abc"}});
    const auto idx_b = chunk.add_constant(Value{std::string{"abc"}});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Equal, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Equal));
    ASSERT_TRUE(has_opcode(chunk, Op::True));
}

// ─── Constant fold must NOT cross a branch-merge point ───

static void test_constant_fold_preserved_across_jump_target() {
    // Mirrors the codegen of `(if c { 2 } else { 3 }) + 5`.  The else branch's
    // Constant sits immediately before the merge point's `Constant 5; Add`, so
    // the byte stream contains an accidental `Constant; Constant; Add` triple —
    // but the then branch jumps directly onto that second Constant.  Folding the
    // triple (and letting compaction redirect the jump past it) would drop the
    // `+ 5` on the then path, yielding 2 instead of 7.  The fold must be skipped
    // because the second Constant is a jump target.
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_2 = chunk.add_constant(Value{static_cast<std::int64_t>(2)});
    const auto idx_3 = chunk.add_constant(Value{static_cast<std::int64_t>(3)});
    const auto idx_5 = chunk.add_constant(Value{static_cast<std::int64_t>(5)});

    chunk.emit_u16(Op::GetLocal, 0, loc);     // 0-2:  condition (runtime value)
    chunk.emit_u32(Op::JumpIfFalse, 8, loc);  // 3-7:  false → else (target 16)
    chunk.emit_u16(Op::Constant, idx_2, loc); // 8-10: then value
    chunk.emit_u32(Op::Jump, 3, loc);         // 11-15: then → merge (target 19)
    chunk.emit_u16(Op::Constant, idx_3, loc); // 16-18: else value (jump target)
    chunk.emit_u16(Op::Constant, idx_5, loc); // 19-21: merge — `+ 5` (jump target)
    chunk.emit(Op::Add, loc);                 // 22
    chunk.emit(Op::EndModule, loc);           // 23

    const Optimizer opt{2};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    // The Add must survive: folding it away corrupts the then-branch path.
    ASSERT_TRUE(has_opcode(chunk, Op::Add));
}

// ─── Unary fold must NOT cross a branch-merge point ───

static void test_unary_fold_preserved_across_jump_target() {
    // Mirrors `-(if c { 2 } else { 3 })`.  The else Constant is immediately
    // followed by the merge-point Negate, forming a foldable `Constant; Negate`
    // pair, but the then branch jumps directly onto the Negate.  Folding it would
    // leave the then value (2) un-negated, yielding 2 instead of -2.
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_2 = chunk.add_constant(Value{static_cast<std::int64_t>(2)});
    const auto idx_3 = chunk.add_constant(Value{static_cast<std::int64_t>(3)});

    chunk.emit_u16(Op::GetLocal, 0, loc);     // 0-2:  condition
    chunk.emit_u32(Op::JumpIfFalse, 8, loc);  // 3-7:  false → else (target 16)
    chunk.emit_u16(Op::Constant, idx_2, loc); // 8-10: then value
    chunk.emit_u32(Op::Jump, 3, loc);         // 11-15: then → merge (target 19)
    chunk.emit_u16(Op::Constant, idx_3, loc); // 16-18: else value (jump target)
    chunk.emit(Op::Negate, loc);              // 19:   merge — unary minus (jump target)
    chunk.emit(Op::EndModule, loc);           // 20

    const Optimizer opt{2};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    // The Negate must survive: folding it corrupts the then-branch path.
    ASSERT_TRUE(has_opcode(chunk, Op::Negate));
}

// ─── Comparison fold must NOT cross a branch-merge point ───

static void test_comparison_fold_preserved_across_jump_target() {
    // Mirrors `(if c { 2 } else { 3 }) < 5`.  The else Constant precedes the
    // merge-point `Constant 5; Less`, forming a foldable comparison triple, but
    // the then branch jumps onto that second Constant.  Folding it to a boolean
    // would leave the then value (2) with no comparison — wrong value AND wrong
    // type (integer instead of boolean).
    Chunk chunk;
    const auto loc = SourceLocation{};
    const auto idx_2 = chunk.add_constant(Value{static_cast<std::int64_t>(2)});
    const auto idx_3 = chunk.add_constant(Value{static_cast<std::int64_t>(3)});
    const auto idx_5 = chunk.add_constant(Value{static_cast<std::int64_t>(5)});

    chunk.emit_u16(Op::GetLocal, 0, loc);     // 0-2:  condition
    chunk.emit_u32(Op::JumpIfFalse, 8, loc);  // 3-7:  false → else (target 16)
    chunk.emit_u16(Op::Constant, idx_2, loc); // 8-10: then value
    chunk.emit_u32(Op::Jump, 3, loc);         // 11-15: then → merge (target 19)
    chunk.emit_u16(Op::Constant, idx_3, loc); // 16-18: else value (jump target)
    chunk.emit_u16(Op::Constant, idx_5, loc); // 19-21: merge — `< 5` (jump target)
    chunk.emit(Op::Less, loc);                // 22
    chunk.emit(Op::EndModule, loc);           // 23

    const Optimizer opt{2};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    // The Less must survive: folding it away corrupts the then-branch path.
    ASSERT_TRUE(has_opcode(chunk, Op::Less));
}

// ─── main ───

int main() {
    // Constant folding.
    RUN(test_constant_fold_integer_add);
    RUN(test_constant_fold_integer_mul);
    RUN(test_constant_fold_skip_div_zero);
    RUN(test_constant_fold_float_add);
    RUN(test_constant_fold_string_concat);
    RUN(test_constant_fold_skips_when_pool_full);
    RUN(test_constant_fold_skips_integer_overflow);
    RUN(test_constant_fold_subtraction);
    RUN(test_constant_fold_modulo);
    RUN(test_constant_fold_skip_modulo_int_min_neg1);

    // Unary constant folding.
    RUN(test_unary_fold_negate_integer);
    RUN(test_unary_fold_negate_double);

    // Comparison constant folding.
    RUN(test_comparison_fold_less_true);
    RUN(test_comparison_fold_equal_false);
    RUN(test_comparison_fold_string_equal);

    // Fold passes must not cross basic-block boundaries.
    RUN(test_constant_fold_preserved_across_jump_target);
    RUN(test_unary_fold_preserved_across_jump_target);
    RUN(test_comparison_fold_preserved_across_jump_target);

    return SUMMARY();
}
