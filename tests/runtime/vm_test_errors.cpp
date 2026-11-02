// VM unit tests: runtime errors, exception handling, and resource limits.

#include <memory>
#include <string>

#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm_stack.hpp"
#include "stdlib_test_helpers.hpp"

using luma::ArrayValue;
using luma::ResourceLimits;
using luma::Value;
using luma::VMStack;
using luma::test::eval;
using luma::test::LimitGuard;

// ─── Arithmetic errors ───

LUMA_TEST(vm_division_by_zero) {
    ASSERT_EVAL_THROWS("integer x = 10\ninteger y = 0\nx / y", "Division: division by zero");
}

LUMA_TEST(vm_modulo_by_zero) {
    ASSERT_EVAL_THROWS("integer x = 10\ninteger y = 0\nx % y", "Modulo: division by zero");
}

LUMA_TEST(vm_integer_division_by_zero) {
    ASSERT_EVAL_THROWS("10 // 0", "Integer division: division by zero");
}

LUMA_TEST(vm_shift_out_of_range) {
    ASSERT_EVAL_THROWS("integer x = 1\ninteger y = 64\nBits.shift_left(x, y)",
                       "shift amount 64 is out of range");
    ASSERT_EVAL_THROWS("integer x = 1\ninteger y = -1\nBits.shift_left(x, y)",
                       "shift amount -1 is out of range");
}

LUMA_TEST(vm_string_repeat_negative) {
    // The type checker rejects a negative repeat count (E0019); on the
    // unchecked path a negative count yields the empty string, both when the
    // count is a literal and when it reaches the VM through a variable.
    const auto folded = eval("\"ha\" * -1");

    ASSERT_TRUE(folded.is_string());
    ASSERT_EQ(folded.as_string(), "");

    const auto runtime = eval("mutable integer n = -1\n\"ha\" * n");

    ASSERT_TRUE(runtime.is_string());
    ASSERT_EQ(runtime.as_string(), "");
}

// Regression (review #5): integer overflow used to widen silently to `number`,
// so an `integer` variable could hold a `number` at runtime.  Every integer
// operation that overflows int64 now raises the documented runtime error.
LUMA_TEST(vm_integer_overflow_raises_instead_of_promoting) {
    // `mutable` keeps the operands out of compile-time constant folding, so the
    // VM's own overflow checks are exercised.
    ASSERT_EVAL_THROWS("mutable integer x = 9223372036854775807\nx + 1",
                       "integer overflow in addition");
    ASSERT_EVAL_THROWS("mutable integer x = -9223372036854775807\nx - 2",
                       "integer overflow in subtraction");
    ASSERT_EVAL_THROWS("mutable integer x = 9223372036854775807\nx * 2",
                       "integer overflow in multiplication");
    ASSERT_EVAL_THROWS("mutable integer x = 9223372036854775807\nx++\nx",
                       "integer overflow in increment");
    ASSERT_EVAL_THROWS("mutable integer x = -9223372036854775807 - 1\nx--\nx",
                       "integer overflow in decrement");
    ASSERT_EVAL_THROWS("mutable integer x = -9223372036854775807 - 1\n"
                       "integer y = -x\n"
                       "y",
                       "integer overflow in negation");
    ASSERT_EVAL_THROWS("function integer f() {\n"
                       "    mutable integer x = 9223372036854775807\n"
                       "    x++\n"
                       "    return x\n"
                       "}\n"
                       "f()",
                       "integer overflow in increment");

    // In-range results are unaffected and stay integers.
    const auto result = eval("mutable integer x = 9223372036854775806\nx + 1");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 9223372036854775807LL);
}

LUMA_TEST(vm_number_division_by_zero) {
    ASSERT_EVAL_THROWS("number x = 10.0\nnumber y = 0.0\nx / y", "Division: division by zero");
}

LUMA_TEST(vm_number_overflow_throws) {
    ASSERT_EVAL_THROWS("number x = 1.0e308\nx * 10.0", "number overflow in multiplication");
    ASSERT_EVAL_THROWS("number x = 1.0e308\nx + x", "number overflow in addition");
}

