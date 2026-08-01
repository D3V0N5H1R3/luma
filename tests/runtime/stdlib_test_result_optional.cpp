// Standard library tests: Result, Optional.

#include "analysis/errors/error.hpp"
#include "stdlib_test_helpers.hpp"

static void test_optional_filter() {
    const auto v = eval("Optional.filter(some(10), (integer x) -> x > 5)");

    ASSERT_EQ(v.as_integer(), 10);

    const auto v2 = eval("Optional.filter(some(3), (integer x) -> x > 5)");

    ASSERT_TRUE(v2.is_null());
}

static void test_optional_flat_map() {
    const auto v = eval("Optional.flat_map(some(5), (integer x) -> some(x + 1))");

    ASSERT_EQ(v.as_integer(), 6);
}

static void test_optional_flat_map_allows_null() {
    // flat_map is allowed to return none — result is none
    const auto v = eval("Optional.flat_map(some(5), (integer x) -> none)");

    ASSERT_TRUE(v.is_null());
}

static void test_optional_flatten() {
    // flatten some(some(42)) → some(42)
    ASSERT_EQ(eval("Optional.flatten(some(some(42)))").as_integer(), 42);
    // flatten none → none
    ASSERT_TRUE(eval("Optional.flatten(none)").is_null());
}

static void test_optional_is_some() {
    const auto t = eval("Optional.is_some(some(42))");

    ASSERT_TRUE(t.is_bool() && t.as_bool());

    const auto f = eval("Optional.is_none(some(42))");

    ASSERT_TRUE(f.is_bool() && !f.as_bool());
}

static void test_optional_map() {
    const auto v = eval("Optional.map(some(5), (integer x) -> x * 2)");

    ASSERT_EQ(v.as_integer(), 10);
}

static void test_optional_map_none() {
    const auto v = eval("Optional.map(none, (integer x) -> x * 2)");

    ASSERT_TRUE(v.is_null());
}

static void test_optional_map_throws_on_null() {
    // map must not return none — a RuntimeError is expected, and the message
    // must steer the user towards Optional.flat_map.
    ASSERT_THROWS_WITH_MESSAGE(eval("Optional.map(some(5), (integer x) -> none)"), "flat_map");
}

static void test_optional_or() {
    const auto v = eval("Optional.or(none, some(42))");

    ASSERT_EQ(v.as_integer(), 42);
}

static void test_optional_to_result() {
    ASSERT_EVAL_INT("Optional.to_result(some(42), \"missing\")", 42);

    ASSERT_EVAL_FAILURE("Optional.to_result(none, \"missing\")");
}

static void test_optional_to_result_failure_payload() {
    // none must produce a failure carrying the supplied error value verbatim.
    ASSERT_EQ(eval("Result.error(Optional.to_result(none, \"missing\"))").as_string(), "missing");
}

static void test_optional_unwrap() {
    const auto v = eval("Optional.unwrap(some(99))");

    ASSERT_EQ(v.as_integer(), 99);
}

static void test_optional_unwrap_none_throws() {
    // unwrap on none throws a RuntimeError whose message names the cause.
    ASSERT_THROWS_WITH_MESSAGE(eval("Optional.unwrap(none)"), "called on none");
}

static void test_optional_unwrap_or() {
    const auto v = eval("Optional.unwrap_or(none, 7)");

    ASSERT_EQ(v.as_integer(), 7);

    const auto v2 = eval("Optional.unwrap_or(some(3), 7)");

    ASSERT_EQ(v2.as_integer(), 3);
}

static void test_optional_zip() {
    // Both some — inner values become a tuple
    const auto both = eval("Optional.zip(some(1), some(2))");

    ASSERT_TRUE(both.is_tuple());
    ASSERT_EQ(both.as_tuple()->elements[0].as_integer(), 1);
    ASSERT_EQ(both.as_tuple()->elements[1].as_integer(), 2);
    // Either none — result is none
    ASSERT_TRUE(eval("Optional.zip(none, some(2))").is_null());
    ASSERT_TRUE(eval("Optional.zip(some(1), none)").is_null());
}

static void test_optional_tap() {
    // some — returns the original optional unchanged.
    const auto some_val = eval("Optional.tap(some(42), (integer x) -> x)");

    ASSERT_EQ(some_val.as_integer(), 42);

    // none — returns none and skips the callback; a throwing callback would
    // surface here if it ran, so the absence of a throw proves it is skipped.
    const auto none_val = eval("Optional.tap(none, (integer x) -> Optional.unwrap(none))");

    ASSERT_TRUE(none_val.is_null());
}

