// Standard library tests: LinkedList.

#include "stdlib_test_helpers.hpp"

static void test_linkedlist_large_destruction_no_overflow() {
    // Regression: a long list must tear down iteratively.  The previous
    // recursive shared_ptr destructor descended one native stack frame per node
    // and overflowed the stack — uncatchably — when destroying a long list (the
    // size limit permits millions).  Building a 1,000,000-node list and dropping
    // it (LinkedList.length consumes the temporary) must complete without
    // crashing.
    ASSERT_EQ(
        eval("LinkedList.from_array(Result.unwrap(Array.range(0, 1000000))) |> LinkedList.length()")
            .as_integer(),
        1000000);
}

static void test_linkedlist_append_last() {
    ASSERT_EQ(
        eval("LinkedList.new() |> LinkedList.append(99) |> LinkedList.last() |> Result.unwrap()")
            .as_integer(),
        99);
}

static void test_linkedlist_at() {
    ASSERT_EQ(eval("LinkedList.from_array([10, 20, 30]) |> LinkedList.at(1) |> Result.unwrap()")
                  .as_integer(),
              20);
}

static void test_linkedlist_concat() {
    ASSERT_EQ(eval("LinkedList.concat(LinkedList.from_array([1, 2]), LinkedList.from_array([3, "
                   "4])) |> LinkedList.length()")
                  .as_integer(),
              4);
}

static void test_linkedlist_contains() {
    ASSERT_TRUE(eval("LinkedList.from_array([1, 2, 3]) |> LinkedList.contains(2)").as_bool());
    ASSERT_FALSE(eval("LinkedList.from_array([1, 2, 3]) |> LinkedList.contains(5)").as_bool());
}

static void test_linkedlist_each() {
    const auto v = eval("LinkedList.from_array([1, 2, 3]) |> LinkedList.each((integer x) -> x)");

    ASSERT_RESULT_SUCCESS(v);
}

static void test_linkedlist_filter() {
    const auto v = eval("LinkedList.from_array([1, 2, 3, 4])"
                        " |> LinkedList.filter((integer x) -> x % 2 == 0)"
                        " |> Result.unwrap()"
                        " |> LinkedList.length()");

    ASSERT_EQ(v.as_integer(), 2);
}

static void test_linkedlist_from_array() {
    ASSERT_EQ(eval("LinkedList.from_array([1, 2, 3]) |> LinkedList.length()").as_integer(), 3);
}

static void test_linkedlist_is_empty() {
    ASSERT_TRUE(eval("LinkedList.new() |> LinkedList.is_empty()").as_bool());
    ASSERT_FALSE(eval("LinkedList.from_array([1]) |> LinkedList.is_empty()").as_bool());
}

static void test_linkedlist_map() {
    const auto v = eval("LinkedList.from_array([1, 2, 3])"
                        " |> LinkedList.map((integer x) -> x * 2)"
                        " |> Result.unwrap()"
                        " |> LinkedList.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 2);
    ASSERT_EQ((*v.as_array()->elements)[1].as_integer(), 4);
    ASSERT_EQ((*v.as_array()->elements)[2].as_integer(), 6);
}

static void test_linkedlist_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("LinkedList.new"));
    ASSERT_TRUE(env->has("LinkedList.from_array"));
    ASSERT_TRUE(env->has("LinkedList.prepend"));
    ASSERT_TRUE(env->has("LinkedList.append"));
    ASSERT_TRUE(env->has("LinkedList.first"));
    ASSERT_TRUE(env->has("LinkedList.last"));
    ASSERT_TRUE(env->has("LinkedList.at"));
    ASSERT_TRUE(env->has("LinkedList.length"));
    ASSERT_TRUE(env->has("LinkedList.is_empty"));
    ASSERT_TRUE(env->has("LinkedList.contains"));
    ASSERT_TRUE(env->has("LinkedList.reverse"));
    ASSERT_TRUE(env->has("LinkedList.to_array"));
    ASSERT_TRUE(env->has("LinkedList.concat"));
    ASSERT_TRUE(env->has("LinkedList.map"));
    ASSERT_TRUE(env->has("LinkedList.filter"));
    ASSERT_TRUE(env->has("LinkedList.each"));
    ASSERT_TRUE(env->has("LinkedList.reduce"));
}

static void test_linkedlist_push_alias() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("LinkedList.push"));
}

