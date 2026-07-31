// vm_dispatch_collections.cpp — Collection indexing, field-access, and range
// opcode handler methods.
//
// Extracted from vm_helpers.cpp as part of the VM dispatch split; the
// aggregate-construction handlers (make_dict/record/array/tuple/choice) now
// live in vm_dispatch_construction.cpp.
// Contains:
//   - resolve_index
//   - handle_index_get, handle_index_set, handle_index_get_opt
//   - handle_array/dict/string/tuple/choice_index_get
//   - handle_get_field, handle_set_field, handle_get_field_opt
//   - handle_contains, make_range

#include <algorithm>
#include <charconv>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "analysis/errors/error.hpp"
#include "common/index_validator.hpp"
#include "common/resource_limits.hpp"
#include "common/utf8.hpp"
#include "common/utf8_iterator.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

// ─────────── Index helpers ───────────

std::int64_t VM::resolve_index(std::int64_t idx, std::size_t size, std::string_view context) const {
    if (is_index_out_of_bounds(idx, size)) {
        runtime_error(vm_errors::index_out_of_bounds(context, idx, size));
    }

    return idx;
}

SmallVector<std::uint16_t, VMConstants::k_small_vector_capacity>
VM::read_name_indices(std::size_t count) {
    SmallVector<std::uint16_t, VMConstants::k_small_vector_capacity> indices(count);
    for (std::size_t i = 0; i < count; ++i) {
        indices[i] = read_u16();
    }
    return indices;
}

// ─────────── Per-type index helpers ───────────

void VM::push_element_at_index(std::span<const Value> elements, const Value& index_val,
                               std::string_view context) {
    if (!index_val.is_integer()) [[unlikely]] {
        runtime_error(vm_errors::index_must_be_integer(context, index_val.display_type_name()),
                      vm_errors::hint_convert_index_integer);
    }
    auto idx = resolve_index(index_val.as_integer(), elements.size(), context);
    push(elements[static_cast<std::size_t>(idx)]);
}

void VM::handle_array_index_get(const Value& container, const Value& index_val) {
    const auto& elems = *container.as_array()->elements;

    if (index_val.is_range()) {
        const auto& range = *index_val.as_range();
        const auto size = static_cast<std::int64_t>(elems.size());
        auto [start, end] =
            compute_slice_range(range.start, range.end, size,
                                range.inclusive ? RangeEnd::Inclusive : RangeEnd::Exclusive);

        auto slice = std::make_shared<ArrayValue>();

        if (start < end) {
            slice->elements->reserve(static_cast<std::size_t>(end - start));
            std::ranges::copy(elems.begin() + start, elems.begin() + end,
                              std::back_inserter(*slice->elements));
        }

        push(Value{std::move(slice)});
        return;
    }

    push_element_at_index(elems, index_val, "Index");
}

void VM::handle_dict_index_get(const Value& container, const Value& index_val) {
    if (!index_val.is_string()) [[unlikely]] {
        runtime_error(vm_errors::dict_index_must_be_string(index_val.display_type_name()),
                      vm_errors::hint_convert_index);
    }

    const auto& key = index_val.as_string();
    const auto* val = container.as_dictionary()->find(key);

    if (val == nullptr) {
        runtime_error(vm_errors::key_not_found(key));
    }

    push(*val);
}

void VM::handle_string_index_get(const Value& container, const Value& index_val) {
    const auto& str = container.as_string();

    if (index_val.is_range()) {
        const auto& range = *index_val.as_range();
        const auto cp_count = utf8_count(str);
        auto [start, end] =
            compute_slice_range(range.start, range.end, cp_count,
                                range.inclusive ? RangeEnd::Inclusive : RangeEnd::Exclusive);

        if (start >= end) {
            push(Value{std::string{}});
        } else {
            const auto byte_start = utf8_byte_offset(str, start);
            const auto byte_end = utf8_byte_offset(str, end);
            push(Value{str.substr(byte_start, byte_end - byte_start)});
        }

        return;
    }

    const auto cp_count = utf8_count_size(str);

    if (!index_val.is_integer()) [[unlikely]] {
        runtime_error(
            vm_errors::index_must_be_integer("String index", index_val.display_type_name()),
            vm_errors::hint_convert_index_integer);
    }

    auto idx = resolve_index(index_val.as_integer(), cp_count, "String index");
    const auto byte_pos = utf8_byte_offset(str, idx);
    push(Value{utf8_char_at_byte(str, byte_pos)});
}