static void test_optional_tap_runs_callback_on_some() {
    // For some, the callback must actually run and observe the inner value.
    // A reference cell captures the side effect so we can assert it happened.
    const auto v = eval("reference<integer> seen = Reference.new(0)\n"
                        "optional<integer> _r = Optional.tap(some(42), "
                        "(integer x) -> Reference.set(seen, x))\n"
                        "Reference.get(seen)\n");

    ASSERT_EQ(v.as_integer(), 42);
}

static void test_optional_and_then() {
    // some — applies the callback, which returns optional<U>.
    ASSERT_EQ(eval("Optional.and_then(some(5), (integer x) -> some(x * 2))").as_integer(), 10);
    // some, callback returns none — result is none.
    ASSERT_TRUE(eval("Optional.and_then(some(5), (integer x) -> none)").is_null());
    // none — short-circuits to none without invoking the callback.
    ASSERT_TRUE(eval("Optional.and_then(none, (integer x) -> some(x * 2))").is_null());
}

static void test_optional_contains() {
    // some equal to the target.
    ASSERT_TRUE(eval("Optional.contains(some(42), 42)").as_bool());
    // some not equal to the target.
    ASSERT_FALSE(eval("Optional.contains(some(42), 99)").as_bool());
    // none never contains a value.
    ASSERT_FALSE(eval("Optional.contains(none, 42)").as_bool());
    // Works for non-integer payloads too.
    ASSERT_TRUE(eval("Optional.contains(some(\"hi\"), \"hi\")").as_bool());
}

static void test_optional_filter_non_boolean_throws() {
    // The filter predicate must return boolean — a non-boolean result throws.
    ASSERT_THROWS_WITH_MESSAGE(eval("Optional.filter(some(5), (integer x) -> x)"), "boolean");
}

static void test_result_collect() {
    const auto ok = eval("Result.collect([success(1), success(2), success(3)])");

    ASSERT_RESULT_SUCCESS(ok);
    ASSERT_TRUE(ok.as_result()->owned_inner->is_array());
    ASSERT_EQ(ok.as_result()->owned_inner->as_array()->elements->size(), 3U);
}

static void test_result_collect_empty() {
    // Collecting an empty array succeeds with an empty array (vacuous truth).
    const auto v = eval("Result.collect([])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_array());
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 0U);
}

static void test_result_error() {
    ASSERT_EQ(eval("Result.error(failure(\"msg\"))").as_string(), "msg");

    ASSERT_THROWS(eval("Result.error(success(42))"));
}

static void test_result_filter() {
    const auto ok = eval("Result.filter(success(10), (integer x) -> x > 5, \"too small\")");

    ASSERT_RESULT_SUCCESS(ok);

    ASSERT_EVAL_FAILURE("Result.filter(success(2), (integer x) -> x > 5, \"too small\")");
}

static void test_result_flat_map() {
    ASSERT_EVAL_INT("Result.flat_map(success(3), (integer x) -> success(x + 1))", 4);
}

static void test_result_flatten() {
    ASSERT_EVAL_INT("Result.flatten(success(success(5)))", 5);
}

static void test_result_is_fail() {
    ASSERT_EQ(eval("Result.is_failure(failure(\"error\"))").as_bool(), true);
    ASSERT_EQ(eval("Result.is_failure(success(42))").as_bool(), false);
}

static void test_result_is_ok() {
    ASSERT_EQ(eval("Result.is_success(success(42))").as_bool(), true);
    ASSERT_EQ(eval("Result.is_success(failure(\"error\"))").as_bool(), false);
}

static void test_result_map() {
    ASSERT_EVAL_INT("Result.map(success(3), (integer x) -> x * 10)", 30);
}

static void test_result_map_boolean_type_mismatch() {
    // Typed map with wrong inner type passes through unchanged.
    ASSERT_EVAL_INT("Result.map_boolean(success(42), (boolean b) -> !b)", 42);
}

static void test_result_map_failure() {
    const auto ok = eval("Result.map_failure(failure(\"err\"), "
                         "(string e) -> \"wrapped: \" + e)");

    ASSERT_RESULT_FAILURE(ok);
    ASSERT_EQ(ok.as_result()->owned_inner->as_string(), "wrapped: err");
}

static void test_result_map_failure_on_success() {
    ASSERT_EVAL_INT("Result.map_failure(success(42), "
                    "(string e) -> \"wrapped: \" + e)",
                    42);
}

