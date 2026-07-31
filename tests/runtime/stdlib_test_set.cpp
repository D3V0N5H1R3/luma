// Standard library tests: Set.

#include "stdlib_test_helpers.hpp"

static void test_set_any_all_count_find() {
    ASSERT_EQ(eval("Set.from_array([1,2,3,4]) |> Set.any((integer x) -> x > 3) |> Result.unwrap()")
                  .as_bool(),
              true);
    ASSERT_EQ(eval("Set.from_array([1,2,3,4]) |> Set.all((integer x) -> x > 0) |> Result.unwrap()")
                  .as_bool(),
              true);
    ASSERT_EQ(eval("Set.from_array([1,2,3,4]) |> Set.count((integer x) -> x % 2 == 0) "
                   "|> Result.unwrap()")
                  .as_integer(),
              2);
    // find returns the first (stored-order) match.
    ASSERT_EQ(eval("Set.from_array([1,2,3,4]) |> Set.find((integer x) -> x % 2 == 0) "
                   "|> Result.unwrap()")
                  .as_integer(),
              2);
    ASSERT_RESULT_FAILURE(eval("Set.from_array([1,3]) |> Set.find((integer x) -> x > 9)"));
}

static void test_set_bulk_ops_preserve_unique_results() {
    // Regression: the O(n+m) bulk-operation rewrite must preserve set semantics.
    // Results stay unique, and de-duplication still treats equal int/number
    // values as one element (integer 5 equals number 5.0).
    ASSERT_EQ(eval("Set.length(Set.from_array([5, 5.0]))").as_integer(), 1);
    ASSERT_EQ(
        eval("Set.length(Set.intersection(Set.from_array([1, 2, 3]), Set.from_array([2, 3, 4])))")
            .as_integer(),
        2);
    ASSERT_EQ(
        eval("Set.length(Set.difference(Set.from_array([1, 2, 3]), Set.from_array([2, 3, 4])))")
            .as_integer(),
        1);
    ASSERT_EQ(eval("Set.length(Set.symmetric_difference(Set.from_array([1, 2, 3]), "
                   "Set.from_array([2, 3, 4])))")
                  .as_integer(),
              2);
}

static void test_set_add() {
    const auto v = eval("Set.length(Set.add(Set.from_array([1, 2, 3]), 4))");

    ASSERT_EQ(v.as_integer(), 4);

    // Adding a duplicate should not change the size.
    const auto v2 = eval("Set.length(Set.add(Set.from_array([1, 2, 3]), 2))");

    ASSERT_EQ(v2.as_integer(), 3);
}

static void test_set_contains() {
    ASSERT_TRUE(eval("Set.contains(Set.from_array([1, 2, 3]), 2)").as_bool());
    ASSERT_FALSE(eval("Set.contains(Set.from_array([1, 2, 3]), 5)").as_bool());
}

static void test_set_difference() {
    const auto v =
        eval("Set.length(Set.difference(Set.from_array([1, 2, 3]), Set.from_array([2, 3, 4])))");

    ASSERT_EQ(v.as_integer(), 1);
}

static void test_set_equals() {
    ASSERT_TRUE(eval("Set.equals(Set.from_array([1, 2, 3]), Set.from_array([3, 2, 1]))").as_bool());
    ASSERT_FALSE(eval("Set.equals(Set.from_array([1, 2]), Set.from_array([1, 2, 3]))").as_bool());
    ASSERT_TRUE(eval("Set.equals(Set.new(), Set.new())").as_bool());
}

