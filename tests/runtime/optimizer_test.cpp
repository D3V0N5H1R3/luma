// Bytecode optimizer unit tests.

#include <cstdint>
#include <string>

#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/optimizer.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Helpers ───

static Chunk make_chunk(std::initializer_list<std::uint8_t> bytes) {
    Chunk chunk;
    chunk.code = std::vector<std::uint8_t>(bytes);
    return chunk;
}

static bool has_opcode(const Chunk& chunk, Op op) {
    for (std::size_t i{0}; i < chunk.code.size(); ++i) {
        if (static_cast<Op>(chunk.code[i]) == op) {
            return true;
        }
    }

    return false;
}

static std::size_t count_opcode(const Chunk& chunk, Op op) {
    std::size_t count = 0;

    for (std::size_t i{0}; i < chunk.code.size(); ++i) {
        if (static_cast<Op>(chunk.code[i]) == op) {
            ++count;
        }
    }

    return count;
}

// ─── Level 0: no optimisation ───

static void test_level0_noop() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::Not),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    Optimizer opt{0};
    auto eliminated = opt.optimize(chunk);

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

    Optimizer opt{1};
    auto eliminated = opt.optimize(chunk);

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

    Optimizer opt{1};
    auto eliminated = opt.optimize(chunk);

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

    Optimizer opt{1};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);

    ASSERT_EQ(count_opcode(chunk, Op::Not), 2U);
}

// ─── Peephole: Negate + Negate is preserved (INT64_MIN type safety) ───

static void test_peephole_double_negate_preserved() {
    // safe_negate promotes INT64_MIN to a double, so `-(-INT64_MIN)` legitimately
    // yields a number.  Cancelling the pair would leave the raw integer INT64_MIN
    // and silently change the value's type, so both Negates must survive.
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Negate),
        static_cast<std::uint8_t>(Op::Negate),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    Optimizer opt{1};
    auto eliminated = opt.optimize(chunk);

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

    Optimizer opt{1};
    auto eliminated = opt.optimize(chunk);

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

    Optimizer opt{1};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);

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

    Optimizer opt{1};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);

    ASSERT_TRUE(has_opcode(chunk, Op::Zero));
    ASSERT_TRUE(has_opcode(chunk, Op::Subtract));
}

// ─── Peephole: One + Add → Increment ───

static void test_peephole_one_add_to_increment() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True), // placeholder stack value
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Add),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    Optimizer opt{1};
    [[maybe_unused]] auto elim = opt.optimize(chunk);

    ASSERT_TRUE(has_opcode(chunk, Op::Increment));
    ASSERT_FALSE(has_opcode(chunk, Op::One));
}

// ─── Peephole: One + Subtract → Decrement ───

static void test_peephole_one_subtract_to_decrement() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True), // placeholder stack value
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Subtract),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    Optimizer opt{1};
    [[maybe_unused]] auto elim = opt.optimize(chunk);

    ASSERT_TRUE(has_opcode(chunk, Op::Decrement));
    ASSERT_FALSE(has_opcode(chunk, Op::One));
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

    Optimizer opt{1};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);

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

    Optimizer opt{1};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);

    ASSERT_TRUE(has_opcode(chunk, Op::Divide));
}

// ─── Constant folding: integer addition ───

static void test_constant_fold_integer_add() {
    // Build: Constant(3) Constant(7) Add
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(3)});
    auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(7)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Add, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    // Should fold to a single Constant(10).
    ASSERT_FALSE(has_opcode(chunk, Op::Add));
}

// ─── Constant folding: integer multiplication ───

static void test_constant_fold_integer_mul() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(6)});
    auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(7)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Multiply, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Multiply));
    // Result (42) should be in the constant pool.
    bool found_42 = false;
    for (const auto& c : chunk.constants) {
        if (c.is_integer() && c.as_integer() == 42) {
            found_42 = true;
        }
    }
    ASSERT_TRUE(found_42);
}

