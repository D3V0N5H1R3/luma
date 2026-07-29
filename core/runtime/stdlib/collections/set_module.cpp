#include "runtime/stdlib/collections/set_module.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <span>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/container_module_builder.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

[[nodiscard]] bool contains_value(std::span<const Value> elements, const Value& needle) {
    return std::ranges::any_of(elements, [&](const Value& v) { return v.equals(needle); });
}

// Predicate helpers for the set-algebra queries.  `set.contains()` consults
// SetValue's own cached hash index, so each test is O(1) average and the whole
// scan is O(n) — no throwaway per-call index is built.
//
// NOTE: contains() lazily builds and caches a *mutable* hash index on first
// use.  Repeated operations on the same set therefore get faster, but a set
// shared read-only across tasks inherits the same shared-mutable-index
// concurrency profile that Set.contains already has.  This is a pre-existing
// trade-off of SetValue::contains, not new to these helpers, and there is no
// characterization test for concurrent set sharing (known gap).
[[nodiscard]] bool all_members_in(std::span<const Value> xs, const SetValue& set) {
    return std::ranges::all_of(xs, [&](const Value& e) { return set.contains(e); });
}

[[nodiscard]] bool any_member_in(std::span<const Value> xs, const SetValue& set) {
    return std::ranges::any_of(xs, [&](const Value& e) { return set.contains(e); });
}

// Builder helper shared by union/intersection/difference/symmetric_difference.
// Appends each element of `xs` to `out` when its membership in `set` equals
// `keep_when_present`.  Both operands are sets (unique elements) and each call
// contributes a slice that is disjoint from what is already in `out`, so a kept
// element is always unique in the result — push directly after the single size
// check instead of an O(n) rescan, keeping each operation O(n + m).
void append_members(std::vector<Value>& out, std::span<const Value> xs, const SetValue& set,
                    bool keep_when_present, std::string_view op, const SourceLocation& loc) {
    for (const auto& elem : xs) {
        if (set.contains(elem) == keep_when_present) {
            validate_container_size(out.size(), ResourceLimits::max_set_size, op, loc);
            out.push_back(elem);
        }
    }
}

// Append a value to a set's element list if not already present.  Centralises
// the dedup-and-size-validation rule so Set.add and Set.from_array cannot drift
// apart from the set-uniqueness invariant.
void push_unique(std::vector<Value>& elements, const Value& value, const SourceLocation& loc) {
    if (!contains_value(elements, value)) {
        validate_container_size(elements.size(), ResourceLimits::max_set_size, "Set", loc);
        elements.push_back(value);
    }
}

} // namespace