static void test_set_filter() {
    const auto v = eval("Set.filter("
                        "Set.from_array([1, 2, 3, 4, 5]),"
                        "(integer x) -> x > 2)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& s = v.as_result()->owned_inner->as_set();

    ASSERT_EQ(s->elements.size(), 3U);
}

static void test_set_filter_empty() {
    const auto v = eval("Set.filter("
                        "Set.from_array([1, 2, 3]),"
                        "(integer x) -> x > 10)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& s = v.as_result()->owned_inner->as_set();

    ASSERT_EQ(s->elements.size(), 0U);
}

static void test_set_from_array() {
    const auto v = eval("Set.length(Set.from_array([1, 2, 2, 3, 3, 3]))");

    ASSERT_EQ(v.as_integer(), 3);
}

static void test_set_intersection() {
    const auto v =
        eval("Set.length(Set.intersection(Set.from_array([1, 2, 3]), Set.from_array([2, 3, 4])))");

    ASSERT_EQ(v.as_integer(), 2);
}

static void test_set_is_disjoint() {
    ASSERT_TRUE(eval("Set.is_disjoint(Set.from_array([1, 2]), Set.from_array([3, 4]))").as_bool());
    ASSERT_TRUE(
        !eval("Set.is_disjoint(Set.from_array([1, 2, 3]), Set.from_array([3, 4, 5]))").as_bool());
    ASSERT_TRUE(eval("Set.is_disjoint(Set.new(), Set.from_array([1, 2]))").as_bool());
}

static void test_set_is_superset() {
    ASSERT_TRUE(
        eval("Set.is_superset(Set.from_array([1, 2, 3, 4]), Set.from_array([1, 2]))").as_bool());
    ASSERT_TRUE(
        !eval("Set.is_superset(Set.from_array([1, 2]), Set.from_array([1, 2, 3]))").as_bool());
    ASSERT_TRUE(eval("Set.is_superset(Set.from_array([1, 2, 3]), Set.new())").as_bool());
}

static void test_set_length() {
    const auto v = eval("Set.length(Set.from_array([1, 2, 3, 2]))");

    ASSERT_EQ(v.as_integer(), 3);
}

static void test_set_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Set.new"));
    ASSERT_TRUE(env->has("Set.from_array"));
    ASSERT_TRUE(env->has("Set.length"));
    ASSERT_TRUE(env->has("Set.union"));
    ASSERT_TRUE(env->has("Set.intersection"));
    ASSERT_TRUE(env->has("Set.is_superset"));
    ASSERT_TRUE(env->has("Set.is_disjoint"));
    ASSERT_TRUE(env->has("Set.equals"));
    ASSERT_TRUE(env->has("Set.add"));
    ASSERT_TRUE(env->has("Set.remove"));
}

static void test_set_partition() {
    const auto v = eval("Set.partition("
                        "Set.from_array([1, 2, 3, 4, 5]),"
                        "(integer x) -> x > 3)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& tup = v.as_result()->owned_inner->as_tuple()->elements;

    ASSERT_EQ(tup.size(), 2U);
    ASSERT_EQ(tup[0].as_set()->elements.size(), 2U);
    ASSERT_EQ(tup[1].as_set()->elements.size(), 3U);
}

static void test_set_reduce() {
    ASSERT_EVAL_INT("Set.reduce("
                    "Set.from_array([1, 2, 3, 4, 5]),"
                    "0,"
                    "(integer acc, integer x) -> acc + x)",
                    15);
}

static void test_set_reduce_empty() {
    ASSERT_EVAL_INT("Set.reduce("
                    "Set.new(),"
                    "42,"
                    "(integer acc, integer x) -> acc + x)",
                    42);
}

static void test_set_remove() {
    const auto v = eval("Set.length(Set.remove(Set.from_array([1, 2, 3]), 2))");

    ASSERT_EQ(v.as_integer(), 2);

    // Removing a non-existent element should not change the size.
    const auto v2 = eval("Set.length(Set.remove(Set.from_array([1, 2, 3]), 5))");

    ASSERT_EQ(v2.as_integer(), 3);
}

static void test_set_symmetric_difference() {
    // Elements in A only or B only: {1} ∪ {4} = 2 elements
    ASSERT_EQ(eval("Set.length(Set.symmetric_difference(Set.from_array([1, 2, 3]), "
                   "Set.from_array([2, 3, 4])))")
                  .as_integer(),
              2);
    // Disjoint sets — all elements appear
    ASSERT_EQ(
        eval("Set.length(Set.symmetric_difference(Set.from_array([1, 2]), Set.from_array([3, 4])))")
            .as_integer(),
        4);
    // Identical sets — symmetric difference is empty
    ASSERT_EQ(
        eval("Set.length(Set.symmetric_difference(Set.from_array([1, 2]), Set.from_array([1, 2])))")
            .as_integer(),
        0);
}

static void test_set_union() {
    const auto v = eval("Set.length(Set.union(Set.from_array([1, 2]), Set.from_array([2, 3])))");

    ASSERT_EQ(v.as_integer(), 3);
}

// ─── Additional positive coverage ─────────────────────────────────────

static void test_set_new_is_empty_set() {
    // new() yields an empty set value (length 0, reports empty).
    ASSERT_TRUE(eval("Set.new() |> Set.is_empty()").as_bool());
    ASSERT_EQ(eval("Set.new() |> Set.length()").as_integer(), 0);
}

static void test_set_is_empty() {
    ASSERT_TRUE(eval("Set.from_array([]) |> Set.is_empty()").as_bool());
    ASSERT_FALSE(eval("Set.from_array([1]) |> Set.is_empty()").as_bool());
}

static void test_set_is_subset() {
    ASSERT_TRUE(eval("Set.is_subset(Set.from_array([1, 2]), Set.from_array([1, 2, 3]))").as_bool());
    ASSERT_FALSE(
        eval("Set.is_subset(Set.from_array([1, 5]), Set.from_array([1, 2, 3]))").as_bool());
    // The empty set is a subset of every set.
    ASSERT_TRUE(eval("Set.is_subset(Set.new(), Set.from_array([1, 2, 3]))").as_bool());
}

static void test_set_to_array() {
    ASSERT_EQ(eval("Set.from_array([3, 1, 2, 1]) |> Set.to_array() |> Array.length()").as_integer(),
              3);
}

static void test_set_map() {
    const auto v = eval("Set.map(Set.from_array([1, 2, 3]), (integer x) -> x * 10)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_set()->elements.size(), 3U);
    ASSERT_TRUE(eval("Set.map(Set.from_array([1, 2, 3]), (integer x) -> x * 10)"
                     "|> Result.unwrap() |> Set.contains(30)")
                    .as_bool());
}

static void test_set_map_deduplicates() {
    // Mapping distinct inputs onto the same output collapses duplicates,
    // preserving set semantics in the result.
    const auto v = eval("Set.map(Set.from_array([1, 2, 3, 4]), (integer x) -> x % 2)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_set()->elements.size(), 2U);
}

static void test_set_each() {
    // each visits every element for its side effects and yields result<none>.
    ASSERT_EQ(eval("reference<integer> total = Reference.new(0)\n"
                   "result<none> _r = Set.each(Set.from_array([10, 20, 30]),"
                   "(integer x) -> { Reference.set(total, Reference.get(total) + x) })\n"
                   "Reference.get(total)")
                  .as_integer(),
              60);
}

static void test_set_concat() {
    ASSERT_EQ(
        eval("Set.concat(Set.from_array([1, 2, 3]), Set.from_array([4, 5, 6])) |> Set.length()")
            .as_integer(),
        6);
    ASSERT_TRUE(
        eval("Set.concat(Set.from_array([1, 2]), Set.from_array([3, 4])) |> Set.contains(3)")
            .as_bool());
}

static void test_set_add_immutability() {
    // add returns a new set; the original receiver is left unchanged.
    ASSERT_EQ(eval("set base = Set.from_array([1, 2])\n"
                   "set _bigger = Set.add(base, 3)\n"
                   "Set.length(base)")
                  .as_integer(),
              2);
}

static void test_set_union_contains() {
    ASSERT_TRUE(eval("Set.union(Set.from_array([1, 2]), Set.from_array([3, 4]))"
                     "|> Set.contains(3)")
                    .as_bool());
}

static void test_set_string_elements() {
    ASSERT_EQ(eval("Set.from_array([\"a\", \"b\", \"a\"]) |> Set.length()").as_integer(), 2);
    ASSERT_TRUE(eval("Set.from_array([\"a\", \"b\"]) |> Set.contains(\"b\")").as_bool());
}

static void test_set_composite_elements() {
    // Set stores any value type and deduplicates composite elements by
    // structural equality.
    ASSERT_EQ(eval("Set.from_array([[1], [2], [1]]) |> Set.length()").as_integer(), 2);
    ASSERT_TRUE(eval("Set.from_array([[1], [2]]) |> Set.contains([2])").as_bool());
}

static void test_set_composite_union() {
    // Set algebra over composite elements relies on structural hashing of the
    // index, so duplicates across the operands collapse correctly.
    ASSERT_EQ(eval("Set.union(Set.from_array([[1], [2]]), Set.from_array([[2], [3]]))"
                   "|> Set.length()")
                  .as_integer(),
              3);
}

static void test_set_map_to_composite_succeeds() {
    // Mapping set elements onto arrays succeeds for Set because elements are
    // compared by structural equality.
    const auto v = eval("Set.map(Set.from_array([1, 2, 3]), (integer x) -> [x])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_set()->elements.size(), 3U);
}

// ─── Error paths ──────────────────────────────────────────────────────
// The eval() helper runs unchecked (no type checking), so wrong receiver and
// argument types reach the runtime type guards (expect_set / expect_array)
// instead of being rejected earlier by the type checker. Each must surface as
// a thrown error. Higher-order operations wrap callback errors into a failure
// result rather than throwing.

static void test_set_length_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Set.length(42)"), "expected set");
}

static void test_set_contains_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.contains(42, 1)"));
}

static void test_set_add_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.add(42, 1)"));
}

static void test_set_remove_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.remove(42, 1)"));
}

static void test_set_union_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.union(Set.new(), 42)"));
}

