// Standard library tests: Resource and core builtins (print, assert, type_of, success, failure).

#include <iostream>
#include <sstream>

#include "stdlib_test_helpers.hpp"

static void test_print_single_value_adds_newline() {
    const CapturedStream captured{std::cout};
    const auto v = eval("print(\"hello\")");

    ASSERT_TRUE(v.is_null()); // print returns none.
    ASSERT_EQ(captured.str(), std::string("hello\n"));
}

static void test_print_multiple_values_space_separated() {
    const CapturedStream captured{std::cout};
    eval("print(1, \"a\", true)");

    // Values are joined by single spaces, then a trailing newline. Strings
    // print unquoted (to_string), booleans as true/false.
    ASSERT_EQ(captured.str(), std::string("1 a true\n"));
}

static void test_print_no_args_prints_only_newline() {
    const CapturedStream captured{std::cout};
    eval("print()");

    ASSERT_EQ(captured.str(), std::string("\n"));
}

// ─── assert ───

static void test_assert_passes_on_truthy_values() {
    // A truthy first argument returns none and does not throw. Truthiness is
    // defined for every type, so non-boolean truthy values are accepted too.
    ASSERT_TRUE(eval("assert(true)").is_null());
    ASSERT_TRUE(eval("assert(true, \"with message\")").is_null());
    ASSERT_TRUE(eval("assert(1)").is_null());
    ASSERT_TRUE(eval("assert(\"non-empty\")").is_null());
}

static void test_assert_false_throws_default_message() {
    ASSERT_THROWS_WITH_MESSAGE(eval("assert(false)"), "assertion failed");
}

static void test_assert_false_throws_custom_message() {
    ASSERT_THROWS_WITH_MESSAGE(eval("assert(false, \"custom boom\")"), "custom boom");
}

static void test_assert_falsy_values_throw() {
    // Falsy non-boolean values (zero, empty string, none) also fail.
    ASSERT_THROWS(eval("assert(0)"));
    ASSERT_THROWS(eval("assert(\"\")"));
    ASSERT_THROWS(eval("assert(none)"));
}

static void test_assert_no_args_throws() {
    ASSERT_THROWS(eval("assert()"));
}

static void test_assert_non_string_message_throws() {
    ASSERT_THROWS(eval("assert(true, 42)"));
}

// ─── type_of ───

static void test_type_of_compound_types() {
    ASSERT_EQ(eval("type_of([1, 2, 3])").as_string(), "array");
    ASSERT_EQ(eval("type_of({\"a\": 1})").as_string(), "dictionary");
    ASSERT_EQ(eval("type_of((1, 2))").as_string(), "tuple");
    ASSERT_EQ(eval("type_of(success(1))").as_string(), "result");
    ASSERT_EQ(eval("type_of(failure(\"e\"))").as_string(), "result");
    ASSERT_EQ(eval("type_of(() -> 1)").as_string(), "function");
}

static void test_type_of_some_reports_inner_type() {
    // An optional holding a value reports the inner runtime type, not "optional".
    ASSERT_EQ(eval("type_of(some(42))").as_string(), "integer");
    ASSERT_EQ(eval("type_of(some(\"hi\"))").as_string(), "string");
}

static void test_type_of_no_args_throws() {
    ASSERT_THROWS(eval("type_of()"));
}

static void test_type_of_too_many_args_throws() {
    ASSERT_THROWS(eval("type_of(1, 2)"));
}

// ─── success / failure ───

