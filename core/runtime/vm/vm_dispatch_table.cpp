// vm_dispatch_table.cpp — Function pointer dispatch table and opcode handlers.
//
// Defines the static dispatch table that maps each opcode index to a
// void (VM::*)() handler method.  The main dispatch loop in vm.cpp indexes
// this table with the raw opcode byte and calls the resulting method pointer.
//
// Each op_*() method corresponds to exactly one opcode.  Methods that merely
// delegate to an existing handle_*() helper keep that call here so the
// handle_*() helpers remain available for non-dispatch callers (e.g. the
// TailCall handler which updates code_start / code_end by reference).
//
// Return-signaling convention:
//   op_return(), op_get_local_return(), and op_end_module() store their
//   result in dispatch_return_value_.  The dispatch loop in run() checks
//   this optional after every call and exits immediately when it has a value.

#include <format>
#include <limits>
#include <string>
#include <utility>

#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

// ─── Dispatch table ──────────────────────────────────────────────────────────
//
// X-macro mapping each opcode to its op_*() handler.  Adding a new opcode
// requires one new entry here — the build function maps by enum value (not
// by position), so entry order does not matter and reserved slots are
// automatically filled with op_invalid.
//
// Macro signature: X(OpEnumName, handler_method)

// clang-format off
#define LUMA_DISPATCH_ENTRIES(X)                   \
    X(Constant,        op_constant)                \
    X(ConstantLong,    op_constant_long)            \
    X(Pop,             op_pop)                      \
    X(Dup,             op_dup)                      \
    X(Dup2,            op_dup2)                     \
    X(Swap,            op_swap)                     \
    X(GetLocal,        op_get_local)                \
    X(SetLocal,        op_set_local)                \
    X(GetUpvalue,      op_get_upvalue)              \
    X(SetUpvalue,      op_set_upvalue)              \
    X(GetGlobal,       op_get_global)               \
    X(SetGlobal,       op_set_global)               \
    X(None,            op_none)                     \
    X(True,            op_true)                     \
    X(False,           op_false)                    \
    X(Zero,            op_zero)                     \
    X(One,             op_one)                      \
    X(Add,             op_add)                      \
    X(Subtract,        op_subtract)                 \
    X(Multiply,        op_multiply)                 \
    X(Divide,          op_divide)                   \
    X(IntDivide,       op_int_divide)               \
    X(Modulo,          op_modulo)                   \
    X(Negate,          op_negate)                   \
    X(Increment,       op_increment)                \
    X(Decrement,       op_decrement)                \
    X(Equal,           op_equal)                    \
    X(NotEqual,        op_not_equal)                \
    X(Less,            op_less)                     \
    X(LessEqual,       op_less_equal)               \
    X(Greater,         op_greater)                  \
    X(GreaterEqual,    op_greater_equal)             \
    X(Not,             op_not)                      \
    X(And,             op_and)                      \
    X(Or,              op_or)                       \
    X(BitwiseAnd,      op_bitwise_and)              \
    X(BitwiseOr,       op_bitwise_or)               \
    X(BitwiseXor,      op_bitwise_xor)              \
    X(BitwiseNot,      op_bitwise_not)              \
    X(ShiftLeft,       op_shift_left)               \
    X(ShiftRight,      op_shift_right)              \
    X(Concatenate,     op_concatenate)              \
    X(Interpolate,     op_interpolate)              \
    X(MakeArray,       op_make_array)               \
    X(MakeDict,        op_make_dict)                \
    X(MakeTuple,       op_make_tuple)               \
    X(MakeRange,       op_make_range)               \
    X(MakeRangeInc,    op_make_range_inc)            \
    X(IndexGet,        op_index_get)                \
    X(IndexSet,        op_index_set)                \
    X(IndexGetOpt,     op_index_get_opt)             \
    X(MakeRecord,      op_make_record)              \
    X(GetField,        op_get_field)                \
    X(SetField,        op_set_field)                \
    X(GetFieldOpt,     op_get_field_opt)             \
    X(RecordWith,      op_record_with)              \
    X(MakeChoice,      op_make_choice)              \
    X(MakeChoiceConstructor, op_make_choice_constructor) \
    X(MakeSuccess,     op_make_success)              \
    X(MakeFailure,     op_make_failure)              \
    X(MakeSome,        op_make_some)                \
    X(Unwrap,          op_unwrap)                   \
    X(ResultInner,     op_result_inner)              \
    X(IsSuccess,       op_is_success)               \
    X(IsSome,          op_is_some)                  \
    X(EnsureSuccess,   op_ensure_success)            \
    X(Downcast,        op_downcast)                 \
    X(TrustedDowncast, op_trusted_downcast)          \
    X(IsType,          op_is_type)                  \
    X(Jump,            op_jump)                     \
    X(JumpIfFalse,     op_jump_if_false)             \
    X(JumpIfTrue,      op_jump_if_true)              \
    X(Loop,            op_loop)                     \
    X(NullCoalesce,    op_null_coalesce)             \
    X(Call,            op_call)                     \
    X(CallNamed,       op_call_named)               \
    X(TailCall,        op_tail_call)                \
    X(Return,          op_return)                   \
    X(MakeClosure,     op_make_closure)              \
    X(Pipe,            op_pipe)                     \
    X(ErrorPipe,       op_error_pipe)               \
    X(TryCatch,        op_try_catch)                \
    X(TryEnd,          op_try_end)                  \
    X(Rethrow,         op_rethrow)                  \
    X(MatchStart,      op_match_start)              \
    X(MatchArm,        op_match_arm)                \
    X(MatchEnd,        op_match_end)                \
    X(Contains,        op_contains)                 \
    X(Spawn,           op_spawn)                    \
    X(Await,           op_await)                    \
    X(TaskScopeBegin,  op_task_scope_begin)          \
    X(TaskScopeEnd,    op_task_scope_end)            \
    X(ForIterInit,     op_for_iter_init)             \
    X(ForIterStep,     op_for_iter_step)             \
    X(ForIterStepKV,   op_for_iter_step_kv)          \
    X(Print,           op_print)                    \
    X(Assert,          op_assert)                   \
    X(TypeOf,          op_type_of)                  \
    X(IncrementLocal,  op_increment_local)           \
    X(DecrementLocal,  op_decrement_local)           \
    X(SetLocalPop,     op_set_local_pop)             \
    X(GetLocalReturn,  op_get_local_return)          \
    X(IntToNumber,     op_int_to_number)             \
    X(Clone,           op_clone)                    \
    X(EndModule,       op_end_module)