static void test_result_map_integer_type_mismatch() {
    // Typed map with wrong inner type passes through unchanged.
    ASSERT_EVAL_STR("Result.map_integer(success(\"hello\"), (integer n) -> n + 1)", "hello");
}

static void test_result_map_number_type_mismatch() {
    // Typed map with wrong inner type passes through unchanged.
    ASSERT_EVAL_STR("Result.map_number(success(\"hello\"), (number x) -> x + 1.0)", "hello");
}

static void test_result_map_string_type_mismatch() {
    // Typed map with wrong inner type passes through unchanged.
    ASSERT_EVAL_INT("Result.map_string(success(42), (string s) -> s + \"!\")", 42);
}

static void test_result_or() {
    ASSERT_EVAL_INT("Result.or(failure(\"err\"), success(42))", 42);
}

static void test_result_recover() {
    ASSERT_EVAL_INT("Result.recover(failure(\"err\"), (string e) -> 99)", 99);
}

static void test_result_tap() {
    ASSERT_EVAL_INT("Result.tap(success(42), (integer x) -> none)", 42);
}

static void test_result_to_optional() {
    // success → some
    ASSERT_EQ(eval("Result.to_optional(success(42))").as_integer(), 42);
    // failure → none
    ASSERT_TRUE(eval("Result.to_optional(failure(\"err\"))").is_null());
}

static void test_result_unwrap() {
    ASSERT_EQ(eval("Result.unwrap(success(42))").as_integer(), 42);
}

static void test_result_expect_success() {
    // On success, expect returns the inner value and ignores the message.
    ASSERT_EQ(eval("Result.expect(success(42), \"should have a value\")").as_integer(), 42);
}

static void test_result_expect_failure_throws() {
    // On failure, expect throws a RuntimeError combining the message with the
    // underlying error text.
    ASSERT_TRUE(luma::test::eval_throws("Result.expect(failure(\"boom\"), \"expected a value\")"));
    ASSERT_THROWS_WITH_MESSAGE(eval("Result.expect(failure(\"boom\"), \"expected a value\")"),
                               "expected a value");
    ASSERT_THROWS_WITH_MESSAGE(eval("Result.expect(failure(\"boom\"), \"expected a value\")"),
                               "boom");
}

static void test_result_unwrap_or() {
    const auto v = eval("Result.unwrap_or(failure(\"err\"), 99)");

    ASSERT_EQ(v.as_integer(), 99);
}

static void test_result_zip() {
    // Both success — inner values become a tuple
    const auto both = eval("Result.zip(success(1), success(2))");

    ASSERT_RESULT_SUCCESS(both);
    ASSERT_TRUE(both.as_result()->owned_inner->is_tuple());
    ASSERT_EQ(both.as_result()->owned_inner->as_tuple()->elements[0].as_integer(), 1);
    ASSERT_EQ(both.as_result()->owned_inner->as_tuple()->elements[1].as_integer(), 2);

    // First fails — propagate
    ASSERT_EVAL_FAILURE("Result.zip(failure(\"err\"), success(2))");

    // Second fails — propagate
    ASSERT_EVAL_FAILURE("Result.zip(success(1), failure(\"err\"))");
}

static void test_result_unwrap_or_success() {
    // unwrap_or on success returns the inner value, not the default.
    ASSERT_EQ(eval("Result.unwrap_or(success(42), 0)").as_integer(), 42);
}

static void test_result_or_keeps_first_success() {
    ASSERT_EVAL_INT("Result.or(success(1), success(2))", 1);
}

static void test_result_recover_keeps_success() {
    ASSERT_EVAL_INT("Result.recover(success(10), (string e) -> 99)", 10);
}

static void test_result_tap_failure_passthrough() {
    // tap on a failure passes it through unchanged; the callback is skipped.
    ASSERT_EVAL_FAILURE("Result.tap(failure(\"err\"), (integer x) -> none)");
}

static void test_result_flat_map_failure_passthrough() {
    ASSERT_EVAL_FAILURE("Result.flat_map(failure(\"err\"), (integer x) -> success(x + 1))");
}

static void test_result_map_integer_success() {
    ASSERT_EVAL_INT("Result.map_integer(success(5), (integer n) -> n * 2)", 10);
}

static void test_result_map_string_success() {
    ASSERT_EVAL_STR("Result.map_string(success(\"hi\"), (string s) -> s + \"!\")", "hi!");
}

static void test_result_map_number_success() {
    ASSERT_EVAL_NUM("Result.map_number(success(2.5), (number x) -> x * 2.0)", 5.0);
}

