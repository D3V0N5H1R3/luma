// Standard library tests: Array.

#include <cstddef>
#include <string>

#include "stdlib_test_helpers.hpp"

static void test_array_all() {
    ASSERT_EVAL_BOOL("Array.all([2, 4, 6], (integer x) -> x % 2 == 0)", true);

    ASSERT_EVAL_BOOL("Array.all([2, 3, 6], (integer x) -> x % 2 == 0)", false);
}

static void test_array_any() {
    ASSERT_EVAL_BOOL("Array.any([1, 3, 4], (integer x) -> x % 2 == 0)", true);

    ASSERT_EVAL_BOOL("Array.any([1, 3, 5], (integer x) -> x % 2 == 0)", false);
}

static void test_array_chunk() {
    const auto ok = eval("Array.chunk([1, 2, 3, 4, 5], 2) |> Result.unwrap()");

    ASSERT_TRUE(ok.is_array());
    ASSERT_EQ(ok.as_array()->elements->size(), 3U);

    ASSERT_EVAL_FAILURE("Array.chunk([1, 2, 3], 0)");
}

static void test_array_concat() {
    const auto v = eval("Array.concat([1, 2], [3, 4])");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 4U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*v.as_array()->elements)[3].as_integer(), 4);
}

static void test_array_contains() {
    ASSERT_TRUE(eval("Array.contains([1, 2, 3], 2)").as_bool());
    ASSERT_FALSE(eval("Array.contains([1, 2, 3], 5)").as_bool());
    ASSERT_FALSE(eval("Array.contains([], 1)").as_bool());
}

static void test_array_count_returns_result() {
    ASSERT_EVAL_INT("Array.count([1, 2, 3, 4, 5, 6], (integer x) -> x % 2 == 0)", 3);
}

static void test_array_filter_returns_result() {
    const auto v = eval("Array.filter([1, 2, 3, 4], (integer x) -> x % 2 == 0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 2U);
}

static void test_array_first() {
    const auto v = eval("Array.first([10, 20, 30])");

    ASSERT_RESULT_SUCCESS(v);
}

static void test_array_flatten() {
    const auto v = eval("Array.flatten([[1, 2], [3, 4]])");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 4U);
}

static void test_array_group_by() {
    const auto v = eval("Array.group_by([\"a\", \"bb\", \"c\", \"dd\"], (string s) -> "
                        "String.length(s) |> Converter.to_string())");

    ASSERT_RESULT_SUCCESS(v);

    const auto& dict = *v.as_result()->owned_inner;

    ASSERT_TRUE(dict.is_dictionary());
    ASSERT_EQ(dict.as_dictionary()->find("1")->as_array()->elements->size(), 2U);
    ASSERT_EQ(dict.as_dictionary()->find("2")->as_array()->elements->size(), 2U);
}

static void test_array_index_of() {
    ASSERT_EVAL_INT("Array.index_of([10, 20, 30], 20)", 1);

    ASSERT_EVAL_FAILURE("Array.index_of([10, 20, 30], 99)");
}

static void test_array_insert_at() {
    const auto ok = eval("Array.insert_at([1, 2, 3], 1, 99)");

    ASSERT_RESULT_SUCCESS(ok);

    const auto& arr = *ok.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(arr.size(), 4U);
    ASSERT_EQ(arr[0].as_integer(), 1);
    ASSERT_EQ(arr[1].as_integer(), 99);
    ASSERT_EQ(arr[2].as_integer(), 2);
    ASSERT_EQ(arr[3].as_integer(), 3);
}

static void test_array_insert_at_end() {
    const auto ok = eval("Array.insert_at([1, 2], 2, 3)");

    ASSERT_RESULT_SUCCESS(ok);

    const auto& arr = *ok.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(arr.size(), 3U);
    ASSERT_EQ(arr[2].as_integer(), 3);
}

static void test_array_insert_at_out_of_bounds() {
    ASSERT_EVAL_FAILURE("Array.insert_at([1, 2], 5, 0)");
}

static void test_array_last() {
    const auto v = eval("Array.last([10, 20, 30])");

    ASSERT_RESULT_SUCCESS(v);
}

static void test_array_length() {
    const auto v = eval("Array.length([1, 2, 3])");

    ASSERT_EQ(v.as_integer(), 3);
}

