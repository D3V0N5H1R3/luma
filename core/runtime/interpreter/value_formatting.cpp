#include <format>
#include <functional>
#include <vector>

#include "common/format_number.hpp"
#include "runtime/interpreter/recursion_guard.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/interpreter/value_collection_helpers.hpp"
#include "runtime/interpreter/value_dispatch.hpp"

// Value dispatch — each value_*.cpp file follows the same pattern:
// branch on value type and handle each case with type-specific semantics.
// Formatting uses std::visit with the overloaded pattern to cover every
// variant alternative.  Copying and equality use switch(value_type())
// for exhaustive dispatch.  See value_dispatch.hpp for the shared
// dispatch utilities.

using luma::collection_helpers::format_container;
using luma::collection_helpers::format_value_sequence;

namespace {

// Format a Value for container display: strings are quoted, other types use to_string().
void append_display_value(std::string& out, const luma::Value& v) {
    if (v.is_string()) {
        out += '"';
        out += v.to_string();
        out += '"';
    } else {
        out += v.to_string();
    }
}

// ─────────── to_string helpers ───────────

[[nodiscard]] std::string format_array_value(const luma::ArrayValue& arr) {
    return format_container(*arr.elements, "[", "]", [](const luma::Value& elem, std::string& r) {
        append_display_value(r, elem);
    });
}

[[nodiscard]] std::string format_dictionary_value(const luma::DictionaryValue& dict) {
    return format_container(dict.entries, "{", "}", [](const auto& kv, std::string& r) {
        r += std::format("\"{}\": ", kv.first);
        append_display_value(r, kv.second);
    });
}

[[nodiscard]] std::string format_result_value(const luma::ResultValue& res) {
    if (res.is_success) {
        return std::format("success({})", res.owned_inner->to_string());
    }

    if (res.has_failure_location) {
        return std::format("failure([{}:{}] {})", res.failure_location.line,
                           res.failure_location.column, res.owned_inner->to_string());
    }

    return std::format("failure({})", res.owned_inner->to_string());
}

[[nodiscard]] std::string format_record_value(const luma::RecordValue& rec) {
    auto prefix = std::format("{} {{ ", rec.type_name);
    return format_container(rec.fields, prefix, " }", [](const auto& kv, std::string& r) {
        r += std::format("{} = {}", kv.first, kv.second.to_string());
    });
}

[[nodiscard]] std::string format_choice_value(const luma::ChoiceValue& choice) {
    if (choice.fields.empty()) {
        return std::format("{}.{}", choice.type_name, choice.variant);
    }

    auto prefix = std::format("{}.{}(", choice.type_name, choice.variant);
    return format_container(choice.fields, prefix, ")",
                            [](const luma::Value& f, std::string& r) { r += f.to_string(); });
}

[[nodiscard]] std::string format_tuple_value(const luma::TupleValue& tup) {
    return format_container(tup.elements, "(", ")",
                            [](const luma::Value& elem, std::string& r) { r += elem.to_string(); });
}

[[nodiscard]] std::string format_range_value(const luma::RangeValue& r) {
    return std::format("{}..{}", r.start, r.end);
}

[[nodiscard]] std::string format_function_value(const luma::FunctionValue& func) {
    return std::format("<function {}>", func.name);
}

[[nodiscard]] std::string format_native_function_value(const luma::NativeFunctionValue& func) {
    return std::format("<native function {}>", func.name);
}

[[nodiscard]] std::string format_reference_value(const luma::ReferenceValue& ref) {
    return std::format("ref({})", ref.get().to_string());
}

} // anonymous namespace