// ─────────── Composite index handlers ───────────

void VM::handle_index_get() {
    auto index_val = pop();
    auto container = pop();

    dispatch_collection(
        container, [&] { handle_array_index_get(container, index_val); },
        [&] { handle_dict_index_get(container, index_val); },
        [&] { handle_string_index_get(container, index_val); },
        [&] {
            if (container.is_tuple()) {
                push_element_at_index(container.as_tuple()->elements, index_val, "Tuple index");
                return;
            }
            if (container.is_choice()) {
                push_element_at_index(container.as_choice()->fields, index_val,
                                      "Choice field index");
                return;
            }
            runtime_error(vm_errors::cannot_index_into(container.display_type_name()));
        });
}

void VM::handle_index_set() {
    auto value = pop();
    auto index_val = pop();
    auto container = pop();

    const auto error = [&] {
        runtime_error(vm_errors::cannot_index_assign_into(container.display_type_name()));
    };

    dispatch_collection(
        container,
        [&] {
            container.as_array()->ensure_unique();
            auto& elems = *container.as_array()->elements;
            if (!index_val.is_integer()) [[unlikely]] {
                runtime_error(
                    vm_errors::index_must_be_integer("Array index", index_val.display_type_name()),
                    vm_errors::hint_convert_index_integer);
            }
            auto idx = resolve_index(index_val.as_integer(), elems.size(), "Array index");
            elems[static_cast<std::size_t>(idx)] = std::move(value);
        },
        [&] {
            if (!index_val.is_string()) [[unlikely]] {
                runtime_error(vm_errors::dict_index_must_be_string(index_val.display_type_name()),
                              vm_errors::hint_convert_index);
            }
            const auto& key = index_val.as_string();
            auto* existing = container.as_dictionary()->find(key);

            if (existing) {
                *existing = std::move(value);
            } else {
                container.as_dictionary()->set(key, std::move(value));
            }
        },
        error, error);

    push(std::move(container));
}

void VM::handle_index_get_opt() {
    auto index_val = pop();
    auto container = pop();

    if (container.is_null()) {
        push(Value{}); // none
        return;
    }

    dispatch_collection(
        container,
        [&] {
            const auto& elems = *container.as_array()->elements;
            if (!index_val.is_integer()) [[unlikely]] {
                runtime_error(
                    vm_errors::index_must_be_integer("Index", index_val.display_type_name()),
                    vm_errors::hint_convert_index_integer);
            }
            // Negative indices are out of bounds (not Python-style wrapping),
            // consistent with the non-optional arr[i] form.  The optional ?[]
            // form returns none instead of throwing.
            const auto idx = index_val.as_integer();
            push(is_index_out_of_bounds(idx, elems.size()) ? Value{}
                                                           : elems[static_cast<std::size_t>(idx)]);
        },
        [&] {
            if (!index_val.is_string()) [[unlikely]] {
                runtime_error(vm_errors::dict_index_must_be_string(index_val.display_type_name()),
                              vm_errors::hint_convert_index);
            }

            const auto* val = container.as_dictionary()->find(index_val.as_string());
            push(val ? *val : Value{});
        },
        [&] { push(Value{}); }, // string — not indexable with ?[]; return none
        [&] {
            push(Value{});
        } // other — not indexable with ?[]; return none
    );
}

// ─────────── Field access ───────────