void register_set_ns(const EnvPtr& env) {
    // Register new() and common ops (length, is_empty, to_array, map,
    // filter, reduce, each, partition, concat) via builder.  The Unique=true
    // template argument makes the container ops honour the set-uniqueness
    // invariant: map deduplicates its output via a hash ValueSet (O(n)), while
    // filter/partition push directly (their subset of an already-unique set
    // stays unique), avoiding the former O(n^2) push_unique rescan.
    const ContainerModuleBuilder<SetValue, /*ReverseEach=*/false, /*Unique=*/true> cmb{
        "Set", env, expect_set, ResourceLimits::max_set_size};
    cmb.register_new();
    cmb.register_common_ops();

    // Set-specific operations (from_array uses dedup logic, so registered manually).
    cmb.builder()
        .func("from_array", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& src = expect_array(args[0], "Set.from_array", loc);

            auto s = std::make_shared<SetValue>();
            dedup_in_order(*src->elements, [&](const Value& elem) {
                validate_container_size(s->elements.size(), ResourceLimits::max_set_size,
                                        "Set.from_array", loc);
                s->elements.push_back(elem);
            });
            return Value{std::move(s)};
        })
        .func("contains", 2)
        .extract_body(expect_set,
                      [](const auto& src, const Args& args, SourceLocation) -> Value {
                          return Value{src->contains(args[1])};
                      })
        .func("union", 2)
        .extract_body(expect_set,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          auto b = expect_set(args[1], "Set.union", loc);
                          auto result = clone_container<SetValue>(a);

                          append_members(result->elements, b->elements, *a,
                                         /*keep_when_present=*/false, "Set.union", loc);
                          return Value{std::move(result)};
                      })
        .func("intersection", 2)
        .extract_body(expect_set,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          auto b = expect_set(args[1], "Set.intersection", loc);
                          auto result = std::make_shared<SetValue>();

                          append_members(result->elements, a->elements, *b,
                                         /*keep_when_present=*/true, "Set.intersection", loc);
                          return Value{std::move(result)};
                      })
        .func("difference", 2)
        .extract_body(expect_set,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          auto b = expect_set(args[1], "Set.difference", loc);
                          auto result = std::make_shared<SetValue>();

                          append_members(result->elements, a->elements, *b,
                                         /*keep_when_present=*/false, "Set.difference", loc);
                          return Value{std::move(result)};
                      })
        .func("is_subset", 2)
        .extract_body(expect_set,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          auto b = expect_set(args[1], "Set.is_subset", loc);
                          return Value{all_members_in(a->elements, *b)};
                      })
        .func("is_superset", 2)
        .extract_body(expect_set,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          auto b = expect_set(args[1], "Set.is_superset", loc);
                          return Value{all_members_in(b->elements, *a)};
                      })
        .func("is_disjoint", 2)
        .extract_body(expect_set,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          auto b = expect_set(args[1], "Set.is_disjoint", loc);
                          return Value{!any_member_in(a->elements, *b)};
                      })
        .func("equals", 2)
        .extract_body(expect_set,
                      [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                          auto b = expect_set(args[1], "Set.equals", loc);

                          if (a->elements.size() != b->elements.size()) {
                              return Value{false};
                          }
                          return Value{all_members_in(a->elements, *b)};
                      })
        .func("add", 2)
        .extract_body(expect_set,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          auto result = clone_container<SetValue>(src);
                          push_unique(result->elements, args[1], loc);
                          return Value{std::move(result)};
                      })
        .func("remove", 2)
        .extract_body(expect_set,
                      [](const auto& src, const Args& args, SourceLocation) -> Value {
                          auto result = std::make_shared<SetValue>();
                          std::ranges::copy_if(
                              src->elements, std::back_inserter(result->elements),
                              [&](const Value& elem) { return !elem.equals(args[1]); });
                          return Value{std::move(result)};
                      })
        .func("symmetric_difference", 2)
        .extract_body(
            expect_set,
            [](const auto& a, const Args& args, SourceLocation loc) -> Value {
                auto b = expect_set(args[1], "Set.symmetric_difference", loc);
                auto result = std::make_shared<SetValue>();

                // (a \ b) followed by (b \ a); the two slices are disjoint, so
                // append_members can push each directly (see its contract).
                append_members(result->elements, a->elements, *b,
                               /*keep_when_present=*/false, "Set.symmetric_difference", loc);
                append_members(result->elements, b->elements, *a,
                               /*keep_when_present=*/false, "Set.symmetric_difference", loc);
                return Value{std::move(result)};
            })
        // Predicate queries mirroring Array.any/all/count/find.  Set stores its
        // elements in insertion order, so find returns the first (in stored
        // order) match, with a result<T> "not found" failure like Array.find.
        .func("any", 2)
        .extract_body(expect_set,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Set.any", loc);
                          return iter_any(src->elements.begin(), src->elements.end(), args[1], loc);
                      })
        .func("all", 2)
        .extract_body(expect_set,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Set.all", loc);
                          return iter_all(src->elements.begin(), src->elements.end(), args[1], loc);
                      })
        .func("count", 2)
        .extract_body(expect_set,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Set.count", loc);
                          return iter_count(src->elements.begin(), src->elements.end(), args[1],
                                            loc);
                      })
        .func("find", 2)
        .extract_body(expect_set,
                      [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                          expect_callable(args[1], "Set.find", loc);
                          return find_with_error_handling(
                              src->elements.begin(), src->elements.end(), args[1],
                              [](const auto& it) { return *it; }, loc);
                      });
}

} // namespace luma