static void test_array_map_returns_result() {
    const auto v = eval("Array.map([1, 2, 3], (integer x) -> x * 2)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 3U);
}

static void test_array_min_max() {
    // Non-empty: returns success(value)
    ASSERT_EVAL_INT("Array.min([3, 1, 2])", 1);

    ASSERT_EVAL_INT("Array.max([3, 1, 2])", 3);

    // Empty: returns fail
    ASSERT_EVAL_FAILURE("Array.min([])");
    ASSERT_EVAL_FAILURE("Array.max([])");
}

static void test_array_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Array.length"));
    ASSERT_TRUE(env->has("Array.push"));
    ASSERT_TRUE(env->has("Array.map"));
}

static void test_array_partition() {
    const auto v = eval("Array.partition([1, 2, 3, 4, 5], (integer x) -> x > 3)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& tup = v.as_result()->owned_inner->as_tuple()->elements;

    ASSERT_EQ(tup.size(), 2U);
    ASSERT_EQ(tup[0].as_array()->elements->size(), 2U);
    ASSERT_EQ(tup[1].as_array()->elements->size(), 3U);
    ASSERT_EQ((*tup[0].as_array()->elements)[0].as_integer(), 4);
    ASSERT_EQ((*tup[0].as_array()->elements)[1].as_integer(), 5);
}

static void test_array_pop() {
    const auto v = eval("Array.pop([1, 2, 3])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_tuple());
    ASSERT_EQ(v.as_result()->owned_inner->as_tuple()->elements.size(), 2U);
}

static void test_array_pop_empty() {
    ASSERT_EVAL_FAILURE("Array.pop([])");
}

static void test_array_push() {
    // Array.push returns a new plain array; it does not mutate.
    const auto v = eval("Array.push([1, 2], 3)");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
}

static void test_array_append_alias() {
    const auto v = eval("Array.append([1, 2], 3)");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[2].as_integer(), 3);
}

static void test_array_reduce() {
    // Fold to number.
    const auto sum = eval("Array.reduce([1, 2, 3, 4], 0, (number acc, number x) -> acc + x)");

    ASSERT_RESULT_SUCCESS(sum);
    ASSERT_TRUE(sum.as_result()->owned_inner->is_number() ||
                sum.as_result()->owned_inner->is_integer());
    ASSERT_EQ(sum.as_result()->owned_inner->to_numeric(), 10.0);

    // Empty array returns success(initial).
    const auto empty = eval("Array.reduce([], 99, (number acc, number x) -> acc + x)");

    ASSERT_RESULT_SUCCESS(empty);
    ASSERT_EQ(empty.as_result()->owned_inner->to_numeric(), 99.0);
}

static void test_array_reduce_non_callable() {
    ASSERT_THROWS(eval("Array.reduce([1, 2, 3], 0, 42)"));
}

static void test_array_remove_at() {
    const auto ok = eval("Array.remove_at([10, 20, 30], 1)");

    ASSERT_RESULT_SUCCESS(ok);

    const auto& tup = ok.as_result()->owned_inner->as_tuple()->elements;

    ASSERT_EQ(tup.size(), 2U);
    ASSERT_EQ(tup[0].as_array()->elements->size(), 2U);
    ASSERT_EQ(tup[1].as_integer(), 20);
}

static void test_array_remove_at_out_of_bounds() {
    ASSERT_EVAL_FAILURE("Array.remove_at([1, 2], 5)");
}

static void test_array_repeat() {
    // Returns a result wrapping an array.
    const auto ok = eval("Array.repeat(0, 4) |> Result.unwrap()");

    ASSERT_TRUE(ok.is_array());
    ASSERT_EQ(ok.as_array()->elements->size(), 4U);
    ASSERT_EQ((*ok.as_array()->elements)[0].as_integer(), 0);

    // Negative count → failure result.
    ASSERT_EVAL_FAILURE("Array.repeat(0, -1)");

    // Zero count → empty array.
    const auto zero = eval("Array.repeat(0, 0) |> Result.unwrap()");

    ASSERT_TRUE(zero.is_array());
    ASSERT_EQ(zero.as_array()->elements->size(), 0U);
}

static void test_array_reverse() {
    const auto v = eval("Array.reverse([1, 2, 3])");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 3);
    ASSERT_EQ((*v.as_array()->elements)[2].as_integer(), 1);
}