void VM::handle_get_field() {
    auto name_idx = read_u16();
    const auto& field_name = checked_name(name_idx);
    auto obj = pop();

    if (obj.is_record()) {
        const auto* val = obj.as_record()->find_field(field_name);

        if (val == nullptr) {
            runtime_error(vm_errors::record_no_field(field_name),
                          vm_errors::hint_check_record_fields);
        }

        push(*val);
    } else if (obj.is_choice()) {
        runtime_error(
            vm_errors::cannot_access_field_on_choice(field_name, obj.as_choice()->type_name),
            vm_errors::hint_use_match_expression);
    } else if (obj.is_tuple()) {
        std::size_t index = 0;
        const auto* begin = field_name.data();
        const auto* end = begin + field_name.size();
        auto [ptr, ec] = std::from_chars(begin, end, index);

        // Safety: std::from_chars into std::size_t can silently accept
        // values larger than the tuple — the bounds check below catches
        // those.  The ec != errc{} check catches actual parse failures
        // (e.g. non-digit characters); ptr != end catches trailing junk.
        // Together these reject any malformed tuple field name.
        if (ec != std::errc{} || ptr != end) {
            runtime_error(vm_errors::invalid_tuple_index(field_name));
        }

        if (index >= obj.as_tuple()->elements.size()) {
            runtime_error(vm_errors::tuple_index_out_of_range(index));
        }

        push(obj.as_tuple()->elements[index]);
    } else {
        runtime_error(vm_errors::cannot_access_field_on(field_name, obj.display_type_name()));
    }
}

void VM::handle_set_field() {
    auto name_idx = read_u16();
    const auto& field_name = checked_name(name_idx);
    auto value = pop();
    auto obj = pop();

    if (obj.is_record()) {
        auto* field = obj.as_record()->find_field(field_name);

        if (field != nullptr) {
            *field = std::move(value);
        } else {
            runtime_error(vm_errors::record_no_field(field_name));
        }

        push(std::move(obj));
    } else {
        runtime_error(vm_errors::cannot_set_field_on(obj.display_type_name()));
    }
}

void VM::handle_get_field_opt() {
    auto name_idx = read_u16();
    const auto& field_name = checked_name(name_idx);
    auto obj = pop();

    // Null propagation is semantically distinct from the non-record fallback,
    // though both yield an empty optional.
    // NOLINTNEXTLINE(bugprone-branch-clone)
    if (obj.is_null()) {
        push(Value{});
    } else if (obj.is_record()) {
        const auto* val = obj.as_record()->find_field(field_name);
        push((val != nullptr) ? *val : Value{});
    } else {
        push(Value{});
    }
}

// ─────────── Contains ───────────

void VM::handle_contains() {
    auto container = pop();
    auto element = pop();

    bool found = false;

    if (container.is_range()) {
        if (!element.is_integer()) [[unlikely]] {
            runtime_error(vm_errors::in_range_requires_integer(element.display_type_name()));
        }
        const auto& range = *container.as_range();
        const auto value = element.as_integer();
        // Mirror the for-in range iterator bounds (iter_step_range): start is
        // always inclusive; end is inclusive for a..=b, exclusive for a..b.
        found = value >= range.start && (range.inclusive ? value <= range.end : value < range.end);
        push(Value{found});
        return;
    }

    dispatch_collection(
        container,
        [&] {
            const auto& elems = *container.as_array()->elements;
            found = std::ranges::any_of(elems, [&](const Value& e) { return e.equals(element); });
        },
        [&] {
            if (!element.is_string()) [[unlikely]] {
                runtime_error(vm_errors::in_dict_requires_string(element.display_type_name()));
            }
            found = container.as_dictionary()->find(element.as_string()) != nullptr;
        },
        [&] {
            if (!element.is_string()) [[unlikely]] {
                runtime_error(vm_errors::in_string_requires_string(element.display_type_name()));
            }
            found = container.as_string().find(element.as_string()) != std::string::npos;
        },
        [] {});

    push(Value{found});
}

// ─────────── Range ───────────

void VM::make_range(bool inclusive) {
    auto end_val = pop();
    auto start_val = pop();

    if (!start_val.is_integer()) [[unlikely]] {
        runtime_error(vm_errors::range_start_must_be_integer(start_val.display_type_name()),
                      vm_errors::hint_ranges_integer_bounds);
    }

    if (!end_val.is_integer()) [[unlikely]] {
        runtime_error(vm_errors::range_end_must_be_integer(end_val.display_type_name()),
                      vm_errors::hint_ranges_integer_bounds);
    }

    auto range = std::make_shared<RangeValue>();
    range->start = start_val.as_integer();
    range->end = end_val.as_integer();
    range->inclusive = inclusive;
    push(Value{std::move(range)});
}

} // namespace luma
