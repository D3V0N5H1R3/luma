// vm_dispatch_control_flow.cpp — Control flow, function call, closure, pipe,
// and exception opcode handler methods.
//
// Extracted from vm_helpers.cpp / vm.cpp as part of the VM dispatch split.
// Contains:
//   - handle_tail_call, handle_call_named, handle_make_closure, forward_upvalue
//   - handle_constant_long, handle_get_upvalue, handle_set_upvalue (new)
//   - handle_get_global, handle_set_global (new)
//   - handle_null_coalesce, handle_error_pipe, handle_pipe (new)
//   - handle_try_catch, handle_loop (new)

#include <format>
#include <string>

#include "analysis/errors/error.hpp"
#include "common/resource_limits.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/environment.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

// ─────────── Constant loading ───────────

void VM::handle_constant_long([[maybe_unused]] const std::uint8_t* code_end) {
    auto index = read_u32();
    const auto& cf = stack_.frames.back();

    if (index >= cf.function->chunk().constants.size()) [[unlikely]] {
        runtime_error(
            vm_errors::constant_index_out_of_bounds(index, cf.function->chunk().constants.size()),
            VMStack::k_internal_error_message);
    }

    push(cf.function->chunk().constants[index]);
}

// ─────────── Variable access ───────────

// Upvalue strategy: the VM uses a dual-storage scheme for captured
// variables.  Immutable upvalues are copied by value into the
// `upvalues` vector at closure creation time — zero overhead on access.
// Mutable upvalues are stored through a shared_ptr "cell" in
// `upvalue_cells` so that mutations are visible to all closures
// sharing the variable.
//
// On every access the handler checks whether a cell exists for the
// given index.  This is a pointer-null check (effectively free on
// modern hardware) and avoids the need for separate opcodes for
// mutable vs immutable upvalues.  Splitting into separate opcodes
// would save one branch per access but would double the opcode
// surface and complicate the compiler for negligible gain — the
// branch predictor handles this pattern well since most upvalues
// are immutable.

void VM::validate_upvalue_index(const CallFrame& cf, std::size_t index) const {
    // Checked unconditionally — even for verified functions.  The verifier only
    // proves `index < upvalue_count` (the static descriptor count); it cannot
    // guarantee the runtime `FunctionValue::upvalues` vector was sized to match.
    // That invariant is established by handle_make_closure but bypassed by other
    // entry paths (e.g. run_top_level for top-level/@main/@test chunks), so a
    // crafted .lumc could otherwise drive an out-of-bounds access here.  The
    // check is a single predicted-not-taken compare — effectively free.
    if ((cf.closure == nullptr) || index >= cf.closure->upvalues.size()) [[unlikely]] {
        runtime_error(vm_errors::invalid_upvalue_index(index), VMStack::k_internal_error_message);
    }
}

// Returns a pointer to the closed-over cell value if the upvalue at
// `index` has been captured into a heap cell, or nullptr if it is
// still stored inline in the closure's upvalue array.
[[nodiscard]] static Value* get_upvalue_cell(const FunctionValue& closure, std::size_t index) {
    if (index < closure.upvalue_cells.size() && closure.upvalue_cells[index]) {
        return closure.upvalue_cells[index].get();
    }
    return nullptr;
}

void VM::handle_get_upvalue() {
    auto& cf = stack_.frames.back();
    auto index = read_u16();

    validate_upvalue_index(cf, index);

    if (auto* cell = get_upvalue_cell(*cf.closure, index)) {
        push(*cell);
    } else {
        push(cf.closure->upvalues[index]);
    }
}

void VM::handle_set_upvalue() {
    auto& cf = stack_.frames.back();
    auto index = read_u16();

    validate_upvalue_index(cf, index);

    if (auto* cell = get_upvalue_cell(*cf.closure, index)) {
        *cell = peek();
    } else {
        cf.closure->upvalues[index] = peek();
    }
}

void VM::handle_get_global() {
    auto name_idx = read_u16();
    const auto& name = checked_name(name_idx); // bounds-checks name_idx (security-critical)

    // Index-keyed fast path: the resolved Binding* is cached in the frame's
    // per-function vector, so only the first access to each name pays the
    // string hash + probe in lookup_global_cache().
    Binding*& slot = global_slot(name_idx);
    if (slot == nullptr) [[unlikely]] {
        slot = lookup_global_cache(name);
    }

    if (slot != nullptr) [[likely]] {
        push(slot->value);
    } else {
        runtime_error(vm_errors::undefined_variable(name), vm_errors::hint_check_variable_spelling);
    }
}