// clang-format on

constexpr std::array<VM::DispatchFn, k_dispatch_table_size> VM::build_dispatch_table() noexcept {
    std::array<DispatchFn, k_dispatch_table_size> table{};
    for (auto& entry : table) {
        entry = &VM::op_invalid;
    }
#define LUMA_REGISTER_OP(op_name, handler)                                                         \
    table[static_cast<std::size_t>(Op::op_name)] = &VM::handler;
    LUMA_DISPATCH_ENTRIES(LUMA_REGISTER_OP)
#undef LUMA_REGISTER_OP
    return table;
}

// build_dispatch_table() is constexpr, but MSVC does not treat
// pointer-to-member initialisers as constant expressions, so we
// cannot use constexpr/constinit on the variable itself.
const std::array<VM::DispatchFn, k_dispatch_table_size> VM::k_dispatch_table =
    VM::build_dispatch_table();

// Verify at compile time that every defined opcode has a dedicated dispatch
// entry.  Unlike k_dispatch_table — whose pointer-to-member initialisers MSVC
// refuses to treat as constant expressions — this check uses only integer
// opcode indices, so it is fully constexpr on every compiler.  An opcode added
// to the Op enum without a matching LUMA_DISPATCH_ENTRIES row leaves its slot
// false and trips this assertion, catching the omission at build time instead
// of as an op_invalid "unknown opcode" trap at run time.
constexpr std::array<bool, opcode_enum_count> dispatch_entry_coverage() noexcept {
    std::array<bool, opcode_enum_count> covered{};
#define LUMA_MARK_OP(op_name, handler) covered[static_cast<std::size_t>(Op::op_name)] = true;
    LUMA_DISPATCH_ENTRIES(LUMA_MARK_OP)
#undef LUMA_MARK_OP
    return covered;
}