// ─── Constant folding: skips division by zero ───

static void test_constant_fold_skip_div_zero() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(10)});
    auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(0)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::IntDivide, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    [[maybe_unused]] auto elim = opt.optimize(chunk);

    // Should NOT fold — division by zero must remain a runtime error.
    ASSERT_TRUE(has_opcode(chunk, Op::IntDivide));
}

// ─── Constant folding: floating-point ───

static void test_constant_fold_float_add() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{1.5});
    auto idx_b = chunk.add_constant(Value{2.5});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Add, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Add));
    // Result (4.0) should be in the constant pool.
    bool found = false;
    for (const auto& c : chunk.constants) {
        if (c.is_number() && c.as_number() == 4.0) {
            found = true;
        }
    }
    ASSERT_TRUE(found);
}

// ─── Dead code elimination ───

static void test_dead_code_after_return() {
    // Build: Return, True, Pop, EndModule
    // True + Pop should be eliminated as dead code.
    Chunk chunk;
    auto loc = SourceLocation{};
    chunk.emit(Op::Return, loc);
    chunk.emit(Op::True, loc);
    chunk.emit(Op::Pop, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
}

// ─── Empty chunk ───

static void test_optimize_empty_chunk() {
    Chunk chunk;

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_EQ(eliminated, 0U);
}

// ─── End-to-end: peephole on compiled chunk ───

static void test_end_to_end_peephole_on_compiled() {
    // Build a chunk with True + Not and apply the optimizer.
    Chunk chunk;
    auto loc = SourceLocation{};
    chunk.emit(Op::True, loc);
    chunk.emit(Op::Not, loc);
    chunk.emit(Op::Return, loc);

    Optimizer opt{1};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_TRUE(has_opcode(chunk, Op::False));
    ASSERT_FALSE(has_opcode(chunk, Op::Not));
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

    Optimizer opt{1};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_TRUE(has_opcode(chunk, Op::False));
    ASSERT_FALSE(has_opcode(chunk, Op::Not));
    ASSERT_FALSE(has_opcode(chunk, Op::Dup));
}

// ─── Constant folding: subtraction ───

static void test_constant_fold_subtraction() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(100)});
    auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(58)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Subtract, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Subtract));
    bool found = false;
    for (const auto& c : chunk.constants) {
        if (c.is_integer() && c.as_integer() == 42) {
            found = true;
        }
    }
    ASSERT_TRUE(found);
}

// ─── Constant folding: modulo ───

static void test_constant_fold_modulo() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(17)});
    auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(5)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Modulo, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Modulo));
    bool found = false;
    for (const auto& c : chunk.constants) {
        if (c.is_integer() && c.as_integer() == 2) {
            found = true;
        }
    }
    ASSERT_TRUE(found);
}

// ─── Constant folding: skips INT64_MIN % -1 (signed-overflow UB) ───

static void test_constant_fold_skip_modulo_int_min_neg1() {
    // INT64_MIN % -1 is undefined behaviour in C++ (the matching division
    // overflows).  The optimizer must bail out and leave the Modulo for the VM
    // rather than evaluating it at compile time — pre-fix this crashed the
    // compiler with an integer-overflow trap.
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(INT64_MIN)});
    auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(-1)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Modulo, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    [[maybe_unused]] auto elim = opt.optimize(chunk);

    // Must NOT fold — the operation stays a runtime opcode.
    ASSERT_TRUE(has_opcode(chunk, Op::Modulo));
}

// ─── Constant folding: bitwise AND ───

static void test_constant_fold_bitwise() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(0xFF)});
    auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(0x0F)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::BitwiseAnd, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::BitwiseAnd));
    bool found = false;
    for (const auto& c : chunk.constants) {
        if (c.is_integer() && c.as_integer() == 0x0F) {
            found = true;
        }
    }
    ASSERT_TRUE(found);
}

// ─── Dead code elimination: after Jump ───