void VM::handle_set_global() {
    auto name_idx = read_u16();
    const auto& name = checked_name(name_idx);

    Binding*& slot = global_slot(name_idx);
    if (slot == nullptr) {
        slot = lookup_global_cache(name);
    }

    if (slot != nullptr) {
        if (!slot->is_mutable) {
            runtime_error(vm_errors::cannot_assign_immutable(name),
                          vm_errors::hint_declare_mutable);
        }

        slot->value = peek();
    } else {
        // First definition — goes through the Environment to insert the
        // binding, then we cache the new entry in both the string-keyed and
        // index-keyed caches.
        global_env_->define_or_assign(name, peek(), true, current_location());
        auto* new_binding = global_env_->find_binding(name);

        if (new_binding != nullptr) {
            global_cache_.insert_or_update(name, new_binding);
            slot = new_binding;
        }
    }

    notify_global_data_breakpoint(name);
}

// ─────────── Jump / Loop ───────────

void VM::handle_loop(const std::uint8_t* code_start) {
    auto& cf = stack_.frames.back();
    auto offset = read_u32();
    if (static_cast<std::size_t>(offset) > static_cast<std::size_t>(cf.ip - code_start))
        [[unlikely]] {
        runtime_error(vm_errors::loop_target_before_start);
    }
    if (cf.ip - offset < code_start) [[unlikely]] {
        runtime_error(vm_errors::loop_target_before_start);
    }
    cf.ip -= offset;
    if (++loop_iterations_ > ResourceLimits::max_while_iterations) [[unlikely]] {
        loop_iterations_ = 0;
        runtime_error(
            vm_errors::loop_exceeded_max_iterations(ResourceLimits::max_while_iterations));
    }
}

void VM::handle_null_coalesce(const std::uint8_t* code_end) {
    auto& cf = stack_.frames.back();
    auto offset = read_u32();
    auto& top = peek();

    if (top.is_result()) {
        const auto& result = top.as_result();

        if (result->is_success) {
            auto inner = *result->owned_inner;
            top = std::move(inner);
            cf.ip += offset;
            if (cf.ip > code_end) [[unlikely]] {
                runtime_error(vm_errors::null_coalesce_jump_beyond_bounds);
            }
        }
        // If failure, fall through to the Pop + RHS.
    } else if (!top.is_null()) {
        cf.ip += offset;
        if (cf.ip > code_end) [[unlikely]] {
            runtime_error(vm_errors::null_coalesce_jump_beyond_bounds);
        }
    }
}

// ─────────── Function calls ───────────

namespace {

/// Validates positional arg count against arity and fills the leading slots of args.
/// The type checker enforces this at the source level; this protects against
/// malformed bytecode.
void bind_positional_args(SmallVector<Value>& args, SmallVector<Value>& pos_args, int full_arity,
                          SourceLocation location) {
    if (static_cast<int>(pos_args.size()) > full_arity) [[unlikely]] {
        throw RuntimeError{vm_errors::arity_error(full_arity, static_cast<int>(pos_args.size())),
                           location};
    }

    for (int i = static_cast<int>(pos_args.size()) - 1; i >= 0; --i) {
        args[static_cast<std::size_t>(i)] = std::move(pos_args[static_cast<std::size_t>(i)]);
    }
}

/// Matches each named argument to its parameter slot via the function's
/// lazily-built param_name_index() hash map for O(1) lookups.
template <typename NamedPairs>
void bind_named_args(SmallVector<Value>& args, NamedPairs& named_pairs,
                     const CompiledFunction& func, SourceLocation location) {
    const auto& index_map = func.param_name_index();

    for (auto& [name, val] : named_pairs) {
        auto it = index_map.find(name);

        if (it != index_map.end()) [[likely]] {
            args[static_cast<std::size_t>(it->second)] = std::move(val);
        } else [[unlikely]] {
            throw RuntimeError{vm_errors::unknown_named_argument(name), location};
        }
    }
}

} // unnamed namespace