static_assert(
    [] {
        constexpr auto coverage = dispatch_entry_coverage();
        for (const bool mapped : coverage) {
            if (!mapped) {
                return false;
            }
        }
        return true;
    }(),
    "Every opcode in [0, opcode_enum_count) must have a LUMA_DISPATCH_ENTRIES handler — "
    "an unmapped opcode would silently dispatch to op_invalid");

#undef LUMA_DISPATCH_ENTRIES

// Verify that EndModule is still the last defined opcode — if a new opcode
// was added to the Op enum without a matching LUMA_DISPATCH_ENTRIES entry,
// this static_assert fires.
static_assert(static_cast<std::size_t>(Op::EndModule) + 1 == opcode_enum_count,
              "Op::EndModule value changed — update LUMA_DISPATCH_ENTRIES");

// ─── Error handler ───────────────────────────────────────────────────────────

void VM::op_invalid() {
    runtime_error(vm_errors::unknown_opcode, VMStack::k_internal_error_message);
}

// ─── Stack manipulation ───────────────────────────────────────────────────────

void VM::op_constant() {
    auto& cf = stack_.frames.back();
    auto index = read_u16();

    if (!cf.function->is_verified() && index >= cf.function->chunk().constants.size())
        [[unlikely]] {
        runtime_error(
            vm_errors::constant_index_out_of_bounds(index, cf.function->chunk().constants.size()),
            VMStack::k_internal_error_message);
    }

    push(cf.function->chunk().constants[index]);
}

void VM::op_constant_long() {
    handle_constant_long(current_code_end());
}

void VM::op_pop() {
    (void)pop();
}

void VM::op_dup() {
    validate_stack_depth(1, "Dup");
    push(*(stack_.top - 1));
}

void VM::op_dup2() {
    validate_stack_depth(2, "Dup2");
    auto b = *(stack_.top - 2);
    auto a = *(stack_.top - 1);
    push(std::move(b));
    push(std::move(a));
}

void VM::op_swap() {
    validate_stack_depth(2, "Swap");
    auto& a = *(stack_.top - 1);
    auto& b = *(stack_.top - 2);
    std::swap(a, b);
}

// ─── Local variables ──────────────────────────────────────────────────────────

void VM::op_get_local() {
    auto& cf = stack_.frames.back();
    auto slot = read_u16();
    push(get_local_slot(cf, slot));
}

void VM::op_set_local() {
    auto& cf = stack_.frames.back();
    auto slot = read_u16();
    get_local_slot(cf, slot) = peek();

    notify_local_data_breakpoint(cf, slot);
}

void VM::op_get_upvalue() {
    handle_get_upvalue();
}

void VM::op_set_upvalue() {
    handle_set_upvalue();
}

void VM::op_get_global() {
    handle_get_global();
}

void VM::op_set_global() {
    handle_set_global();
}

// ─── Literals ─────────────────────────────────────────────────────────────────

void VM::op_none() {
    push(Value{});
}

void VM::op_true() {
    push(Value{true});
}

void VM::op_false() {
    push(Value{false});
}

void VM::op_zero() {
    push(Value{static_cast<std::int64_t>(0)});
}

void VM::op_one() {
    push(Value{static_cast<std::int64_t>(1)});
}

// ─── Arithmetic ───────────────────────────────────────────────────────────────

void VM::op_add() {
    auto [a, b] = pop_binary_ref();
    a = numeric_binary_op(a, b, Op::Add);
}

void VM::op_subtract() {
    auto [a, b] = pop_binary_ref();
    a = numeric_binary_op(a, b, Op::Subtract);
}

