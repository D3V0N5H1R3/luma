#pragma once

// Shared helper functions for CollectionObject subtype operations.
//
// These free-function templates consolidate the common patterns used by
// vector-backed collections (QueueValue, StackValue, SetValue) across
// value_copying.cpp, value_equality.cpp, and value_formatting.cpp.
//
// By centralising these patterns in one header, each collection's
// virtual-method override becomes a clean one-liner delegation, and the
// logic lives in a single place rather than being duplicated in
// anonymous namespaces across three translation units.

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "runtime/interpreter/value_type.hpp"

namespace luma::collection_helpers {

// ─────────── Formatting helpers ───────────

// Safety limits to prevent unbounded output when formatting large containers.
constexpr std::size_t k_max_format_output = 1024 * 1024; // 1 MB
constexpr std::size_t k_max_format_items = 1000;

// Format a container's elements as a comma-separated list between delimiters.
// Generic version used by all formatting helpers below.
// Output is truncated when it exceeds k_max_format_output bytes or
// k_max_format_items elements to guard against runaway allocations.
template <typename Container, typename Formatter>
[[nodiscard]] std::string format_container(const Container& items, std::string_view open,
                                           std::string_view close, Formatter fmt) {
    std::string r{open};
    r.reserve(open.size() + close.size() + items.size() * 16);

    bool first = true;
    std::size_t item_count = 0;

    for (const auto& item : items) {
        if (item_count >= k_max_format_items) {
            r += ", ...";
            break;
        }

        if (r.size() >= k_max_format_output) {
            r += "...";
            break;
        }

        if (!first) {
            r += ", ";
        }

        first = false;
        ++item_count;
        fmt(item, r);
    }

    r += close;
    return r;
}

// Format a vector<Value> as "type_name[elem1, elem2, ...]".
// Used by QueueValue and SetValue whose display format is identical
// except for the type name prefix.
[[nodiscard]] inline std::string format_value_sequence(const std::vector<Value>& elements,
                                                       std::string_view type_name) {
    auto open = std::string{type_name} + "[";
    return format_container(elements, open, "]",
                            [](const Value& elem, std::string& r) { r += elem.to_string(); });
}

// ─────────── Deep-copy helpers ───────────

// Access the element vector regardless of storage strategy (handles
// ArrayValue's COW shared_ptr vs direct vector members).
template <typename T> auto& elements_of(T& collection) {
    if constexpr (std::is_same_v<std::remove_const_t<T>, ArrayValue>) {
        return *collection.elements;
    } else {
        return collection.elements;
    }
}

// Deep-copy a collection type whose values live in an elements vector.
// Covers ArrayValue, TupleValue, QueueValue, StackValue, and SetValue.
template <typename Collection>
[[nodiscard]] Value deep_copy_elements_collection(const Collection& source) {
    auto copy = std::make_shared<Collection>();
    const auto& src = elements_of(source);
    auto& dst = elements_of(*copy);
    dst.reserve(src.size());
    std::ranges::transform(src, std::back_inserter(dst),
                           [](const Value& elem) { return elem.deep_copy(); });
    return Value{std::move(copy)};
}

// ─────────── Equality helpers ───────────

// Compare two sequential containers element-by-element using Value::equals().
template <typename Container>
[[nodiscard]] bool elements_equal(const Container& a, const Container& b) {
    return std::ranges::equal(a, b, [](const auto& x, const auto& y) { return x.equals(y); });
}

// Compare two CollectionObject subclasses that store elements in a vector.
// Covers QueueValue and StackValue (sequential, order-dependent equality).
template <typename Collection>
[[nodiscard]] bool sequential_collection_equals(const Collection& a,
                                                const CollectionObject& other) {
    return elements_equal(a.elements, static_cast<const Collection&>(other).elements);
}

} // namespace luma::collection_helpers
