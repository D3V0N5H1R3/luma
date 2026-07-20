// Type checker unit tests — namespaces, downcast, optional chains, any-warn, return analysis.

#include "type_checker_test_helpers.hpp"

// ─── Namespace ───

static void test_namespace_valid() {
    ASSERT_TRUE(passes("namespace Geometry {\n"
                       "    function number area(number r) {\n"
                       "        return r * r\n"
                       "    }\n"
                       "}\n"));
}

static void test_namespace_qualified_record_type() {
    // Qualified type annotation: Namespace.TypeName is valid and resolves
    // to the same type as the bare name after 'use'.
    ASSERT_TRUE(passes("namespace Shapes {\n"
                       "    record Circle { number radius }\n"
                       "}\n"
                       "use Shapes\n"
                       "function void foo() {\n"
                       "    Shapes.Circle c = Shapes.Circle { radius = 1.0 }\n"
                       "    Circle c2 = Circle { radius = 2.0 }\n"
                       "}\n"));
}

static void test_namespace_type_only() {
    // A namespace that contains only types (no functions) must still allow
    // qualified type access.
    ASSERT_TRUE(passes("namespace Domain {\n"
                       "    choice Status { Active  Inactive }\n"
                       "    record User { string name }\n"
                       "}\n"
                       "function void foo() {\n"
                       "    Domain.Status s = Domain.Status.Active\n"
                       "    Domain.User u = Domain.User { name = \"alice\" }\n"
                       "}\n"));
}

static void test_namespace_qualified_unknown_type() {
    // Accessing a name that doesn't exist in a namespace with functions
    // is a type error.
    ASSERT_TRUE(fails("namespace Shapes {\n"
                      "    function number area(number r) { return r * r }\n"
                      "}\n"
                      "function void foo() {\n"
                      "    Shapes.DoesNotExist x = Shapes.area(5.0)\n"
                      "}\n"));
}

static void test_namespace_qualified_interface_type() {
    // A public interface declared in a namespace is usable as a qualified
    // parameter type, and its fields are accessible through that parameter.
    ASSERT_TRUE(passes("namespace Geo {\n"
                       "    interface Shape { number area }\n"
                       "}\n"
                       "function number describe(Geo.Shape s) { return s.area }\n"));
}

// ─── Downcast ───

static void test_downcast_valid() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    integer val = 42\n"
                       "    result<integer> r = downcast<integer>(val)\n"
                       "}\n"));
}

// downcast<number> must accept an integer-typed value (widening).
static void test_downcast_number_accepts_integer() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    integer val = 42\n"
                       "    result<number> r = downcast<number>(val)\n"
                       "}\n"));
}

// downcast<T> with an unknown type name is a type error.
static void test_downcast_unknown_type_error() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    integer val = 42\n"
                      "    result<integer> r = downcast<NoSuchType>(val)\n"
                      "}\n"));
}

// is<T> with an unknown type name is a type error.
static void test_is_unknown_type_error() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    integer val = 42\n"
                      "    boolean b = is<NoSuchType>(val)\n"
                      "}\n"));
}

// Casting between unrelated primitive types emits a warning (but is not an error).
static void test_impossible_cast_warning() {
    ASSERT_TRUE(has_warnings("function void foo() {\n"
                             "    integer val = 42\n"
                             "    result<string> r = downcast<string>(val)\n"
                             "}\n"));
}

// downcast<T> when the value is already of type T emits a warning.
static void test_redundant_downcast_warning() {
    ASSERT_TRUE(has_warnings("function void foo() {\n"
                             "    integer val = 42\n"
                             "    result<integer> r = downcast<integer>(val)\n"
                             "}\n"));
}

// trusted_downcast<T> returns T directly (not result<T>), so it is assignable
// to a non-result target variable.
static void test_trusted_downcast_valid() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    integer val = 42\n"
                       "    integer n = trusted_downcast<integer>(val)\n"
                       "}\n"));
}

// trusted_downcast<T> with unknown type is a type error.
static void test_trusted_downcast_unknown_type_error() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    integer val = 42\n"
                      "    integer n = trusted_downcast<NoSuchType>(val)\n"
                      "}\n"));
}

// Assigning a ?. chain result returns optional<T> which must be matched.
static void test_optional_chain_warn_non_nullable() {
    ASSERT_TRUE(fails("record User {\n"
                      "    string name\n"
                      "}\n"
                      "function void foo(User user) {\n"
                      "    string name = user?.name\n"
                      "}\n"));
}

