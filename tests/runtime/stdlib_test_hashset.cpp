// Standard library tests: HashSet.

#include "common/resource_limits.hpp"
#include "stdlib_test_helpers.hpp"

static void test_hashset_int_number_equivalent() {
    // integer(5) and number(5.0) compare equal, so HashSet must treat them as
    // the same element for both membership and de-duplication. (Regression:
    // HashSet used a type-sensitive hash that bucketed int and number apart,
    // so contains(5.0) missed an added 5 and add(5.0) duplicated it.)
    ASSERT_TRUE(eval("HashSet.new() |> HashSet.add(5) |> HashSet.contains(5.0)").as_bool());
    ASSERT_EQ(eval("HashSet.new() |> HashSet.add(5) |> HashSet.add(5.0) |> HashSet.length()")
                  .as_integer(),
              1);
}

static void test_hashset_add_contains() {
    ASSERT_TRUE(eval("HashSet.new() |> HashSet.add(42) |> HashSet.contains(42)").as_bool());
    ASSERT_FALSE(eval("HashSet.new() |> HashSet.add(42) |> HashSet.contains(99)").as_bool());
}

static void test_hashset_difference() {
    ASSERT_EQ(eval("HashSet.difference(HashSet.from_array([1, 2, 3]), HashSet.from_array([2, 3, "
                   "4])) |> HashSet.length()")
                  .as_integer(),
              1);
}

static void test_hashset_equals() {
    ASSERT_TRUE(eval("HashSet.equals(HashSet.from_array([1, 2, 3]), HashSet.from_array([3, 2, 1]))")
                    .as_bool());
    ASSERT_FALSE(eval("HashSet.equals(HashSet.from_array([1, 2]), HashSet.from_array([1, 2, 3]))")
                     .as_bool());
}

static void test_hashset_filter() {
    const auto v = eval("HashSet.filter("
                        "HashSet.from_array([1, 2, 3, 4, 5]),"
                        "(integer x) -> x > 3)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& hs = v.as_result()->owned_inner->as_hash_set();

    ASSERT_EQ(hs->count_, 2);
}

static void test_hashset_filter_empty() {
    const auto v = eval("HashSet.filter("
                        "HashSet.from_array([1, 2, 3]),"
                        "(integer x) -> x > 10)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& hs = v.as_result()->owned_inner->as_hash_set();

    ASSERT_EQ(hs->count_, 0);
}

static void test_hashset_from_array() {
    ASSERT_EQ(eval("HashSet.from_array([1, 2, 3, 2, 1]) |> HashSet.length()").as_integer(), 3);
}

static void test_hashset_intersection() {
    ASSERT_EQ(eval("HashSet.intersection(HashSet.from_array([1, 2, 3]), HashSet.from_array([2, 3, "
                   "4])) |> HashSet.length()")
                  .as_integer(),
              2);
}

static void test_hashset_is_empty() {
    ASSERT_TRUE(eval("HashSet.new() |> HashSet.is_empty()").as_bool());
    ASSERT_FALSE(eval("HashSet.from_array([1]) |> HashSet.is_empty()").as_bool());
}

static void test_hashset_is_subset() {
    ASSERT_TRUE(eval("HashSet.is_subset(HashSet.from_array([1, 2]), HashSet.from_array([1, 2, 3]))")
                    .as_bool());
    ASSERT_TRUE(
        !eval("HashSet.is_subset(HashSet.from_array([1, 2, 4]), HashSet.from_array([1, 2, 3]))")
             .as_bool());
}

static void test_hashset_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("HashSet.new"));
    ASSERT_TRUE(env->has("HashSet.from_array"));
    ASSERT_TRUE(env->has("HashSet.contains"));
    ASSERT_TRUE(env->has("HashSet.add"));
    ASSERT_TRUE(env->has("HashSet.remove"));
    ASSERT_TRUE(env->has("HashSet.length"));
    ASSERT_TRUE(env->has("HashSet.is_empty"));
    ASSERT_TRUE(env->has("HashSet.union"));
    ASSERT_TRUE(env->has("HashSet.intersection"));
    ASSERT_TRUE(env->has("HashSet.difference"));
    ASSERT_TRUE(env->has("HashSet.to_array"));
    ASSERT_TRUE(env->has("HashSet.to_set"));
    ASSERT_TRUE(env->has("HashSet.from_set"));
    ASSERT_TRUE(env->has("HashSet.is_subset"));
    ASSERT_TRUE(env->has("HashSet.is_superset"));
    ASSERT_TRUE(env->has("HashSet.is_disjoint"));
    ASSERT_TRUE(env->has("HashSet.equals"));
    ASSERT_TRUE(env->has("HashSet.symmetric_difference"));
    ASSERT_TRUE(env->has("HashSet.filter"));
    ASSERT_TRUE(env->has("HashSet.reduce"));
    ASSERT_TRUE(env->has("HashSet.map"));
    ASSERT_TRUE(env->has("HashSet.each"));
    ASSERT_TRUE(env->has("HashSet.partition"));
}

