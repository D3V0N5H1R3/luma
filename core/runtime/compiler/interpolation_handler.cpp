// ─────────────────────────────────────────────────────────────────────────────
// InterpolationHandler — implementation
// ─────────────────────────────────────────────────────────────────────────────

#include "runtime/compiler/interpolation_handler.hpp"

#include "runtime/compiler/compiler_errors.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/i_compilation_backend.hpp"

namespace luma {

// ─── Part merging ────────────────────────────────────────────────────────────

std::vector<InterpolationHandler::MergedPart>
InterpolationHandler::merge_parts(const StringInterpolationExpression& expr) {
    std::vector<MergedPart> merged;

    for (std::size_t i = 0; i < expr.parts.size(); ++i) {
        if (!expr.parts[i].empty()) {
            // Try to merge with previous literal part.
            if (!merged.empty() && merged.back().is_literal) {
                merged.back().literal_value += expr.parts[i];
            } else {
                merged.push_back(
                    {.is_literal = true, .literal_value = expr.parts[i], .expression_index = 0});
            }
        }

        if (i < expr.expressions.size()) {
            // Check if this expression is a string literal — if so, fold it
            // into the surrounding text at compile time.
            const auto& sub_expr = *expr.expressions[i];
            if (sub_expr.kind == ExpressionKind::Literal) {
                const auto& lit = static_cast<const LiteralExpression&>(sub_expr);
                if (lit.literal_type() == LiteralExpression::LiteralType::String) {
                    if (!merged.empty() && merged.back().is_literal) {
                        merged.back().literal_value += lit.string_value();
                    } else {
                        merged.push_back({.is_literal = true,
                                          .literal_value = lit.string_value(),
                                          .expression_index = 0});
                    }
                    continue;
                }
            }
            merged.push_back({.is_literal = false, .literal_value = {}, .expression_index = i});
        }
    }

    return merged;
}

// ─── Bytecode emission ──────────────────────────────────────────────────────

void InterpolationHandler::compile(const StringInterpolationExpression& expr) {
    const auto merged = merge_parts(expr);

    // Emit the merged parts.
    std::size_t part_count = 0;

    // Each emitted part stays on the operand stack until Interpolate consumes
    // them all, so reserve a placeholder local for every part beneath the one
    // being compiled. This keeps a value-producing block (match/if used as an
    // expression) part's local slots aligned with its true runtime position.
    std::size_t scratch = 0;

    for (const auto& part : merged) {
        if (part.is_literal) {
            if (!part.literal_value.empty()) {
                if (part_count > 0) {
                    api_.reserve_scratch_slots(1, expr.location);
                    ++scratch;
                }
                api_.emit_constant(Value{part.literal_value}, expr.location);
                ++part_count;
            }
        } else {
            if (part_count > 0) {
                api_.reserve_scratch_slots(1, expr.location);
                ++scratch;
            }
            api_.compile_expression(*expr.expressions[part.expression_index]);
            ++part_count;
        }
    }

    if (scratch > 0) {
        api_.release_scratch_slots(scratch);
    }

    if (part_count > CompilerLimits::k_max_arguments) {
        auto e = compiler_errors::too_many_interpolation_parts(CompilerLimits::k_max_arguments);
        api_.error(e.message, expr.location, e.hint);
        return;
    }

    // Special case: if all parts merged into a single string constant,
    // we don't need Interpolate at all — the constant is already on the stack.
    if (merged.size() == 1 && merged[0].is_literal) {
        return;
    }

    api_.emit_u8(Op::Interpolate, static_cast<std::uint8_t>(part_count), expr.location);
}

} // namespace luma
