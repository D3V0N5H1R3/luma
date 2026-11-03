// Bytecode optimizer unit tests: dead code, jump threading, dead stores, tail
// calls, and cross-function inlining.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/byte_utils.hpp"
#include "compiler_test_helpers.hpp"
#include "optimizer_test_helpers.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/compiled_function.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/optimizer.hpp"
#include "test_framework.hpp"

using luma::Chunk;
using luma::CompiledFunction;
using luma::Op;
using luma::Optimizer;
using luma::SourceLocation;
using luma::test::count_opcode;
using luma::test::decode_instructions;
using luma::test::has_opcode;
using luma::test::make_chunk;

// ─── Helpers ───

// The opcodes of the chunk, in order.
static std::vector<Op> opcodes_of(const Chunk& chunk) {
    std::vector<Op> ops;

    for (const auto& insn : decode_instructions(chunk)) {
        ops.push_back(insn.op);
    }

    return ops;
}

// Absolute target of the forward jump (u32 operand) that starts at `offset`.
static std::size_t jump_target(const Chunk& chunk, std::size_t offset) {
    constexpr std::size_t k_jump_size = 5;

    if (offset + k_jump_size > chunk.code.size()) {
        luma::test::throw_assertion_error("jump_target: truncated jump at offset " +
                                          std::to_string(offset));
    }

    return offset + k_jump_size + luma::read_u32_be(&chunk.code[offset + 1]);
}

// ─── Dead code elimination ───

static void test_dead_code_after_return() {
    // Build: Return, True, Pop, EndModule
    // True + Pop should be eliminated as dead code.
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit(Op::Return, loc);
    chunk.emit(Op::True, loc);
    chunk.emit(Op::Pop, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GE(eliminated, 2U);
    ASSERT_EQ(decode_instructions(chunk).front().op, Op::Return);
    ASSERT_FALSE(has_opcode(chunk, Op::True));
    ASSERT_FALSE(has_opcode(chunk, Op::Pop));
}

// ─── Dead code elimination: after Jump ───

static void test_dead_code_after_jump() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    // Jump +2 (skip True + Pop), land on EndModule.
    chunk.emit_u32(Op::Jump, 2, loc); // offset 0-4: jump over 2 bytes
    chunk.emit(Op::True, loc);        // offset 5: dead
    chunk.emit(Op::Pop, loc);         // offset 6: dead
    chunk.emit(Op::EndModule, loc);   // offset 7: target

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GE(eliminated, 2U);
    ASSERT_FALSE(has_opcode(chunk, Op::True));
    ASSERT_FALSE(has_opcode(chunk, Op::Pop));
    ASSERT_TRUE(has_opcode(chunk, Op::EndModule));
}

// ─── Dead code elimination: preserves jump targets ───

static void test_dead_code_preserves_jump_target() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    // Return then jump target — code at the target must survive.
    chunk.emit(Op::True, loc);               // offset 0: push a value
    chunk.emit_u32(Op::JumpIfFalse, 2, loc); // offset 1-5: conditional jump
    chunk.emit(Op::Return, loc);             // offset 6: return (terminator)
    chunk.emit(Op::True, loc);               // offset 7: dead (between return and target)
    // offset 8: jump target for JumpIfFalse (1+5+2=8)
    chunk.emit(Op::False, loc);  // offset 8: reachable via jump
    chunk.emit(Op::Return, loc); // offset 9

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    // Only the dead True is removed; the False at the jump target survives and
    // the jump is retargeted onto it.
    ASSERT_EQ(eliminated, 1U);
    ASSERT_EQ(opcodes_of(chunk),
              (std::vector<Op>{Op::True, Op::JumpIfFalse, Op::Return, Op::False, Op::Return}));
    ASSERT_EQ(static_cast<Op>(chunk.code[jump_target(chunk, 1)]), Op::False);
}

// ─── Jump threading: jump-to-jump resolved ───