// Using ?? after ?. unwraps optional<T> — assigns T to T variable, no error.
static void test_optional_chain_coalesce_no_warn() {
    ASSERT_TRUE(passes("record User {\n"
                       "    string name\n"
                       "}\n"
                       "function void foo(User user) {\n"
                       "    string name = user?.name ?? \"unknown\"\n"
                       "}\n"));
}

// Assigning a ?[i] result (optional<T>) to a non-nullable variable is a type error.
static void test_optional_index_warn_non_nullable() {
    ASSERT_TRUE(fails("function void foo(array<integer> arr) {\n"
                      "    integer v = arr?[0]\n"
                      "}\n"));
}

// Using ?? after ?[i] suppresses the type error.
static void test_optional_index_coalesce_no_warn() {
    ASSERT_TRUE(passes("function void foo(array<integer> arr) {\n"
                       "    integer v = arr?[0] ?? 0\n"
                       "}\n"));
}

// Storing the result of a void function in a variable emits a warning.
static void test_void_call_site_warn() {
    ASSERT_TRUE(has_warnings("function void do_thing() {}\n"
                             "function void foo() {\n"
                             "    integer n = do_thing()\n"
                             "}\n"));
}

// Calling a non-void function and discarding the return value emits a warning.
static void test_discarded_value_warn() {
    ASSERT_TRUE(has_warnings("function integer compute() { return 42 }\n"
                             "@main\n"
                             "function void main() {\n"
                             "    compute()\n"
                             "}\n"));
}

// Assigning a non-void call to a variable does not emit a discarded-value warning.
static void test_discarded_value_no_warn_when_assigned() {
    ASSERT_FALSE(has_warnings("function integer compute() { return 42 }\n"
                              "@main\n"
                              "function void main() {\n"
                              "    integer n = compute()\n"
                              "    print(n)\n"
                              "}\n"));
}

// Calling a void function as a statement does not emit a discarded-value warning.
static void test_discarded_value_no_warn_void() {
    ASSERT_FALSE(has_warnings("function void do_thing() {}\n"
                              "@main\n"
                              "function void main() {\n"
                              "    do_thing()\n"
                              "}\n"));
}

// Using '?' inside @main is allowed — propagated failures cause a RuntimeError.
static void test_error_propagation_in_main() {
    ASSERT_TRUE(passes("function result<integer> fallible() { return success(1) }\n"
                       "@main\n"
                       "function void main() {\n"
                       "    integer n = fallible()?\n"
                       "}\n"));
}

// Using '?' in a function that does not return result<T> is an error.
static void test_error_propagation_requires_result_return() {
    ASSERT_TRUE(fails("function result<integer> fallible() { return success(1) }\n"
                      "function integer bad() {\n"
                      "    integer n = fallible()?\n"
                      "    return n\n"
                      "}\n"));
}

// Using '?' on optional<T> in a function returning optional<T> is valid.
static void test_optional_propagation_in_optional_function() {
    ASSERT_TRUE(passes("function optional<integer> get_value(optional<integer> x) {\n"
                       "    integer v = x?\n"
                       "    return some(v + 1)\n"
                       "}\n"));
}

// Using '?' on optional<T> in a function not returning optional/result is an error.
static void test_optional_propagation_requires_optional_return() {
    ASSERT_TRUE(fails("function integer bad(optional<integer> x) {\n"
                      "    integer v = x?\n"
                      "    return v\n"
                      "}\n"));
}

// '?' on an optional<T> inside a result<T>-returning function is a wrapper-kind
// mismatch — a propagated 'none' cannot become a 'failure'. (Regression: this
// was accepted, letting a none escape a result-returning function.)
static void test_optional_propagation_in_result_function_is_error() {
    ASSERT_TRUE(fails("function result<integer> bad(optional<integer> x) {\n"
                      "    integer v = x?\n"
                      "    return success(v)\n"
                      "}\n"));
}

// '?' on a result<T> inside an optional<T>-returning function is a wrapper-kind
// mismatch — a propagated 'failure' cannot become a 'none'.
static void test_result_propagation_in_optional_function_is_error() {
    ASSERT_TRUE(fails("function optional<integer> bad() {\n"
                      "    result<integer> r = success(1)\n"
                      "    integer v = r?\n"
                      "    return some(v)\n"
                      "}\n"));
}

// 'any' keyword is no longer valid — programs using it should fail to parse.
static void test_any_warn_operator() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = 1\n"
                      "    any y = x + 1\n"
                      "}\n"));
}

// 'any' as a type in arithmetic expressions is no longer valid.
static void test_any_warn_arithmetic_minus() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = 5\n"
                      "    any y = x - 1\n"
                      "}\n"));
}

