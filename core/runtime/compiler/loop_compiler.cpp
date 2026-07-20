#include "runtime/compiler/loop_compiler.hpp"

#include <cstdint>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/hidden_var.hpp"
#include "runtime/compiler/i_compilation_backend.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

void LoopCompiler::compile_for(const ForStatement& stmt) {
    const ICompilationBackend::ScopeGuard outer_scope(api_);

    // Evaluate the iterable and initialize the iterator.
    api_.compile_expression(*stmt.iterable);
    api_.emit(Op::ForIterInit, stmt.location);

    // The iterator state is now on the stack as a local.
    auto iter_slot = api_.declare_local(to_string(HiddenVar::Iterator), true, stmt.location);

    // Determine whether we need key-value iteration (for dictionaries).
    const bool has_index = !stmt.index_variable.empty();
    const bool use_kv = has_index;
    const bool use_destructure = !stmt.destructure_variables.empty();

    // Declare the loop variable (will be assigned each iteration).
    std::uint16_t loop_variable_slot = 0;
    std::vector<std::uint16_t> destructure_slots;

    if (use_destructure) {
        // Declare a hidden element variable plus individual destructure vars.
        loop_variable_slot = declare_loop_variable(to_string(HiddenVar::Element), stmt.location);

        for (const auto& var : stmt.destructure_variables) {
            destructure_slots.push_back(declare_loop_variable(var, stmt.location));
        }
    } else {
        loop_variable_slot = declare_loop_variable(stmt.loop_variable, stmt.location);
    }

    // Declare the index variable if present.
    std::uint16_t index_slot = 0;

    if (has_index) {
        index_slot = declare_loop_variable(stmt.index_variable, stmt.location);
    }

    auto loop_start = api_.current_offset();

    api_.begin_loop(loop_start);

    ForIterationState state;
    state.iterator_slot = iter_slot;
    state.loop_variable_slot = loop_variable_slot;
    state.index_slot = index_slot;
    state.destructure_slots = destructure_slots;
    state.loop_start = loop_start;
    state.use_key_value = use_kv;

    compile_for_iteration(stmt, state);

    // Patch all break jumps and pop the loop.
    api_.end_loop();
}

void LoopCompiler::compile_for_iteration(const ForStatement& stmt, const ForIterationState& state) {
    api_.emit_u16(Op::GetLocal, state.iterator_slot, stmt.location);

    if (state.use_key_value) {
        // ForIterStepKV: pushes (value, key, true) or (false).
        api_.emit(Op::ForIterStepKV, stmt.location);
    } else {
        // ForIterStep: pushes (element, true) or (false) depending on exhaustion.
        api_.emit(Op::ForIterStep, stmt.location);
    }

    auto exit_jump = api_.emit_jump(Op::JumpIfFalse, stmt.location);
    api_.emit(Op::Pop, stmt.location); // Pop the true.

    if (state.use_key_value) {
        // Store key (top of stack) into index_variable.
        api_.emit_u16(Op::SetLocal, state.index_slot, stmt.location);
        api_.emit(Op::Pop, stmt.location);
    }

    // Store value/element into loop_variable.
    api_.emit_u16(Op::SetLocal, state.loop_variable_slot, stmt.location);
    api_.emit(Op::Pop, stmt.location);

    // Destructure the element into individual variables.
    if (!state.use_key_value && !state.destructure_slots.empty()) {
        for (std::size_t i = 0; i < state.destructure_slots.size(); ++i) {
            api_.emit_u16(Op::GetLocal, state.loop_variable_slot, stmt.location);
            api_.emit_constant(Value{static_cast<std::int64_t>(i)}, stmt.location);
            api_.emit(Op::IndexGet, stmt.location);
            api_.emit_u16(Op::SetLocal, state.destructure_slots[i], stmt.location);
            api_.emit(Op::Pop, stmt.location);
        }
    }

    // Compile the loop body.
    {
        const ICompilationBackend::ScopeGuard body_scope(api_);

        for (const auto& body_stmt : stmt.body) {
            api_.compile_statement(*body_stmt);
        }
    }

    // Loop back.
    api_.emit_loop(state.loop_start, stmt.location);

    // Patch the exit jump.
    api_.patch_jump(exit_jump);
    api_.emit(Op::Pop, stmt.location); // Pop the false from ForIterStep/ForIterStepKV.
}

void LoopCompiler::compile_while(const WhileStatement& stmt) {
    auto loop_start = api_.current_offset();

    api_.begin_loop(loop_start);

    api_.compile_expression(*stmt.condition);

    auto exit_jump = api_.emit_jump(Op::JumpIfFalse, stmt.location);
    api_.emit(Op::Pop, stmt.location); // Pop the condition.

    {
        const ICompilationBackend::ScopeGuard scope(api_);

        for (const auto& s : stmt.body) {
            api_.compile_statement(*s);
        }
    }

    api_.emit_loop(loop_start, stmt.location);

    api_.patch_jump(exit_jump);
    api_.emit(Op::Pop, stmt.location); // Pop the condition.

    // Patch break jumps and pop the loop.
    api_.end_loop();
}

std::uint16_t LoopCompiler::declare_loop_variable(std::string_view name,
                                                  const SourceLocation& loc) {
    api_.emit(Op::None, loc);
    return api_.declare_local(name, false, loc);
}

} // namespace luma