static void test_jump_threading_simple() {
    // Build: Jump(+0) → Jump(+0) → EndModule
    // First jump targets second jump, which targets EndModule.
    // After threading, first jump should target EndModule directly.
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit_u32(Op::Jump, 0, loc); // offset 0: jump to offset 5 (next instruction)
    chunk.emit_u32(Op::Jump, 0, loc); // offset 5: jump to offset 10 (next instruction)
    chunk.emit(Op::EndModule, loc);   // offset 10

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    // The first Jump is threaded straight to EndModule, which leaves the
    // second Jump unreachable; dead code elimination removes it.
    ASSERT_EQ(eliminated, 5U);
    ASSERT_EQ(opcodes_of(chunk), (std::vector<Op>{Op::Jump, Op::EndModule}));
    ASSERT_EQ(static_cast<Op>(chunk.code[jump_target(chunk, 0)]), Op::EndModule);
}

// ─── Jump threading: conditional jump through unconditional ───

static void test_jump_threading_conditional() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit(Op::True, loc);               // offset 0: push condition
    chunk.emit_u32(Op::JumpIfFalse, 1, loc); // offset 1: jump to offset 7 (the Jump)
    chunk.emit(Op::Return, loc);             // offset 6: return (if true)
    chunk.emit_u32(Op::Jump, 0, loc);        // offset 7: jump to offset 12
    chunk.emit(Op::False, loc);              // offset 12: push false
    chunk.emit(Op::Return, loc);             // offset 13: return

    // JumpIfFalse target (offset 7) is an unconditional Jump to offset 12.
    // After threading, JumpIfFalse jumps directly to the False, which leaves
    // the intermediate Jump unreachable; dead code elimination removes it.
    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_EQ(eliminated, 5U);
    ASSERT_EQ(opcodes_of(chunk),
              (std::vector<Op>{Op::True, Op::JumpIfFalse, Op::Return, Op::False, Op::Return}));
    ASSERT_EQ(static_cast<Op>(chunk.code[jump_target(chunk, 1)]), Op::False);
}

// ─── Dead store: SetLocal + SetLocal same slot ───

static void test_dead_store_set_local_same_slot() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);             // Push value 1.
    chunk.emit_u16(Op::SetLocal, 0, loc); // Set local 0 (dead — overwritten next).
    chunk.emit_u16(Op::SetLocal, 0, loc); // Set local 0 again.
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    // Only one SetLocal should remain.
    ASSERT_EQ(count_opcode(chunk, Op::SetLocal), 1U);
}

// ─── Dead store: SetLocalPop + SetLocalPop same slot → Pop + SetLocalPop ───

static void test_dead_store_set_local_pop_same_slot() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);                // Push first value.
    chunk.emit(Op::Zero, loc);               // Push second value.
    chunk.emit_u16(Op::SetLocalPop, 0, loc); // Pop second, set local 0 (dead store).
    chunk.emit_u16(Op::SetLocalPop, 0, loc); // Pop first, set local 0 again.
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    const auto eliminated = opt.optimize(chunk);

    ASSERT_GT(eliminated, 0U);
    // The first SetLocalPop should be replaced with Pop.
    ASSERT_TRUE(has_opcode(chunk, Op::Pop));
    // One SetLocalPop should remain.
    ASSERT_EQ(count_opcode(chunk, Op::SetLocalPop), 1U);
}

// ─── Dead store: different slots not eliminated ───

static void test_dead_store_different_slots_preserved() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);
    chunk.emit_u16(Op::SetLocal, 0, loc); // Set local 0.
    chunk.emit_u16(Op::SetLocal, 1, loc); // Set local 1 (different slot — not dead).
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    ASSERT_EQ(opt.optimize(chunk), 0U);
    ASSERT_EQ(count_opcode(chunk, Op::SetLocal), 2U);
}

// ─── Tail call: Call + Return → TailCall ───

static void test_tail_call_simple() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);        // Push argument.
    chunk.emit_u8(Op::Call, 1, loc); // Call with 1 arg.
    chunk.emit(Op::Return, loc);     // Return.
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    (void)opt.optimize(chunk);

    ASSERT_TRUE(has_opcode(chunk, Op::TailCall));
    ASSERT_FALSE(has_opcode(chunk, Op::Call));
    // Return is kept after TailCall (needed for native callee fallback).
    ASSERT_TRUE(has_opcode(chunk, Op::Return));
}

// ─── Tail call: Call not in tail position preserved ───