static void test_result_map_boolean_success() {
    ASSERT_EVAL_BOOL("Result.map_boolean(success(true), (boolean b) -> !b)", false);
}

static void test_result_bimap_success() {
    ASSERT_EVAL_INT("Result.map_both(success(5), (integer v) -> v * 2, (string e) -> 0)", 10);
}

static void test_result_bimap_failure() {
    // bimap maps the error branch but the result stays a failure.
    const auto v = eval("Result.map_both(failure(\"bad\"), (integer v) -> v, (string e) -> 99)");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_integer(), 99);
}

static void test_result_fold_success() {
    ASSERT_EQ(
        eval("Result.fold(success(7), (integer v) -> \"ok\", (string e) -> \"err\")").as_string(),
        "ok");
}

static void test_result_fold_failure() {
    ASSERT_EQ(eval("Result.fold(failure(\"z\"), (integer v) -> \"ok\", (string e) -> \"err\")")
                  .as_string(),
              "err");
}

static void test_result_or_else_rescue() {
    ASSERT_EVAL_INT("Result.or_else(failure(\"x\"), (string e) -> success(0))", 0);
}

static void test_result_or_else_keeps_success() {
    ASSERT_EVAL_INT("Result.or_else(success(7), (string e) -> success(0))", 7);
}

static void test_result_error_code_from_stdlib() {
    // A stdlib failure carries a machine-readable code and source function.
    ASSERT_EQ(eval("Result.error_code(Array.get([1, 2, 3], 10))").as_string(),
              "index_out_of_bounds");
}

static void test_result_source_function_from_stdlib() {
    ASSERT_EQ(eval("Result.source_function(Array.get([1, 2, 3], 10))").as_string(), "Array.get");
}

static void test_result_error_code_empty_for_plain_failure() {
    // A plain string failure carries no machine-readable metadata.
    ASSERT_EQ(eval("Result.error_code(failure(\"oops\"))").as_string(), "");
    ASSERT_EQ(eval("Result.source_function(failure(\"oops\"))").as_string(), "");
}

static void test_result_error_code_empty_for_success() {
    ASSERT_EQ(eval("Result.error_code(success(1))").as_string(), "");
    ASSERT_EQ(eval("Result.source_function(success(1))").as_string(), "");
}

static void test_result_unwrap_on_failure_throws() {
    ASSERT_THROWS(eval("Result.unwrap(failure(\"boom\"))"));
}

static void test_result_unwrap_non_result_throws() {
    ASSERT_THROWS(eval("Result.unwrap(42)"));
}

static void test_result_is_success_non_result_throws() {
    ASSERT_THROWS(eval("Result.is_success(42)"));
}

static void test_result_flat_map_non_result_callback_throws() {
    // flat_map requires the callback to return a result value.
    ASSERT_THROWS(eval("Result.flat_map(success(1), (integer x) -> x + 1)"));
}

static void test_result_or_else_non_result_callback_throws() {
    // or_else requires the callback to return a result value.
    ASSERT_THROWS(eval("Result.or_else(failure(\"x\"), (string e) -> 7)"));
}

static void test_result_map_on_failure_passthrough() {
    // map on a failure leaves it unchanged and never runs the callback.
    const auto v = eval("Result.map(failure(\"boom\"), (integer x) -> x * 10)");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_string(), "boom");
}

static void test_result_flatten_failure_passthrough() {
    // flatten on a failure returns the failure untouched.
    const auto v = eval("Result.flatten(failure(\"boom\"))");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_string(), "boom");
}

static void test_result_flatten_non_result_inner() {
    // flatten of a success whose inner is not itself a result returns it as-is.
    ASSERT_EVAL_INT("Result.flatten(success(5))", 5);
}

static void test_result_filter_on_failure_passthrough() {
    // filter on a failure passes it through; the predicate is never evaluated.
    const auto v = eval("Result.filter(failure(\"boom\"), (integer x) -> x > 0, \"rejected\")");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_string(), "boom");
}

static void test_result_map_number_on_integer_inner() {
    // map_number accepts an integer inner (the is_integer() guard branch) and
    // produces a number result.
    ASSERT_EVAL_NUM("Result.map_number(success(3), (number x) -> x * 2.0)", 6.0);
}

