#pragma once

// vm_error_messages.hpp — Centralised VM error message strings.
//
// Groups commonly used (and often duplicated) runtime error messages
// into a single header so that wording stays consistent and changes
// propagate automatically.
//
// Error message convention: Messages start with lowercase unless they begin
// with a proper noun or type name. Use sentence fragments without trailing periods.
//
// Convention:
//   - constexpr string_view  for fixed messages (no formatting).
//   - inline functions        for messages that embed runtime values.

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace luma::vm_errors {

// ─── Stack errors ────────────────────────────────────────────────────────────

constexpr std::string_view stack_overflow{"stack overflow"};
constexpr std::string_view stack_underflow{"stack underflow"};

[[nodiscard]] inline std::string stack_overflow_detail(std::size_t needed, std::size_t available,
                                                       std::size_t max) {
    return std::format("stack overflow: need {} slots but only {} of {} available", needed,
                       available, max);
}

[[nodiscard]] inline std::string stack_underflow_on_peek(std::size_t distance, std::size_t size) {
    return std::format("stack underflow on peek (distance {} with stack size {})", distance, size);
}

[[nodiscard]] inline std::string stack_underflow_depth(std::string_view context,
                                                       std::size_t required, std::size_t actual) {
    return std::format("{}: stack underflow (expected {} values, got {})", context, required,
                       actual);
}

[[nodiscard]] inline std::string stack_restore_too_large(std::size_t n, std::size_t max) {
    return std::format("cannot restore stack: size {} exceeds maximum {}", n, max);
}

// ─── Bytecode / internal errors ──────────────────────────────────────────────

constexpr std::string_view jump_target_beyond_bounds{"jump target beyond bytecode bounds"};
constexpr std::string_view null_coalesce_jump_beyond_bounds{
    "NullCoalesce jump target beyond bytecode bounds"};
constexpr std::string_view try_end_without_try_catch{"TryEnd without matching TryCatch"};
constexpr std::string_view task_scope_end_without_begin{
    "TaskScopeEnd without matching TaskScopeBegin"};
constexpr std::string_view invalid_iterator_state{"invalid iterator state"};
constexpr std::string_view invalid_named_call_operand{
    "invalid named-call operand: argument name is not a string"};
constexpr std::string_view loop_target_before_start{"loop target before bytecode start"};
constexpr std::string_view try_catch_beyond_end{
    "TryCatch: catch offset points beyond bytecode end"};
constexpr std::string_view too_many_exception_handlers{"too many nested exception handlers"};
constexpr std::string_view make_record_truncated{
    "MakeRecord: bytecode truncated, not enough bytes for field names"};
constexpr std::string_view record_with_truncated{
    "RecordWith: bytecode truncated, not enough bytes for field names"};
constexpr std::string_view bytecode_truncated{"bytecode truncated: not enough bytes for operand"};
constexpr std::string_view unknown_opcode{"VM dispatch: unknown or reserved opcode executed"};
constexpr std::string_view spawned_not_callable{"spawned expression is not callable"};
constexpr std::string_view with_requires_record{"'with' requires a record"};

[[nodiscard]] inline std::string constant_index_out_of_bounds(std::size_t index, std::size_t size) {
    return std::format("constant index {} out of bounds (size {})", index, size);
}

[[nodiscard]] inline std::string name_index_out_of_bounds(std::size_t index, std::size_t size) {
    return std::format("name index {} out of bounds (size {})", index, size);
}

[[nodiscard]] inline std::string invalid_upvalue_index(std::size_t index) {
    return std::format("invalid upvalue index {}", index);
}

[[nodiscard]] inline std::string invalid_function_index(std::uint16_t index) {
    return std::format("invalid function index {}", index);
}

[[nodiscard]] inline std::string undefined_variable(std::string_view name) {
    return std::format("undefined variable '{}'", name);
}

[[nodiscard]] inline std::string loop_exceeded_max_iterations(std::int64_t max) {
    return std::format("loop exceeded maximum iteration count ({})", max);
}

[[nodiscard]] inline std::string local_variable_out_of_bounds(std::uint16_t slot,
                                                              std::size_t offset) {
    return std::format("local variable index out of bounds (slot {} at offset {})", slot, offset);
}

// ─── Arithmetic / numeric errors ─────────────────────────────────────────────

constexpr std::string_view number_overflow{"number overflow"};
constexpr std::string_view integer_division_overflow{"integer division overflow"};
constexpr std::string_view integer_division_requires_integers{
    "integer division requires integer operands"};
constexpr std::string_view string_concat_exceeds_max{
    "string concatenation exceeds maximum string size"};
constexpr std::string_view string_repetition_exceeds_max{
    "string repetition exceeds maximum string size"};
constexpr std::string_view string_interpolation_exceeds_max{
    "string interpolation exceeds maximum string size"};

[[nodiscard]] inline std::string cannot_operate_on(std::string_view verb, std::string_view type_a,
                                                   std::string_view type_b) {
    return std::format("cannot {} '{}' and '{}'", verb, type_a, type_b);
}

[[nodiscard]] inline std::string division_by_zero_op(std::string_view op_name) {
    return std::format("{}: division by zero", op_name);
}

[[nodiscard]] inline std::string shift_out_of_range(std::int64_t shift, int max) {
    return std::format("shift amount {} out of range (0..{})", shift, max);
}

[[nodiscard]] inline std::string
requires_integer_operands(std::string_view op, std::string_view type_a, std::string_view type_b) {
    return std::format("{} requires integer operands, got '{}' and '{}'", op, type_a, type_b);
}

[[nodiscard]] inline std::string requires_integer_operand(std::string_view op,
                                                          std::string_view type) {
    return std::format("{} requires an integer operand, got '{}'", op, type);
}

[[nodiscard]] inline std::string requires_integer_or_number(std::string_view op,
                                                            std::string_view type) {
    return std::format("{} requires an integer or number, got '{}'", op, type);
}

// ─── Collection / field errors ───────────────────────────────────────────────

constexpr std::string_view unwrap_failed_none{"unwrap failed: value is none"};

[[nodiscard]] inline std::string index_out_of_bounds(std::string_view context, std::int64_t idx,
                                                     std::size_t size) {
    return std::format("{} {} out of bounds (size {})", context, idx, size);
}

[[nodiscard]] inline std::string key_not_found(std::string_view key) {
    return std::format("key '{}' not found in dictionary", key);
}

[[nodiscard]] inline std::string cannot_index_into(std::string_view type) {
    return std::format("cannot index into '{}'", type);
}

[[nodiscard]] inline std::string cannot_index_assign_into(std::string_view type) {
    return std::format("cannot index-assign into '{}'", type);
}

[[nodiscard]] inline std::string cannot_access_field_on(std::string_view field,
                                                        std::string_view type) {
    return std::format("cannot access field '{}' on '{}'", field, type);
}

[[nodiscard]] inline std::string cannot_access_field_on_choice(std::string_view field,
                                                               std::string_view type) {
    return std::format("cannot access field '{}' on choice type '{}'", field, type);
}

[[nodiscard]] inline std::string cannot_set_field_on(std::string_view type) {
    return std::format("cannot set field on '{}'", type);
}

[[nodiscard]] inline std::string record_no_field(std::string_view field) {
    return std::format("record has no field '{}'", field);
}

[[nodiscard]] inline std::string record_type_no_field(std::string_view type,
                                                      std::string_view field) {
    return std::format("record type '{}' has no field '{}'", type, field);
}

[[nodiscard]] inline std::string invalid_tuple_index(std::string_view field) {
    return std::format("invalid tuple index '{}'", field);
}

[[nodiscard]] inline std::string tuple_index_out_of_range(std::size_t index) {
    return std::format("tuple index {} out of range", index);
}

[[nodiscard]] inline std::string cannot_iterate_over(std::string_view type) {
    return std::format("cannot iterate over '{}'", type);
}

[[nodiscard]] inline std::string dict_index_must_be_string(std::string_view type) {
    return std::format("dictionary index must be a string, got '{}'", type);
}

[[nodiscard]] inline std::string dict_key_must_be_string(std::string_view type) {
    return std::format("dictionary key must be a string, got '{}'", type);
}

[[nodiscard]] inline std::string in_dict_requires_string(std::string_view type) {
    return std::format("'in' on dictionary requires a string key, got '{}'", type);
}

[[nodiscard]] inline std::string in_string_requires_string(std::string_view type) {
    return std::format("'in' on string requires a string element, got '{}'", type);
}

[[nodiscard]] inline std::string in_range_requires_integer(std::string_view type) {
    return std::format("'in' on a range requires an integer, got '{}'", type);
}

[[nodiscard]] inline std::string range_start_must_be_integer(std::string_view type) {
    return std::format("range start must be an integer, got '{}'", type);
}

[[nodiscard]] inline std::string range_end_must_be_integer(std::string_view type) {
    return std::format("range end must be an integer, got '{}'", type);
}

[[nodiscard]] inline std::string index_must_be_integer(std::string_view context,
                                                       std::string_view type) {
    return std::format("{} must be an integer, got '{}'", context, type);
}

// ─── Call / function errors ──────────────────────────────────────────────────

constexpr std::string_view nil_function_call{"attempt to call a nil function"};
constexpr std::string_view task_cancelled{"task cancelled"};
constexpr std::string_view await_consumed_task{"await called on an already-consumed task"};

[[nodiscard]] inline std::string arity_error(int expected, int got) {
    return std::format("too many positional arguments: function expects {} but got {}", expected,
                       got);
}

[[nodiscard]] inline std::string cannot_call(std::string_view type) {
    return std::format("cannot call '{}'", type);
}

[[nodiscard]] inline std::string call_stack_overflow(int limit) {
    return std::format("call stack overflow (depth limit: {})", limit);
}

[[nodiscard]] inline std::string call_stack_overflow_recursion(int limit) {
    return std::format("call stack overflow: maximum recursion depth ({}) exceeded", limit);
}

[[nodiscard]] inline std::string call_frame_overflow(std::string_view name, std::size_t limit) {
    return std::format(
        "call frame overflow: cannot call '{}' \xe2\x80\x94 frame limit ({}) reached", name, limit);
}

[[nodiscard]] inline std::string await_requires_task(std::string_view type) {
    return std::format("await requires a task value, got '{}'", type);
}

[[nodiscard]] inline std::string tail_call_arity_mismatch(int expected, int got) {
    return std::format("tail call arity mismatch: expected {} arguments, got {}", expected, got);
}

[[nodiscard]] inline std::string unknown_named_argument(std::string_view name) {
    return std::format("unknown named argument '{}'", name);
}

[[nodiscard]] inline std::string cannot_assign_immutable(std::string_view name) {
    return std::format("cannot assign to immutable variable '{}'", name);
}

[[nodiscard]] inline std::string cannot_downcast(std::string_view from, std::string_view to) {
    return std::format("cannot downcast '{}' to '{}'", from, to);
}

[[nodiscard]] inline std::string trusted_downcast_failed(std::string_view expected,
                                                         std::string_view got) {
    return std::format("trusted_downcast failed: expected '{}', got '{}'", expected, got);
}

// ─── Hint strings ────────────────────────────────────────────────────────────

constexpr std::string_view hint_divisor_not_zero{"the divisor must not be zero"};
constexpr std::string_view hint_bitwise_integers_only{
    "bitwise operators only work with integer values"};
constexpr std::string_view hint_string_too_large{
    "the resulting string is too large \xe2\x80\x94 consider writing to "
    "a file instead"};
constexpr std::string_view hint_integer_division_only{"use '//' only with integer values"};
constexpr std::string_view hint_integer_division_overflow{
    "this operation would overflow \xe2\x80\x94 check for division of "
    "minimum integer by -1"};
constexpr std::string_view hint_ranges_integer_bounds{"ranges only support integer bounds"};
constexpr std::string_view hint_check_variable_spelling{
    "check the variable name spelling and ensure it is in scope"};
constexpr std::string_view hint_use_match_expression{
    "use a match expression to access choice variant fields"};
constexpr std::string_view hint_check_record_fields{
    "check the record type definition for available fields"};
constexpr std::string_view hint_declare_mutable{
    "declare the variable with 'mutable' to allow reassignment"};
constexpr std::string_view hint_nil_function{
    "the function variable is nil \xe2\x80\x94 ensure it is assigned before calling"};
constexpr std::string_view hint_check_recursion{
    "check for infinite recursion in your function calls"};
constexpr std::string_view hint_deep_recursion{"check for deep or infinite recursion"};
constexpr std::string_view hint_convert_index{"use Converter.to_string() to convert the index"};
constexpr std::string_view hint_convert_index_integer{
    "use Converter.to_integer() to convert the index"};
constexpr std::string_view hint_convert_key{"use Converter.to_string() to convert the key"};

[[nodiscard]] inline std::string hint_shift_range(int max) {
    return std::format("shift amount must be between 0 and {}", max);
}

} // namespace luma::vm_errors
