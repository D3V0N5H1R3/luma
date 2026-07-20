// vm_dispatch_construction.cpp — Collection- and record-construction opcode
// handler methods.
//
// Split out of vm_dispatch_collections.cpp: that file had grown to bundle
// indexing, field access, and construction. This file owns the handlers that
// build new aggregate values from stack operands.
// Contains:
//   - handle_make_dict
//   - handle_make_record, handle_record_with
//   - handle_make_array, handle_make_tuple, handle_make_choice
//   - handle_make_choice_constructor

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "analysis/errors/error.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

namespace {

// Each dictionary entry on the stack consists of a key and a value.
constexpr std::size_t k_key_value_pair_size = 2;

// Validate that all override field names exist in the record.
// Throws a RuntimeError if any field name is not found.
void validate_record_field_names(const RecordValue& record,
                                 std::span<const std::string_view> field_names,
                                 const SourceLocation& loc) {
    for (const auto& fname : field_names) {
        if (record.find_field(fname) == nullptr) [[unlikely]] {
            throw RuntimeError{vm_errors::record_type_no_field(record.type_name, fname), loc};
        }
    }
}

} // namespace

// ─────────── Dictionary ───────────

void VM::handle_make_dict() {
    auto count = read_u16();
    validate_stack_space(1);
    auto dict = std::make_shared<DictionaryValue>();

    // Pop key-value pairs: each entry occupies two stack slots.
    auto popped = pop_sequence(static_cast<std::size_t>(count) * k_key_value_pair_size);
    std::vector<std::pair<std::string, Value>> pairs(count);

    for (int i = 0; i < count; ++i) {
        auto& key = popped[static_cast<std::size_t>(i) * k_key_value_pair_size];
        auto& value = popped[(static_cast<std::size_t>(i) * k_key_value_pair_size) + 1];

        if (!key.is_string()) [[unlikely]] {
            runtime_error(vm_errors::dict_key_must_be_string(key.display_type_name()),
                          vm_errors::hint_convert_key);
        }

        pairs[static_cast<std::size_t>(i)] = {key.as_string(), std::move(value)};
    }

    // Move pairs directly into the dictionary, avoiding per-entry
    // duplicate scanning that set() performs on an unindexed dict.
    dict->entries = std::move(pairs);

    push(Value{std::move(dict)});
}

// ─────────── Record ───────────

void VM::handle_make_record(const std::uint8_t* code_end) {
    auto& cf = stack_.frames.back();
    auto type_name_idx = read_u16();
    auto field_count = read_byte();
    const auto& type_name = checked_name(type_name_idx);

    if (cf.ip + (static_cast<std::ptrdiff_t>(field_count) * 2) > code_end) [[unlikely]] {
        runtime_error(vm_errors::make_record_truncated);
    }

    validate_stack_depth(static_cast<std::size_t>(field_count), "MakeRecord");

    auto field_name_indices = read_name_indices(field_count);
    assert(field_name_indices.size() == field_count &&
           "read_name_indices must return exactly field_count elements");

    auto record = std::make_shared<RecordValue>();
    record->type_name = type_name;
    record->fields.resize(field_count);

    auto popped = pop_sequence(static_cast<std::size_t>(field_count));
    for (std::size_t i = 0; i < field_count; ++i) {
        record->fields[i] = {std::string{checked_name(field_name_indices[i])},
                             std::move(popped[i])};
    }

    push(Value{std::move(record)});
}

void VM::handle_record_with(const std::uint8_t* code_end) {
    auto& cf = stack_.frames.back();
    auto override_count = read_byte();

    if (cf.ip + (static_cast<std::ptrdiff_t>(override_count) * 2) > code_end) [[unlikely]] {
        runtime_error(vm_errors::record_with_truncated);
    }

    auto override_name_indices = read_name_indices(override_count);
    assert(override_name_indices.size() == static_cast<std::size_t>(override_count) &&
           "read_name_indices must return exactly override_count elements");

    auto override_values = pop_sequence(static_cast<std::size_t>(override_count));

    auto base = pop();

    if (!base.is_record()) {
        runtime_error(vm_errors::with_requires_record);
    }

    auto new_record = std::make_shared<RecordValue>();
    new_record->type_name = base.as_record()->type_name;
    new_record->fields = base.as_record()->fields;

    // Resolve override field names upfront for validation and application.
    SmallVector<std::string_view> override_names(override_count);
    for (std::size_t i = 0; i < override_count; ++i) {
        override_names[i] = checked_name(override_name_indices[i]);
    }

    // Two-pass design is intentional: the first pass validates that ALL
    // field names exist before the second pass applies any mutations.
    // This guarantees the record is never left in a partially-updated
    // state if a later field name is invalid.
    validate_record_field_names(
        *new_record,
        std::span<const std::string_view>{override_names.data(), override_names.size()},
        current_location());

    for (std::size_t i = 0; i < override_values.size(); ++i) {
        auto* field = new_record->find_field(override_names[i]);
        *field = std::move(override_values[i]);
    }

    push(Value{std::move(new_record)});
}

// ─────────── New collection handlers ───────────

void VM::handle_make_array() {
    auto count = read_u16();
    validate_stack_space(1);
    auto arr = std::make_shared<ArrayValue>();
    arr->elements->resize(count);

    auto popped = pop_sequence(static_cast<std::size_t>(count));
    std::ranges::move(popped, arr->elements->begin());

    push(Value{std::move(arr)});
}

void VM::handle_make_tuple() {
    auto count = read_u16();
    validate_stack_space(1);
    auto tuple = std::make_shared<TupleValue>();
    tuple->elements.resize(count);

    auto popped = pop_sequence(static_cast<std::size_t>(count));
    std::ranges::move(popped, tuple->elements.begin());

    push(Value{std::move(tuple)});
}

void VM::handle_make_choice() {
    auto variant_name = pop();
    auto type_name = pop();

    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = type_name.is_string() ? type_name.as_string() : type_name.to_string();
    cv->variant = variant_name.is_string() ? variant_name.as_string() : variant_name.to_string();

    push(Value{std::move(cv)});
}

void VM::handle_make_choice_constructor() {
    const auto field_count = static_cast<std::size_t>(read_byte());
    auto variant_name_val = pop();
    auto type_name_val = pop();

    auto type_name = type_name_val.is_string() ? std::string{type_name_val.as_string()}
                                               : type_name_val.to_string();
    auto variant_name = variant_name_val.is_string() ? std::string{variant_name_val.as_string()}
                                                     : variant_name_val.to_string();

    auto ctor = std::make_shared<NativeFunctionValue>();
    ctor->name = type_name + "." + variant_name;
    ctor->function = [type_name = std::move(type_name), variant_name = std::move(variant_name),
                      field_count](std::span<const Value> args, SourceLocation) -> Value {
        auto cv = std::make_shared<ChoiceValue>();
        cv->type_name = type_name;
        cv->variant = variant_name;
        cv->fields.reserve(field_count);
        for (std::size_t i{0}; i < field_count && i < args.size(); ++i) {
            cv->fields.push_back(args[i]);
        }
        return Value{std::move(cv)};
    };

    push(Value{std::move(ctor)});
}

} // namespace luma