static void test_dead_code_after_jump() {
    Chunk chunk;
    auto loc = SourceLocation{};
    // Jump +2 (skip True + Pop), land on EndModule.
    chunk.emit_u32(Op::Jump, 2, loc); // offset 0-4: jump over 2 bytes
    chunk.emit(Op::True, loc);        // offset 5: dead
    chunk.emit(Op::Pop, loc);         // offset 6: dead
    chunk.emit(Op::EndModule, loc);   // offset 7: target

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
}

// ─── Dead code elimination: preserves jump targets ───

static void test_dead_code_preserves_jump_target() {
    Chunk chunk;
    auto loc = SourceLocation{};
    // Return then jump target — code at the target must survive.
    chunk.emit(Op::True, loc);               // offset 0: push a value
    chunk.emit_u32(Op::JumpIfFalse, 2, loc); // offset 1-5: conditional jump
    chunk.emit(Op::Return, loc);             // offset 6: return (terminator)
    chunk.emit(Op::True, loc);               // offset 7: dead (between return and target)
    // offset 8: jump target for JumpIfFalse (1+5+2=8)
    chunk.emit(Op::False, loc);  // offset 8: reachable via jump
    chunk.emit(Op::Return, loc); // offset 9

    Optimizer opt{2};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);

    // The False at the jump target must survive.
    ASSERT_TRUE(has_opcode(chunk, Op::False));
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

    const Case cases[] = {
        {.cmp = Op::Less, .inverse = Op::GreaterEqual},
        {.cmp = Op::LessEqual, .inverse = Op::Greater},
        {.cmp = Op::Greater, .inverse = Op::LessEqual},
        {.cmp = Op::GreaterEqual, .inverse = Op::Less},
    };

    for (const auto& c : cases) {
        auto chunk = make_chunk({
            static_cast<std::uint8_t>(c.cmp),
            static_cast<std::uint8_t>(Op::Not),
            static_cast<std::uint8_t>(Op::EndModule),
        });

        Optimizer opt{1};
        auto eliminated = opt.optimize(chunk);

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

    Optimizer opt_eq{1};
    ASSERT_GT(opt_eq.optimize(eq_chunk), 0U);
    ASSERT_TRUE(has_opcode(eq_chunk, Op::NotEqual));
    ASSERT_FALSE(has_opcode(eq_chunk, Op::Not));

    auto ne_chunk = make_chunk({
        static_cast<std::uint8_t>(Op::NotEqual),
        static_cast<std::uint8_t>(Op::Not),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    Optimizer opt_ne{1};
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
    auto loc = SourceLocation{};
    chunk.emit(Op::Equal, loc);              // offset 0: left comparison
    chunk.emit_u32(Op::JumpIfFalse, 2, loc); // offset 1-5: target = 6 + 2 = 8 (the Not)
    chunk.emit(Op::Pop, loc);                // offset 6
    chunk.emit(Op::Equal, loc);              // offset 7: right comparison
    chunk.emit(Op::Not, loc);                // offset 8: jump target — must survive
    chunk.emit(Op::EndModule, loc);          // offset 9

    Optimizer opt{1};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);

    ASSERT_TRUE(has_opcode(chunk, Op::Not));
    ASSERT_FALSE(has_opcode(chunk, Op::NotEqual));
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

    Optimizer opt{1};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_EQ(count_opcode(chunk, Op::None), 0U);
    ASSERT_EQ(count_opcode(chunk, Op::Pop), 0U);
}

// ─── SetLocal + GetLocal → SetLocal + Dup (peephole fusion) ───

static void test_set_local_get_local_fusion() {
    Chunk chunk;
    auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);             // Push a value.
    chunk.emit_u16(Op::SetLocal, 0, loc); // SetLocal slot 0.
    chunk.emit_u16(Op::GetLocal, 0, loc); // GetLocal slot 0.
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{1};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_TRUE(has_opcode(chunk, Op::Dup));
}