static void test_success_construction() {
    const auto v = eval("success(42)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_integer(), 42);
    // A success result carries no failure location.
    ASSERT_FALSE(v.as_result()->has_failure_location);
}

static void test_fail_has_location() {
    const auto v = eval("failure(\"oops\")");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_TRUE(v.as_result()->has_failure_location);
    ASSERT_EQ(v.as_result()->failure_location.line, 1);
}

static void test_read_file_limited() {
    // Non-existent file returns fail.
    ASSERT_EVAL_FAILURE("FileSystem.read_file_limited(\"_no_such_file_xyz.txt\", 1024)");

    // Negative max_bytes returns fail.
    ASSERT_EVAL_FAILURE("FileSystem.read_file_limited(\"_no_such_file_xyz.txt\", -1)");
}

static void test_resource_with() {
    // Cleanup is called after body; body return value is returned.
    const auto result = eval("Resource.with(42, (integer v) -> v, (integer v) -> none)");

    ASSERT_TRUE(result.as_integer() == 42);

    // Cleanup is called even when it modifies nothing (smoke test).
    const auto r2 =
        eval("Resource.with(\"hello\", (string v) -> String.uppercase(v), (string v) -> none)");

    ASSERT_EQ(r2.as_string(), "HELLO");
}

static void test_resource_with_cleanup_on_error() {
    // Cleanup must run even when body throws. Use a reference to track cleanup.
    const auto v = eval("reference<boolean> ran = Reference.new(false)\n"
                        "try {\n"
                        "    Resource.with(\n"
                        "        \"res\",\n"
                        "        (string _v) -> {\n"
                        "            assert(false, \"force error\")\n"
                        "            return \"unreachable\"\n"
                        "        },\n"
                        "        (string _v) -> Reference.set(ran, true)\n"
                        "    )\n"
                        "} catch(err) {\n"
                        "    \"caught\"\n"
                        "}\n"
                        "Reference.get(ran)\n");

    ASSERT_TRUE(v.as_bool());
}

static void test_stdlib_fail_no_location() {
    const auto v = eval("String.parse_integer(\"bad\")");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_FALSE(v.as_result()->has_failure_location);
}

static void test_stdlib_functions_registered() {
    const auto env = luma::test::make_std_env();

    // Verify core builtins exist.
    ASSERT_TRUE(env->has("print"));
    ASSERT_TRUE(env->has("assert"));
    ASSERT_TRUE(env->has("type_of"));
}

static void test_type_of_builtin() {
    ASSERT_EQ(eval("type_of(42)").as_string(), "integer");
    ASSERT_EQ(eval("type_of(3.14)").as_string(), "number");
    ASSERT_EQ(eval("type_of(\"hi\")").as_string(), "string");
    ASSERT_EQ(eval("type_of(true)").as_string(), "boolean");
    ASSERT_EQ(eval("type_of(none)").as_string(), "none");
}

// ═══════════════════════════════════════════════════════════
// Resource — additional tests
// ═══════════════════════════════════════════════════════════

static void test_resource_using_basic() {
    // Resource.using: acquire, use, release.
    const auto v = eval("Resource.using(\n"
                        "    () -> 100,\n"
                        "    (integer r) -> r + 1,\n"
                        "    (integer r) -> none\n"
                        ")\n");

    ASSERT_EQ(v.as_integer(), 101);
}

static void test_resource_using_cleanup_on_error() {
    // Release must run even when body throws.
    const auto v = eval("reference<boolean> released = Reference.new(false)\n"
                        "try {\n"
                        "    Resource.using(\n"
                        "        () -> \"acquired\",\n"
                        "        (string _r) -> {\n"
                        "            assert(false, \"force error\")\n"
                        "            return \"unreachable\"\n"
                        "        },\n"
                        "        (string _r) -> Reference.set(released, true)\n"
                        "    )\n"
                        "} catch(e) {\n"
                        "    \"caught\"\n"
                        "}\n"
                        "Reference.get(released)\n");

    ASSERT_TRUE(v.as_bool());
}

static void test_resource_with_returns_body_value() {
    // Body return value propagates through Resource.with.
    const auto v = eval("Resource.with(10, (integer x) -> x * 2, (integer x) -> none)");

    ASSERT_EQ(v.as_integer(), 20);
}

static void test_resource_with_non_callable_body_throws() {
    // The error must identify the offending role (body) with uniform wording.
    ASSERT_THROWS_WITH_MESSAGE(eval("Resource.with(1, 2, (integer x) -> none)"),
                               "Resource.with (body): expected callable");
}

static void test_resource_with_non_callable_cleanup_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Resource.with(1, (integer x) -> x, 3)"),
                               "Resource.with (cleanup): expected callable");
}