static void test_set_intersection_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.intersection(42, Set.new())"));
}

static void test_set_difference_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.difference(Set.new(), 42)"));
}

static void test_set_is_subset_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.is_subset(Set.new(), 42)"));
}

static void test_set_is_superset_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.is_superset(42, Set.new())"));
}

static void test_set_is_disjoint_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.is_disjoint(Set.new(), 42)"));
}

static void test_set_equals_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.equals(Set.new(), 42)"));
}

static void test_set_symmetric_difference_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.symmetric_difference(42, Set.new())"));
}

static void test_set_from_array_non_array_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Set.from_array(42)"), "expected array");
}

static void test_set_to_array_wrong_type_throws() {
    ASSERT_THROWS(eval("Set.to_array(42)"));
}

static void test_set_map_callback_error() {
    // A callback that raises a runtime error surfaces as a failure result.
    ASSERT_RESULT_FAILURE(eval("Set.map(Set.from_array([1, 2, 3]), (integer x) -> x / 0)"));
}

static void test_set_filter_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Set.filter(Set.from_array([1, 2, 3]), (integer x) -> x / 0)"));
}

static void test_set_reduce_callback_error() {
    ASSERT_RESULT_FAILURE(
        eval("Set.reduce(Set.from_array([1, 2, 3]), 0, (integer acc, integer x) -> acc / 0)"));
}

