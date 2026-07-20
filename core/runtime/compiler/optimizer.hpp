#ifndef LUMA_COMPILER_OPTIMIZER_HPP
#define LUMA_COMPILER_OPTIMIZER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/opcode.hpp"

namespace luma {

// Bytecode optimizer — applies peephole optimizations and constant folding
// to a compiled chunk.  Operates in-place on the bytecode stream.
class Optimizer {
public:
    // Highest optimization level accepted by the constructor (see below).
    // Exposed so callers can iterate every level without hard-coding the bound.
    static constexpr int k_max_level = 2;

    // Optimization level:
    //   0 = no optimization (pass-through)
    //   1 = peephole only (safe, fast)
    //   2 = peephole + constant folding + dead code elimination
    explicit Optimizer(int level = 1) : level_{level} {}

    // Optimize a chunk in-place.  Returns the number of bytes eliminated.
    [[nodiscard]] std::size_t optimize(Chunk& chunk) const;

    // Cross-function inlining: replace MakeClosure+Call for trivially small
    // functions with their inlined body.  Must be called before per-chunk
    // optimize() so the inlined code benefits from subsequent passes.
    [[nodiscard]] std::size_t
    inline_small_functions(Chunk& caller_chunk,
                           const std::vector<CompiledFunction>& all_functions) const;

    // Describes a single optimization pass: its display name, method pointer,
    // and the minimum optimization level required to run it.
    struct OptimizationPass {
        std::string_view name;
        std::size_t (Optimizer::*fn)(Chunk&) const;
        int min_level;
    };

    // Registry of all optimization passes, ordered by execution priority.
    // Defined out-of-line after the class body so that all member function
    // declarations are visible at the point of initialisation.
    static const std::array<OptimizationPass, 8>& passes();

private:
    // ─── Peephole optimizations ───

    // Replace sequences like Push(0) + Add → noop, Push(1) + Add → Increment, etc.
    [[nodiscard]] std::size_t peephole_pass(Chunk& chunk) const;

    // ─── Constant folding ───

    // Fold constant arithmetic: Constant(a) + Constant(b) + ArithOp → Constant(result)
    [[nodiscard]] std::size_t constant_fold_pass(Chunk& chunk) const;

    // ─── Unary constant folding ───

    // Fold Constant(a) + UnaryOp → Constant(result) (Negate, Not, BitwiseNot).
    [[nodiscard]] std::size_t unary_fold_pass(Chunk& chunk) const;

    // ─── Comparison constant folding ───

    // Fold Constant(a) + Constant(b) + CompareOp → True/False.
    [[nodiscard]] std::size_t comparison_fold_pass(Chunk& chunk) const;

    // ─── Dead code elimination ───

    // Remove unreachable code after unconditional jumps and returns.
    [[nodiscard]] std::size_t dead_code_pass(Chunk& chunk) const;

    // ─── Jump threading ───

    // Resolve chains of jumps (jump → jump → target becomes jump → target).
    [[nodiscard]] std::size_t jump_threading_pass(Chunk& chunk) const;

    // ─── Dead store elimination ───

    // Remove stores to variables that are immediately overwritten.
    [[nodiscard]] std::size_t dead_store_pass(Chunk& chunk) const;

    // ─── Tail call optimization ───

    // Detect Call immediately followed by Return and replace with TailCall.
    [[nodiscard]] std::size_t tail_call_pass(Chunk& chunk) const;

    // Note: a constant-propagation pass (GetLocal → Constant for single-assigned
    // locals) was intentionally removed.  It was unsound at the bytecode level:
    // the optimizer cannot see local scopes/liveness, so a slot holding a
    // single-assigned constant can be freed at end of scope and reused by a
    // different binding whose value is left on the stack (an invisible write).
    // Propagating the stale constant into the reused binding's reads miscompiled
    // exotic patterns (e.g. match `case == <block-with-locals>`).  Re-enabling
    // requires a proper per-slot liveness analysis, mirroring the earlier removal
    // of the unsound multiply→shift strength reduction.

    // ─── Helpers ───

    // Remove NOP'd bytes and fix up jump offsets and source maps.
    [[nodiscard]] std::size_t compact(Chunk& chunk) const;

    // ─── Constant folding helpers ───

    struct FoldCandidate {
        std::size_t i;         // offset of first Constant in code
        std::size_t op2_pos;   // offset of second Constant
        std::size_t arith_pos; // offset of the arithmetic operation
        std::uint16_t idx1;    // constant pool index for the first operand
        std::uint16_t idx2;    // constant pool index for the second operand
        Op arith_op;           // the operation to fold
    };

    [[nodiscard]] static std::optional<FoldCandidate> find_foldable_pair(const Chunk& chunk,
                                                                         std::size_t from);

    [[nodiscard]] static std::optional<Value> try_fold_operation(const Value& val1,
                                                                 const Value& val2, Op op);

    static bool apply_fold_optimization(Chunk& chunk, const FoldCandidate& candidate);

    int level_;
};

} // namespace luma

#endif // LUMA_COMPILER_OPTIMIZER_HPP