static void test_resource_using_non_callable_acquire_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Resource.using(42, (integer x) -> x, (integer x) -> none)"),
                               "Resource.using (acquire): expected callable");
}

static void test_resource_using_non_callable_body_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Resource.using(() -> 1, 2, (integer x) -> none)"),
                               "Resource.using (body): expected callable");
}

static void test_resource_using_non_callable_release_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Resource.using(() -> 1, (integer x) -> x, 3)"),
                               "Resource.using (release): expected callable");
}

static void test_resource_using_acquire_error_skips_release() {
    // If acquire throws, release must NOT be called.
    const auto v = eval("reference<boolean> released = Reference.new(false)\n"
                        "try {\n"
                        "    Resource.using(\n"
                        "        () -> {\n"
                        "            assert(false, \"acquire fails\")\n"
                        "            return 0\n"
                        "        },\n"
                        "        (integer _r) -> _r,\n"
                        "        (integer _r) -> Reference.set(released, true)\n"
                        "    )\n"
                        "} catch(e) {\n"
                        "    \"caught\"\n"
                        "}\n"
                        "Reference.get(released)\n");

    ASSERT_FALSE(v.as_bool());
}

static void test_resource_with_cleanup_runs_on_success() {
    // On the success path the cleanup must still run (observable side-effect).
    const auto v = eval("reference<boolean> ran = Reference.new(false)\n"
                        "integer out = Resource.with(\n"
                        "    7,\n"
                        "    (integer v) -> v + 1,\n"
                        "    (integer _v) -> Reference.set(ran, true)\n"
                        ")\n"
                        "assert(out == 8)\n"
                        "Reference.get(ran)\n");

    ASSERT_TRUE(v.as_bool());
}

static void test_resource_using_release_runs_on_success() {
    // On the success path the release must still run (observable side-effect).
    const auto v = eval("reference<boolean> released = Reference.new(false)\n"
                        "integer out = Resource.using(\n"
                        "    () -> 10,\n"
                        "    (integer v) -> v + 1,\n"
                        "    (integer _v) -> Reference.set(released, true)\n"
                        ")\n"
                        "assert(out == 11)\n"
                        "Reference.get(released)\n");

    ASSERT_TRUE(v.as_bool());
}

static void test_resource_nested_using() {
    // Nested Resource.using: value propagates outward and both releases run.
    // Encoded result: out * 100 + release_count => 30 * 100 + 2 = 3002.
    const auto v = eval("reference<integer> ran = Reference.new(0)\n"
                        "integer out = Resource.using(\n"
                        "    () -> 10,\n"
                        "    (integer outer) -> Resource.using(\n"
                        "        () -> outer + 5,\n"
                        "        (integer inner) -> inner * 2,\n"
                        "        (integer _v) -> Reference.update(ran, (integer n) -> n + 1)\n"
                        "    ),\n"
                        "    (integer _v) -> Reference.update(ran, (integer n) -> n + 1)\n"
                        ")\n"
                        "out * 100 + Reference.get(ran)\n");

    ASSERT_EQ(v.as_integer(), 3002);
}

static void test_resource_with_cleanup_error_on_success_propagates() {
    // If the body succeeds but cleanup throws, that error propagates to the caller.
    ASSERT_THROWS_WITH_MESSAGE(eval("Resource.with(\n"
                                    "    \"res\",\n"
                                    "    (string v) -> v,\n"
                                    "    (string _v) -> {\n"
                                    "        assert(false, \"CLEANUP_FAILURE\")\n"
                                    "        return none\n"
                                    "    }\n"
                                    ")\n"),
                               "CLEANUP_FAILURE");
}

static void test_resource_with_body_error_suppresses_cleanup_error() {
    // When both body and cleanup throw, the body error wins and the cleanup
    // error is swallowed (logged, not rethrown) by safe_cleanup.
    const auto v = eval("reference<string> caught = Reference.new(\"\")\n"
                        "try {\n"
                        "    Resource.with(\n"
                        "        \"res\",\n"
                        "        (string _v) -> {\n"
                        "            assert(false, \"BODY_FAILURE\")\n"
                        "            return \"unreachable\"\n"
                        "        },\n"
                        "        (string _v) -> {\n"
                        "            assert(false, \"CLEANUP_FAILURE\")\n"
                        "            return none\n"
                        "        }\n"
                        "    )\n"
                        "} catch(e) {\n"
                        "    Reference.set(caught, e)\n"
                        "}\n"
                        "Reference.get(caught)\n");

    const auto msg = v.as_string();
    ASSERT_TRUE(msg.find("BODY_FAILURE") != std::string::npos);
    ASSERT_TRUE(msg.find("CLEANUP_FAILURE") == std::string::npos);
}