static void test_set_partition_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Set.partition(Set.from_array([1, 2, 3]), (integer x) -> x / 0)"));
}

static void test_set_each_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Set.each(Set.from_array([1, 2, 3]), (integer x) -> x / 0)"));
}

int main() {
    RUN(test_set_add);
    RUN(test_set_add_immutability);
    RUN(test_set_add_wrong_type_throws);
    RUN(test_set_composite_elements);
    RUN(test_set_composite_union);
    RUN(test_set_concat);
    RUN(test_set_contains);
    RUN(test_set_contains_wrong_type_throws);
    RUN(test_set_difference);
    RUN(test_set_difference_wrong_type_throws);
    RUN(test_set_each);
    RUN(test_set_each_callback_error);
    RUN(test_set_equals);
    RUN(test_set_equals_wrong_type_throws);
    RUN(test_set_filter);
    RUN(test_set_filter_callback_error);
    RUN(test_set_filter_empty);
    RUN(test_set_from_array);
    RUN(test_set_from_array_non_array_throws);
    RUN(test_set_intersection);
    RUN(test_set_intersection_wrong_type_throws);
    RUN(test_set_is_disjoint);
    RUN(test_set_is_disjoint_wrong_type_throws);
    RUN(test_set_is_empty);
    RUN(test_set_is_subset);
    RUN(test_set_is_subset_wrong_type_throws);
    RUN(test_set_is_superset);
    RUN(test_set_is_superset_wrong_type_throws);
    RUN(test_set_length);
    RUN(test_set_length_wrong_type_throws);
    RUN(test_set_map);
    RUN(test_set_map_callback_error);
    RUN(test_set_map_deduplicates);
    RUN(test_set_map_to_composite_succeeds);
    RUN(test_set_module);
    RUN(test_set_new_is_empty_set);
    RUN(test_set_partition);
    RUN(test_set_partition_callback_error);
    RUN(test_set_reduce);
    RUN(test_set_reduce_callback_error);
    RUN(test_set_reduce_empty);
    RUN(test_set_remove);
    RUN(test_set_remove_wrong_type_throws);
    RUN(test_set_string_elements);
    RUN(test_set_symmetric_difference);
    RUN(test_set_symmetric_difference_wrong_type_throws);
    RUN(test_set_to_array);
    RUN(test_set_to_array_wrong_type_throws);
    RUN(test_set_union);
    RUN(test_set_union_contains);
    RUN(test_set_union_wrong_type_throws);
    RUN(test_set_bulk_ops_preserve_unique_results);
    RUN(test_set_any_all_count_find);
    return SUMMARY();
}