static void test_any_warn_arithmetic_star() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = 3\n"
                      "    any y = x * 2\n"
                      "}\n"));
}

static void test_any_warn_arithmetic_slash() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = 10\n"
                      "    any y = x / 2\n"
                      "}\n"));
}

static void test_any_warn_arithmetic_percent() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = 10\n"
                      "    any y = x % 3\n"
                      "}\n"));
}

// 'any' as a type in comparison expressions is no longer valid.
static void test_any_warn_comparison_less() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = 3\n"
                      "    boolean y = x < 5\n"
                      "}\n"));
}

static void test_any_warn_comparison_greater() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = 3\n"
                      "    boolean y = x > 1\n"
                      "}\n"));
}

// 'any' as a function parameter type is no longer valid.
static void test_any_warn_concrete_param() {
    ASSERT_TRUE(fails("function integer takes_integer(integer n) { return n }\n"
                      "function void foo() {\n"
                      "    any x = 1\n"
                      "    takes_integer(x)\n"
                      "}\n"));
}

// 'any' in equality expressions is no longer valid.
static void test_any_warn_equality() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = 1\n"
                      "    boolean y = x == 1\n"
                      "}\n"));
}

static void test_any_warn_not_equal() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = 1\n"
                      "    boolean y = x != 2\n"
                      "}\n"));
}

// 'any' in string interpolation is no longer valid.
static void test_any_warn_string_interpolation() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = 42\n"
                      "    string s = \"value is ${x}\"\n"
                      "}\n"));
}

// String interpolation with a concrete type does NOT warn.
static void test_no_warn_string_interpolation_concrete() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    integer x = 42\n"
                       "    string s = \"value is ${x}\"\n"
                       "}\n"));
}

// A function with a concrete return type that has a return on every path: no warning.
static void test_no_warn_definite_return() {
    ASSERT_TRUE(passes("function string greet(boolean flag) {\n"
                       "    if flag {\n"
                       "        return \"yes\"\n"
                       "    } else {\n"
                       "        return \"no\"\n"
                       "    }\n"
                       "}\n"));
}

// A function with a concrete return type that can fall through: TypeError.
static void test_error_missing_return() {
    ASSERT_TRUE(fails("function string greet(boolean flag) {\n"
                      "    if flag {\n"
                      "        return \"yes\"\n"
                      "    }\n"
                      "}\n"));
}

// `while true` whose body can exit via `break` is NOT guaranteed to return, so a
// concrete-typed function that relies on it (with no trailing return) can fall
// through: TypeError. Regression guard for the definitely_returns break-analysis.
static void test_error_missing_return_while_true_break() {
    ASSERT_TRUE(fails("function integer f(boolean c) {\n"
                      "    while true {\n"
                      "        if c {\n"
                      "            break\n"
                      "        }\n"
                      "        return 1\n"
                      "    }\n"
                      "}\n"));
}

// `while true` with no reachable break always returns (or loops forever), so a
// concrete-typed function ending in it does not fall through: no error. Guards
// against the break-analysis fix over-reporting on genuine infinite loops.
static void test_no_warn_while_true_no_break() {
    ASSERT_TRUE(passes("function integer g() {\n"
                       "    while true {\n"
                       "        return 1\n"
                       "    }\n"
                       "}\n"));
}

// A `break` inside a nested loop binds to that inner loop, so the outer
// `while true` still definitely returns: no error.
static void test_no_warn_while_true_break_in_nested_loop() {
    ASSERT_TRUE(passes("function integer h() {\n"
                       "    while true {\n"
                       "        for x in [1, 2, 3] {\n"
                       "            break\n"
                       "        }\n"
                       "        return 1\n"
                       "    }\n"
                       "}\n"));
}

// A `break` in statically unreachable dead code (after a guaranteed return)
// cannot make the loop fall through, so the break-analysis must ignore it: the
// function still definitely returns. Guards against the reachability-insensitive
// false-positive where dead `break`s flipped a returning function to an error.
static void test_no_warn_while_true_dead_break_after_return() {
    ASSERT_TRUE(passes("function integer f() {\n"
                       "    while true {\n"
                       "        return 1\n"
                       "        break\n"
                       "    }\n"
                       "}\n"));
}

// A `break` guarded by an exhaustive-return branch is still reachable (the guard
// may be false), so this function CAN fall through: TypeError. Ensures the
// reachability stop does not swallow breaks before a non-total return.
static void test_error_while_true_break_after_partial_return() {
    ASSERT_TRUE(fails("function integer f(boolean c) {\n"
                      "    while true {\n"
                      "        if c {\n"
                      "            return 1\n"
                      "        }\n"
                      "        break\n"
                      "    }\n"
                      "}\n"));
}

