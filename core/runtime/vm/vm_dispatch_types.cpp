// vm_dispatch_types.cpp — Type system, result/optional, downcast, string
// interpolation, and type matching opcode handler methods.
//
// Extracted from vm_helpers.cpp / vm.cpp as part of the VM dispatch split.
// Contains:
//   - handle_interpolate, matches_type
//   - TypeMatcher implementation
//   - handle_make_failure, handle_unwrap, handle_result_inner (new)
//   - handle_is_success, handle_downcast, handle_trusted_downcast (new)

#include <algorithm>
#include <format>
#include <string>

#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_error_messages.hpp"
#include "runtime/vm/vm_types.hpp"

namespace luma {

// ─────────── String interpolation ───────────

void VM::handle_interpolate() {
    auto part_count = read_byte();

    validate_stack_depth(part_count, "Interpolate");

    const auto base = stack_size() - part_count;
    std::size_t total_size = 0;
    // SmallVector inline capacity (8) handles typical interpolations;
    // heap fallback for larger ones.  Only non-string operands are formatted
    // into this buffer; already-string operands are appended straight from the
    // stack, avoiding an intermediate to_string() copy.
    SmallVector<std::string, VMConstants::k_small_vector_capacity> formatted(part_count);

    for (std::size_t pi = 0; pi < part_count; ++pi) {
        const Value& operand = stack_.base[base + pi];
        std::size_t part_size = 0;
        if (operand.is_string()) {
            part_size = operand.as_string().size();
        } else {
            formatted[pi] = operand.to_string();
            part_size = formatted[pi].size();
        }

        if (!check_string_concat_size(total_size, part_size)) [[unlikely]] {
            runtime_error(vm_errors::string_interpolation_exceeds_max,
                          vm_errors::hint_string_too_large);
        }

        total_size += part_size;
    }

    std::string result;
    result.reserve(total_size);

    for (std::size_t pi = 0; pi < part_count; ++pi) {
        const Value& operand = stack_.base[base + pi];
        if (operand.is_string()) {
            result += operand.as_string();
        } else {
            result += formatted[pi];
        }
    }

    stack_.top = stack_.base + base;
    push(Value{std::move(result)});
}

// ─────────── Type matching ───────────

bool VM::matches_type(const Value& val, std::string_view type_name) const {
    return TypeMatcher::matches(val, type_name);
}

// ─────────── TypePattern implementation ───────────

namespace {

// Split a comma-separated type-argument list into its top-level components,
// ignoring commas nested inside angle brackets or parentheses.  For example
// "integer,array<string>,(integer,number)" yields three components:
// "integer", "array<string>", and "(integer,number)".  Always appends at
// least one component (an empty view for an empty input).
void split_top_level_params(std::string_view inner, std::vector<std::string_view>& out) {
    int depth = 0;
    std::size_t start = 0;

    for (std::size_t i = 0; i < inner.size(); ++i) {
        const char c = inner[i];

        if (c == '<' || c == '(') {
            ++depth;
        } else if (c == '>' || c == ')') {
            --depth;
        } else if (c == ',' && depth == 0) {
            out.push_back(inner.substr(start, i - start));
            start = i + 1;
        }
    }

    out.push_back(inner.substr(start));
}

} // namespace

std::optional<TypePattern> TypePattern::parse(std::string_view pattern) {
    if (pattern.empty()) {
        return std::nullopt;
    }

    // Tuple type: "(T1,T2,...)" — split on top-level commas so nested generic
    // and tuple element types are preserved.
    if (pattern.front() == '(' && pattern.back() == ')') {
        TypePattern tp;
        tp.kind = Kind::Tuple;
        split_top_level_params(pattern.substr(1, pattern.size() - 2), tp.params);
        return tp;
    }

    // Parameterized type: "base<T1,T2,...>" — possibly nested, e.g.
    // "dictionary<array<integer>>" or "result<integer,string>".
    const auto angle = pattern.find('<');

    if (angle != std::string_view::npos) {
        // Locate the '>' that closes the opening '<', tracking nesting depth.
        // It must be the final character for the pattern to be well-formed.
        int depth = 0;
        std::size_t close = std::string_view::npos;

        for (std::size_t i = angle; i < pattern.size(); ++i) {
            if (pattern[i] == '<') {
                ++depth;
            } else if (pattern[i] == '>') {
                --depth;

                if (depth == 0) {
                    close = i;
                    break;
                }

                if (depth < 0) {
                    return std::nullopt;
                }
            }
        }

        if (close != pattern.size() - 1) {
            return std::nullopt;
        }

        TypePattern tp;
        tp.kind = Kind::Parameterized;
        tp.base = pattern.substr(0, angle);
        split_top_level_params(pattern.substr(angle + 1, close - angle - 1), tp.params);
        return tp;
    }

    // Simple type: "integer", "string", choice name, record name, etc.
    TypePattern tp;
    tp.kind = Kind::Simple;
    tp.base = pattern;
    return tp;
}

bool TypePattern::matches(std::string_view type_string) const noexcept {
    switch (kind) {
        case Kind::Simple:
            return type_string == base || (base == "number" && type_string == "integer");

        case Kind::Parameterized:
            // Only the outer base type is checked here; element-level checks
            // require a Value and are handled in TypeMatcher.
            return type_string == base;

        case Kind::Tuple:
            return type_string == "tuple";
    }

    return false;
}

// ─────────── TypeMatcher implementation ───────────

bool TypeMatcher::matches(const Value& val, std::string_view type_pattern) {
    return matches(val, type_pattern, 0);
}

bool TypeMatcher::matches(const Value& val, std::string_view type_pattern, int depth) {
    // A crafted .lumc can reference a type-pattern string nested far deeper than
    // any source-level type (source nesting is capped at parse time).  Cap the
    // recursion so such input yields a safe "no match" instead of overflowing
    // the native stack — every recursive descent below re-enters here.
    if (depth > ResourceLimits::max_call_depth) {
        return false;
    }

    const auto pattern = TypePattern::parse(type_pattern);

    if (!pattern) {
        return false;
    }

    switch (pattern->kind) {
        case TypePattern::Kind::Tuple:
            return matches_tuple_type(val, *pattern, depth);
        case TypePattern::Kind::Parameterized:
            return matches_parameterized_type(val, *pattern, depth);
        case TypePattern::Kind::Simple:
            return matches_simple_type(val, *pattern);
    }

    return false;
}

bool TypeMatcher::matches_tuple_type(const Value& val, const TypePattern& pattern, int depth) {
    if (!val.is_tuple()) {
        return false;
    }

    const auto& elements = val.as_tuple()->elements;

    if (elements.size() != pattern.params.size()) {
        return false;
    }

    // Recurse so nested generic/tuple element types are verified at every level.
    for (std::size_t i = 0; i < elements.size(); ++i) {
        if (!matches(elements[i], pattern.params[i], depth + 1)) {
            return false;
        }
    }

    return true;
}

bool TypeMatcher::matches_parameterized_type(const Value& val, const TypePattern& pattern,
                                             int depth) {
    // optional<T> has no distinct runtime representation: `none` is a null value
    // and `some(x)` is just x.  A value satisfies optional<T> when it is `none`,
    // or when it matches the inner type T.
    if (pattern.base == "optional") {
        if (val.is_null()) {
            return true;
        }

        return pattern.params.empty() || matches(val, pattern.params[0], depth + 1);
    }

    // Every other parameterized type must match its base type first.
    if (!pattern.matches(val.display_type_name())) {
        return false;
    }

    // An untyped container pattern (e.g. plain "array") matches any instance.
    if (pattern.params.empty()) {
        return true;
    }

    const auto element_pattern = pattern.params[0];

    const auto all_match = [&](const auto& values) {
        return std::ranges::all_of(values, [&](const Value& element) {
            return matches(element, element_pattern, depth + 1);
        });
    };

    if (pattern.base == "array" && val.is_array()) {
        return all_match(*val.as_array()->elements);
    }

    if (pattern.base == "dictionary" && val.is_dictionary()) {
        return std::ranges::all_of(val.as_dictionary()->entries, [&](const auto& entry) {
            return matches(entry.second, element_pattern, depth + 1);
        });
    }

    if (pattern.base == "result" && val.is_result()) {
        const auto& result = val.as_result();

        if (result->is_success) {
            return matches(*result->owned_inner, element_pattern, depth + 1);
        }

        // For a failure the success type T is irrelevant; when an explicit error
        // type E is supplied (result<T, E>), verify the error value against it.
        if (pattern.params.size() >= 2) {
            return matches(*result->owned_inner, pattern.params[1], depth + 1);
        }

        return true;
    }

    if (pattern.base == "reference" && val.is_reference()) {
        return matches(val.as_reference()->get(), element_pattern, depth + 1);
    }

    if (pattern.base == "queue" && val.is_queue()) {
        return all_match(val.as_queue()->elements);
    }

    if (pattern.base == "stack" && val.is_stack()) {
        return all_match(val.as_stack()->elements);
    }

    if (pattern.base == "set" && val.is_set()) {
        return all_match(val.as_set()->elements);
    }

    // channel<T> and task<T> wrap values that are not materialised for
    // inspection at runtime: a channel's buffer is concurrency-controlled and a
    // task's result lives behind a future that has not been awaited.  Their
    // element type therefore cannot be verified; the base-type check above is
    // the strongest guarantee available.
    return true;
}

bool TypeMatcher::matches_simple_type(const Value& val, const TypePattern& pattern) {
    if (val.is_choice()) {
        return val.as_choice()->variant == pattern.base ||
               val.as_choice()->type_name == pattern.base;
    }

    if (val.is_record()) {
        return val.as_record()->type_name == pattern.base;
    }

    return pattern.matches(val.display_type_name());
}

// ─────────── Result / Optional handlers ───────────

void VM::handle_make_failure() {
    auto val = pop();
    auto result = ResultValue::failure(std::move(val), {}, {}, current_location());
    push(Value{std::move(result)});
}

void VM::handle_unwrap() {
    auto val = pop();

    if (val.is_result()) {
        const auto& result = val.as_result();

        if (result->is_success) {
            push(*result->owned_inner);
        } else {
            runtime_error(result->owned_inner->to_string());
        }
    } else if (val.is_null()) {
        runtime_error(vm_errors::unwrap_failed_none);
    } else {
        push(std::move(val)); // Already unwrapped.
    }
}

void VM::handle_result_inner() {
    auto val = pop();

    if (val.is_result()) {
        auto inner = *val.as_result()->owned_inner;
        push(std::move(inner));
    } else if (val.is_null()) {
        push(Value{NullValue{}}); // none stays none.
    } else {
        push(std::move(val)); // Non-result, non-null: pass through.
    }
}

void VM::handle_is_success() {
    auto val = pop();

    if (val.is_result()) {
        push(Value{val.as_result()->is_success});
    } else if (val.is_null()) {
        push(Value{false});
    } else {
        push(Value{true}); // Non-null, non-result values are "successful".
    }
}

// ─────────── Downcast / Is ───────────

void VM::handle_downcast() {
    auto name_idx = read_u16();
    const auto& type_name = checked_name(name_idx);
    auto val = pop();

    if (matches_type(val, type_name)) {
        push(Value{ResultValue::success(std::move(val))});
    } else {
        push(Value{ResultValue::failure(
            Value{vm_errors::cannot_downcast(val.display_type_name(), type_name)})});
    }
}

void VM::handle_trusted_downcast() {
    auto name_idx = read_u16();
    const auto& type_name = checked_name(name_idx);
    auto val = pop();

    if (!matches_type(val, type_name)) {
        runtime_error(vm_errors::trusted_downcast_failed(type_name, val.display_type_name()));
    }

    // Widen integer to number if the target type is number.
    if (type_name == "number" && val.is_integer()) {
        push(Value{static_cast<double>(val.as_integer())});
    } else {
        push(std::move(val));
    }
}

} // namespace luma