// ─── Peephole: One + IntDivide is NOT folded (x // 1 is unsound for number) ───

// `//` requires integer operands and handle_int_divide raises a RuntimeError on a
// non-integer, but bytecode carries no static type and an integer-typed slot can
// hold an overflow-promoted `number`.  The `One; IntDivide` cancel-pair was removed
// (same class as `x % 1`), so IntDivide must survive the optimizer intact.
static void test_peephole_one_int_divide_preserved() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True), // placeholder stack value
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::IntDivide),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    Optimizer opt{1};
    [[maybe_unused]] auto elim = opt.optimize(chunk);

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

    Optimizer opt{1};
    [[maybe_unused]] auto elim = opt.optimize(chunk);

    ASSERT_TRUE(has_opcode(chunk, Op::Modulo));
    ASSERT_TRUE(has_opcode(chunk, Op::One));
    ASSERT_FALSE(has_opcode(chunk, Op::Zero));
}

// ─── Peephole: BitwiseNot + BitwiseNot is NOT folded (~~x is unsound for number) ───

// `~` requires an integer operand and op_bitwise_not raises a RuntimeError on a
// non-integer, but an integer-typed slot can hold an overflow-promoted `number`
// (integer overflow promotes to double).  The `BitwiseNot; BitwiseNot` cancel-pair
// was removed, so both ops must survive the optimizer so the VM can enforce the
// integer-operand contract at -O1 exactly as it does at -O0.
static void test_peephole_double_bitwise_not_preserved() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::GetLocal),
        0x00,
        0x00,
        static_cast<std::uint8_t>(Op::BitwiseNot),
        static_cast<std::uint8_t>(Op::BitwiseNot),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    Optimizer opt{1};
    [[maybe_unused]] auto elim = opt.optimize(chunk);

    ASSERT_EQ(count_opcode(chunk, Op::BitwiseNot), 2U);
}

// ─── Peephole: IntToNumber + IntToNumber → single IntToNumber ───

static void test_peephole_double_int_to_number() {
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::IntToNumber),
        static_cast<std::uint8_t>(Op::IntToNumber),
        static_cast<std::uint8_t>(Op::EndModule),
    });

    Optimizer opt{1};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_EQ(count_opcode(chunk, Op::IntToNumber), 1U);
}

// ─── Constant folding: string concatenation ───

static void test_constant_fold_string_concat() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{std::string{"hello "}});
    auto idx_b = chunk.add_constant(Value{std::string{"world"}});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Concatenate, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Concatenate));
    bool found = false;
    for (const auto& c : chunk.constants) {
        if (c.is_string() && c.as_string() == "hello world") {
            found = true;
        }
    }
    ASSERT_TRUE(found);
}

// ─── Unary constant folding: Negate integer ───

static void test_unary_fold_negate_integer() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx = chunk.add_constant(Value{static_cast<std::int64_t>(42)});

    chunk.emit_u16(Op::Constant, idx, loc);
    chunk.emit(Op::Negate, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Negate));
    bool found = false;
    for (const auto& c : chunk.constants) {
        if (c.is_integer() && c.as_integer() == -42) {
            found = true;
        }
    }
    ASSERT_TRUE(found);
}

// ─── Unary constant folding: Negate double ───

static void test_unary_fold_negate_double() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx = chunk.add_constant(Value{3.14});

    chunk.emit_u16(Op::Constant, idx, loc);
    chunk.emit(Op::Negate, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Negate));
    bool found = false;
    for (const auto& c : chunk.constants) {
        if (c.is_number() && c.as_number() == -3.14) {
            found = true;
        }
    }
    ASSERT_TRUE(found);
}

// ─── Unary constant folding: BitwiseNot integer ───