namespace luma {

// ─────────── Value::to_string ───────────

std::string Value::to_string() const {
    const runtime::RecursionGuard guard{runtime::RecursionKind::to_string};
    if (!guard.entered()) {
        return "...";
    }

    return std::visit(
        luma::overloaded{
            [](const NullValue&) -> std::string { return "none"; },
            [](bool v) -> std::string { return v ? "true" : "false"; },
            [](std::int64_t v) -> std::string { return std::to_string(v); },
            [](double v) -> std::string { return luma::format_number(v); },
            [](const std::string& v) -> std::string { return v; },
            [](const std::shared_ptr<ArrayValue>& v) -> std::string {
                return format_array_value(*v);
            },
            [](const std::shared_ptr<DictionaryValue>& v) -> std::string {
                return format_dictionary_value(*v);
            },
            [](const std::shared_ptr<TupleValue>& v) -> std::string {
                return format_tuple_value(*v);
            },
            [](const std::shared_ptr<ResultValue>& v) -> std::string {
                return format_result_value(*v);
            },
            [](const std::shared_ptr<RecordValue>& v) -> std::string {
                return format_record_value(*v);
            },
            [](const std::shared_ptr<RangeValue>& v) -> std::string {
                return format_range_value(*v);
            },
            [](const std::shared_ptr<ChoiceValue>& v) -> std::string {
                return format_choice_value(*v);
            },
            [](const std::shared_ptr<FunctionValue>& v) -> std::string {
                return format_function_value(*v);
            },
            [](const std::shared_ptr<NativeFunctionValue>& v) -> std::string {
                return format_native_function_value(*v);
            },
            [](const std::shared_ptr<TaskValue>&) -> std::string { return "<task>"; },
            [](const std::shared_ptr<ChannelValue>&) -> std::string { return "<channel>"; },
            [](const std::shared_ptr<SocketValue>&) -> std::string { return "<socket>"; },
            [](const std::shared_ptr<CollectionObject>& v) -> std::string {
                return v->to_display_string();
            },
            [](const std::shared_ptr<ReferenceValue>& v) -> std::string {
                return format_reference_value(*v);
            },
        },
        data_);
}

// ─────────── Value::display_type_name ───────────

std::string Value::display_type_name() const {
    return std::visit(
        luma::overloaded{
            [](NullValue) -> std::string { return "none"; },
            [](bool) -> std::string { return "boolean"; },
            [](std::int64_t) -> std::string { return "integer"; },
            [](double) -> std::string { return "number"; },
            [](const std::string&) -> std::string { return "string"; },
            [](const std::shared_ptr<ArrayValue>&) -> std::string { return "array"; },
            [](const std::shared_ptr<DictionaryValue>&) -> std::string { return "dictionary"; },
            [](const std::shared_ptr<TupleValue>&) -> std::string { return "tuple"; },
            [](const std::shared_ptr<ResultValue>&) -> std::string { return "result"; },
            [](const std::shared_ptr<RecordValue>& v) -> std::string { return v->type_name; },
            [](const std::shared_ptr<RangeValue>&) -> std::string { return "range"; },
            [](const std::shared_ptr<ChoiceValue>& v) -> std::string { return v->type_name; },
            [](const std::shared_ptr<FunctionValue>&) -> std::string { return "function"; },
            [](const std::shared_ptr<NativeFunctionValue>&) -> std::string { return "function"; },
            [](const std::shared_ptr<TaskValue>&) -> std::string { return "task"; },
            [](const std::shared_ptr<ChannelValue>&) -> std::string { return "channel"; },
            [](const std::shared_ptr<SocketValue>&) -> std::string { return "socket"; },
            [](const std::shared_ptr<CollectionObject>& v) -> std::string {
                return v->display_type_name();
            },
            [](const std::shared_ptr<ReferenceValue>&) -> std::string { return "reference"; },
        },
        data_);
}

// ─────────── CollectionObject::to_display_string implementations ───────────

// QueueValue, SetValue — simple vector-backed formatting via shared helper.

std::string QueueValue::to_display_string() const {
    return format_value_sequence(elements, "queue");
}

// StackValue

std::string StackValue::to_display_string() const {
    // Stack displays in reverse (top-first) order, so build a reversed view.
    const std::vector<std::reference_wrapper<const Value>> reversed(elements.rbegin(),
                                                                    elements.rend());
    return format_container(reversed, "stack[", "]",
                            [](const auto& elem, std::string& r) { r += elem.get().to_string(); });
}

// SetValue

std::string SetValue::to_display_string() const {
    return format_value_sequence(elements, "set");
}

// XmlValue

std::string XmlValue::to_display_string() const {
    if (node_type != NodeType::Element) {
        return tag_or_content;
    }

    return std::format("<{}>...</{}>", tag_or_content, tag_or_content);
}

// KeyValueStoreValue

std::string KeyValueStoreValue::to_display_string() const {
    const std::scoped_lock lock{mutex};
    return std::format("<key_value_store:{} entries>", entries.size());
}

// HashSetValue

std::string HashSetValue::to_display_string() const {
    // Flatten all bucket elements into a single view for formatting.
    std::vector<std::reference_wrapper<const Value>> all_elements;
    all_elements.reserve(count_);

    for (const auto& [h, bucket] : buckets) {
        for (const auto& elem : bucket) {
            all_elements.emplace_back(elem);
        }
    }

    return format_container(all_elements, "hash_set[", "]",
                            [](const auto& elem, std::string& r) { r += elem.get().to_string(); });
}

// LinkedListValue

std::string LinkedListValue::to_display_string() const {
    // Collect node values into a temporary vector for format_container.
    std::vector<std::reference_wrapper<const Value>> values;
    values.reserve(count_);
    for (auto cur = head; cur; cur = cur->next) {
        values.emplace_back(cur->value);
    }

    return format_container(values, "linked_list[", "]",
                            [](const auto& elem, std::string& r) { r += elem.get().to_string(); });
}

// BinaryTreeValue

std::string BinaryTreeValue::to_display_string() const {
    return std::format("<binary_tree:{} nodes>", count_);
}

// GraphValue

std::string GraphValue::to_display_string() const {
    return std::format("<graph:{} vertices, {} edges>", adjacency.size(), logical_edge_count());
}

} // namespace luma