static void test_linkedlist_partition() {
    const auto v = eval("LinkedList.partition("
                        "LinkedList.from_array([1, 2, 3, 4, 5]),"
                        "(integer x) -> x > 3)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& tup = v.as_result()->owned_inner->as_tuple()->elements;

    ASSERT_EQ(tup.size(), 2U);
    ASSERT_EQ(tup[0].as_linked_list()->count_, 2);
    ASSERT_EQ(tup[1].as_linked_list()->count_, 3);
}

static void test_linkedlist_prepend_first() {
    ASSERT_EQ(
        eval("LinkedList.new() |> LinkedList.prepend(42) |> LinkedList.first() |> Result.unwrap()")
            .as_integer(),
        42);
}

static void test_linkedlist_reduce() {
    const auto v = eval("LinkedList.from_array([1, 2, 3, 4])"
                        " |> LinkedList.reduce(0, (integer acc, integer x) -> acc + x)"
                        " |> Result.unwrap()");

    ASSERT_EQ(v.as_integer(), 10);
}

static void test_linkedlist_reduce_empty() {
    // reduce on empty list returns init value
    const auto v = eval("LinkedList.new()"
                        " |> LinkedList.reduce(99, (integer acc, integer x) -> acc + x)"
                        " |> Result.unwrap()");

    ASSERT_EQ(v.as_integer(), 99);
}

static void test_linkedlist_remove_first() {
    ASSERT_EQ(eval("LinkedList.from_array([1, 2, 3]) |> LinkedList.remove_first() |> "
                   "Result.unwrap() |> LinkedList.length()")
                  .as_integer(),
              2);
}

static void test_linkedlist_remove_last() {
    ASSERT_EQ(eval("LinkedList.from_array([1, 2, 3]) |> LinkedList.remove_last() |> "
                   "Result.unwrap() |> LinkedList.length()")
                  .as_integer(),
              2);
}

static void test_linkedlist_reverse() {
    const auto result = eval("LinkedList.from_array([1, 2, 3]) |> LinkedList.reverse() |> "
                             "LinkedList.first() |> Result.unwrap()");

    ASSERT_EQ(result.as_integer(), 3);
}

static void test_linkedlist_to_array() {
    const auto result = eval("LinkedList.from_array([1, 2, 3]) |> LinkedList.to_array()");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), std::size_t{3});
    ASSERT_EQ((*result.as_array()->elements)[0].as_integer(), 1);
}