static void test_resource_using_release_error_on_success_propagates() {
    // If the body succeeds but release throws, that error propagates to the caller.
    ASSERT_THROWS_WITH_MESSAGE(eval("Resource.using(\n"
                                    "    () -> 10,\n"
                                    "    (integer v) -> v + 1,\n"
                                    "    (integer _v) -> {\n"
                                    "        assert(false, \"RELEASE_FAILURE\")\n"
                                    "        return none\n"
                                    "    }\n"
                                    ")\n"),
                               "RELEASE_FAILURE");
}

static void test_resource_using_body_error_suppresses_release_error() {
    // When both body and release throw, the body error wins and the release
    // error is swallowed (logged, not rethrown) by safe_cleanup.
    const auto v = eval("reference<string> caught = Reference.new(\"\")\n"
                        "try {\n"
                        "    Resource.using(\n"
                        "        () -> \"acquired\",\n"
                        "        (string _v) -> {\n"
                        "            assert(false, \"BODY_FAILURE\")\n"
                        "            return \"unreachable\"\n"
                        "        },\n"
                        "        (string _v) -> {\n"
                        "            assert(false, \"RELEASE_FAILURE\")\n"
                        "            return none\n"
                        "        }\n"
                        "    )\n"
                        "} catch(e) {\n"
                        "    Reference.set(caught, e)\n"
                        "}\n"
                        "Reference.get(caught)\n");

    const auto msg = v.as_string();
    ASSERT_TRUE(msg.find("BODY_FAILURE") != std::string::npos);
    ASSERT_TRUE(msg.find("RELEASE_FAILURE") == std::string::npos);
}

int main() {
    RUN(test_print_single_value_adds_newline);
    RUN(test_print_multiple_values_space_separated);
    RUN(test_print_no_args_prints_only_newline);
    RUN(test_assert_passes_on_truthy_values);
    RUN(test_assert_false_throws_default_message);
    RUN(test_assert_false_throws_custom_message);
    RUN(test_assert_falsy_values_throw);
    RUN(test_assert_no_args_throws);
    RUN(test_assert_non_string_message_throws);
    RUN(test_type_of_compound_types);
    RUN(test_type_of_some_reports_inner_type);
    RUN(test_type_of_no_args_throws);
    RUN(test_type_of_too_many_args_throws);
    RUN(test_success_construction);
    RUN(test_fail_has_location);
    RUN(test_read_file_limited);
    RUN(test_resource_with);
    RUN(test_resource_with_cleanup_on_error);
    RUN(test_resource_using_basic);
    RUN(test_resource_using_cleanup_on_error);
    RUN(test_resource_with_returns_body_value);
    RUN(test_resource_with_non_callable_body_throws);
    RUN(test_resource_with_non_callable_cleanup_throws);
    RUN(test_resource_using_non_callable_acquire_throws);
    RUN(test_resource_using_non_callable_body_throws);
    RUN(test_resource_using_non_callable_release_throws);
    RUN(test_resource_using_acquire_error_skips_release);
    RUN(test_resource_with_cleanup_runs_on_success);
    RUN(test_resource_using_release_runs_on_success);
    RUN(test_resource_nested_using);
    RUN(test_resource_with_cleanup_error_on_success_propagates);
    RUN(test_resource_with_body_error_suppresses_cleanup_error);
    RUN(test_resource_using_release_error_on_success_propagates);
    RUN(test_resource_using_body_error_suppresses_release_error);
    RUN(test_stdlib_fail_no_location);
    RUN(test_stdlib_functions_registered);
    RUN(test_type_of_builtin);

    return SUMMARY();
}