void VM::handle_call_named() {
    auto pos_count = read_byte();
    auto named_count = read_byte();
    auto total = pos_count + (named_count * 2);
    auto callee = peek(static_cast<std::size_t>(total));

    if (callee.is_function() && (callee.as_function()->compiled != nullptr)) {
        const auto* compiled = callee.as_function()->compiled;

        SmallVector<std::pair<std::string, Value>> named_pairs(named_count);

        for (int i = named_count - 1; i >= 0; --i) {
            auto val = pop();
            auto name_val = pop();
            // The compiler always emits argument names as string constants, but a
            // crafted .lumc could leave a non-string in the name slot; guard the
            // type so as_string_mut() cannot throw std::bad_variant_access.
            if (!name_val.is_string()) {
                runtime_error(vm_errors::invalid_named_call_operand);
            }
            named_pairs[static_cast<std::size_t>(i)] = {std::move(name_val.as_string_mut()),
                                                        std::move(val)};
        }

        auto pos_args = pop_sequence(static_cast<std::size_t>(pos_count));

        SmallVector<Value> args(static_cast<std::size_t>(compiled->arity));
        bind_positional_args(args, pos_args, compiled->arity, current_location());
        bind_named_args(args, named_pairs, *compiled, current_location());

        for (int i = 0; i < compiled->arity; ++i) {
            push(std::move(args[static_cast<std::size_t>(i)]));
        }

        call_value(callee, compiled->arity);
    } else {
        // NOTE: Named arguments for native functions are passed positionally;
        // native callables do not support named binding.
        SmallVector<Value> named_values(named_count);

        for (int i = named_count - 1; i >= 0; --i) {
            named_values[static_cast<std::size_t>(i)] = pop();
            (void)pop(); // discard name
        }

        auto pos_args = pop_sequence(static_cast<std::size_t>(pos_count));

        for (auto& a : pos_args) {
            push(std::move(a));
        }

        for (auto& a : named_values) {
            push(std::move(a));
        }

        call_value(callee, pos_count + named_count);
    }
}

void VM::handle_tail_call(const std::uint8_t*& code_start, const std::uint8_t*& code_end) {
    auto& cf = stack_.frames.back();
    int arg_count = read_byte();
    auto callee = peek(static_cast<std::size_t>(arg_count));

    if (callee.is_function() && (callee.as_function()->compiled != nullptr)) {
        const auto* compiled = callee.as_function()->compiled;

        if (arg_count < compiled->arity) {
            for (int i = arg_count; i < compiled->arity; ++i) {
                push(Value{});
            }

            arg_count = compiled->arity;
        }

        if (arg_count != compiled->arity) [[unlikely]] {
            runtime_error(vm_errors::tail_call_arity_mismatch(compiled->arity, arg_count));
        }

        auto base = cf.slot_offset;
        stack_.base[base] = callee;

        const auto count = static_cast<std::size_t>(arg_count);
        const auto src_base = stack_size() - count;

        for (std::size_t i = 0; i < count; ++i) {
            stack_.base[base + 1 + i] = stack_.base[src_base + i];
        }

        stack_.top = stack_.base + base + 1 + arg_count;

        // Reusing this frame for a different function invalidates its
        // per-function global inline cache.  cf.global_bindings points into
        // global_index_cache_[old function] and is sized to that function's
        // name table; the tail-called function has its own, generally larger,
        // name table.  Leaving the stale pointer in place would make the next
        // GetGlobal/SetGlobal index a too-small std::vector out of bounds
        // (global_slot only re-resolves when global_bindings is null),
        // corrupting memory and surfacing as nondeterministic native crashes.
        // Clear it so the next global access re-resolves it for `compiled`.  A
        // tail call to the same function keeps the cache (self-recursion fast
        // path).
        if (cf.function != compiled) {
            cf.global_bindings = nullptr;
        }

        cf.function = compiled;
        cf.ip = compiled->chunk().code.data();

        if (callee.is_function()) {
            cf.closure = callee.as_function().get();
        }

        code_start = cf.function->chunk().code.data();
        code_end = code_start + cf.function->chunk().code.size();
        cf.code_end = code_end;
    } else {
        call_value(callee, arg_count);
    }
}

// ─────────── Closure ───────────