static void test_unary_fold_bitwise_not() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx = chunk.add_constant(Value{static_cast<std::int64_t>(0xFF)});

    chunk.emit_u16(Op::Constant, idx, loc);
    chunk.emit(Op::BitwiseNot, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::BitwiseNot));
    bool found = false;
    for (const auto& c : chunk.constants) {
        if (c.is_integer() && c.as_integer() == ~static_cast<std::int64_t>(0xFF)) {
            found = true;
        }
    }
    ASSERT_TRUE(found);
}

// ─── Comparison constant folding: integer less than (true) ───

static void test_comparison_fold_less_true() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(3)});
    auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(7)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Less, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Less));
    ASSERT_TRUE(has_opcode(chunk, Op::True));
}

// ─── Comparison constant folding: integer equal (false) ───

static void test_comparison_fold_equal_false() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{static_cast<std::int64_t>(5)});
    auto idx_b = chunk.add_constant(Value{static_cast<std::int64_t>(10)});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Equal, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Equal));
    ASSERT_TRUE(has_opcode(chunk, Op::False));
}

// ─── Comparison constant folding: string comparison ───

static void test_comparison_fold_string_equal() {
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_a = chunk.add_constant(Value{std::string{"abc"}});
    auto idx_b = chunk.add_constant(Value{std::string{"abc"}});

    chunk.emit_u16(Op::Constant, idx_a, loc);
    chunk.emit_u16(Op::Constant, idx_b, loc);
    chunk.emit(Op::Equal, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_FALSE(has_opcode(chunk, Op::Equal));
    ASSERT_TRUE(has_opcode(chunk, Op::True));
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

    Optimizer opt{1};
    [[maybe_unused]] auto elim = opt.optimize(chunk);

    // Both Zero and Multiply must remain — no folding.
    ASSERT_TRUE(has_opcode(chunk, Op::Zero));
    ASSERT_TRUE(has_opcode(chunk, Op::Multiply));
}

// ─── Jump threading: jump-to-jump resolved ───

static void test_jump_threading_simple() {
    // Build: Jump(+0) → Jump(+0) → EndModule
    // First jump targets second jump, which targets EndModule.
    // After threading, first jump should target EndModule directly.
    Chunk chunk;
    auto loc = SourceLocation{};
    chunk.emit_u32(Op::Jump, 0, loc); // offset 0: jump to offset 5 (next instruction)
    chunk.emit_u32(Op::Jump, 0, loc); // offset 5: jump to offset 10 (next instruction)
    chunk.emit(Op::EndModule, loc);   // offset 10

    Optimizer opt{2};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);

    // After threading, the first Jump should target EndModule (offset 10),
    // so its offset should be 5 (= 10 - 5). The second jump is dead code
    // after dead code elimination.
    // Verify the code still ends properly.
    ASSERT_TRUE(has_opcode(chunk, Op::EndModule));
}

// ─── Jump threading: conditional jump through unconditional ───

static void test_jump_threading_conditional() {
    Chunk chunk;
    auto loc = SourceLocation{};
    chunk.emit(Op::True, loc);               // offset 0: push condition
    chunk.emit_u32(Op::JumpIfFalse, 0, loc); // offset 1: jump to offset 6
    chunk.emit(Op::Return, loc);             // offset 6: return (if true)
    chunk.emit_u32(Op::Jump, 0, loc);        // offset 7: jump to offset 12
    chunk.emit(Op::False, loc);              // offset 12: push false
    chunk.emit(Op::Return, loc);             // offset 13: return

    // JumpIfFalse target (offset 7) is an unconditional Jump to offset 12.
    // After threading, JumpIfFalse should jump directly to offset 12.
    Optimizer opt{2};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);

    // Verify basic structure is preserved.
    ASSERT_TRUE(has_opcode(chunk, Op::Return));
}

// ─── Dead store: SetLocal + SetLocal same slot ───

static void test_dead_store_set_local_same_slot() {
    Chunk chunk;
    auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);             // Push value 1.
    chunk.emit_u16(Op::SetLocal, 0, loc); // Set local 0 (dead — overwritten next).
    chunk.emit_u16(Op::SetLocal, 0, loc); // Set local 0 again.
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    // Only one SetLocal should remain.
    ASSERT_EQ(count_opcode(chunk, Op::SetLocal), 1U);
}