static void test_result_tap_runs_callback_on_success() {
    // For success, tap must actually run the callback and observe the inner value.
    const auto v = eval("reference<integer> seen = Reference.new(0)\n"
                        "result<integer> _r = Result.tap(success(42), "
                        "(integer x) -> Reference.set(seen, x))\n"
                        "Reference.get(seen)\n");

    ASSERT_EQ(v.as_integer(), 42);
}

static void test_result_collect_non_result_element_throws() {
    // collect requires every element to be a result value.
    ASSERT_THROWS(eval("Result.collect([success(1), 42])"));
}

static void test_result_collect_non_array_throws() {
    // collect requires its argument to be an array.
    ASSERT_THROWS(eval("Result.collect(42)"));
}

static void test_result_zip_non_result_second_throws() {
    // zip validates the second argument is a result once the first succeeds.
    ASSERT_THROWS(eval("Result.zip(success(1), 42)"));
}

static void test_result_typed_map_arity_throws() {
    // The typed map_* variants validate their argument count.
    ASSERT_THROWS(eval("Result.map_integer(success(1))"));
}

int main() {
    RUN(test_optional_filter);
    RUN(test_optional_flat_map);
    RUN(test_optional_flat_map_allows_null);
    RUN(test_optional_flatten);
    RUN(test_optional_is_some);
    RUN(test_optional_map);
    RUN(test_optional_map_none);
    RUN(test_optional_map_throws_on_null);
    RUN(test_optional_or);
    RUN(test_optional_to_result);
    RUN(test_optional_to_result_failure_payload);
    RUN(test_optional_unwrap);
    RUN(test_optional_unwrap_none_throws);
    RUN(test_optional_unwrap_or);
    RUN(test_optional_zip);
    RUN(test_optional_tap);
    RUN(test_optional_tap_runs_callback_on_some);
    RUN(test_optional_and_then);
    RUN(test_optional_contains);
    RUN(test_optional_filter_non_boolean_throws);
    RUN(test_result_collect);
    RUN(test_result_collect_empty);
    RUN(test_result_error);
    RUN(test_result_filter);
    RUN(test_result_flat_map);
    RUN(test_result_flatten);
    RUN(test_result_is_fail);
    RUN(test_result_is_ok);
    RUN(test_result_map);
    RUN(test_result_map_boolean_type_mismatch);
    RUN(test_result_map_failure);
    RUN(test_result_map_failure_on_success);
    RUN(test_result_map_integer_type_mismatch);
    RUN(test_result_map_number_type_mismatch);
    RUN(test_result_map_string_type_mismatch);
    RUN(test_result_or);
    RUN(test_result_or_keeps_first_success);
    RUN(test_result_recover);
    RUN(test_result_recover_keeps_success);
    RUN(test_result_tap);
    RUN(test_result_tap_failure_passthrough);
    RUN(test_result_flat_map_failure_passthrough);
    RUN(test_result_map_integer_success);
    RUN(test_result_map_string_success);
    RUN(test_result_map_number_success);
    RUN(test_result_map_boolean_success);
    RUN(test_result_bimap_success);
    RUN(test_result_bimap_failure);
    RUN(test_result_fold_success);
    RUN(test_result_fold_failure);
    RUN(test_result_or_else_rescue);
    RUN(test_result_or_else_keeps_success);
    RUN(test_result_error_code_from_stdlib);
    RUN(test_result_source_function_from_stdlib);
    RUN(test_result_error_code_empty_for_plain_failure);
    RUN(test_result_error_code_empty_for_success);
    RUN(test_result_unwrap_on_failure_throws);
    RUN(test_result_unwrap_non_result_throws);
    RUN(test_result_is_success_non_result_throws);
    RUN(test_result_flat_map_non_result_callback_throws);
    RUN(test_result_or_else_non_result_callback_throws);
    RUN(test_result_map_on_failure_passthrough);
    RUN(test_result_flatten_failure_passthrough);
    RUN(test_result_flatten_non_result_inner);
    RUN(test_result_filter_on_failure_passthrough);
    RUN(test_result_map_number_on_integer_inner);
    RUN(test_result_tap_runs_callback_on_success);
    RUN(test_result_collect_non_result_element_throws);
    RUN(test_result_collect_non_array_throws);
    RUN(test_result_zip_non_result_second_throws);
    RUN(test_result_typed_map_arity_throws);
    RUN(test_result_to_optional);
    RUN(test_result_unwrap);
    RUN(test_result_expect_success);
    RUN(test_result_expect_failure_throws);
    RUN(test_result_unwrap_or);
    RUN(test_result_unwrap_or_success);
    RUN(test_result_zip);

    return SUMMARY();
}
