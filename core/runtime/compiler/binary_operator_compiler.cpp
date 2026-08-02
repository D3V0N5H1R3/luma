#include "runtime/compiler/binary_operator_compiler.hpp"

#include <format>
#include <functional>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler_errors.hpp"
#include "runtime/compiler/i_compilation_backend.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/scratch_slot_guard.hpp"

namespace luma {

// ─────────── Token-to-opcode lookup tables ───────────────────────────────────

namespace {

inline constexpr auto k_binary_op_table = [] {
    std::array<std::optional<Op>, static_cast<std::size_t>(TokenType::Count_)> table{};
    table[static_cast<std::size_t>(TokenType::Plus)] = Op::Add;
    table[static_cast<std::size_t>(TokenType::Minus)] = Op::Subtract;
    table[static_cast<std::size_t>(TokenType::Star)] = Op::Multiply;
    table[static_cast<std::size_t>(TokenType::Slash)] = Op::Divide;
    table[static_cast<std::size_t>(TokenType::SlashSlash)] = Op::IntDivide;
    table[static_cast<std::size_t>(TokenType::Percent)] = Op::Modulo;
    table[static_cast<std::size_t>(TokenType::EqualsEquals)] = Op::Equal;
    table[static_cast<std::size_t>(TokenType::BangEquals)] = Op::NotEqual;
    table[static_cast<std::size_t>(TokenType::Less)] = Op::Less;
    table[static_cast<std::size_t>(TokenType::LessEquals)] = Op::LessEqual;
    table[static_cast<std::size_t>(TokenType::Greater)] = Op::Greater;
    table[static_cast<std::size_t>(TokenType::GreaterEquals)] = Op::GreaterEqual;
    table[static_cast<std::size_t>(TokenType::In)] = Op::Contains;
    table[static_cast<std::size_t>(TokenType::PlusPlus)] = Op::Concatenate;
    return table;
}();

inline constexpr auto k_compound_op_table = [] {
    std::array<std::optional<Op>, static_cast<std::size_t>(TokenType::Count_)> table{};
    table[static_cast<std::size_t>(TokenType::PlusEquals)] = Op::Add;
    table[static_cast<std::size_t>(TokenType::MinusEquals)] = Op::Subtract;
    table[static_cast<std::size_t>(TokenType::StarEquals)] = Op::Multiply;
    table[static_cast<std::size_t>(TokenType::SlashEquals)] = Op::Divide;
    table[static_cast<std::size_t>(TokenType::PercentEquals)] = Op::Modulo;
    table[static_cast<std::size_t>(TokenType::SlashSlashEquals)] = Op::IntDivide;
    return table;
}();

// Bounds-checked lookup into a constexpr token-to-opcode table.
template <typename Table>
[[nodiscard]] constexpr auto lookup_op_table(const Table& table, std::size_t index)
    -> Table::value_type {
    if (index >= table.size()) {
        return std::nullopt;
    }
    return table[index];
}

} // anonymous namespace

constexpr std::optional<Op> binary_op_for_token(TokenType t) noexcept {
    return lookup_op_table(k_binary_op_table, static_cast<std::size_t>(t));
}

constexpr std::optional<Op> compound_op_for_token(TokenType t) noexcept {
    return lookup_op_table(k_compound_op_table, static_cast<std::size_t>(t));
}

// ─────────── Short-circuit compilation ───────────

void BinaryOperatorCompiler::compile_short_circuit(const BinaryExpression& expr, Op jump_op,
                                                   bool is_and) {
    // Boolean constant folding — both operands are compile-time literals.
    if (expr.left->kind == ExpressionKind::Literal && expr.right->kind == ExpressionKind::Literal) {
        const auto& lhs = static_cast<const LiteralExpression&>(*expr.left);
        const auto& rhs = static_cast<const LiteralExpression&>(*expr.right);

        if (lhs.literal_type() == LiteralExpression::LiteralType::Boolean &&
            rhs.literal_type() == LiteralExpression::LiteralType::Boolean) {
            const bool result = is_and ? (lhs.boolean_value() && rhs.boolean_value())
                                       : (lhs.boolean_value() || rhs.boolean_value());
            api_.emit(result ? Op::True : Op::False, expr.location);
            return;
        }
    }

    api_.compile_expression(*expr.left);

    auto end_jump = api_.emit_jump(jump_op, expr.location);
    api_.emit(Op::Pop, expr.location);

    api_.compile_expression(*expr.right);

    api_.patch_jump(end_jump);
}

// ─────────── Binary expression compilation ───────────

void BinaryOperatorCompiler::compile_binary(const BinaryExpression& expr) {
    // Short-circuit operators.
    if (expr.op == TokenType::AmpersandAmpersand) {
        compile_short_circuit(expr, Op::JumpIfFalse, true);
        return;
    }

    if (expr.op == TokenType::PipePipe) {
        compile_short_circuit(expr, Op::JumpIfTrue, false);
        return;
    }

    // Null coalescing.
    if (expr.op == TokenType::QuestionQuestion) {
        api_.compile_expression(*expr.left);

        auto end_jump = api_.emit_jump(Op::NullCoalesce, expr.location);
        api_.emit(Op::Pop, expr.location);

        api_.compile_expression(*expr.right);

        api_.patch_jump(end_jump);

        return;
    }

    // Constant folding — both operands are compile-time literals.
    if (expr.left->kind == ExpressionKind::Literal && expr.right->kind == ExpressionKind::Literal) {
        if (api_.try_fold_binary_at_compile_time(static_cast<const LiteralExpression&>(*expr.left),
                                                 static_cast<const LiteralExpression&>(*expr.right),
                                                 expr.op, expr.location)) {
            return;
        }
    }

    // Regular binary operators — table-driven lookup.
    api_.compile_expression(*expr.left);
    // The left operand remains on the operand stack while the right is compiled,
    // so reserve a placeholder local; otherwise a value-producing block (match/if
    // used as an expression) on the right would compute local slots that ignore
    // the left temporary and corrupt the stack.
    {
        const ScratchSlotGuard scratch{api_, 1, expr.location};
        api_.compile_expression(*expr.right);
    }

    if (const auto opcode = binary_op_for_token(expr.op)) {
        api_.emit(*opcode, expr.location);
    } else {
        auto e = compiler_errors::unknown_binary_operator();
        api_.error(e.message, expr.location, e.hint);
    }
}

// ─────────── Compound assignment operators ───────────

void BinaryOperatorCompiler::emit_compound_op(TokenType op, SourceLocation loc) {
    if (const auto opcode = compound_op_for_token(op)) {
        api_.emit(*opcode, loc);
    } else {
        auto e = compiler_errors::unknown_compound_assignment_operator();
        api_.error(e.message, loc, e.hint);
    }
}

} // namespace luma