// ─── Dead store: SetLocalPop + SetLocalPop same slot → Pop + SetLocalPop ───

static void test_dead_store_set_local_pop_same_slot() {
    Chunk chunk;
    auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);                // Push first value.
    chunk.emit(Op::Zero, loc);               // Push second value.
    chunk.emit_u16(Op::SetLocalPop, 0, loc); // Pop second, set local 0 (dead store).
    chunk.emit_u16(Op::SetLocalPop, 0, loc); // Pop first, set local 0 again.
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    // The first SetLocalPop should be replaced with Pop.
    ASSERT_TRUE(has_opcode(chunk, Op::Pop));
    // One SetLocalPop should remain.
    ASSERT_EQ(count_opcode(chunk, Op::SetLocalPop), 1U);
}

// ─── Dead store: different slots not eliminated ───

static void test_dead_store_different_slots_preserved() {
    Chunk chunk;
    auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);
    chunk.emit_u16(Op::SetLocal, 0, loc); // Set local 0.
    chunk.emit_u16(Op::SetLocal, 1, loc); // Set local 1 (different slot — not dead).
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);
    ASSERT_EQ(count_opcode(chunk, Op::SetLocal), 2U);
}

// ─── Tail call: Call + Return → TailCall ───

static void test_tail_call_simple() {
    Chunk chunk;
    auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);        // Push argument.
    chunk.emit_u8(Op::Call, 1, loc); // Call with 1 arg.
    chunk.emit(Op::Return, loc);     // Return.
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    (void)opt.optimize(chunk);

    ASSERT_TRUE(has_opcode(chunk, Op::TailCall));
    ASSERT_FALSE(has_opcode(chunk, Op::Call));
    // Return is kept after TailCall (needed for native callee fallback).
    ASSERT_TRUE(has_opcode(chunk, Op::Return));
}

// ─── Tail call: Call not in tail position preserved ───

static void test_tail_call_not_tail_position() {
    Chunk chunk;
    auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);
    chunk.emit_u8(Op::Call, 1, loc); // Call with 1 arg.
    chunk.emit(Op::Pop, loc);        // Not Return — not tail position.
    chunk.emit(Op::Return, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);

    // Call should NOT be converted to TailCall.
    ASSERT_TRUE(has_opcode(chunk, Op::Call));
    ASSERT_FALSE(has_opcode(chunk, Op::TailCall));
}

// ─── Strength reduction: multiply by power of two must NOT become a shift ───

