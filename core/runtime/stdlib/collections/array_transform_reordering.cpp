// Array module — transform/query: reordering operations (reverse, sort, flatten, zip).
// Split from array_transform.cpp for readability.  Registered by
// register_array_transform() via register_array_transform_reordering().

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numeric>
#include <ranges>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/overflow.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/array_module.hpp"
#include "runtime/stdlib/collections/value_compare.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

/// Comparison function for sort — wraps a Luma callable.
/// Returns true if a should precede b according to the comparator.
/// `call_args` is a reusable 2-element buffer supplied by the caller so the sort
/// does not heap-allocate a fresh argument vector on every O(n log n) comparison.
[[nodiscard]] bool sort_comparator(const Value& a, const Value& b, const Value& comparator,
                                   std::vector<Value>& call_args, const SourceLocation& loc) {
    call_args[0] = a;
    call_args[1] = b;
    const auto result = invoke_callable(comparator, call_args, loc);
    return result.to_numeric() < 0;
}

/// Flattens one level of nested arrays into a single output array.
/// Returns a failure Value if the result would exceed resource limits.
[[nodiscard]] Value array_flatten_impl(const std::shared_ptr<ArrayValue>& src, SourceLocation loc) {
    auto arr = std::make_shared<ArrayValue>();

    for (const auto& elem : *src->elements) {
        if (elem.is_array()) {
            append_bounded(*arr->elements, *elem.as_array()->elements, loc, "Array.flatten");
        } else {
            arr->elements->push_back(elem);
        }
    }
    return Value{std::move(arr)};
}

[[nodiscard]] Value array_sort_by_impl(const std::shared_ptr<ArrayValue>& src, const Args& args,
                                       SourceLocation loc) {
    const auto& key_fn = args[1];

    return apply_with_error_handling([&]() -> Value {
        const auto n = src->elements->size();

        struct KeyIndex {
            Value key;
            std::size_t idx;
        };

        std::vector<KeyIndex> key_indices;
        key_indices.reserve(n);

        std::vector<Value> call_args(1);
        for (std::size_t i = 0; i < n; ++i) {
            call_args[0] = (*src->elements)[i];
            key_indices.push_back({.key = invoke_callable(key_fn, call_args, loc), .idx = i});
        }

        // stable_sort, not sort: compare_values need not form a strict weak
        // ordering — a key set mixing integer and number values whose
        // magnitudes reach 2^53 compares intransitively (the exact int64 path
        // and the double-widening path disagree at the precision boundary).
        // std::ranges::sort (introsort) has undefined behaviour then: its
        // unguarded partition uses the pivot as a sentinel and can read/write
        // past the buffer, corrupting the heap.  stable_sort's merge path only
        // ever compares elements within valid bounds, so a malformed comparator
        // yields an unspecified-but-memory-safe order (matching Array.sort and
        // LinkedList.sort).  Stability also preserves the original order of
        // equal-key elements, so no explicit index tie-break is required.
        std::ranges::stable_sort(key_indices, [&loc](const KeyIndex& a, const KeyIndex& b) {
            return compare_values(a.key, b.key, loc, "Array.sort_by") < 0;
        });

        auto arr = std::make_shared<ArrayValue>();
        arr->elements->reserve(n);

        for (const auto& ki : key_indices) {
            arr->elements->push_back((*src->elements)[ki.idx]);
        }

        return Value{std::move(arr)};
    });
}

} // namespace

void register_array_transform_reordering(const EnvPtr& env) {
    ModuleBuilder{"Array", env}
        .func("reverse", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          auto arr = clone_array(src);
                          std::ranges::reverse(*arr->elements);
                          return Value{std::move(arr)};
                      })
        .func("sort", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          const auto& comparator = args[1];

                          return apply_with_error_handling([&]() -> Value {
                              auto arr = clone_array(src);
                              // Reuse one 2-element buffer across all comparisons
                              // instead of allocating a vector per comparison.
                              std::vector<Value> call_args(2);
                              // stable_sort, not sort: the comparator is
                              // arbitrary user code that need not form a strict
                              // weak ordering.  std::ranges::sort (introsort)
                              // has undefined behaviour then — its unguarded
                              // partition uses the pivot as a sentinel and can
                              // read/write past the buffer, corrupting the heap.
                              // stable_sort's merge path only ever compares
                              // elements within valid bounds, so a malformed
                              // comparator yields an unspecified-but-memory-safe
                              // order (matching JS/Python) instead of a crash.
                              std::ranges::stable_sort(
                                  *arr->elements,
                                  [&comparator, &call_args, &loc](const Value& a, const Value& b) {
                                      return sort_comparator(a, b, comparator, call_args, loc);
                                  });
                              return Value{std::move(arr)};
                          });
                      })
        .func("sort_by", 2)
        .extract_body(expect_array,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          return array_sort_by_impl(src, args, loc);
                      })
        .func("unique", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation) -> Value {
                          auto arr = std::make_shared<ArrayValue>();
                          dedup_in_order(*src->elements, [&](const Value& elem) {
                              arr->elements->push_back(elem);
                          });
                          return Value{std::move(arr)};
                      })
        .func("flatten", 1)
        .extract_body(expect_array,
                      [](const auto& src, const Args&, SourceLocation loc) -> Value {
                          return array_flatten_impl(src, loc);
                      })
        .func("zip", 2)
        .extract_body(expect_array,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          const auto& b = *expect_array(args[1], "Array.zip", loc)->elements;

                          auto arr = std::make_shared<ArrayValue>();
                          const auto len = std::min(a->elements->size(), b.size());
                          arr->elements->reserve(len);

                          for (std::size_t i{0}; i < len; ++i) {
                              arr->elements->push_back(make_tuple_pair((*a->elements)[i], b[i]));
                          }
                          return Value{std::move(arr)};
                      })
        // Array.unzip(array<(T, U)>) -> (array<T>, array<U>) — the inverse of
        // Array.zip: split an array of pairs back into two parallel arrays.
        .func("unzip", 1)
        .extract_body(expect_array, [](const auto& src, const Args&, SourceLocation loc) -> Value {
            auto firsts = std::make_shared<ArrayValue>();
            auto seconds = std::make_shared<ArrayValue>();
            firsts->elements->reserve(src->elements->size());
            seconds->elements->reserve(src->elements->size());

            for (const auto& elem : *src->elements) {
                if (!elem.is_tuple() || elem.as_tuple()->elements.size() != 2) {
                    throw RuntimeError{"Array.unzip: each element must be a 2-tuple", loc,
                                       "pass an array of (T, U) pairs, e.g. the "
                                       "output of Array.zip"};
                }

                const auto& pair = elem.as_tuple()->elements;
                firsts->elements->push_back(pair[0]);
                seconds->elements->push_back(pair[1]);
            }

            return make_tuple_pair(Value{std::move(firsts)}, Value{std::move(seconds)});
        });
}

} // namespace luma