static void test_linkedlist_insert_at() {
    const auto v = eval("LinkedList.from_array([1, 2, 3]) |> LinkedList.insert_at(1, 99) |> "
                        "Result.unwrap() |> LinkedList.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 4U);
    ASSERT_EQ((*v.as_array()->elements)[1].as_integer(), 99);
}

static void test_linkedlist_insert_at_head_and_tail() {
    ASSERT_EQ(eval("LinkedList.from_array([2, 3]) |> LinkedList.insert_at(0, 1) |> Result.unwrap() "
                   "|> LinkedList.first() |> Result.unwrap()")
                  .as_integer(),
              1);
    ASSERT_EQ(eval("LinkedList.from_array([1, 2]) |> LinkedList.insert_at(2, 3) |> Result.unwrap() "
                   "|> LinkedList.last() |> Result.unwrap()")
                  .as_integer(),
              3);
}

static void test_linkedlist_remove_at() {
    const auto v = eval("LinkedList.from_array([1, 2, 3]) |> LinkedList.remove_at(1) |> "
                        "Result.unwrap() |> LinkedList.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*v.as_array()->elements)[1].as_integer(), 3);
}

static void test_linkedlist_remove_at_head() {
    ASSERT_EQ(eval("LinkedList.from_array([10, 20, 30]) |> LinkedList.remove_at(0) |> "
                   "Result.unwrap() |> LinkedList.first() |> Result.unwrap()")
                  .as_integer(),
              20);
}

static void test_linkedlist_find() {
    ASSERT_EQ(
        eval("LinkedList.find(LinkedList.from_array([10, 20, 30]), (integer x) -> x == 20) |> "
             "Result.unwrap()")
            .as_integer(),
        20);
}

static void test_linkedlist_zip() {
    const auto v = eval(R"(LinkedList.zip(LinkedList.from_array([1, 2, 3]), )"
                        R"(LinkedList.from_array(["a", "b"])) |> LinkedList.to_array())");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);

    const auto& pair = (*v.as_array()->elements)[0];

    ASSERT_TRUE(pair.is_tuple());
    ASSERT_EQ(pair.as_tuple()->elements[0].as_integer(), 1);
    ASSERT_EQ(pair.as_tuple()->elements[1].as_string(), std::string("a"));
}

static void test_linkedlist_sort() {
    const auto v = eval("LinkedList.sort(LinkedList.from_array([3, 1, 2]), (integer a, integer b) "
                        "-> a < b) |> Result.unwrap() |> LinkedList.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*v.as_array()->elements)[1].as_integer(), 2);
    ASSERT_EQ((*v.as_array()->elements)[2].as_integer(), 3);
}

static void test_linkedlist_sort_pathological_comparator_safe() {
    // The comparator is arbitrary user code and need not form a strict weak
    // ordering.  A comparator that always returns true is undefined behaviour
    // for std::ranges::sort (introsort can read/write past the buffer);
    // stable_sort's merge path stays in bounds.  The resulting order is
    // unspecified, but the operation must not crash and must preserve every
    // element.  Regression for the sort -> stable_sort hardening.
    const auto v = eval("LinkedList.sort(LinkedList.from_array([8, 3, 5, 1, 9, 2, 7, 4, 6, 0, 15, "
                        "11, 13, 10, 14, 12]), (integer a, integer b) -> true) |> "
                        "Result.unwrap() |> LinkedList.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 16U);
}

static void test_linkedlist_unique() {
    const auto v = eval("LinkedList.from_array([3, 1, 3, 2, 1]) |> LinkedList.unique() |> "
                        "LinkedList.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 3);
    ASSERT_EQ((*v.as_array()->elements)[1].as_integer(), 1);
    ASSERT_EQ((*v.as_array()->elements)[2].as_integer(), 2);
}

static void test_linkedlist_push_appends() {
    ASSERT_EQ(eval("LinkedList.new() |> LinkedList.push(1) |> LinkedList.push(2) |> "
                   "LinkedList.last() |> Result.unwrap()")
                  .as_integer(),
              2);
}

// ─── Negative: empty-container failures ───────────────────────────

static void test_linkedlist_first_empty_fails() {
    ASSERT_RESULT_FAILURE(eval("LinkedList.new() |> LinkedList.first()"));
}

static void test_linkedlist_last_empty_fails() {
    ASSERT_RESULT_FAILURE(eval("LinkedList.new() |> LinkedList.last()"));
}

static void test_linkedlist_remove_first_empty_fails() {
    ASSERT_RESULT_FAILURE(eval("LinkedList.new() |> LinkedList.remove_first()"));
}

static void test_linkedlist_remove_last_empty_fails() {
    ASSERT_RESULT_FAILURE(eval("LinkedList.new() |> LinkedList.remove_last()"));
}

// Regression: empty-container failures must carry exactly one "LinkedList."
// prefix. The message previously doubled it (e.g. "LinkedList.LinkedList.first:
// container is empty") because check_not_empty passed an already-qualified name
// to the two-argument empty_container overload.
static void test_linkedlist_empty_message_single_prefix() {
    const auto first = eval("LinkedList.new() |> LinkedList.first()");
    ASSERT_RESULT_FAILURE(first);
    ASSERT_EQ(first.as_result()->owned_inner->as_string(),
              std::string("LinkedList.first: container is empty"));

    const auto last = eval("LinkedList.new() |> LinkedList.last()");
    ASSERT_RESULT_FAILURE(last);
    ASSERT_EQ(last.as_result()->owned_inner->as_string(),
              std::string("LinkedList.last: container is empty"));

    const auto remove_first = eval("LinkedList.new() |> LinkedList.remove_first()");
    ASSERT_RESULT_FAILURE(remove_first);
    ASSERT_EQ(remove_first.as_result()->owned_inner->as_string(),
              std::string("LinkedList.remove_first: container is empty"));

    const auto remove_last = eval("LinkedList.new() |> LinkedList.remove_last()");
    ASSERT_RESULT_FAILURE(remove_last);
    ASSERT_EQ(remove_last.as_result()->owned_inner->as_string(),
              std::string("LinkedList.remove_last: container is empty"));
}

// ─── Negative: out-of-bounds index failures ───────────────────────

static void test_linkedlist_at_out_of_bounds_fails() {
    ASSERT_RESULT_FAILURE(eval("LinkedList.from_array([1, 2]) |> LinkedList.at(5)"));
}

static void test_linkedlist_at_negative_fails() {
    ASSERT_RESULT_FAILURE(eval("LinkedList.from_array([1, 2]) |> LinkedList.at(-1)"));
}

static void test_linkedlist_insert_at_out_of_bounds_fails() {
    ASSERT_RESULT_FAILURE(eval("LinkedList.from_array([1, 2]) |> LinkedList.insert_at(5, 9)"));
}

static void test_linkedlist_insert_at_negative_fails() {
    ASSERT_RESULT_FAILURE(eval("LinkedList.from_array([1, 2]) |> LinkedList.insert_at(-1, 9)"));
}

static void test_linkedlist_remove_at_out_of_bounds_fails() {
    ASSERT_RESULT_FAILURE(eval("LinkedList.from_array([1, 2]) |> LinkedList.remove_at(5)"));
}

static void test_linkedlist_remove_at_negative_fails() {
    ASSERT_RESULT_FAILURE(eval("LinkedList.from_array([1, 2]) |> LinkedList.remove_at(-1)"));
}

static void test_linkedlist_find_not_found_fails() {
    ASSERT_RESULT_FAILURE(
        eval("LinkedList.find(LinkedList.from_array([1, 2, 3]), (integer x) -> x == 99)"));
}

// ─── Negative: wrong-argument-type errors ─────────────────────────

static void test_linkedlist_length_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("LinkedList.length(42)"), "linked_list");
}

static void test_linkedlist_append_wrong_type_throws() {
    ASSERT_THROWS(eval("LinkedList.append(42, 1)"));
}

static void test_linkedlist_at_non_integer_index_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval(R"(LinkedList.at(LinkedList.from_array([1, 2]), "x"))"),
                               "integer");
}