void VM::forward_upvalue(FunctionValue& target, std::size_t index,
                         const CompiledFunction::Upvalue& desc, const CallFrame& cf) const {
    if (desc.is_local) {
        const auto slot = frame_slot_index(cf, desc.index);

        // Bounds-check the computed slot unconditionally, even for verified
        // functions.  The verifier only proves the GetUpvalue/SetUpvalue operands
        // stay within a function's upvalue_count; it never inspects the upvalue
        // *descriptor* array, and the deserializer accepts any u16 desc.index.
        // slot_offset is a runtime quantity, so a crafted .lumc could otherwise
        // drive an out-of-bounds stack read here (mirrors get_local_slot).
        if (slot >= stack_size()) [[unlikely]] {
            runtime_error(vm_errors::invalid_upvalue_index(desc.index),
                          VMStack::k_internal_error_message);
        }

        if (desc.is_mutable) {
            target.upvalue_cells[index] = std::make_shared<Value>(stack_.base[slot]);
        } else {
            target.upvalues[index] = stack_.base[slot];
        }
    } else {
        // Bound the descriptor index against the parent closure's upvalue array
        // before indexing it — same rationale as the local path: the verifier
        // does not check upvalue descriptors and the deserializer does not bound
        // desc.index, so a crafted .lumc could otherwise read past the parent's
        // upvalues (an out-of-bounds Value copy that manipulates a refcount
        // through a garbage pointer).
        if ((cf.closure == nullptr) || desc.index >= cf.closure->upvalues.size()) [[unlikely]] {
            runtime_error(vm_errors::invalid_upvalue_index(desc.index),
                          VMStack::k_internal_error_message);
        }

        if (desc.is_mutable && desc.index < cf.closure->upvalue_cells.size() &&
            cf.closure->upvalue_cells[desc.index]) {
            target.upvalue_cells[index] = cf.closure->upvalue_cells[desc.index];
        } else {
            target.upvalues[index] = cf.closure->upvalues[desc.index];
        }
    }
}

void VM::handle_make_closure() {
    auto& cf = stack_.frames.back();
    auto func_idx = read_u16();
    (void)read_byte(); // upvalue_count — read but not used directly

    if ((compiled_functions_ == nullptr) || func_idx >= compiled_functions_->size()) {
        runtime_error(vm_errors::invalid_function_index(func_idx));
    }

    const auto* compiled = &(*compiled_functions_)[func_idx];

    auto func_val = std::make_shared<FunctionValue>();
    func_val->name = compiled->name;
    func_val->compiled = compiled;

    func_val->upvalues.resize(compiled->upvalues.size());
    func_val->upvalue_cells.resize(compiled->upvalues.size());

    for (std::size_t i = 0; i < compiled->upvalues.size(); ++i) {
        forward_upvalue(*func_val, i, compiled->upvalues[i], cf);
    }

    push(Value{std::move(func_val)});
}

// ─────────── Pipe operators ───────────

void VM::handle_pipe() {
    auto callee = pop();
    auto value = pop();

    push(std::move(callee));
    push(std::move(value));
    call_value(peek(1), 1);
}

void VM::handle_error_pipe() {
    auto callee = pop();
    auto value = pop();

    if (value.is_result()) {
        const auto& result = value.as_result();

        if (result->is_success) {
            push(std::move(callee));
            push(*result->owned_inner);
            call_value(peek(1), 1);
        } else {
            push(std::move(value)); // Short-circuit failure.
        }
    } else {
        push(std::move(callee));
        push(std::move(value));
        call_value(peek(1), 1);
    }
}

// ─────────── Exception handling ───────────

void VM::handle_try_catch(const std::uint8_t* code_end) {
    auto catch_offset = read_u32();
    auto& current_frame = stack_.frames.back();
    const auto* target_ip = current_frame.ip + catch_offset;
    if (target_ip > code_end) [[unlikely]] {
        runtime_error(vm_errors::try_catch_beyond_end);
    }
    ExceptionHandler handler;
    handler.catch_ip = target_ip;
    handler.frame_index = stack_.frames.size() - 1;
    handler.stack_depth = stack_size();
    handler.task_scope_depth = task_manager_.task_scopes.size();
    exceptions_.push_handler(handler);
}

} // namespace luma