// A void function without return: no warning.
static void test_no_warn_void_no_return() {
    ASSERT_TRUE(passes("function void greet() {\n"
                       "    print(\"hello\")\n"
                       "}\n"));
}

// A top-level return is definite.
static void test_no_warn_top_level_return() {
    ASSERT_TRUE(passes("function string foo() {\n"
                       "    return \"ok\"\n"
                       "}\n"));
}

// ─── any-warn: subscript / iterate / match / field access ───

// Subscripting an any-typed value no longer valid.
static void test_any_warn_subscript() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any xs = [1, 2, 3]\n"
                      "    any v = xs[0]\n"
                      "}\n"));
}

// Iterating over an any-typed value no longer valid.
static void test_any_warn_for_iteration() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any xs = [1, 2, 3]\n"
                      "    for x in xs {\n"
                      "        print(x)\n"
                      "    }\n"
                      "}\n"));
}

// Pattern matching on an any-typed value no longer valid.
static void test_any_warn_match() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = true\n"
                      "    match x {\n"
                      "        case == true  { print(\"yes\") }\n"
                      "        else          { print(\"no\") }\n"
                      "    }\n"
                      "}\n"));
}

// Field access on an any-typed value no longer valid.
static void test_any_warn_field_access() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    any x = none\n"
                      "    any n = x.name\n"
                      "}\n"));
}

// Optional chaining on a concrete record returns optional<T>.
static void test_optional_chain_concrete_type() {
    ASSERT_TRUE(passes("record User { string name }\n"
                       "function void foo(User u) {\n"
                       "    string n = u?.name ?? \"default\"\n"
                       "}\n"));
}

// ─── Try / catch / finally ───

// The catch variable is bound as a string holding the error message, so it can
// be assigned to a string without conversion.
static void test_try_catch_var_is_string() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    try {\n"
                       "        integer x = 1\n"
                       "        print(x)\n"
                       "    } catch(err) {\n"
                       "        string message = err\n"
                       "        print(message)\n"
                       "    }\n"
                       "}\n"));
}

// Using the catch variable where a non-string type is expected is a type error.
static void test_try_catch_var_not_integer() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    try {\n"
                      "        integer x = 1\n"
                      "        print(x)\n"
                      "    } catch(err) {\n"
                      "        integer n = err\n"
                      "        print(n)\n"
                      "    }\n"
                      "}\n"));
}

// Type errors inside the try body are still reported.
static void test_try_body_is_type_checked() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    try {\n"
                      "        integer x = \"not an integer\"\n"
                      "        print(x)\n"
                      "    } catch(err) {\n"
                      "        print(err)\n"
                      "    }\n"
                      "}\n"));
}

// Type errors inside the catch body are still reported.
static void test_try_catch_body_is_type_checked() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    try {\n"
                      "        print(\"ok\")\n"
                      "    } catch(err) {\n"
                      "        integer y = \"not an integer\"\n"
                      "        print(y)\n"
                      "    }\n"
                      "}\n"));
}

// Type errors inside the finally body are still reported.
static void test_try_finally_body_is_type_checked() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    try {\n"
                      "        print(\"ok\")\n"
                      "    } finally {\n"
                      "        integer z = \"not an integer\"\n"
                      "        print(z)\n"
                      "    }\n"
                      "}\n"));
}

// ─── Match arm: unknown choice variant (shared binding helper) ───
// Characterization for the one behavioural asymmetry preserved when the
// per-arm binding logic was unified: a match *statement* arm naming a variant
// the choice does not declare reports a "has no variant" diagnostic, whereas
// the match *expression* path deliberately stays silent about it.
static void test_match_statement_reports_unknown_choice_variant() {
    const auto diags = check("choice Shape { Circle(number r) Point }\n"
                             "function void describe(Shape s) {\n"
                             "    match s {\n"
                             "        case Shape.Circle(r) { print(r) }\n"
                             "        case Shape.Ghost(x)  { print(x) }\n"
                             "        case Shape.Point     { print(0) }\n"
                             "    }\n"
                             "}\n");

    bool found = false;
    for (const auto& d : diags) {
        if (d.message.find("has no variant") != std::string::npos &&
            d.message.find("Ghost") != std::string::npos) {
            found = true;
        }
    }
    ASSERT_TRUE(found);
}

