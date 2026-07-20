#include <format>
#include <string_view>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/types/compile_time_arithmetic.hpp"
#include "analysis/types/statement_type_checker.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "analysis/types/type_info.hpp"

namespace luma {

namespace {

// Emit the "borrowed variable is read-only" error for a write attempt.
// `verb` is the action wording ("assign to" / "compound-assign to" /
// "increment" / "decrement") and `target_desc` names the written part —
// empty for a bare variable, "field of " or "element of " for a member or
// element write.  Centralises the message shared by every borrow-write site.
void report_borrow_readonly(TypeCheckingServices& tc, std::string_view verb,
                            std::string_view target_desc, std::string_view name,
                            const SourceLocation& loc) {
    tc.error(std::format("cannot {} {}borrowed variable '{}' — "
                         "borrow variables are read-only",
                         verb, target_desc, name),
             loc,
             "borrowed variables are read-only — use the original variable or make a "
             "mutable copy");
}

// Type-check an increment or decrement target.  `verb` is the action wording
// ("increment" / "decrement") and `op` the quoted operator ("'++'" / "'--'")
// used in diagnostics.  Shared by visit_increment and visit_decrement, whose
// logic differs only in that wording.
void check_inc_dec_target(TypeCheckingServices& tc, const Expression& target,
                          const SourceLocation& loc, std::string_view verb, std::string_view op) {
    if (target.kind != ExpressionKind::Variable) {
        tc.error(std::format("{} requires a variable", verb), loc,
                 std::format("{} can only be applied to a variable name", op));
        return;
    }

    const auto& var = static_cast<const VariableExpression&>(target);
    const auto* sym = tc.lookup_variable(var.name);

    if ((sym != nullptr) && !sym->is_mutable) {
        tc.error(std::format("cannot {} immutable variable '{}'", verb, var.name), loc,
                 "declare the variable with 'mutable' to allow mutation",
                 DiagnosticCode::ImmutableAssignment);
    }

    if ((sym != nullptr) && sym->is_borrow) {
        report_borrow_readonly(tc, verb, "", var.name, loc);
    }

    // Track mutation.
    tc.context().current_scope->mark_written(var.name);

    const auto type = tc.infer_assignment_target(target);

    (void)type_check_helpers::require_numeric_operand(tc, type, op, loc);
}

} // namespace

void StatementTypeChecker::check_target_mutability(const Expression& target,
                                                   const SourceLocation& loc, AssignmentKind kind) {
    const bool plain = kind == AssignmentKind::Plain;
    const std::string_view verb = plain ? "assign to" : "compound-assign to";

    if (target.kind == ExpressionKind::Variable) {
        const auto& var = static_cast<const VariableExpression&>(target);
        const auto* sym = tc_.lookup_variable(var.name);

        if ((sym != nullptr) && !sym->is_mutable) {
            // Plain assignment offers a quick-fix to insert the 'mutable' keyword.
            if (plain) {
                tc_.error(std::format("cannot {} immutable variable '{}'", verb, var.name), loc,
                          "declare the variable with 'mutable' to allow reassignment",
                          DiagnosticCode::ImmutableAssignment,
                          Fix::insert(loc, "mutable ", "add 'mutable' keyword"));
            } else {
                tc_.error(std::format("cannot {} immutable variable '{}'", verb, var.name), loc,
                          "declare the variable with 'mutable' to allow reassignment",
                          DiagnosticCode::ImmutableAssignment);
            }
        }

        if ((sym != nullptr) && sym->is_borrow) {
            report_borrow_readonly(tc_, verb, "", var.name, loc);
        }

        // A plain assignment overwrites the variable, so a consumed unique may
        // not be its target; compound assignment reads it and is checked
        // elsewhere.
        if (plain && (sym != nullptr) && sym->is_unique && sym->is_consumed) {
            tc_.error(
                std::format("cannot assign to consumed unique variable '{}'", var.name), loc,
                "unique values can only be used once — the variable has already been consumed");
        }

        // Track mutation for mutable-but-never-mutated warning.
        tc_.context().current_scope->mark_written(var.name);
    } else if (target.kind == ExpressionKind::FieldAccess) {
        // Walk the chain of field accesses to find the root variable.
        // Stop at IndexAccess — indexed element field mutation is allowed.
        if (const auto* var = root_variable_of(target, ChainTraversal::FieldsOnly)) {
            const auto* sym = tc_.lookup_variable(var->name);

            if ((sym != nullptr) && !sym->is_mutable) {
                tc_.error(
                    std::format("cannot {} field of immutable variable '{}'", verb, var->name), loc,
                    "declare the variable with 'mutable' to allow mutation",
                    DiagnosticCode::ImmutableAssignment);
            }

            if ((sym != nullptr) && sym->is_borrow) {
                report_borrow_readonly(tc_, verb, "field of ", var->name, loc);
            }

            tc_.context().current_scope->mark_written(var->name);
        }
    } else if (target.kind == ExpressionKind::IndexAccess) {
        // Walk through nested index/field accesses to find the root variable.
        if (const auto* var = root_variable_of(target, ChainTraversal::FieldsAndIndices)) {
            const auto* sym = tc_.lookup_variable(var->name);

            if ((sym != nullptr) && !sym->is_mutable) {
                if (plain) {
                    tc_.error(std::format("cannot assign to element of immutable variable '{}'",
                                          var->name),
                              loc, "declare the variable with 'mutable' to allow mutation",
                              DiagnosticCode::ImmutableAssignment);
                } else {
                    tc_.error(std::format("cannot compound-assign to element of immutable "
                                          "variable '{}' — declare with 'mutable' to allow",
                                          var->name),
                              loc, "declare the variable with 'mutable' to allow mutation",
                              DiagnosticCode::ImmutableAssignment);
                }
            }

            if ((sym != nullptr) && sym->is_borrow) {
                report_borrow_readonly(tc_, verb, "element of ", var->name, loc);
            }

            tc_.context().current_scope->mark_written(var->name);
        }
    } else if (plain) {
        tc_.error(
            "invalid assignment target", loc,
            "you can assign to variables, array elements, dictionary entries, and record fields");
    }
}

void StatementTypeChecker::visit_assignment(const AssignmentStatement& stmt) {
    // Check that target is mutable.
    check_target_mutability(*stmt.target, stmt.location, AssignmentKind::Plain);

    const auto target_type = tc_.infer_assignment_target(*stmt.target);
    const auto value_type = tc_.infer_expression_type(*stmt.value);

    // Warn on self-assignment (x = x).
    if (stmt.target->kind == ExpressionKind::Variable &&
        stmt.value->kind == ExpressionKind::Variable) {
        const auto& target_var = static_cast<const VariableExpression&>(*stmt.target);
        const auto& value_var = static_cast<const VariableExpression&>(*stmt.value);

        if (target_var.name == value_var.name) {
            tc_.warn("self-assignment has no effect", stmt.location,
                     "assigning a variable to itself does nothing", DiagnosticCode::SelfAssignment);
        }
    }

    (void)type_check_helpers::check_type_assignable(tc_, target_type, value_type,
                                                    "type mismatch in assignment", stmt.location);
}

void StatementTypeChecker::visit_compound_assignment(const CompoundAssignmentStatement& stmt) {
    check_compound_target_mutability(stmt);

    const auto target_type = tc_.infer_assignment_target(*stmt.target);
    const auto value_type = tc_.infer_expression_type(*stmt.value);

    // String concatenation with += is allowed.
    if (stmt.op == TokenType::PlusEquals && target_type.kind == TypeInfo::Kind::String &&
        value_type.kind == TypeInfo::Kind::String) {
        return;
    }

    check_compound_literal_folding(stmt);
    check_compound_operand_types(stmt, target_type, value_type);
}

void StatementTypeChecker::check_compound_target_mutability(
    const CompoundAssignmentStatement& stmt) {
    check_target_mutability(*stmt.target, stmt.location, AssignmentKind::Compound);
}

void StatementTypeChecker::check_compound_literal_folding(const CompoundAssignmentStatement& stmt) {
    // Constant folding: detect division by zero and shift out of range
    // when the right-hand side is a literal.
    if (stmt.value->kind == ExpressionKind::Literal) {
        if (const auto int_val = get_integer_value(*stmt.value)) {
            if ((stmt.op == TokenType::SlashEquals || stmt.op == TokenType::PercentEquals ||
                 stmt.op == TokenType::SlashSlashEquals) &&
                *int_val == 0) {
                tc_.error("division by zero", stmt.location,
                          "the divisor is always zero — this will crash at runtime",
                          DiagnosticCode::DivisionByZero);
            }

            if ((stmt.op == TokenType::LessLessEquals ||
                 stmt.op == TokenType::GreaterGreaterEquals) &&
                (*int_val < 0 || *int_val >= compile_time_arithmetic::k_max_shift_bits)) {
                tc_.error("shift amount out of range", stmt.location,
                          std::format("shift amount must be between 0 and {}",
                                      compile_time_arithmetic::k_max_shift_bits - 1),
                          DiagnosticCode::ShiftOutOfRange);
            }
        }

        if (is_number_literal(*stmt.value) &&
            (stmt.op == TokenType::SlashEquals || stmt.op == TokenType::PercentEquals) &&
            static_cast<const LiteralExpression&>(*stmt.value).number_value() == 0.0) {
            tc_.error("division by zero", stmt.location,
                      "the divisor is always zero — this will crash at runtime",
                      DiagnosticCode::DivisionByZero);
        }
    }
}

void StatementTypeChecker::check_compound_operand_types(const CompoundAssignmentStatement& stmt,
                                                        const TypeInfo& target_type,
                                                        const TypeInfo& value_type) {
    // Integer-only compound operators.
    const bool is_integer_only_op =
        stmt.op == TokenType::SlashSlashEquals || stmt.op == TokenType::AmpersandEquals ||
        stmt.op == TokenType::PipeEquals || stmt.op == TokenType::CaretEquals ||
        stmt.op == TokenType::LessLessEquals || stmt.op == TokenType::GreaterGreaterEquals;

    if (is_integer_only_op) {
        if (target_type.kind != TypeInfo::Kind::Integer &&
            target_type.kind != TypeInfo::Kind::StdlibAny &&
            target_type.kind != TypeInfo::Kind::Unknown) {
            tc_.error(std::format("compound assignment '{}' requires integer type, got '{}'",
                                  token_type_to_string(stmt.op), target_type.to_string()),
                      stmt.location,
                      "bitwise and integer division operators only work with integer values",
                      DiagnosticCode::InvalidOperand);
        }

        if (value_type.kind != TypeInfo::Kind::Integer &&
            value_type.kind != TypeInfo::Kind::StdlibAny &&
            value_type.kind != TypeInfo::Kind::Unknown) {
            tc_.error(std::format("compound assignment '{}' requires integer value, got '{}'",
                                  token_type_to_string(stmt.op), value_type.to_string()),
                      stmt.location, "convert the value to an integer using Converter.to_integer()",
                      DiagnosticCode::InvalidOperand);
        }

        return;
    }

    // Arithmetic compound assignment requires numeric types.
    (void)type_check_helpers::require_numeric_operand(
        tc_, target_type, std::format("compound assignment '{}'", token_type_to_string(stmt.op)),
        stmt.location);
    (void)type_check_helpers::require_numeric_operand(
        tc_, value_type,
        std::format("compound assignment '{}' value", token_type_to_string(stmt.op)),
        stmt.location);
}

void StatementTypeChecker::visit_increment(const IncrementStatement& stmt) {
    check_inc_dec_target(tc_, *stmt.target, stmt.location, "increment", "'++'");
}

void StatementTypeChecker::visit_decrement(const DecrementStatement& stmt) {
    check_inc_dec_target(tc_, *stmt.target, stmt.location, "decrement", "'--'");
}

void StatementTypeChecker::visit_return(const ReturnStatement& stmt) {
    const auto& ctx = tc_.context();
    if (!ctx.current_return_type) {
        tc_.error("return statement outside of function", stmt.location,
                  "return can only be used inside a function body");
        return;
    }

    if (stmt.value) {
        const auto value_type = tc_.infer_expression_type(*stmt.value);

        if (ctx.current_return_type->kind != TypeInfo::Kind::Void) {
            (void)type_check_helpers::check_type_assignable(
                tc_, *ctx.current_return_type, value_type, "return type mismatch", stmt.location);
        }
    } else {
        if (ctx.current_return_type->kind != TypeInfo::Kind::Void &&
            ctx.current_return_type->kind != TypeInfo::Kind::StdlibAny) {
            tc_.error(std::format("return without value in function expecting '{}'",
                                  ctx.current_return_type->to_string()),
                      stmt.location, "add a return value that matches the declared return type");
        }
    }
}

} // namespace luma
