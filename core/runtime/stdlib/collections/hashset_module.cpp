#include "runtime/stdlib/collections/hashset_module.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/native_function_containers.hpp"

namespace luma {

// A hash set uses hashing for O(1) average-case membership tests.
// Only hashable primitive types (integer, number, string, boolean)
// are supported.  Non-hashable values cause a runtime error.

namespace {

// Hash a Value.  Throws RuntimeError for non-hashable types.
[[nodiscard]] std::size_t hash_value(const Value& v, const SourceLocation& loc) {
    // Reuse the canonical structural hash (ValueHash) so membership stays
    // consistent with Value::equals: integer(5) and number(5.0) compare equal
    // and therefore MUST land in the same bucket. Hashing integers as int64
    // while numbers hash as double would scatter equal values across different
    // buckets and break both contains() and de-duplication.
    if (v.is_integer() || v.is_number() || v.is_string() || v.is_bool()) {
        return ValueHash{}(v);
    }

    throw RuntimeError{
        error_msg("HashSet", "add",
                  std::format("only hashable types (integer, number, string, boolean) "
                              "are supported, got '{}'",
                              v.display_type_name())),
        loc};
}

// Find a value in a bucket chain.
[[nodiscard]] bool bucket_contains(std::span<const Value> bucket, const Value& needle) {
    return std::ranges::any_of(bucket, [&](const Value& v) { return v.equals(needle); });
}

// Membership test using a hash the caller has already computed (the bucket key
// yielded while iterating another set), so the set-algebra operations avoid
// re-hashing every element.
[[nodiscard]] bool set_contains(const HashSetValue& hs, std::size_t h, const Value& elem) {
    const auto it = hs.buckets.find(h);

    return it != hs.buckets.end() && bucket_contains(it->second, elem);
}

// Insert a value into a hash set if not already present.
void insert_into_set(HashSetValue& hs, const Value& elem, const SourceLocation& loc) {
    const auto h = hash_value(elem, loc);

    auto& bucket = hs.buckets[h];

    if (!bucket_contains(bucket, elem)) {
        validate_container_size(hs.count_, ResourceLimits::max_hash_set_size, "HashSet.add", loc);

        bucket.push_back(elem);

        ++hs.count_;
    }
}

// Append an element the caller has already shown to be absent, using a
// precomputed hash.  Skips the dedup scan that insert_into_set performs — the
// set-algebra operations build results whose uniqueness is guaranteed by
// construction — but still enforces the max_hash_set_size cap: a result must
// not exceed the limit (symmetric_difference can emit |a| + |b| elements from
// two in-cap inputs, and the oversized result could be fed back in).
void push_known(HashSetValue& hs, std::size_t h, const Value& elem, std::string_view function_name,
                const SourceLocation& loc) {
    validate_container_size(hs.count_, ResourceLimits::max_hash_set_size, function_name, loc);

    hs.buckets[h].push_back(elem);

    ++hs.count_;
}

// Apply a callable to each element in a hash set, iterating all buckets.
template <typename Fn> void for_each_element(const HashSetValue& hs, Fn&& fn) {
    for (const auto& [h, bucket] : hs.buckets) {
        for (const auto& elem : bucket) {
            fn(h, elem);
        }
    }
}

// Return true if pred(h, elem) holds for every element, stopping at the first
// element that fails.  Unlike for_each_element this short-circuits, so the
// predicate operations (is_subset, equals, ...) do not scan the whole set once
// the answer is known.
template <typename Fn> [[nodiscard]] bool all_of_elements(const HashSetValue& hs, Fn&& pred) {
    for (const auto& [h, bucket] : hs.buckets) {
        for (const auto& elem : bucket) {
            if (!pred(h, elem)) {
                return false;
            }
        }
    }

    return true;
}

// Clone a hash set (copy-on-write).
[[nodiscard]] std::shared_ptr<HashSetValue>
clone_hash_set(const std::shared_ptr<HashSetValue>& src) {
    auto copy = std::make_shared<HashSetValue>();
    copy->buckets = src->buckets;
    copy->count_ = src->count_;
    return copy;
}

} // namespace

void register_hashset_ns(const EnvPtr& env) {
    // HashSet uses bucket-based hashing so it cannot use ContainerOps or
    // ContainerModuleBuilder (which assume a contiguous `elements` vector).
    // Register new() directly and define all operations via ModuleBuilder.
    ModuleBuilder{"HashSet", env}.func("new", 0).raw_body(
        []([[maybe_unused]] std::span<const Value>, [[maybe_unused]] SourceLocation) -> Value {
            return Value{std::make_shared<HashSetValue>()};
        });

    // HashSet-specific operations.
    ModuleBuilder{"HashSet", env}
        .func("from_array", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& src = expect_array(args[0], "HashSet.from_array", loc);

            auto hs = std::make_shared<HashSetValue>();

            for (const auto& elem : *src->elements) {
                insert_into_set(*hs, elem, loc);
            }

            return Value{std::move(hs)};
        })
        .func("contains", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.contains", loc);

            return Value{set_contains(*src, hash_value(args[1], loc), args[1])};
        })
        .func("length", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.length", loc);

            return Value{static_cast<std::int64_t>(src->count_)};
        })
        .func("is_empty", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.is_empty", loc);

            return Value{src->count_ == 0};
        })
        .func("add", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.add", loc);

            auto result = clone_hash_set(src);

            insert_into_set(*result, args[1], loc);

            return Value{std::move(result)};
        })
        .func("remove", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.remove", loc);

            auto result = clone_hash_set(src);

            const auto h = hash_value(args[1], loc);

            auto it = result->buckets.find(h);

            if (it != result->buckets.end()) {
                auto& bucket = it->second;

                auto pos =
                    std::ranges::find_if(bucket, [&](const Value& v) { return v.equals(args[1]); });

                if (pos != bucket.end()) {
                    bucket.erase(pos);

                    --result->count_;

                    if (bucket.empty()) {
                        result->buckets.erase(it);
                    }
                }
            }

            return Value{std::move(result)};
        })
        .func("union", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_hash_set(args[0], "HashSet.union", loc);
            auto b = expect_hash_set(args[1], "HashSet.union", loc);

            auto result = clone_hash_set(a);

            for_each_element(
                *b, [&](std::size_t, const Value& elem) { insert_into_set(*result, elem, loc); });

            return Value{std::move(result)};
        })
        .func("intersection", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_hash_set(args[0], "HashSet.intersection", loc);
            auto b = expect_hash_set(args[1], "HashSet.intersection", loc);

            auto result = std::make_shared<HashSetValue>();

            for_each_element(*a, [&](std::size_t h, const Value& elem) {
                if (set_contains(*b, h, elem)) {
                    push_known(*result, h, elem, "HashSet.intersection", loc);
                }
            });

            return Value{std::move(result)};
        })
        .func("difference", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_hash_set(args[0], "HashSet.difference", loc);
            auto b = expect_hash_set(args[1], "HashSet.difference", loc);

            auto result = std::make_shared<HashSetValue>();

            for_each_element(*a, [&](std::size_t h, const Value& elem) {
                if (!set_contains(*b, h, elem)) {
                    push_known(*result, h, elem, "HashSet.difference", loc);
                }
            });

            return Value{std::move(result)};
        })
        .func("is_subset", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_hash_set(args[0], "HashSet.is_subset", loc);
            auto b = expect_hash_set(args[1], "HashSet.is_subset", loc);

            return Value{all_of_elements(
                *a, [&](std::size_t h, const Value& elem) { return set_contains(*b, h, elem); })};
        })
        .func("is_superset", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_hash_set(args[0], "HashSet.is_superset", loc);
            auto b = expect_hash_set(args[1], "HashSet.is_superset", loc);

            return Value{all_of_elements(
                *b, [&](std::size_t h, const Value& elem) { return set_contains(*a, h, elem); })};
        })
        .func("is_disjoint", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_hash_set(args[0], "HashSet.is_disjoint", loc);
            auto b = expect_hash_set(args[1], "HashSet.is_disjoint", loc);

            return Value{all_of_elements(
                *a, [&](std::size_t h, const Value& elem) { return !set_contains(*b, h, elem); })};
        })
        .func("equals", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_hash_set(args[0], "HashSet.equals", loc);
            auto b = expect_hash_set(args[1], "HashSet.equals", loc);

            if (a->count_ != b->count_) {
                return Value{false};
            }

            return Value{all_of_elements(
                *a, [&](std::size_t h, const Value& elem) { return set_contains(*b, h, elem); })};
        })
        .func("to_array", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.to_array", loc);
            auto result = std::make_shared<ArrayValue>();

            for_each_element(
                *src, [&](std::size_t, const Value& elem) { result->elements->push_back(elem); });

            return Value{std::move(result)};
        })
        .func("to_set", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.to_set", loc);
            auto result = std::make_shared<SetValue>();

            for_each_element(
                *src, [&](std::size_t, const Value& elem) { result->elements.push_back(elem); });

            return Value{std::move(result)};
        })
        .func("from_set", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& src = expect_set(args[0], "HashSet.from_set", loc);

            auto hs = std::make_shared<HashSetValue>();

            for (const auto& elem : src->elements) {
                insert_into_set(*hs, elem, loc);
            }

            return Value{std::move(hs)};
        })
        .func("symmetric_difference", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = expect_hash_set(args[0], "HashSet.symmetric_difference", loc);
            auto b = expect_hash_set(args[1], "HashSet.symmetric_difference", loc);

            auto result = std::make_shared<HashSetValue>();

            // Elements in a but not in b.
            for_each_element(*a, [&](std::size_t h, const Value& elem) {
                if (!set_contains(*b, h, elem)) {
                    push_known(*result, h, elem, "HashSet.symmetric_difference", loc);
                }
            });

            // Elements in b but not in a.
            for_each_element(*b, [&](std::size_t h, const Value& elem) {
                if (!set_contains(*a, h, elem)) {
                    push_known(*result, h, elem, "HashSet.symmetric_difference", loc);
                }
            });

            return Value{std::move(result)};
        })
        .func("filter", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.filter", loc);
            auto result = std::make_shared<HashSetValue>();

            return iter_filter(
                HashSetBucketIterator{src->buckets.begin(), src->buckets.end()},
                HashSetBucketIterator{}, args[1],
                [&](const Value& v) { insert_into_set(*result, v, loc); },
                [&]() -> Value { return Value{std::move(result)}; }, loc);
        })
        .func("reduce", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.reduce", loc);

            return iter_reduce(HashSetBucketIterator{src->buckets.begin(), src->buckets.end()},
                               HashSetBucketIterator{}, args[1], args[2], loc);
        })
        .func("map", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.map", loc);
            auto result = std::make_shared<HashSetValue>();

            return iter_map(
                HashSetBucketIterator{src->buckets.begin(), src->buckets.end()},
                HashSetBucketIterator{}, args[1],
                [&](Value mapped) { insert_into_set(*result, mapped, loc); },
                [&]() -> Value { return Value{std::move(result)}; }, loc);
        })
        .func("each", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.each", loc);

            return container_each(HashSetBucketIterator{src->buckets.begin(), src->buckets.end()},
                                  HashSetBucketIterator{}, args[1], loc);
        })
        .func("partition", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto src = expect_hash_set(args[0], "HashSet.partition", loc);
            auto matches = std::make_shared<HashSetValue>();
            auto rest = std::make_shared<HashSetValue>();

            return iter_partition(
                HashSetBucketIterator{src->buckets.begin(), src->buckets.end()},
                HashSetBucketIterator{}, args[1],
                [&](const Value& v) { insert_into_set(*matches, v, loc); },
                [&](const Value& v) { insert_into_set(*rest, v, loc); },
                [&]() -> Value {
                    return make_tuple_pair(Value{std::move(matches)}, Value{std::move(rest)});
                },
                loc);
        });
}
} // namespace luma
