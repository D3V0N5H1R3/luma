// ─────────────────────────────────────────────────────────────────────────────
// Assignment compilation — Compiler method implementations
// ─────────────────────────────────────────────────────────────────────────────
// Extracted from statement_compiler.cpp: simple assignment, compound assignment,
// and unary mutation (increment/decrement) for all target kinds (variable,
// field access, index access).
// ─────────────────────────────────────────────────────────────────────────────

#include <cassert>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/assignment_target_dispatch.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/scratch_slot_guard.hpp"

namespace luma {

// ─────────── Shared variable-store helper ───────────

void Compiler::emit_variable_store(const VarSlot& resolved, bool use_set_local_pop,
                                   SourceLocation loc) {
    switch (resolved.location) {
        case VarLocation::Local: {
            const bool is_number =
                static_cast<std::size_t>(resolved.slot) < current_scope().locals.size() &&
                current_scope().locals[resolved.slot].is_number_type;
            emit_number_widening_if_needed(is_number, loc);

            if (use_set_local_pop) {
                emit_u16(Op::SetLocalPop, resolved.slot, loc);
                return; // SetLocalPop already discards; no trailing Pop needed.
            }

            emit_u16(Op::SetLocal, resolved.slot, loc);
            break;
        }

        case VarLocation::Upvalue:
            emit_u16(Op::SetUpvalue, resolved.slot, loc);
            break;

        case VarLocation::Global:
            emit_u16(Op::SetGlobal, resolved.slot, loc);
            break;
    }

    emit(Op::Pop, loc);
}

// ─────────── Shared mutation emit helpers ───────────

// Emit SetField + Pop — used by simple, compound, and unary field assignments.
void Compiler::emit_field_set_pop(std::uint16_t name_idx, SourceLocation loc) {
    emit_u16(Op::SetField, name_idx, loc);
    emit(Op::Pop, loc);
}

// Emit IndexSet + Pop — used by simple, compound, and unary index assignments.
void Compiler::emit_index_set_pop(SourceLocation loc) {
    emit(Op::IndexSet, loc);
    emit(Op::Pop, loc);
}

// Reserve placeholder local slots for live operand-stack temporaries so that a
// value expression which declares locals mid-compilation (e.g. a match/if value
// block) receives slot indices matching its true runtime stack position. The
// empty name is skipped by duplicate detection and is never resolved as a user
// variable, so several scratch slots can coexist safely.
void Compiler::reserve_scratch_slots(std::size_t count, SourceLocation loc) {
    for (std::size_t i = 0; i < count; ++i) {
        static_cast<void>(declare_local("", /*is_mutable=*/false, loc));
    }
}

// Drop scratch slots reserved by reserve_scratch_slots. The placeholders are the
// topmost locals once the value expression has balanced its own scope, so they
// are removed by truncation without emitting runtime pops — the corresponding
// operand-stack temporaries are consumed by the trailing IndexSet/SetField.
void Compiler::release_scratch_slots(std::size_t count) {
    auto& locals = current_scope().locals;

    for (std::size_t i = 0; i < count && !locals.empty(); ++i) {
        locals.pop_back();
    }
}

// ─────────── Simple assignment target helpers ───────────

void Compiler::compile_assign_to_variable(const VariableExpression& var, const Expression& value,
                                          SourceLocation loc) {
    compile_expression(value);
    const auto resolved = resolve_variable(var.name, loc);
    emit_variable_store(resolved, /*use_set_local_pop=*/true, loc);
}

void Compiler::compile_assign_to_field(const FieldAccessExpression& field, const Expression& value,
                                       SourceLocation loc) {
    compile_expression(*field.object);
    // The object sits on the operand stack while the value is compiled; reserve
    // a placeholder slot so a value-producing block computes correct local slots.
    {
        const ScratchSlotGuard scratch{access_, 1, loc};
        compile_expression(value);
    }
    // Stack: [object, value] — matches SetField pop order (value, obj).

    auto name_idx = add_name(field.field_name);
    emit_field_set_pop(name_idx, loc);
}

void Compiler::compile_assign_to_index(const IndexAccessExpression& index, const Expression& value,
                                       SourceLocation loc) {
    compile_expression(*index.object);
    compile_expression(*index.index);
    // The container and index sit on the operand stack while the value is
    // compiled; reserve placeholder slots so a value-producing block (match/if
    // used as an expression) computes correct local slots.
    {
        const ScratchSlotGuard scratch{access_, 2, loc};
        compile_expression(value);
    }
    // Stack: [container, index, value] — matches IndexSet pop order.
    emit_index_set_pop(loc);
}

// ─────────── Compound assignment target helpers ───────────

void Compiler::compile_compound_assign_to_variable(const VariableExpression& var,
                                                   const Expression& value, TokenType op,
                                                   SourceLocation loc) {
    compile_compound_op_common([&] { compile_variable(var); },
                               [&] {
                                   const auto resolved = resolve_variable(var.name, loc);
                                   emit_variable_store(resolved, /*use_set_local_pop=*/false, loc);
                               },
                               value, op, loc, /*temps_beneath=*/1);
}

void Compiler::compile_compound_assign_to_field(const FieldAccessExpression& field,
                                                const Expression& value, TokenType op,
                                                SourceLocation loc) {
    compile_expression(*field.object);
    emit(Op::Dup, loc);
    auto name_idx = add_name(field.field_name);

    compile_compound_op_common([&] { emit_u16(Op::GetField, name_idx, loc); },
                               [&] { emit_field_set_pop(name_idx, loc); }, value, op, loc,
                               /*temps_beneath=*/2);
}

void Compiler::compile_compound_assign_to_index(const IndexAccessExpression& index,
                                                const Expression& value, TokenType op,
                                                SourceLocation loc) {
    compile_expression(*index.object);
    compile_expression(*index.index);
    emit(Op::Dup2, loc);

    compile_compound_op_common([&] { emit(Op::IndexGet, loc); }, [&] { emit_index_set_pop(loc); },
                               value, op, loc, /*temps_beneath=*/3);
}

// ─────────── Assignment dispatch ───────────

void Compiler::compile_assignment(const AssignmentStatement& stmt) {
    dispatch_assignment_target(
        *stmt.target,
        [&](const VariableExpression& var) {
            compile_assign_to_variable(var, *stmt.value, stmt.location);
        },
        [&](const FieldAccessExpression& field) {
            compile_assign_to_field(field, *stmt.value, stmt.location);
        },
        [&](const IndexAccessExpression& index) {
            compile_assign_to_index(index, *stmt.value, stmt.location);
        });
}

void Compiler::compile_compound_assignment(const CompoundAssignmentStatement& stmt) {
    dispatch_assignment_target(
        *stmt.target,
        [&](const VariableExpression& var) {
            compile_compound_assign_to_variable(var, *stmt.value, stmt.op, stmt.location);
        },
        [&](const FieldAccessExpression& field) {
            compile_compound_assign_to_field(field, *stmt.value, stmt.op, stmt.location);
        },
        [&](const IndexAccessExpression& index) {
            compile_compound_assign_to_index(index, *stmt.value, stmt.op, stmt.location);
        });
}

// ─────────── Unary mutation (increment/decrement) ───────────

void Compiler::compile_unary_mutation(const Expression& target, Op local_op, Op op,
                                      SourceLocation loc) {
    dispatch_assignment_target(
        target,
        [&](const VariableExpression& var) {
            const auto resolved = resolve_variable(var.name, loc);

            if (resolved.location == VarLocation::Local) {
                emit_u16(local_op, resolved.slot, loc);
            } else {
                compile_variable(var);
                emit(op, loc);

                switch (resolved.location) {
                    case VarLocation::Local:
                        // Already handled by the fast path above.
                        assert(false && "compile_unary_mutation: Local should be handled above");
                        break;

                    case VarLocation::Upvalue:
                        emit_u16(Op::SetUpvalue, resolved.slot, loc);
                        break;

                    case VarLocation::Global:
                        emit_u16(Op::SetGlobal, resolved.slot, loc);
                        break;
                }

                emit(Op::Pop, loc);
            }
        },
        [&](const FieldAccessExpression& field) {
            compile_expression(*field.object);
            emit(Op::Dup, loc);
            auto name_idx = add_name(field.field_name);
            emit_u16(Op::GetField, name_idx, loc);
            emit(op, loc);
            emit_field_set_pop(name_idx, loc);
        },
        [&](const IndexAccessExpression& index) {
            compile_expression(*index.object);
            compile_expression(*index.index);
            emit(Op::Dup2, loc);
            emit(Op::IndexGet, loc);
            emit(op, loc);
            emit_index_set_pop(loc);
        });
}

} // namespace luma