static void test_hashset_partition() {
    const auto v = eval("HashSet.partition("
                        "HashSet.from_array([1, 2, 3, 4, 5]),"
                        "(integer x) -> x > 3)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& tup = v.as_result()->owned_inner->as_tuple()->elements;

    ASSERT_EQ(tup.size(), 2U);
    ASSERT_EQ(tup[0].as_hash_set()->count_ + tup[1].as_hash_set()->count_, 5);
}

static void test_hashset_reduce() {
    ASSERT_EVAL_INT("HashSet.reduce("
                    "HashSet.from_array([10, 20, 30]),"
                    "0,"
                    "(integer acc, integer x) -> acc + x)",
                    60);
}

static void test_hashset_reduce_empty() {
    ASSERT_EVAL_INT("HashSet.reduce("
                    "HashSet.new(),"
                    "99,"
                    "(integer acc, integer x) -> acc + x)",
                    99);
}

static void test_hashset_remove() {
    ASSERT_EQ(
        eval("HashSet.from_array([1, 2, 3]) |> HashSet.remove(2) |> HashSet.length()").as_integer(),
        2);
}

static void test_hashset_symmetric_difference() {
    // Elements unique to either set: {1} ∪ {4} = 2 elements
    ASSERT_EQ(eval("HashSet.symmetric_difference(HashSet.from_array([1, 2, 3]), "
                   "HashSet.from_array([2, 3, 4])) |> HashSet.length()")
                  .as_integer(),
              2);
    // Disjoint — all elements present
    ASSERT_EQ(eval("HashSet.symmetric_difference(HashSet.from_array([1, 2]), "
                   "HashSet.from_array([3, 4])) |> HashSet.length()")
                  .as_integer(),
              4);
}

static void test_hashset_symmetric_difference_caps_size() {
    // Regression: symmetric_difference appended results through a helper that
    // skipped the max_hash_set_size cap that HashSet.add/union enforce, so two
    // disjoint in-cap sets could build a result of |a| + |b| elements — past
    // the limit, and re-feedable to grow without bound. Lower the cap so a
    // small pair trips it.
    const LimitGuard guard{ResourceLimits::max_hash_set_size, static_cast<std::size_t>(4)};

    // Each 3-element input is within the cap, but their disjoint union is 6 > 4.
    ASSERT_THROWS(eval("HashSet.symmetric_difference(HashSet.from_array([1, 2, 3]), "
                       "HashSet.from_array([4, 5, 6]))"));
}

static void test_hashset_to_array() {
    ASSERT_EQ(eval("HashSet.from_array([5]) |> HashSet.to_array() |> Array.length()").as_integer(),
              1);
}

static void test_hashset_union() {
    ASSERT_EQ(eval("HashSet.union(HashSet.from_array([1, 2]), HashSet.from_array([2, 3])) |> "
                   "HashSet.length()")
                  .as_integer(),
              3);
}

// ─── Additional positive coverage ─────────────────────────────────────

static void test_hashset_add_immutability() {
    // add returns a new set; the original receiver is left unchanged.
    ASSERT_EQ(eval("hash_set base = HashSet.from_array([1, 2])\n"
                   "hash_set _bigger = HashSet.add(base, 3)\n"
                   "HashSet.length(base)")
                  .as_integer(),
              2);
}

static void test_hashset_boolean_elements() {
    ASSERT_EQ(eval("HashSet.from_array([true, false, true]) |> HashSet.length()").as_integer(), 2);
    ASSERT_TRUE(eval("HashSet.from_array([true, false]) |> HashSet.contains(false)").as_bool());
}

static void test_hashset_each() {
    // each visits every element for its side effects and yields result<none>.
    ASSERT_EQ(eval("reference<integer> total = Reference.new(0)\n"
                   "result<none> _r = HashSet.each(HashSet.from_array([10, 20, 30]),"
                   "(integer x) -> { Reference.set(total, Reference.get(total) + x) })\n"
                   "Reference.get(total)")
                  .as_integer(),
              60);
}

static void test_hashset_from_set() {
    ASSERT_EQ(
        eval("Set.from_array([1, 2, 3]) |> HashSet.from_set() |> HashSet.length()").as_integer(),
        3);
}

static void test_hashset_is_disjoint() {
    ASSERT_TRUE(eval("HashSet.is_disjoint(HashSet.from_array([1, 2]), HashSet.from_array([3, 4]))")
                    .as_bool());
    ASSERT_FALSE(
        eval("HashSet.is_disjoint(HashSet.from_array([1, 2, 3]), HashSet.from_array([3, 4]))")
            .as_bool());
}

static void test_hashset_is_superset() {
    ASSERT_TRUE(
        eval("HashSet.is_superset(HashSet.from_array([1, 2, 3]), HashSet.from_array([1, 2]))")
            .as_bool());
    ASSERT_FALSE(
        eval("HashSet.is_superset(HashSet.from_array([1, 2]), HashSet.from_array([1, 2, 3]))")
            .as_bool());
}

static void test_hashset_map() {
    const auto v = eval("HashSet.map(HashSet.from_array([1, 2, 3]), (integer x) -> x + 100)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_hash_set()->count_, 3);
    ASSERT_TRUE(eval("HashSet.map(HashSet.from_array([1, 2, 3]), (integer x) -> x + 100)"
                     "|> Result.unwrap() |> HashSet.contains(101)")
                    .as_bool());
}

static void test_hashset_map_deduplicates() {
    // Mapping distinct inputs onto the same output collapses duplicates,
    // preserving set semantics in the result.
    const auto v = eval("HashSet.map(HashSet.from_array([1, 2, 3, 4]), (integer x) -> x % 2)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_hash_set()->count_, 2);
}

static void test_hashset_number_elements() {
    ASSERT_EQ(eval("HashSet.from_array([1.5, 2.5, 1.5]) |> HashSet.length()").as_integer(), 2);
    ASSERT_TRUE(eval("HashSet.from_array([1.5, 2.5]) |> HashSet.contains(2.5)").as_bool());
}

static void test_hashset_remove_absent() {
    // Removing a value that is not present leaves the set unchanged.
    ASSERT_EQ(eval("HashSet.from_array([1, 2, 3]) |> HashSet.remove(99) |> HashSet.length()")
                  .as_integer(),
              3);
}

static void test_hashset_string_elements() {
    ASSERT_EQ(eval("HashSet.from_array([\"a\", \"b\", \"a\"]) |> HashSet.length()").as_integer(),
              2);
    ASSERT_TRUE(eval("HashSet.from_array([\"a\", \"b\"]) |> HashSet.contains(\"b\")").as_bool());
}

static void test_hashset_to_set() {
    ASSERT_EQ(
        eval("HashSet.from_array([1, 2, 3]) |> HashSet.to_set() |> Set.length()").as_integer(), 3);
}

static void test_hashset_union_contains() {
    ASSERT_TRUE(eval("HashSet.union(HashSet.from_array([1, 2]), HashSet.from_array([3, 4]))"
                     "|> HashSet.contains(3)")
                    .as_bool());
}

// ─── Error paths ──────────────────────────────────────────────────────
// A hash set stores values as `any`, so the type checker permits non-hashable
// elements (arrays, dictionaries, ...). Hashing them fails at runtime, and
// every hashing operation must surface that as a thrown error rather than
// silently misbehaving. The eval() helper runs unchecked (no type checking),
// so wrong receiver types also reach the runtime type guards instead of being
// rejected earlier by the type checker.

static void test_hashset_add_non_hashable_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("HashSet.add(HashSet.new(), [1, 2])"), "hashable");
}

static void test_hashset_from_array_non_hashable_throws() {
    ASSERT_THROWS(eval("HashSet.from_array([[1], [2]])"));
}

static void test_hashset_contains_non_hashable_throws() {
    ASSERT_THROWS(eval("HashSet.contains(HashSet.from_array([1, 2, 3]), [1])"));
}

static void test_hashset_remove_non_hashable_throws() {
    ASSERT_THROWS(eval("HashSet.remove(HashSet.from_array([1, 2, 3]), [1])"));
}

static void test_hashset_length_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("HashSet.length(42)"), "hash_set");
}