static void test_array_slice() {
    // Success case: returns result<array<T>>.
    const auto v = eval("Array.slice([1, 2, 3, 4, 5], 1, 3)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner;

    ASSERT_TRUE(arr.is_array());
    ASSERT_EQ(arr.as_array()->elements->size(), 2U);
    ASSERT_EQ((*arr.as_array()->elements)[0].as_integer(), 2);

    // Negative from index → fail.
    ASSERT_EVAL_FAILURE("Array.slice([1, 2, 3], -1, 2)");

    // from > to → fail.
    ASSERT_EVAL_FAILURE("Array.slice([1, 2, 3], 3, 1)");
}

static void test_array_sort() {
    const auto v = eval("Array.sort([3, 1, 2], (integer a, integer b) -> a - b)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(arr[0].as_integer(), 1);
    ASSERT_EQ(arr[1].as_integer(), 2);
    ASSERT_EQ(arr[2].as_integer(), 3);
}

static void test_array_sort_fail() {
    // Comparator that triggers a runtime error (div-by-zero) — sort must return fail.
    ASSERT_EVAL_FAILURE("Array.sort([3, 1, 2], (integer a, integer b) -> a / 0)");
}

static void test_array_sort_inconsistent_comparator_is_safe() {
    // Regression: a comparator that is not a strict weak ordering must not drive
    // an out-of-bounds access.  `b - a - 1` is negative when a == b, so it
    // violates irreflexivity; on an array past the introsort threshold this
    // corrupted the heap under std::ranges::sort.  Array.sort now uses
    // stable_sort, whose merge path stays within bounds, so the call completes
    // safely and returns every element (in an unspecified order).
    const auto v =
        eval("Array.sort(Result.unwrap(Array.range(0, 64)), (integer a, integer b) -> b - a - 1)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 64U);
}

static void test_array_sum() {
    ASSERT_EVAL_INT("Array.sum([1, 2, 3, 4])", 10);
}

static void test_array_sum_non_numeric() {
    ASSERT_EVAL_FAILURE("Array.sum([1, \"bad\", 3])");
}

static void test_array_min_max_non_numeric() {
    // A non-numeric element must surface as a result failure, not an uncaught
    // RuntimeError. eval() runs unchecked, so these heterogeneous arrays reach
    // the runtime (the type checker would otherwise reject them).
    ASSERT_EVAL_FAILURE("Array.min([1, \"bad\", 3])");
    ASSERT_EVAL_FAILURE("Array.max([1, \"bad\", 3])");
}

static void test_array_unique() {
    const auto v = eval("Array.unique([1, 2, 2, 3, 3])");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
}

static void test_array_find_last() {
    ASSERT_EVAL_INT("Array.find_last([1, 2, 3, 4, 5], (integer x) -> x % 2 == 0)", 4);

    ASSERT_EVAL_FAILURE("Array.find_last([1, 3, 5], (integer x) -> x % 2 == 0)");
}

static void test_array_find_last_index() {
    ASSERT_EVAL_INT("Array.find_last_index([1, 2, 3, 4, 5], (integer x) -> x % 2 == 0)", 3);
}

static void test_array_get_out_of_bounds() {
    ASSERT_EVAL_FAILURE("Array.get([1, 2, 3], 10)");
}

static void test_array_get_negative_index() {
    ASSERT_EVAL_FAILURE("Array.get([1, 2, 3], -1)");
}

static void test_array_first_empty() {
    ASSERT_EVAL_FAILURE("Array.first([])");
}

static void test_array_last_empty() {
    ASSERT_EVAL_FAILURE("Array.last([])");
}

static void test_array_chunk_negative() {
    ASSERT_EVAL_FAILURE("Array.chunk([1, 2, 3], -1)");
}

static void test_array_windows_too_large() {
    const auto v = eval("Array.windows([1, 2], 5)");
    ASSERT_RESULT_SUCCESS(v);
    // Windows larger than array should return empty or handle gracefully.
    ASSERT_TRUE(v.as_result()->owned_inner->is_array());
}

static void test_array_map_callback_error() {
    // Division by zero inside callback should produce failure result.
    ASSERT_EVAL_FAILURE("Array.map([1, 0, 3], (integer x) -> 10 / x)");
}

static void test_array_sort_callback_error() {
    // Error in comparator should produce failure result.
    ASSERT_EVAL_FAILURE("Array.sort([3, 1, 2], (integer a, integer b) -> 1 / 0)");
}

static void test_array_compact() {
    // Compact on a non-null array should return the same elements.
    const auto v = eval("Array.compact([1, 2, 3])");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
}

static void test_array_compact_empty() {
    const auto v = eval("Array.compact([])");
    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 0U);
}

static void test_array_binary_search_found() {
    ASSERT_EVAL_INT("Array.binary_search([1, 3, 5, 7, 9], 5)", 2);
}

static void test_array_binary_search_not_found() {
    ASSERT_EVAL_FAILURE("Array.binary_search([1, 3, 5, 7, 9], 4)");
}

static void test_array_binary_search_empty() {
    ASSERT_EVAL_FAILURE("Array.binary_search([], 1)");
}

static void test_array_binary_search_strings() {
    ASSERT_EVAL_INT(R"(Array.binary_search(["apple", "banana", "cherry"], "banana"))", 1);
}

static void test_array_binary_search_first_element() {
    ASSERT_EVAL_INT("Array.binary_search([10, 20, 30], 10)", 0);
}

static void test_array_binary_search_last_element() {
    ASSERT_EVAL_INT("Array.binary_search([10, 20, 30], 30)", 2);
}

// ───────────────────────────────────────────────────────────
// Coverage for Array functions previously exercised only by Luma
// feature tests: set, is_empty, each, find, find_index, take_while,
// drop_while, flat_map, scan, sort_by, zip, take, drop, enumerate,
// join, range, intersperse, rotate, transpose.
// ───────────────────────────────────────────────────────────

static void test_array_set() {
    const auto ok = eval("Array.set([10, 20, 30], 1, 99)");

    ASSERT_RESULT_SUCCESS(ok);

    const auto& arr = *ok.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(arr.size(), 3U);
    ASSERT_EQ(arr[0].as_integer(), 10);
    ASSERT_EQ(arr[1].as_integer(), 99);
    ASSERT_EQ(arr[2].as_integer(), 30);
}

static void test_array_set_out_of_bounds() {
    ASSERT_RESULT_FAILURE(eval("Array.set([1, 2, 3], 5, 99)"));
    ASSERT_RESULT_FAILURE(eval("Array.set([1, 2, 3], -1, 99)"));
}

static void test_array_is_empty() {
    ASSERT_TRUE(eval("Array.is_empty([])").as_bool());
    ASSERT_FALSE(eval("Array.is_empty([1, 2, 3])").as_bool());
}

static void test_array_each() {
    // each returns success(none) and does not mutate the input.
    const auto v = eval("Array.each([1, 2, 3], (integer x) -> x)");

    ASSERT_RESULT_SUCCESS(v);
}

static void test_array_each_callback_error() {
    // A runtime error inside the callback propagates as a failure result.
    ASSERT_RESULT_FAILURE(eval("Array.each([1, 0, 3], (integer x) -> 10 / x)"));
}

static void test_array_find() {
    ASSERT_EVAL_INT("Array.find([1, 2, 3, 4, 5], (integer x) -> x > 3)", 4);

    ASSERT_RESULT_FAILURE(eval("Array.find([1, 2, 3], (integer x) -> x > 10)"));
}

static void test_array_find_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Array.find([2, 0, 3], (integer x) -> (10 / x) > 5)"));
}

static void test_array_find_index() {
    ASSERT_EVAL_INT("Array.find_index([10, 20, 30], (integer x) -> x == 20)", 1);

    ASSERT_RESULT_FAILURE(eval("Array.find_index([1, 2, 3], (integer x) -> x == 99)"));
}

static void test_array_take_while() {
    const auto v = eval("Array.take_while([1, 2, 3, 4, 1], (integer x) -> x < 4)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(arr.size(), 3U);
    ASSERT_EQ(arr[0].as_integer(), 1);
    ASSERT_EQ(arr[2].as_integer(), 3);
}

static void test_array_take_while_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Array.take_while([1, 0, 3], (integer x) -> (10 / x) > 0)"));
}

static void test_array_drop_while() {
    const auto v = eval("Array.drop_while([1, 2, 3, 4, 5], (integer x) -> x < 3)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(arr.size(), 3U);
    ASSERT_EQ(arr[0].as_integer(), 3);
}

static void test_array_flat_map() {
    const auto v = eval("Array.flat_map([1, 2, 3], (integer x) -> [x, x * 10])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 6U);
}

static void test_array_flat_map_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Array.flat_map([1, 0, 3], (integer x) -> [10 / x])"));
}

static void test_array_scan() {
    const auto v = eval("Array.scan([1, 2, 3], 0, (integer acc, integer x) -> acc + x)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(arr.size(), 4U);
    ASSERT_EQ(arr[0].as_integer(), 0);
    ASSERT_EQ(arr[1].as_integer(), 1);
    ASSERT_EQ(arr[2].as_integer(), 3);
    ASSERT_EQ(arr[3].as_integer(), 6);
}

static void test_array_scan_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Array.scan([1, 0, 3], 100, (integer acc, integer x) -> acc / x)"));
}

static void test_array_sort_by() {
    const auto v = eval("Array.sort_by([3, 1, 2], (integer x) -> x)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(arr[0].as_integer(), 1);
    ASSERT_EQ(arr[1].as_integer(), 2);
    ASSERT_EQ(arr[2].as_integer(), 3);
}

static void test_array_sort_by_callback_error() {
    ASSERT_RESULT_FAILURE(eval("Array.sort_by([3, 1, 2], (integer x) -> x / 0)"));
}

static void test_array_sort_by_stable() {
    // Equal-key elements must keep their original relative order. Use a large
    // array (past the introsort insertion-sort threshold) with a constant key
    // so every element ties: only a stable sort yields the deterministic
    // original order 0, 1, 2, ...  sort_by relies on std::ranges::stable_sort
    // for this, which is also what keeps it memory-safe under an intransitive
    // comparator (see test_array_sort_by_inconsistent_keys_is_safe).
    std::string src = "Array.sort_by([";
    constexpr int count = 64;
    for (int i = 0; i < count; ++i) {
        src += std::to_string(i);
        if (i < count - 1) {
            src += ", ";
        }
    }
    src += "], (integer x) -> 0)";

    const auto v = eval(src);
    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner->as_array()->elements;
    ASSERT_EQ(arr.size(), static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        ASSERT_EQ(arr[static_cast<std::size_t>(i)].as_integer(), i);
    }
}

static void test_array_sort_by_inconsistent_keys_is_safe() {
    // Regression: sort_by's key comparison (compare_values) is not a strict
    // weak ordering when the keys mix integer and number values whose
    // magnitudes reach 2^53. 2^53 + 1 is exactly representable as int64 but not
    // as double, so for P = Integer(2^53+1), Q = Number(2^53), R = Integer(2^53)
    // the widening path gives P == Q and Q == R while the exact int path gives
    // P > R — intransitive. On an array past the introsort threshold this drove
    // an out-of-bounds partition under std::ranges::sort and corrupted the heap.
    // sort_by now uses stable_sort, whose merge path stays within bounds, so the
    // call completes safely and returns every element. eval() runs unchecked, so
    // the heterogeneous numeric array reaches the runtime (the type checker would
    // otherwise reject it); the identity key preserves each element's int/number
    // runtime type.
    std::string src = "Array.sort_by([";
    constexpr int triples = 22;
    for (int i = 0; i < triples; ++i) {
        src += "9007199254740993, 9007199254740992.0, 9007199254740992";
        if (i < triples - 1) {
            src += ", ";
        }
    }
    src += "], (number x) -> x)";

    const auto v = eval(src);
    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(),
              static_cast<std::size_t>(triples * 3));
}

static void test_array_zip() {
    const auto v = eval("Array.zip([1, 2, 3], [10, 20, 30])");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);

    const auto& first = (*v.as_array()->elements)[0].as_tuple()->elements;

    ASSERT_EQ(first.size(), 2U);
    ASSERT_EQ(first[0].as_integer(), 1);
    ASSERT_EQ(first[1].as_integer(), 10);
}

static void test_array_zip_unequal_lengths() {
    // Result length is the minimum of the two input lengths.
    ASSERT_EQ(eval("Array.zip([1, 2, 3], [10])").as_array()->elements->size(), 1U);
}

static void test_array_take() {
    const auto v = eval("Array.take([1, 2, 3, 4, 5], 2)");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*v.as_array()->elements)[1].as_integer(), 2);

    // Over-length take is clamped; negative count yields an empty array.
    ASSERT_EQ(eval("Array.take([1, 2], 10)").as_array()->elements->size(), 2U);
    ASSERT_EQ(eval("Array.take([1, 2, 3], -1)").as_array()->elements->size(), 0U);
}