// Regression (review #19): `/` had no overflow check, so finite operands could
// silently produce Infinity.
LUMA_TEST(vm_number_division_overflow_throws) {
    ASSERT_EVAL_THROWS("number x = 1.0e308\nnumber y = 1.0e-308\nx / y",
                       "number overflow in division");

    const auto result = eval("7 / 2");

    ASSERT_TRUE(result.is_number());
    ASSERT_EQ(result.as_number(), 3.5);
}

// ─── Index errors ───

LUMA_TEST(vm_array_index_out_of_bounds) {
    ASSERT_EVAL_THROWS("[1, 2, 3][10]", "Index 10 out of bounds");
}

LUMA_TEST(vm_dictionary_key_not_found) {
    ASSERT_EVAL_THROWS("dictionary<string> d = {\"a\": \"hello\"}\n"
                       "d[\"missing\"]",
                       "key 'missing' not found");
}

LUMA_TEST(vm_negative_array_index) {
    // Negative indices are out of bounds (not Python-style wrapping).
    ASSERT_EVAL_THROWS("array<integer> a = [10, 20, 30]\n"
                       "a[-1]",
                       "Index -1 out of bounds");
}

// The type checker rejects a non-integer left operand of `in <range>`, but a
// mistyped value can still reach the VM through dynamic paths (e.g. an
// `any`-typed value). The unchecked eval() path reproduces that: the VM must
// surface a clean RuntimeError rather than crash on an unchecked as_integer().
LUMA_TEST(vm_contains_range_non_integer_throws) {
    ASSERT_THROWS_WITH_MESSAGE(eval("\"x\" in 1..=100"), "'in' on a range requires an integer");
    ASSERT_THROWS_WITH_MESSAGE(eval("1.5 in 1..100"), "'in' on a range requires an integer");
}

LUMA_TEST(vm_string_index_out_of_bounds) {
    ASSERT_EVAL_THROWS("\"hi\"[10]", "String index 10 out of bounds");
}

LUMA_TEST(vm_tuple_index_out_of_bounds) {
    ASSERT_EVAL_THROWS("(1, 2, 3)[5]", "Tuple index 5 out of bounds");
}

// ─── Index type-mismatch guards ───

// The type checker normally rejects these, but a mistyped value can reach an
// index operation at runtime through dynamic paths (e.g. a dynamically typed
// value reaching an index op). The VM must surface a clean RuntimeError
// rather than crash on an unchecked std::variant access (bad_variant_access).
// The unchecked eval() path reproduces that dynamic situation.

LUMA_TEST(vm_string_index_non_integer_throws) {
    ASSERT_EVAL_THROWS("\"hello\"[\"x\"]", "String index must be an integer, got 'string'");
}

LUMA_TEST(vm_array_index_non_integer_throws) {
    ASSERT_EVAL_THROWS("[1, 2, 3][\"x\"]", "Index must be an integer, got 'string'");
}

LUMA_TEST(vm_dict_index_non_string_throws) {
    ASSERT_EVAL_THROWS("dictionary<integer> d = {\"a\": 1}\n"
                       "d[0]",
                       "dictionary index must be a string, got 'integer'");
}

LUMA_TEST(vm_array_index_set_non_integer_throws) {
    ASSERT_EVAL_THROWS("mutable array<integer> a = [1, 2, 3]\n"
                       "a[\"x\"] = 9",
                       "Array index must be an integer, got 'string'");
}

LUMA_TEST(vm_dict_index_set_non_string_throws) {
    ASSERT_EVAL_THROWS("mutable dictionary<integer> d = {\"a\": 1}\n"
                       "d[0] = 9",
                       "dictionary index must be a string, got 'integer'");
}

// ─── Try, catch, and finally ───