static void test_hashset_contains_wrong_type_throws() {
    ASSERT_THROWS(eval("HashSet.contains(42, 1)"));
}

static void test_hashset_add_wrong_type_throws() {
    ASSERT_THROWS(eval("HashSet.add(42, 1)"));
}

static void test_hashset_union_wrong_type_throws() {
    ASSERT_THROWS(eval("HashSet.union(HashSet.new(), 42)"));
}

static void test_hashset_intersection_wrong_type_throws() {
    ASSERT_THROWS(eval("HashSet.intersection(42, HashSet.new())"));
}

static void test_hashset_from_set_non_set_throws() {
    ASSERT_THROWS(eval("HashSet.from_set(42)"));
}

static void test_hashset_from_array_non_array_throws() {
    ASSERT_THROWS(eval("HashSet.from_array(42)"));
}

static void test_hashset_map_failure_propagates() {
    // Mapping to a non-hashable value surfaces as a failure result (the error
    // handling wrapper catches the runtime error) rather than throwing.
    ASSERT_EVAL_FAILURE("HashSet.map(HashSet.from_array([1, 2, 3]), (integer x) -> [x])");
}

int main() {
    RUN(test_hashset_add_contains);
    RUN(test_hashset_add_immutability);
    RUN(test_hashset_add_non_hashable_throws);
    RUN(test_hashset_add_wrong_type_throws);
    RUN(test_hashset_boolean_elements);
    RUN(test_hashset_contains_non_hashable_throws);
    RUN(test_hashset_contains_wrong_type_throws);
    RUN(test_hashset_difference);
    RUN(test_hashset_each);
    RUN(test_hashset_equals);
    RUN(test_hashset_filter);
    RUN(test_hashset_filter_empty);
    RUN(test_hashset_from_array);
    RUN(test_hashset_from_array_non_array_throws);
    RUN(test_hashset_from_array_non_hashable_throws);
    RUN(test_hashset_from_set);
    RUN(test_hashset_from_set_non_set_throws);
    RUN(test_hashset_intersection);
    RUN(test_hashset_intersection_wrong_type_throws);
    RUN(test_hashset_is_disjoint);
    RUN(test_hashset_is_empty);
    RUN(test_hashset_is_subset);
    RUN(test_hashset_is_superset);
    RUN(test_hashset_length_wrong_type_throws);
    RUN(test_hashset_map);
    RUN(test_hashset_map_deduplicates);
    RUN(test_hashset_map_failure_propagates);
    RUN(test_hashset_module);
    RUN(test_hashset_number_elements);
    RUN(test_hashset_partition);
    RUN(test_hashset_reduce);
    RUN(test_hashset_reduce_empty);
    RUN(test_hashset_remove);
    RUN(test_hashset_remove_absent);
    RUN(test_hashset_remove_non_hashable_throws);
    RUN(test_hashset_string_elements);
    RUN(test_hashset_symmetric_difference);
    RUN(test_hashset_symmetric_difference_caps_size);
    RUN(test_hashset_to_array);
    RUN(test_hashset_to_set);
    RUN(test_hashset_union);
    RUN(test_hashset_union_contains);
    RUN(test_hashset_union_wrong_type_throws);
    RUN(test_hashset_int_number_equivalent);
    return SUMMARY();
}