static void test_multiply_power_of_two_not_strength_reduced() {
    // x * 2^n must keep the Multiply opcode.  Multiply is overloaded (string
    // repeat, number*int, int promotion-on-overflow), so a blind rewrite to
    // ShiftLeft (which demands two integers and wraps on overflow) would
    // miscompile "ab" * 4, 3.5 * 2, and large-integer products.  The optimizer
    // has no operand type or range information at the bytecode level, so the
    // transform was removed entirely.
    Chunk chunk;
    auto loc = SourceLocation{};
    auto idx_four = chunk.add_constant(Value{static_cast<std::int64_t>(4)});

    chunk.emit(Op::True, loc); // Non-constant placeholder left operand.
    chunk.emit_u16(Op::Constant, idx_four, loc);
    chunk.emit(Op::Multiply, loc);
    chunk.emit(Op::EndModule, loc);

    Optimizer opt{2};
    [[maybe_unused]] auto eliminated = opt.optimize(chunk);

    // Multiply preserved; no ShiftLeft introduced.
    ASSERT_TRUE(has_opcode(chunk, Op::Multiply));
    ASSERT_FALSE(has_opcode(chunk, Op::ShiftLeft));
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
    auto loc = SourceLocation{};
    auto idx_2 = chunk.add_constant(Value{static_cast<std::int64_t>(2)});
    auto idx_3 = chunk.add_constant(Value{static_cast<std::int64_t>(3)});
    auto idx_5 = chunk.add_constant(Value{static_cast<std::int64_t>(5)});

    chunk.emit_u16(Op::GetLocal, 0, loc);     // 0-2:  condition (runtime value)
    chunk.emit_u32(Op::JumpIfFalse, 8, loc);  // 3-7:  false → else (target 16)
    chunk.emit_u16(Op::Constant, idx_2, loc); // 8-10: then value
    chunk.emit_u32(Op::Jump, 3, loc);         // 11-15: then → merge (target 19)
    chunk.emit_u16(Op::Constant, idx_3, loc); // 16-18: else value (jump target)
    chunk.emit_u16(Op::Constant, idx_5, loc); // 19-21: merge — `+ 5` (jump target)
    chunk.emit(Op::Add, loc);                 // 22
    chunk.emit(Op::EndModule, loc);           // 23

    Optimizer opt{2};
    (void)opt.optimize(chunk);

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
    auto loc = SourceLocation{};
    auto idx_2 = chunk.add_constant(Value{static_cast<std::int64_t>(2)});
    auto idx_3 = chunk.add_constant(Value{static_cast<std::int64_t>(3)});

    chunk.emit_u16(Op::GetLocal, 0, loc);     // 0-2:  condition
    chunk.emit_u32(Op::JumpIfFalse, 8, loc);  // 3-7:  false → else (target 16)
    chunk.emit_u16(Op::Constant, idx_2, loc); // 8-10: then value
    chunk.emit_u32(Op::Jump, 3, loc);         // 11-15: then → merge (target 19)
    chunk.emit_u16(Op::Constant, idx_3, loc); // 16-18: else value (jump target)
    chunk.emit(Op::Negate, loc);              // 19:   merge — unary minus (jump target)
    chunk.emit(Op::EndModule, loc);           // 20

    Optimizer opt{2};
    (void)opt.optimize(chunk);

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
    auto loc = SourceLocation{};
    auto idx_2 = chunk.add_constant(Value{static_cast<std::int64_t>(2)});
    auto idx_3 = chunk.add_constant(Value{static_cast<std::int64_t>(3)});
    auto idx_5 = chunk.add_constant(Value{static_cast<std::int64_t>(5)});

    chunk.emit_u16(Op::GetLocal, 0, loc);     // 0-2:  condition
    chunk.emit_u32(Op::JumpIfFalse, 8, loc);  // 3-7:  false → else (target 16)
    chunk.emit_u16(Op::Constant, idx_2, loc); // 8-10: then value
    chunk.emit_u32(Op::Jump, 3, loc);         // 11-15: then → merge (target 19)
    chunk.emit_u16(Op::Constant, idx_3, loc); // 16-18: else value (jump target)
    chunk.emit_u16(Op::Constant, idx_5, loc); // 19-21: merge — `< 5` (jump target)
    chunk.emit(Op::Less, loc);                // 22
    chunk.emit(Op::EndModule, loc);           // 23

    Optimizer opt{2};
    (void)opt.optimize(chunk);

    // The Less must survive: folding it away corrupts the then-branch path.
    ASSERT_TRUE(has_opcode(chunk, Op::Less));
}

// ─── Peephole Dup+SetLocalPop preserves the slot operand ───