static void test_linkedlist_from_array_wrong_type_throws() {
    ASSERT_THROWS(eval("LinkedList.from_array(42)"));
}

static void test_linkedlist_concat_wrong_type_throws() {
    ASSERT_THROWS(eval("LinkedList.concat(LinkedList.new(), 42)"));
}

int main() {
    RUN(test_linkedlist_append_last);
    RUN(test_linkedlist_at);
    RUN(test_linkedlist_concat);
    RUN(test_linkedlist_contains);
    RUN(test_linkedlist_each);
    RUN(test_linkedlist_filter);
    RUN(test_linkedlist_from_array);
    RUN(test_linkedlist_is_empty);
    RUN(test_linkedlist_map);
    RUN(test_linkedlist_module);
    RUN(test_linkedlist_partition);
    RUN(test_linkedlist_prepend_first);
    RUN(test_linkedlist_push_alias);
    RUN(test_linkedlist_reduce);
    RUN(test_linkedlist_reduce_empty);
    RUN(test_linkedlist_remove_first);
    RUN(test_linkedlist_remove_last);
    RUN(test_linkedlist_reverse);
    RUN(test_linkedlist_to_array);
    RUN(test_linkedlist_insert_at);
    RUN(test_linkedlist_insert_at_head_and_tail);
    RUN(test_linkedlist_remove_at);
    RUN(test_linkedlist_remove_at_head);
    RUN(test_linkedlist_find);
    RUN(test_linkedlist_zip);
    RUN(test_linkedlist_sort);
    RUN(test_linkedlist_sort_pathological_comparator_safe);
    RUN(test_linkedlist_unique);
    RUN(test_linkedlist_push_appends);
    RUN(test_linkedlist_first_empty_fails);
    RUN(test_linkedlist_last_empty_fails);
    RUN(test_linkedlist_remove_first_empty_fails);
    RUN(test_linkedlist_remove_last_empty_fails);
    RUN(test_linkedlist_empty_message_single_prefix);
    RUN(test_linkedlist_at_out_of_bounds_fails);
    RUN(test_linkedlist_at_negative_fails);
    RUN(test_linkedlist_insert_at_out_of_bounds_fails);
    RUN(test_linkedlist_insert_at_negative_fails);
    RUN(test_linkedlist_remove_at_out_of_bounds_fails);
    RUN(test_linkedlist_remove_at_negative_fails);
    RUN(test_linkedlist_find_not_found_fails);
    RUN(test_linkedlist_length_wrong_type_throws);
    RUN(test_linkedlist_append_wrong_type_throws);
    RUN(test_linkedlist_at_non_integer_index_throws);
    RUN(test_linkedlist_from_array_wrong_type_throws);
    RUN(test_linkedlist_concat_wrong_type_throws);
    RUN(test_linkedlist_large_destruction_no_overflow);
    return SUMMARY();
}