static void test_match_expression_silent_on_unknown_choice_variant() {
    const auto diags = check("choice Shape { Circle(number r) Point }\n"
                             "function number describe(Shape s) {\n"
                             "    return match s {\n"
                             "        case Shape.Circle(r) { r }\n"
                             "        case Shape.Ghost(x)  { 1.0 }\n"
                             "        case Shape.Point     { 0.0 }\n"
                             "    }\n"
                             "}\n");

    for (const auto& d : diags) {
        ASSERT_TRUE(d.message.find("has no variant") == std::string::npos);
    }
}

// Regression: mutually-recursive interfaces used in an assignability check must
// not drive is_assignable into unbounded recursion (which previously overflowed
// the stack).  Structurally A and B are equivalent — each has a single field
// pointing at the other — so passing a B where an A is expected type-checks
// cleanly under the coinductive structural-subtyping rule.
static void test_mutually_recursive_interfaces_terminate() {
    ASSERT_TRUE(passes("interface A {\n"
                       "    B other\n"
                       "}\n"
                       "interface B {\n"
                       "    A other\n"
                       "}\n"
                       "function void take_a(A a) {}\n"
                       "function void run(B b) {\n"
                       "    take_a(b)\n"
                       "}\n"));
}

int main() {
    // ─── Namespace ───

    RUN(test_namespace_valid);
    RUN(test_namespace_qualified_record_type);
    RUN(test_namespace_type_only);
    RUN(test_namespace_qualified_unknown_type);
    RUN(test_namespace_qualified_interface_type);
    RUN(test_mutually_recursive_interfaces_terminate);

    // ─── Downcast ───

    RUN(test_downcast_valid);
    RUN(test_downcast_number_accepts_integer);
    RUN(test_downcast_unknown_type_error);
    RUN(test_is_unknown_type_error);
    RUN(test_impossible_cast_warning);
    RUN(test_redundant_downcast_warning);
    RUN(test_trusted_downcast_valid);
    RUN(test_trusted_downcast_unknown_type_error);
    RUN(test_optional_chain_warn_non_nullable);
    RUN(test_optional_chain_coalesce_no_warn);
    RUN(test_optional_index_warn_non_nullable);
    RUN(test_optional_index_coalesce_no_warn);
    RUN(test_void_call_site_warn);
    RUN(test_discarded_value_warn);
    RUN(test_discarded_value_no_warn_when_assigned);
    RUN(test_discarded_value_no_warn_void);
    RUN(test_error_propagation_in_main);
    RUN(test_error_propagation_requires_result_return);
    RUN(test_optional_propagation_in_optional_function);
    RUN(test_optional_propagation_requires_optional_return);
    RUN(test_optional_propagation_in_result_function_is_error);
    RUN(test_result_propagation_in_optional_function_is_error);
    RUN(test_any_warn_operator);
    RUN(test_any_warn_arithmetic_minus);
    RUN(test_any_warn_arithmetic_star);
    RUN(test_any_warn_arithmetic_slash);
    RUN(test_any_warn_arithmetic_percent);
    RUN(test_any_warn_comparison_less);
    RUN(test_any_warn_comparison_greater);
    RUN(test_any_warn_concrete_param);
    RUN(test_any_warn_equality);
    RUN(test_any_warn_not_equal);
    RUN(test_any_warn_string_interpolation);
    RUN(test_no_warn_string_interpolation_concrete);
    RUN(test_no_warn_definite_return);
    RUN(test_error_missing_return);
    RUN(test_error_missing_return_while_true_break);
    RUN(test_no_warn_while_true_no_break);
    RUN(test_no_warn_while_true_break_in_nested_loop);
    RUN(test_no_warn_while_true_dead_break_after_return);
    RUN(test_error_while_true_break_after_partial_return);
    RUN(test_no_warn_void_no_return);
    RUN(test_no_warn_top_level_return);

    // ─── any-warn: subscript / iterate / match / field access ───

    RUN(test_any_warn_subscript);
    RUN(test_any_warn_for_iteration);
    RUN(test_any_warn_match);
    RUN(test_any_warn_field_access);
    RUN(test_optional_chain_concrete_type);
    RUN(test_try_catch_var_is_string);
    RUN(test_try_catch_var_not_integer);
    RUN(test_try_body_is_type_checked);
    RUN(test_try_catch_body_is_type_checked);
    RUN(test_try_finally_body_is_type_checked);
    RUN(test_match_statement_reports_unknown_choice_variant);
    RUN(test_match_expression_silent_on_unknown_choice_variant);
    return SUMMARY();
}