static void test_peephole_dup_set_local_pop_preserves_slot() {
    // `Dup; SetLocalPop <slot>` fuses to `SetLocal <slot>`.  Dup is one byte, so
    // the rewritten slot operand lands at i+1..i+2; nopping from i+1 (rather than
    // the single dead byte at i+3) would clobber the slot just written, producing
    // `SetLocal 0xFFFF`.  Use a distinctive slot so a clobber is unmistakable.
    constexpr std::uint16_t k_slot = 7;
    Chunk chunk;
    auto loc = SourceLocation{};
    chunk.emit(Op::Dup, loc);                     // 0
    chunk.emit_u16(Op::SetLocalPop, k_slot, loc); // 1-3
    chunk.emit(Op::EndModule, loc);               // 4

    Optimizer opt{1};
    auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    ASSERT_TRUE(has_opcode(chunk, Op::SetLocal));
    ASSERT_FALSE(has_opcode(chunk, Op::Dup));
    ASSERT_FALSE(has_opcode(chunk, Op::SetLocalPop));

    // The slot operand must be intact (big-endian u16 immediately after SetLocal).
    bool checked_slot = false;
    for (std::size_t i{0}; i + 2 < chunk.code.size(); ++i) {
        if (static_cast<Op>(chunk.code[i]) == Op::SetLocal) {
            const auto slot =
                static_cast<std::uint16_t>((chunk.code[i + 1] << 8) | chunk.code[i + 2]);
            ASSERT_EQ(slot, k_slot);
            checked_slot = true;
        }
    }
    ASSERT_TRUE(checked_slot);
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
    RUN(test_peephole_one_add_to_increment);
    RUN(test_peephole_one_subtract_to_decrement);
    RUN(test_peephole_one_multiply_preserved);
    RUN(test_peephole_one_divide_preserved);
    RUN(test_peephole_one_int_divide_preserved);
    RUN(test_peephole_one_modulo_preserved);
    RUN(test_peephole_double_bitwise_not_preserved);
    RUN(test_peephole_double_int_to_number);
    RUN(test_peephole_zero_multiply_preserved);

    // Constant folding.
    RUN(test_constant_fold_integer_add);
    RUN(test_constant_fold_integer_mul);
    RUN(test_constant_fold_skip_div_zero);
    RUN(test_constant_fold_float_add);
    RUN(test_constant_fold_string_concat);

    // Unary constant folding.
    RUN(test_unary_fold_negate_integer);
    RUN(test_unary_fold_negate_double);
    RUN(test_unary_fold_bitwise_not);

    // Comparison constant folding.
    RUN(test_comparison_fold_less_true);
    RUN(test_comparison_fold_equal_false);
    RUN(test_comparison_fold_string_equal);

    // Dead code elimination.
    RUN(test_dead_code_after_return);

    // Edge cases.
    RUN(test_optimize_empty_chunk);
    RUN(test_end_to_end_peephole_on_compiled);

    // Chained optimizations.
    RUN(test_chained_peephole_patterns);
    RUN(test_constant_fold_subtraction);
    RUN(test_constant_fold_modulo);
    RUN(test_constant_fold_skip_modulo_int_min_neg1);
    RUN(test_constant_fold_bitwise);
    RUN(test_dead_code_after_jump);
    RUN(test_dead_code_preserves_jump_target);
    RUN(test_peephole_relational_not_not_fused);
    RUN(test_peephole_equality_not_still_fuses);
    RUN(test_peephole_cmp_not_preserved_across_jump_target);
    RUN(test_peephole_stacked_cancel_pairs_all_eliminated);
    RUN(test_set_local_get_local_fusion);

    // Jump threading.
    RUN(test_jump_threading_simple);
    RUN(test_jump_threading_conditional);

    // Dead store elimination.
    RUN(test_dead_store_set_local_same_slot);
    RUN(test_dead_store_set_local_pop_same_slot);
    RUN(test_dead_store_different_slots_preserved);
    RUN(test_multiply_power_of_two_not_strength_reduced);

    // Fold passes must not cross basic-block boundaries.
    RUN(test_constant_fold_preserved_across_jump_target);
    RUN(test_unary_fold_preserved_across_jump_target);
    RUN(test_comparison_fold_preserved_across_jump_target);
    RUN(test_peephole_dup_set_local_pop_preserves_slot);

    // Tail call optimization.
    RUN(test_tail_call_simple);
    RUN(test_tail_call_not_tail_position);
    return SUMMARY();
}