void VM::op_multiply() {
    auto [a, b] = pop_binary_ref();
    a = numeric_binary_op(a, b, Op::Multiply);
}

void VM::op_divide() {
    handle_divide();
}

void VM::op_int_divide() {
    handle_int_divide();
}

void VM::op_modulo() {
    handle_modulo();
}

void VM::op_negate() {
    validate_stack_depth(1, "Negate");
    auto& top = *(stack_.top - 1);
    handle_unary_numeric(top, safe_negate, [](double n) -> Value { return Value{-n}; }, "Negate");
}

void VM::op_increment() {
    validate_stack_depth(1, "Increment");
    auto& top = *(stack_.top - 1);
    handle_unary_numeric(
        top, safe_increment, [](double n) -> Value { return Value{n + 1.0}; }, "Increment");
}

void VM::op_decrement() {
    validate_stack_depth(1, "Decrement");
    auto& top = *(stack_.top - 1);
    handle_unary_numeric(
        top, safe_decrement, [](double n) -> Value { return Value{n - 1.0}; }, "Decrement");
}

// ─── Comparison ───────────────────────────────────────────────────────────────

void VM::op_equal() {
    auto [a, b] = pop_binary_ref();
    a = Value{a.equals(b)};
}

void VM::op_not_equal() {
    auto [a, b] = pop_binary_ref();
    a = Value{!a.equals(b)};
}

void VM::op_less() {
    auto [a, b] = pop_binary_ref();
    a = compare_values(a, b, Op::Less);
}

void VM::op_less_equal() {
    auto [a, b] = pop_binary_ref();
    a = compare_values(a, b, Op::LessEqual);
}

void VM::op_greater() {
    auto [a, b] = pop_binary_ref();
    a = compare_values(a, b, Op::Greater);
}

void VM::op_greater_equal() {
    auto [a, b] = pop_binary_ref();
    a = compare_values(a, b, Op::GreaterEqual);
}

// ─── Logical ──────────────────────────────────────────────────────────────────

void VM::op_not() {
    validate_stack_depth(1, "Not");
    auto& top = *(stack_.top - 1);
    top = Value{!top.is_truthy()};
}

void VM::op_and() {
    auto [a, b] = pop_binary_ref();
    a = Value{a.is_truthy() && b.is_truthy()};
}

void VM::op_or() {
    auto [a, b] = pop_binary_ref();
    a = Value{a.is_truthy() || b.is_truthy()};
}

// ─── Bitwise ──────────────────────────────────────────────────────────────────

void VM::op_bitwise_and() {
    apply_integer_binary_op("Bitwise AND", [](std::int64_t x, std::int64_t y) { return x & y; });
}

void VM::op_bitwise_or() {
    apply_integer_binary_op("Bitwise OR", [](std::int64_t x, std::int64_t y) { return x | y; });
}

void VM::op_bitwise_xor() {
    apply_integer_binary_op("Bitwise XOR", [](std::int64_t x, std::int64_t y) { return x ^ y; });
}

void VM::op_bitwise_not() {
    validate_stack_depth(1, "BitwiseNot");
    auto& top = *(stack_.top - 1);
    validate_integer_operand(top, "Bitwise NOT");
    top = Value{~top.as_integer()};
}

void VM::op_shift_left() {
    apply_shift_op("Shift left", [](std::int64_t x, std::int64_t y) { return x << y; });
}

void VM::op_shift_right() {
    apply_shift_op("Shift right", [](std::int64_t x, std::int64_t y) { return x >> y; });
}

// ─── Strings ──────────────────────────────────────────────────────────────────

void VM::op_concatenate() {
    handle_concatenate();
}

void VM::op_interpolate() {
    handle_interpolate();
}

// ─── Collections ──────────────────────────────────────────────────────────────

void VM::op_make_array() {
    handle_make_array();
}

void VM::op_make_dict() {
    handle_make_dict();
}

void VM::op_make_tuple() {
    handle_make_tuple();
}

void VM::op_make_range() {
    make_range(false);
}

void VM::op_make_range_inc() {
    make_range(true);
}