static void test_tail_call_not_tail_position() {
    Chunk chunk;
    const auto loc = SourceLocation{};
    chunk.emit(Op::One, loc);
    chunk.emit_u8(Op::Call, 1, loc); // Call with 1 arg.
    chunk.emit(Op::Pop, loc);        // Not Return — not tail position.
    chunk.emit(Op::Return, loc);
    chunk.emit(Op::EndModule, loc);

    const Optimizer opt{2};
    ASSERT_EQ(opt.optimize(chunk), 0U);

    // Call should NOT be converted to TailCall.
    ASSERT_TRUE(has_opcode(chunk, Op::Call));
    ASSERT_FALSE(has_opcode(chunk, Op::TailCall));
}

// ─── Cross-function inlining ───

static CompiledFunction make_callee(std::initializer_list<std::uint8_t> bytes) {
    CompiledFunction callee;
    callee.name = "<lambda>";
    callee.mutable_chunk().code = std::vector<std::uint8_t>(bytes);
    return callee;
}

// A straight-line `One; Return` callee is inlined in place of MakeClosure+Call.
static void test_inline_straight_line_callee() {
    const std::vector<CompiledFunction> functions{make_callee({
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Return),
    })};

    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::MakeClosure),
        0,
        0,
        0, // func 0, no upvalues
        static_cast<std::uint8_t>(Op::Call),
        0, // call with 0 args
        static_cast<std::uint8_t>(Op::Return),
    });

    const Optimizer opt{2};
    ASSERT_GT(opt.inline_small_functions(chunk, functions), 0U);

    ASSERT_EQ(opcodes_of(chunk), (std::vector<Op>{Op::One, Op::Return}));
}

// Regression: the inliner copied a callee body that contained a Return before
// the final one (`One Return None Return`, as a block lambda `{ return 1 }` used
// to compile), so the *caller* returned early.
static void test_inline_rejects_body_with_mid_body_return() {
    const std::vector<CompiledFunction> functions{make_callee({
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Return),
        static_cast<std::uint8_t>(Op::None),
        static_cast<std::uint8_t>(Op::Return),
    })};

    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::MakeClosure),
        0,
        0,
        0,
        static_cast<std::uint8_t>(Op::Call),
        0,
        static_cast<std::uint8_t>(Op::Pop),
        static_cast<std::uint8_t>(Op::None),
        static_cast<std::uint8_t>(Op::Return),
    });
    const auto before = chunk.code;

    const Optimizer opt{2};
    ASSERT_EQ(opt.inline_small_functions(chunk, functions), 0U);

    ASSERT_EQ(chunk.code, before);
}

// Regression: a jump landing on the Call skipped the MakeClosure on that path;
// after inlining it landed inside the copied body.  Such calls are not inlined.
static void test_inline_skips_call_that_is_jump_target() {
    const std::vector<CompiledFunction> functions{make_callee({
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Return),
    })};

    // clang-format off
    auto chunk = make_chunk({
        static_cast<std::uint8_t>(Op::True),        // 0
        static_cast<std::uint8_t>(Op::JumpIfFalse), // 1: → 10 (the Call)
        0, 0, 0, 4,
        static_cast<std::uint8_t>(Op::MakeClosure), // 6
        0, 0, 0,
        static_cast<std::uint8_t>(Op::Call),        // 10
        0,
        static_cast<std::uint8_t>(Op::Return),      // 12
    });
    // clang-format on
    const auto before = chunk.code;

    const Optimizer opt{2};
    ASSERT_EQ(opt.inline_small_functions(chunk, functions), 0U);

    ASSERT_EQ(chunk.code, before);
}

// ─── main ───

int main() {
    // Dead code elimination.
    RUN(test_dead_code_after_return);
    RUN(test_dead_code_after_jump);
    RUN(test_dead_code_preserves_jump_target);

    // Jump threading.
    RUN(test_jump_threading_simple);
    RUN(test_jump_threading_conditional);

    // Dead store elimination.
    RUN(test_dead_store_set_local_same_slot);
    RUN(test_dead_store_set_local_pop_same_slot);
    RUN(test_dead_store_different_slots_preserved);

    // Tail call optimization.
    RUN(test_tail_call_simple);
    RUN(test_tail_call_not_tail_position);

    // Cross-function inlining.
    RUN(test_inline_straight_line_callee);
    RUN(test_inline_rejects_body_with_mid_body_return);
    RUN(test_inline_skips_call_that_is_jump_target);

    return SUMMARY();
}