static void test_array_drop() {
    const auto v = eval("Array.drop([1, 2, 3, 4, 5], 2)");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 3);

    // Dropping past the end yields empty; negative count drops nothing.
    ASSERT_EQ(eval("Array.drop([1, 2], 10)").as_array()->elements->size(), 0U);
    ASSERT_EQ(eval("Array.drop([1, 2, 3], -1)").as_array()->elements->size(), 3U);
}

static void test_array_enumerate() {
    const auto v = eval(R"(Array.enumerate(["a", "b", "c"]))");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);

    const auto& pair = (*v.as_array()->elements)[1].as_tuple()->elements;

    ASSERT_EQ(pair[0].as_integer(), 1);
    ASSERT_EQ(pair[1].as_string(), std::string("b"));
}

static void test_array_join() {
    ASSERT_EQ(eval(R"(Array.join(["a", "b", "c"], ", "))").as_string(), std::string("a, b, c"));
    ASSERT_EQ(eval(R"(Array.join([], "-"))").as_string(), std::string(""));
    ASSERT_EQ(eval(R"(Array.join([1, 2, 3], ""))").as_string(), std::string("123"));
}

static void test_array_range() {
    const auto v = eval("Array.range(0, 5)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(arr.size(), 5U);
    ASSERT_EQ(arr[0].as_integer(), 0);
    ASSERT_EQ(arr[4].as_integer(), 4);

    // end == start yields an empty range.
    ASSERT_EQ(eval("Array.range(5, 5)").as_result()->owned_inner->as_array()->elements->size(), 0U);
}

static void test_array_range_exceeds_limit() {
    // A range wider than max_array_size (10M) is rejected before allocation.
    ASSERT_RESULT_FAILURE(eval("Array.range(0, 10000001)"));
}

static void test_array_range_reversed_is_empty() {
    // Regression: Array.range(start, end) with end < start must yield an empty
    // array, not loop unbounded.  std::views::iota(start, end) reaches its
    // sentinel by incrementing start up to end, so a smaller end is never hit
    // and the range previously appended until OOM / signed-overflow UB.
    const auto v = eval("Array.range(5, 2)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 0U);
}

static void test_array_range_extreme_span_fails_safely() {
    // Regression: a span exceeding INT64_MAX must fail cleanly, not overflow.
    // end - start here (5e18 - -5e18 = 1e19) overflows a signed int64, which was
    // undefined behaviour in the old size check; the count is now computed with
    // unsigned subtraction, so this reports "exceeds maximum array size" instead
    // of tripping UBSan / relying on wrap-around.
    ASSERT_RESULT_FAILURE(eval("Array.range(-5000000000000000000, 5000000000000000000)"));
}

static void test_array_intersperse() {
    const auto v = eval("Array.intersperse([1, 2, 3], 0)");

    ASSERT_TRUE(v.is_array());

    const auto& arr = *v.as_array()->elements;

    ASSERT_EQ(arr.size(), 5U);
    ASSERT_EQ(arr[0].as_integer(), 1);
    ASSERT_EQ(arr[1].as_integer(), 0);
    ASSERT_EQ(arr[2].as_integer(), 2);
    ASSERT_EQ(arr[3].as_integer(), 0);
    ASSERT_EQ(arr[4].as_integer(), 3);

    // Single-element and empty arrays are returned unchanged.
    ASSERT_EQ(eval("Array.intersperse([42], 0)").as_array()->elements->size(), 1U);
    ASSERT_EQ(eval("Array.intersperse([], 0)").as_array()->elements->size(), 0U);
}

static void test_array_rotate() {
    // Positive shift rotates left.
    const auto left_val = eval("Array.rotate([1, 2, 3, 4, 5], 2)");
    const auto& left = *left_val.as_array()->elements;

    ASSERT_EQ(left[0].as_integer(), 3);
    ASSERT_EQ(left[4].as_integer(), 2);

    // Negative shift rotates right.
    const auto right_val = eval("Array.rotate([1, 2, 3, 4, 5], -1)");
    const auto& right = *right_val.as_array()->elements;

    ASSERT_EQ(right[0].as_integer(), 5);
    ASSERT_EQ(right[1].as_integer(), 1);

    // Empty array stays empty.
    ASSERT_EQ(eval("Array.rotate([], 3)").as_array()->elements->size(), 0U);
}

static void test_array_transpose() {
    const auto v = eval("Array.transpose([[1, 2, 3], [4, 5, 6]])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& rows = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(rows.size(), 3U);
    ASSERT_EQ((*rows[0].as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*rows[0].as_array()->elements)[1].as_integer(), 4);
    ASSERT_EQ((*rows[2].as_array()->elements)[1].as_integer(), 6);

    // Empty input transposes to an empty array.
    ASSERT_RESULT_SUCCESS(eval("Array.transpose([])"));
}

static void test_array_transpose_errors() {
    // Unequal row lengths and non-array elements are rejected.
    ASSERT_RESULT_FAILURE(eval("Array.transpose([[1, 2], [3]])"));
    ASSERT_RESULT_FAILURE(eval("Array.transpose([1, 2, 3])"));
}

static void test_array_windows_invalid_size() {
    // Zero and negative window sizes are rejected.
    ASSERT_RESULT_FAILURE(eval("Array.windows([1, 2, 3], 0)"));
    ASSERT_RESULT_FAILURE(eval("Array.windows([1, 2, 3], -2)"));
}

static void test_array_sum_overflow_promotes_to_number() {
    // Summing past INT64_MAX must promote to a number (double) rather than
    // wrapping around — mirrors Luma's integer-overflow-promotes-to-number rule.
    const auto v = eval("Array.sum([9223372036854775807, 1])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;

    ASSERT_TRUE(inner.is_number());
    ASSERT_TRUE(inner.as_number() > 9.0e18);
}

static void test_array_rotate_wraps() {
    // A shift larger than the length wraps via modulo: rotate by 7 over 5
    // elements behaves like rotate by 2.
    const auto left = eval("Array.rotate([1, 2, 3, 4, 5], 7)");
    const auto& l = *left.as_array()->elements;

    ASSERT_EQ(l[0].as_integer(), 3);
    ASSERT_EQ(l[4].as_integer(), 2);

    // A large negative shift wraps and rotates right.
    const auto right = eval("Array.rotate([1, 2, 3, 4, 5], -7)");
    const auto& r = *right.as_array()->elements;

    ASSERT_EQ(r[0].as_integer(), 4);
    ASSERT_EQ(r[4].as_integer(), 3);

    // A shift equal to the length is a no-op.
    const auto same = eval("Array.rotate([1, 2, 3, 4, 5], 5)");

    ASSERT_EQ((*same.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*same.as_array()->elements)[4].as_integer(), 5);
}

static void test_array_unique_preserves_order() {
    // unique keeps the first occurrence of each element and preserves order.
    const auto v = eval("Array.unique([3, 1, 3, 2, 1])");
    const auto& arr = *v.as_array()->elements;

    ASSERT_EQ(arr.size(), 3U);
    ASSERT_EQ(arr[0].as_integer(), 3);
    ASSERT_EQ(arr[1].as_integer(), 1);
    ASSERT_EQ(arr[2].as_integer(), 2);

    // The same ordering guarantee holds for strings.
    const auto s = eval(R"(Array.unique(["b", "a", "b", "c", "a"]))");
    const auto& sarr = *s.as_array()->elements;

    ASSERT_EQ(sarr.size(), 3U);
    ASSERT_EQ(sarr[0].as_string(), std::string("b"));
    ASSERT_EQ(sarr[1].as_string(), std::string("a"));
    ASSERT_EQ(sarr[2].as_string(), std::string("c"));
}

static void test_array_min_max_numbers() {
    // min/max operate on number (double) arrays, not just integers.
    ASSERT_EVAL_NUM("Array.min([3.5, 1.5, 2.5])", 1.5);

    ASSERT_EVAL_NUM("Array.max([3.5, 1.5, 2.5])", 3.5);

    // Mixed integer/number arrays compare numerically (the catalog type is
    // array<integer | number>), matching the previous to_numeric() ordering.
    ASSERT_EVAL_NUM("Array.min([3, 1.5, 2])", 1.5);

    ASSERT_EVAL_NUM("Array.max([1, 3.5, 2])", 3.5);
}

int main() {
    RUN(test_array_all);
    RUN(test_array_any);
    RUN(test_array_binary_search_found);
    RUN(test_array_binary_search_not_found);
    RUN(test_array_binary_search_empty);
    RUN(test_array_binary_search_strings);
    RUN(test_array_binary_search_first_element);
    RUN(test_array_binary_search_last_element);
    RUN(test_array_chunk);
    RUN(test_array_compact);
    RUN(test_array_compact_empty);
    RUN(test_array_concat);
    RUN(test_array_contains);
    RUN(test_array_count_returns_result);
    RUN(test_array_filter_returns_result);
    RUN(test_array_first);
    RUN(test_array_find_last);
    RUN(test_array_find_last_index);
    RUN(test_array_flatten);
    RUN(test_array_group_by);
    RUN(test_array_index_of);
    RUN(test_array_insert_at);
    RUN(test_array_insert_at_end);
    RUN(test_array_insert_at_out_of_bounds);
    RUN(test_array_last);
    RUN(test_array_length);
    RUN(test_array_map_returns_result);
    RUN(test_array_min_max);
    RUN(test_array_module);
    RUN(test_array_partition);
    RUN(test_array_pop);
    RUN(test_array_pop_empty);
    RUN(test_array_push);
    RUN(test_array_append_alias);
    RUN(test_array_reduce);
    RUN(test_array_reduce_non_callable);
    RUN(test_array_remove_at);
    RUN(test_array_remove_at_out_of_bounds);
    RUN(test_array_repeat);
    RUN(test_array_reverse);
    RUN(test_array_slice);
    RUN(test_array_sort);
    RUN(test_array_sort_fail);
    RUN(test_array_sort_inconsistent_comparator_is_safe);
    RUN(test_array_sum);
    RUN(test_array_sum_non_numeric);
    RUN(test_array_min_max_non_numeric);
    RUN(test_array_unique);
    RUN(test_array_get_out_of_bounds);
    RUN(test_array_get_negative_index);
    RUN(test_array_first_empty);
    RUN(test_array_last_empty);
    RUN(test_array_chunk_negative);
    RUN(test_array_windows_too_large);
    RUN(test_array_map_callback_error);
    RUN(test_array_sort_callback_error);
    RUN(test_array_set);
    RUN(test_array_set_out_of_bounds);
    RUN(test_array_is_empty);
    RUN(test_array_each);
    RUN(test_array_each_callback_error);
    RUN(test_array_find);
    RUN(test_array_find_callback_error);
    RUN(test_array_find_index);
    RUN(test_array_take_while);
    RUN(test_array_take_while_callback_error);
    RUN(test_array_drop_while);
    RUN(test_array_flat_map);
    RUN(test_array_flat_map_callback_error);
    RUN(test_array_scan);
    RUN(test_array_scan_callback_error);
    RUN(test_array_sort_by);
    RUN(test_array_sort_by_callback_error);
    RUN(test_array_sort_by_stable);
    RUN(test_array_sort_by_inconsistent_keys_is_safe);
    RUN(test_array_zip);
    RUN(test_array_zip_unequal_lengths);
    RUN(test_array_take);
    RUN(test_array_drop);
    RUN(test_array_enumerate);
    RUN(test_array_join);
    RUN(test_array_range);
    RUN(test_array_range_exceeds_limit);
    RUN(test_array_range_reversed_is_empty);
    RUN(test_array_range_extreme_span_fails_safely);
    RUN(test_array_intersperse);
    RUN(test_array_rotate);
    RUN(test_array_transpose);
    RUN(test_array_transpose_errors);
    RUN(test_array_windows_invalid_size);
    RUN(test_array_sum_overflow_promotes_to_number);
    RUN(test_array_rotate_wraps);
    RUN(test_array_unique_preserves_order);
    RUN(test_array_min_max_numbers);

    return SUMMARY();
}
