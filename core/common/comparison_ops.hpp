// comparison_ops.hpp — Shared comparison operation helpers.
//
// Provides:
//   - apply_comparison<T>(l, r, Op) → bool    runtime comparison dispatch
//   - token_to_comparison_op(TokenType) → Op   token-to-opcode mapping
//
// Lives in common/ because it bridges two otherwise independent modules:
//   - analysis/lexer (TokenType) — the front-end token classification
//   - runtime/compiler (Op)      — the back-end VM opcode set
// Both the compiler and the VM need these mappings, so placing them in either
// module would create a reverse dependency.  The coupling is minimal: only
// two enum types are referenced, and all functions are constexpr.

#ifndef LUMA_COMMON_COMPARISON_OPS_HPP
#define LUMA_COMMON_COMPARISON_OPS_HPP

#include <cassert>
#include <concepts>
#include <optional>

#include "analysis/lexer/token_type.hpp"
#include "runtime/compiler/opcode.hpp"

namespace luma {

// Evaluate a comparison on two values given the comparison opcode.
// Supports all six comparison operators: < <= > >= == !=.
template <std::totally_ordered T>
[[nodiscard]] constexpr bool apply_comparison(const T& left, const T& right, Op op) {
    switch (op) {
        case Op::Less:
            return left < right;
        case Op::LessEqual:
            return left <= right;
        case Op::Greater:
            return left > right;
        case Op::GreaterEqual:
            return left >= right;
        case Op::Equal:
            return left == right;
        case Op::NotEqual:
            return left != right;
        default:
            assert(false && "apply_comparison: invalid comparison opcode");
            return false;
    }
}

// Map a comparison token type to its corresponding VM opcode.
[[nodiscard]] constexpr std::optional<Op> token_to_comparison_op(TokenType token) {
    switch (token) {
        case TokenType::EqualsEquals:
            return Op::Equal;
        case TokenType::BangEquals:
            return Op::NotEqual;
        case TokenType::Less:
            return Op::Less;
        case TokenType::LessEquals:
            return Op::LessEqual;
        case TokenType::Greater:
            return Op::Greater;
        case TokenType::GreaterEquals:
            return Op::GreaterEqual;
        default:
            return std::nullopt;
    }
}

} // namespace luma

#endif // LUMA_COMMON_COMPARISON_OPS_HPP