void VM::op_index_get() {
    handle_index_get();
}

void VM::op_index_set() {
    handle_index_set();
}

void VM::op_index_get_opt() {
    handle_index_get_opt();
}

// ─── Records ──────────────────────────────────────────────────────────────────

void VM::op_make_record() {
    validate_stack_space(1);
    handle_make_record(current_code_end());
}

void VM::op_get_field() {
    handle_get_field();
}

void VM::op_set_field() {
    handle_set_field();
}

void VM::op_get_field_opt() {
    handle_get_field_opt();
}

void VM::op_record_with() {
    handle_record_with(current_code_end());
}

// ─── Choice types ─────────────────────────────────────────────────────────────

void VM::op_make_choice() {
    handle_make_choice();
}

void VM::op_make_choice_constructor() {
    handle_make_choice_constructor();
}

// ─── Result / Optional ────────────────────────────────────────────────────────

void VM::op_make_success() {
    auto val = pop();
    push(Value{ResultValue::success(std::move(val))});
}

void VM::op_make_failure() {
    handle_make_failure();
}

void VM::op_make_some() {
    // In Luma, some(x) is just x (non-null); already on the stack.
}

void VM::op_unwrap() {
    handle_unwrap();
}

void VM::op_result_inner() {
    handle_result_inner();
}

void VM::op_is_success() {
    handle_is_success();
}

void VM::op_is_some() {
    validate_stack_depth(1, "IsSome");
    auto& top = *(stack_.top - 1);
    top = Value{!top.is_null()};
}

void VM::op_ensure_success() {
    validate_stack_depth(1, "EnsureSuccess");
    auto& top = *(stack_.top - 1);
    if (!top.is_result()) {
        top = Value{ResultValue::success(std::move(top))};
    }
}

// ─── Downcast / Is ────────────────────────────────────────────────────────────

void VM::op_downcast() {
    handle_downcast();
}

void VM::op_trusted_downcast() {
    handle_trusted_downcast();
}

void VM::op_is_type() {
    auto name_idx = read_u16();
    const auto& type_name = checked_name(name_idx);
    auto val = pop();
    push(Value{matches_type(val, type_name)});
}

// ─── Control flow ─────────────────────────────────────────────────────────────

void VM::op_jump() {
    auto& cf = stack_.frames.back();
    const auto* code_end = current_code_end();
    auto offset = read_u32();
    cf.ip += offset;
    validate_jump_target(cf.ip, code_end);
}

void VM::jump_if(bool jump_on_truthy) {
    auto& cf = stack_.frames.back();
    const auto* code_end = current_code_end();
    const auto offset = read_u32();
    if (peek().is_truthy() == jump_on_truthy) {
        cf.ip += offset;
        validate_jump_target(cf.ip, code_end);
    }
}

void VM::op_jump_if_false() {
    jump_if(false);
}

void VM::op_jump_if_true() {
    jump_if(true);
}

void VM::op_loop() {
    handle_loop(current_code_start());
}

void VM::op_null_coalesce() {
    handle_null_coalesce(current_code_end());
}

// ─── Functions ────────────────────────────────────────────────────────────────

void VM::op_call() {
    auto arg_count = read_byte();
    auto callee = peek(arg_count);
    call_value(callee, arg_count);
}

void VM::op_call_named() {
    handle_call_named();
}

void VM::op_tail_call() {
    auto& cf = stack_.frames.back();
    const auto* code_start = cf.function->chunk().code.data();
    const auto* code_end = code_start + cf.function->chunk().code.size();
    handle_tail_call(code_start, code_end);
    // code_start / code_end updated inside but not needed here;
    // the next dispatch iteration re-reads stack_.frames.back().ip.
}

void VM::complete_return(Value result, std::size_t callee_slot_offset) {
    stack_.frames.pop_back();
    stack_.top = stack_.base + callee_slot_offset;
    if (stack_.frames.size() <= base_depth_) {
        dispatch_return_value_ = std::move(result);
    } else {
        push(std::move(result));
    }
}