LUMA_TEST(vm_try_catch) {
    const auto result = eval("function string try_test() {\n"
                             "    try {\n"
                             "        integer x = 1 / 0\n"
                             "        return \"no error\"\n"
                             "    } catch(e) {\n"
                             "        return \"caught\"\n"
                             "    }\n"
                             "}\n"
                             "try_test()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "caught");
}

LUMA_TEST(vm_try_finally_always_runs) {
    const auto result = eval("function string finally_test() {\n"
                             "    mutable string log = \"\"\n"
                             "    try {\n"
                             "        log = log + \"try\"\n"
                             "    } finally {\n"
                             "        log = log + \"-finally\"\n"
                             "    }\n"
                             "    return log\n"
                             "}\n"
                             "finally_test()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "try-finally");
}

LUMA_TEST(vm_try_catch_finally) {
    const auto result = eval("function string f() {\n"
                             "    mutable string log = \"\"\n"
                             "    try {\n"
                             "        log = log + \"try\"\n"
                             "        integer x = 1 / 0\n"
                             "    } catch(e) {\n"
                             "        log = log + \"-catch\"\n"
                             "    } finally {\n"
                             "        log = log + \"-finally\"\n"
                             "    }\n"
                             "    return log\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "try-catch-finally");
}

// Regression: finally must run even when the catch body itself raises a new
// error. The original lowering left the catch body unprotected, so finally was
// skipped whenever the catch threw. The new error still propagates to the
// enclosing handler — but only AFTER finally has run. Expected order: catch
// runs ("C"), finally runs ("F"), then the outer handler catches ("O").
LUMA_TEST(vm_finally_runs_when_catch_throws) {
    const auto result = eval("function string f() {\n"
                             "    mutable string log = \"\"\n"
                             "    try {\n"
                             "        try {\n"
                             "            integer a = 1 / 0\n"
                             "        } catch(e) {\n"
                             "            log = log + \"C\"\n"
                             "            integer b = 1 / 0\n"
                             "        } finally {\n"
                             "            log = log + \"F\"\n"
                             "        }\n"
                             "    } catch(outer) {\n"
                             "        log = log + \"O\"\n"
                             "    }\n"
                             "    return log\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "CFO");
}

// Regression: finally must run when control leaves the catch body via break.
LUMA_TEST(vm_finally_runs_on_break_from_catch) {
    const auto result = eval("function integer f() {\n"
                             "    mutable integer ran = 0\n"
                             "    for i in 0 .. 5 {\n"
                             "        try {\n"
                             "            integer a = 1 / 0\n"
                             "        } catch(e) {\n"
                             "            break\n"
                             "        } finally {\n"
                             "            ran = ran + 1\n"
                             "        }\n"
                             "    }\n"
                             "    return ran\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 1);
}

// finally must run when the catch body returns from the function. A Reference
// makes the side effect observable after the early return: the catch appends
// "C" and the finally appends "F" before control leaves, so the shared log is
// "CF" and the returned value is "ret".
LUMA_TEST(vm_finally_runs_on_return_from_catch) {
    const auto result = eval("function string body(reference<string> log) {\n"
                             "    try {\n"
                             "        integer a = 1 / 0\n"
                             "    } catch(e) {\n"
                             "        Reference.set(log, Reference.get(log) + \"C\")\n"
                             "        return \"ret\"\n"
                             "    } finally {\n"
                             "        Reference.set(log, Reference.get(log) + \"F\")\n"
                             "    }\n"
                             "    return \"end\"\n"
                             "}\n"
                             "function string f() {\n"
                             "    reference<string> log = Reference.new(\"\")\n"
                             "    string r = body(log)\n"
                             "    return Reference.get(log) + \"|\" + r\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "CF|ret");
}

// Regression: a non-local exit (return) inside an INNER finally must still run
// every ENCLOSING finally. The inner `return 1` triggers the inner finally,
// which appends "Fi" and itself does `return 2`; that nested return has to
// unwind the outer try and run its finally ("Fo") before leaving. A prior
// emit_try_unwind that detached all unwound handlers up front left no active
// handler while the inner finally compiled, silently skipping "Fo" (log became
// "Fi" instead of "FiFo"). Expected: outer finally runs, return 2 wins.
LUMA_TEST(vm_inner_finally_return_runs_outer_finally) {
    const auto result = eval("function integer body(reference<string> log) {\n"
                             "    try {\n"
                             "        try {\n"
                             "            return 1\n"
                             "        } finally {\n"
                             "            Reference.set(log, Reference.get(log) + \"Fi\")\n"
                             "            return 2\n"
                             "        }\n"
                             "    } finally {\n"
                             "        Reference.set(log, Reference.get(log) + \"Fo\")\n"
                             "    }\n"
                             "}\n"
                             "function string f() {\n"
                             "    reference<string> log = Reference.new(\"\")\n"
                             "    integer r = body(log)\n"
                             "    return Reference.get(log) + \"|\" + \"${r}\"\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "FiFo|2");
}

// The catch variable binds the runtime error message string, which the catch
// body can read and return.
LUMA_TEST(vm_catch_binds_error_message) {
    const auto result = eval("function string f() {\n"
                             "    try {\n"
                             "        integer a = 1 / 0\n"
                             "        return \"unreachable\"\n"
                             "    } catch(err) {\n"
                             "        return err\n"
                             "    }\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_NE(result.as_string().find("division by zero"), std::string::npos);
}

// The manual's nested example: an inner try/finally with no catch runs its
// finally body and then re-raises, so the outer catch still observes the error.
// Expected order: try ("t"), inner finally ("-f"), outer catch ("-c").
LUMA_TEST(vm_nested_finally_runs_before_propagation) {
    const auto result = eval("function string f() {\n"
                             "    mutable string log = \"\"\n"
                             "    try {\n"
                             "        try {\n"
                             "            log = log + \"t\"\n"
                             "            integer a = 1 / 0\n"
                             "            log = log + \"X\"\n"
                             "        } finally {\n"
                             "            log = log + \"-f\"\n"
                             "        }\n"
                             "    } catch(e) {\n"
                             "        log = log + \"-c\"\n"
                             "    }\n"
                             "    return log\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "t-f-c");
}

// Regression (review #20): the default assert message matches the manual.
LUMA_TEST(vm_assert_default_message_is_lowercase) {
    ASSERT_EVAL_THROWS("assert(false)", "assertion failed");
}

// ─── Call depth and stack limits ───

LUMA_TEST(vm_recursion_depth_limit) {
    // Recursion beyond 256 frames should throw a RuntimeError.
    // Use `1 + infinite(...)` to prevent tail-call optimisation.
    ASSERT_EVAL_THROWS("function integer infinite(integer n) {\n"
                       "    return 1 + infinite(n + 1)\n"
                       "}\n"
                       "infinite(0)",
                       "call stack overflow");
}

// Regression (review #17): LUMA_LIMIT_MAX_CALL_DEPTH above the VM's 256-frame
// capacity must not lift the cap — every call path enforces the smaller limit.
LUMA_TEST(vm_call_depth_override_cannot_exceed_frame_limit) {
    const LimitGuard guard{ResourceLimits::max_call_depth, 100000};

    ASSERT_EVAL_THROWS("function integer down(integer n) {\n"
                       "    if n == 0 { return 0 }\n"
                       "    return 1 + down(n - 1)\n"
                       "}\n"
                       "down(1000)",
                       "call stack overflow");
}

// Regression (review #10): lowering the stack top must destroy the values
// above it, not leave them alive until overwritten.
LUMA_TEST(vm_stack_truncate_releases_values) {
    VMStack stack;
    const auto array = std::make_shared<ArrayValue>();
    stack.push(Value{array});
    stack.push(Value{array});

    ASSERT_EQ(array.use_count(), 3L);

    stack.truncate_to(stack.base);

    ASSERT_EQ(array.use_count(), 1L);
    ASSERT_EQ(stack.size(), 0);
}

// Regression (review #14): task VMs recycle their stack storage per thread
// instead of allocating and initialising a full 65,536-slot stack per spawn.
LUMA_TEST(vm_stack_storage_is_recycled) {
    const Value* first_base = nullptr;

    {
        const VMStack stack;
        first_base = stack.base;
    }

    const VMStack again;

    ASSERT_EQ(again.base, first_base);
}

// ─── Main ───

int main() {
    LUMA_RUN_ALL();
}