void VM::op_return() {
    auto result = pop();
    complete_return(std::move(result), stack_.frames.back().slot_offset);
}

void VM::op_make_closure() {
    handle_make_closure();
}

// ─── Pipe operators ───────────────────────────────────────────────────────────

void VM::op_pipe() {
    handle_pipe();
}

void VM::op_error_pipe() {
    handle_error_pipe();
}

// ─── Exception handling ───────────────────────────────────────────────────────

void VM::op_try_catch() {
    handle_try_catch(current_code_end());
}

void VM::op_try_end() {
    if (exceptions_.empty()) [[unlikely]] {
        runtime_error(vm_errors::try_end_without_try_catch);
    }
    exceptions_.pop_handler_discard();
}

void VM::op_rethrow() {
    auto error_msg = pop();
    runtime_error(error_msg.is_string() ? error_msg.as_string() : error_msg.to_string());
}

// ─── Match ────────────────────────────────────────────────────────────────────

void VM::op_match_start() {
    // Match is compiled into jumps — these opcodes are no-ops at runtime.
}

void VM::op_match_arm() {
    // No-op: handled by compiled jump instructions.
}

void VM::op_match_end() {
    // No-op: handled by compiled jump instructions.
}

// ─── Containment ──────────────────────────────────────────────────────────────

void VM::op_contains() {
    handle_contains();
}

// ─── Concurrency ──────────────────────────────────────────────────────────────

void VM::op_spawn() {
    handle_spawn();
}

void VM::op_await() {
    handle_await();
}

void VM::op_task_scope_begin() {
    handle_task_scope_begin();
}

void VM::op_task_scope_end() {
    handle_task_scope_end();
}

// ─── Iteration ────────────────────────────────────────────────────────────────

void VM::op_for_iter_init() {
    handle_for_iter_init();
}

void VM::op_for_iter_step() {
    handle_for_iter_step();
}

void VM::op_for_iter_step_kv() {
    handle_for_iter_step_pair();
}

// ─── Misc ─────────────────────────────────────────────────────────────────────

void VM::op_print() {
    handle_print();
}

void VM::op_assert() {
    handle_assert();
}

void VM::op_type_of() {
    validate_stack_depth(1, "TypeOf");
    auto& top = *(stack_.top - 1);
    top = Value{top.display_type_name()};
}

// ─── Fused opcodes ────────────────────────────────────────────────────────────

void VM::op_increment_local() {
    auto& cf = stack_.frames.back();
    auto slot = read_u16();
    auto& val = get_local_slot(cf, slot);
    handle_unary_numeric(
        val, safe_increment, [](double n) -> Value { return Value{n + 1.0}; }, "Increment");
}

void VM::op_decrement_local() {
    auto& cf = stack_.frames.back();
    auto slot = read_u16();
    auto& val = get_local_slot(cf, slot);
    handle_unary_numeric(
        val, safe_decrement, [](double n) -> Value { return Value{n - 1.0}; }, "Decrement");
}

void VM::op_set_local_pop() {
    auto& cf = stack_.frames.back();
    auto slot = read_u16();
    get_local_slot(cf, slot) = pop();

    notify_local_data_breakpoint(cf, slot);
}

void VM::op_get_local_return() {
    auto& cf = stack_.frames.back();
    auto slot = read_u16();
    auto result = get_local_slot(cf, slot);
    complete_return(std::move(result), cf.slot_offset);
}

// ─── Conversions ──────────────────────────────────────────────────────────────

void VM::op_int_to_number() {
    validate_stack_depth(1, "IntToNumber");
    auto& top = *(stack_.top - 1);
    if (top.is_integer()) {
        top = Value{static_cast<double>(top.as_integer())};
    }
}

void VM::op_clone() {
    validate_stack_depth(1, "Clone");
    auto& top = *(stack_.top - 1);
    top = top.deep_copy();
}

// ─── Module ───────────────────────────────────────────────────────────────────

void VM::op_end_module() {
    dispatch_return_value_ = Value{};
}

} // namespace luma
