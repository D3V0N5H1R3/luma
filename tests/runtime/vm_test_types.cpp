// VM unit tests: records, choice types, match, result, and optional.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/errors/error.hpp"
#include "runtime/compiler/compiled_function.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_introspection.hpp"
#include "stdlib_test_helpers.hpp"

// ─── Record tests ───

LUMA_TEST(vm_record_creation) {
    const auto result = eval("record Point {\n"
                             "    integer x,\n"
                             "    integer y\n"
                             "}\n"
                             "function integer get_x() {\n"
                             "    Point p = Point { x = 3, y = 4 }\n"
                             "    return p.x\n"
                             "}\n"
                             "get_x()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 3);
}

LUMA_TEST(vm_record_field_access) {
    const auto result = eval("record Person {\n"
                             "    string name,\n"
                             "    integer age\n"
                             "}\n"
                             "function string get_name() {\n"
                             "    Person p = Person { name = \"Alice\", age = 30 }\n"
                             "    return p.name\n"
                             "}\n"
                             "get_name()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "Alice");
}

LUMA_TEST(vm_record_mutation) {
    const auto result = eval("record Point {\n"
                             "    integer x,\n"
                             "    integer y\n"
                             "}\n"
                             "function integer f() {\n"
                             "    mutable Point p = Point { x = 1, y = 2 }\n"
                             "    p.x = 10\n"
                             "    p.y = 20\n"
                             "    return p.x + p.y\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 30);
}

LUMA_TEST(vm_record_equality) {
    // Structural equality: same fields compare equal, differing fields do not.
    const auto result = eval("record Point {\n"
                             "    integer x,\n"
                             "    integer y\n"
                             "}\n"
                             "function boolean f() {\n"
                             "    Point a = Point { x = 1, y = 2 }\n"
                             "    Point b = Point { x = 1, y = 2 }\n"
                             "    Point c = Point { x = 9, y = 2 }\n"
                             "    return (a == b) && (a != c)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_nested_record_field_access) {
    const auto result = eval("record Inner { integer v }\n"
                             "record Outer { Inner inner, integer k }\n"
                             "function integer f() {\n"
                             "    Outer o = Outer { inner = Inner { v = 7 }, k = 3 }\n"
                             "    return o.inner.v + o.k\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 10);
}

// ─── Choice type tests ───

LUMA_TEST(vm_choice_type) {
    const auto result = eval("choice Shape {\n"
                             "    Circle(number radius)\n"
                             "    Square(number side)\n"
                             "}\n"
                             "Shape s = Shape.Circle(3.14)\n"
                             "number area = match s {\n"
                             "    case Shape.Circle(r) { r }\n"
                             "    case Shape.Square(s) { s }\n"
                             "}\n"
                             "area");

    ASSERT_TRUE(result.is_number());
    ASSERT_NEAR(result.as_number(), 3.14, 0.001);
}

LUMA_TEST(vm_choice_unit_variant_equality) {
    // Unit variants of the same choice compare equal only to themselves,
    // and a match dispatches to the corresponding arm.
    const auto result = eval("choice Color { Red  Green  Blue }\n"
                             "function string f() {\n"
                             "    Color c = Color.Green\n"
                             "    assert((c == Color.Green) && (c != Color.Red))\n"
                             "    return match c {\n"
                             "        case Color.Red   { \"r\" }\n"
                             "        case Color.Green { \"g\" }\n"
                             "        case Color.Blue  { \"b\" }\n"
                             "    }\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "g");
}

LUMA_TEST(vm_choice_data_variant_equality) {
    // Data variants compare structurally: same variant and payload are
    // equal; a different payload or a different variant are not.
    const auto result = eval("choice Shape {\n"
                             "    Circle(number radius)\n"
                             "    Rectangle(number width, number height)\n"
                             "}\n"
                             "function boolean f() {\n"
                             "    Shape a = Shape.Circle(5.0)\n"
                             "    Shape b = Shape.Circle(5.0)\n"
                             "    Shape c = Shape.Circle(9.0)\n"
                             "    Shape d = Shape.Rectangle(5.0, 5.0)\n"
                             "    return (a == b) && (a != c) && (a != d)\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_choice_to_string) {
    // String interpolation renders unit variants as Type.Variant and data
    // variants as Type.Variant(args).
    const auto result = eval("choice Color { Red  Green  Blue }\n"
                             "choice Shape {\n"
                             "    Circle(number radius)\n"
                             "    Point\n"
                             "}\n"
                             "function string f() {\n"
                             "    Color c = Color.Green\n"
                             "    Shape s = Shape.Circle(2.5)\n"
                             "    return \"${c}|${s}\"\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "Color.Green|Shape.Circle(2.5)");
}

LUMA_TEST(vm_recursive_adt_eval) {
    // Non-generic recursive ADT: evaluate (3 + 4) * 5 = 35 over an
    // expression tree built from nested data variants.
    const auto result =
        eval("choice Expr {\n"
             "    Num(integer value)\n"
             "    Add(Expr left, Expr right)\n"
             "    Mul(Expr left, Expr right)\n"
             "}\n"
             "function integer ev(Expr e) {\n"
             "    return match e {\n"
             "        case Expr.Num(v)    { v }\n"
             "        case Expr.Add(l, r) { ev(l) + ev(r) }\n"
             "        case Expr.Mul(l, r) { ev(l) * ev(r) }\n"
             "    }\n"
             "}\n"
             "function integer f() {\n"
             "    Expr e = Expr.Mul(Expr.Add(Expr.Num(3), Expr.Num(4)), Expr.Num(5))\n"
             "    return ev(e)\n"
             "}\n"
             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 35);
}

LUMA_TEST(vm_generic_recursive_list) {
    // Generic recursive ADT: sum a List<integer> built with Cons/Nil.
    const auto result =
        eval("choice List<T> {\n"
             "    Nil\n"
             "    Cons(T head, List<T> tail)\n"
             "}\n"
             "function integer sum(List<integer> xs) {\n"
             "    return match xs {\n"
             "        case List.Nil        { 0 }\n"
             "        case List.Cons(h, t) { h + sum(t) }\n"
             "    }\n"
             "}\n"
             "function integer f() {\n"
             "    List<integer> xs = List.Cons(1, List.Cons(2, List.Cons(3, List.Nil)))\n"
             "    return sum(xs)\n"
             "}\n"
             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 6);
}

// ─── Match expression tests ───

LUMA_TEST(vm_match_integer) {
    const auto result = eval("function string describe(integer n) {\n"
                             "    return match n {\n"
                             "        case == 1 { \"one\" }\n"
                             "        case == 2 { \"two\" }\n"
                             "        else { \"other\" }\n"
                             "    }\n"
                             "}\n"
                             "describe(2)");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "two");
}

LUMA_TEST(vm_match_default) {
    const auto result = eval("function string describe(integer n) {\n"
                             "    return match n {\n"
                             "        case == 1 { \"one\" }\n"
                             "        else { \"unknown\" }\n"
                             "    }\n"
                             "}\n"
                             "describe(99)");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "unknown");
}

// ─── Match guard tests (when clauses) ───

LUMA_TEST(vm_match_guard_expression_fallthrough) {
    // Expression-form guard: when the guard is false the arm is skipped and
    // matching continues. Regression test for VM stack corruption on guard
    // fall-through.
    const auto result = eval("function string classify(integer n) {\n"
                             "    return match n {\n"
                             "        case >= 0 when n == 0 { \"zero\" }\n"
                             "        case >= 0 { \"positive\" }\n"
                             "        else { \"negative\" }\n"
                             "    }\n"
                             "}\n"
                             "\"${classify(0)}|${classify(5)}|${classify(-1)}\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "zero|positive|negative");
}

LUMA_TEST(vm_match_guard_binding_fallthrough) {
    // Expression-form guard on a binding pattern: a false guard must fall
    // through to the unguarded `some` arm without corrupting the bound value.
    const auto result = eval("function string check(optional<integer> o) {\n"
                             "    return match o {\n"
                             "        case some(v) when v > 10 { \"big\" }\n"
                             "        case some(v) { \"small\" }\n"
                             "        case none { \"none\" }\n"
                             "    }\n"
                             "}\n"
                             "\"${check(some(20))}|${check(some(5))}|${check(none)}\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "big|small|none");
}

LUMA_TEST(vm_match_guard_statement_fallthrough) {
    // Statement-form guard on non-binding arms: a false guard must skip the
    // arm's body and continue matching.
    const auto result = eval("function string run(integer n) {\n"
                             "    mutable string out = \"unset\"\n"
                             "    match n {\n"
                             "        case >= 0 when n == 0 { out = \"zero\" }\n"
                             "        case >= 0 { out = \"positive\" }\n"
                             "        else { out = \"negative\" }\n"
                             "    }\n"
                             "    return out\n"
                             "}\n"
                             "\"${run(0)}|${run(5)}|${run(-1)}\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "zero|positive|negative");
}

LUMA_TEST(vm_match_guard_statement_binding_fallthrough) {
    // Statement-form guard on a binding pattern with fall-through.
    const auto result = eval("function integer f(optional<integer> o) {\n"
                             "    mutable integer out = -1\n"
                             "    match o {\n"
                             "        case some(v) when v > 10 { out = v * 2 }\n"
                             "        case some(v) { out = v }\n"
                             "        case none { out = 0 }\n"
                             "    }\n"
                             "    return out\n"
                             "}\n"
                             "\"${f(some(20))}|${f(some(5))}|${f(none)}\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "40|5|0");
}

LUMA_TEST(vm_match_not_equal_catch_all) {
    // `!=` acts as an exhaustive catch-all without an else arm.
    const auto result = eval("function string f(integer n) {\n"
                             "    return match n {\n"
                             "        case == 0 { \"zero\" }\n"
                             "        case != 0 { \"nonzero\" }\n"
                             "    }\n"
                             "}\n"
                             "\"${f(0)}|${f(7)}\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "zero|nonzero");
}

LUMA_TEST(vm_match_alternatives_runtime) {
    // Alternative patterns (`a | b`) dispatch to the shared arm.
    const auto result = eval("choice Color { Red  Green  Blue }\n"
                             "function string name(Color c) {\n"
                             "    return match c {\n"
                             "        case Color.Red | Color.Blue { \"extreme\" }\n"
                             "        case Color.Green { \"middle\" }\n"
                             "    }\n"
                             "}\n"
                             "\"${name(Color.Red)}|${name(Color.Green)}|${name(Color.Blue)}\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "extreme|middle|extreme");
}

LUMA_TEST(vm_match_string_runtime) {
    // Matching on string equality.
    const auto result = eval("function string f(string s) {\n"
                             "    return match s {\n"
                             "        case == \"hi\" { \"greeting\" }\n"
                             "        case == \"bye\" { \"farewell\" }\n"
                             "        else { \"other\" }\n"
                             "    }\n"
                             "}\n"
                             "string a = f(\"hi\")\n"
                             "string b = f(\"bye\")\n"
                             "string c = f(\"x\")\n"
                             "\"${a}|${b}|${c}\"");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), "greeting|farewell|other");
}

// ─── Null coalescing test ───

LUMA_TEST(vm_null_coalescing) {
    const auto result = eval("optional<integer> a = none\n"
                             "integer x = a ?? 42\n"
                             "x");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

// ─── Result type tests ───

LUMA_TEST(vm_success_result) {
    const auto result = eval("result<integer> r = success(42)\n"
                             "integer v = match r {\n"
                             "    success(v) { v }\n"
                             "    failure(e) { 0 }\n"
                             "}\n"
                             "v");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_failure_result) {
    const auto result = eval("result<integer> r = failure(\"oops\")\n"
                             "integer val = match r {\n"
                             "    success(s) { s }\n"
                             "    failure(e) { -1 }\n"
                             "}\n"
                             "val");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), -1);
}

// ─── Optional type tests ───

LUMA_TEST(vm_optional_some) {
    const auto result = eval("optional<integer> o = some(99)\n"
                             "integer v = match o {\n"
                             "    case some(v) { v }\n"
                             "    case none { 0 }\n"
                             "}\n"
                             "v");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 99);
}

LUMA_TEST(vm_optional_none) {
    const auto result = eval("optional<integer> o = none\n"
                             "integer v = match o {\n"
                             "    case some(v) { v }\n"
                             "    case none { -1 }\n"
                             "}\n"
                             "v");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), -1);
}

// ─── Error pipe operator tests ───

LUMA_TEST(vm_error_pipe_success) {
    const auto result =
        eval("function result<integer> parse(string s) { return success(21) }\n"
             "function result<integer> double_it(integer x) { return success(x * 2) }\n"
             "function result<integer> pipeline() { return parse(\"21\") !> double_it() }\n"
             "result<integer> r = pipeline()\n"
             "integer v = match r {\n"
             "    success(s) { s }\n"
             "    failure(e) { 0 }\n"
             "}\n"
             "v");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_error_pipe_short_circuits) {
    const auto result =
        eval("function result<integer> fail_parse(string s) { return failure(\"bad\") }\n"
             "function result<integer> double_it(integer x) { return success(x * 2) }\n"
             "function result<integer> pipeline() { return fail_parse(\"x\") !> double_it() }\n"
             "result<integer> r = pipeline()\n"
             "boolean is_fail = match r {\n"
             "    success(s) { false }\n"
             "    failure(e) { true }\n"
             "}\n"
             "is_fail");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_error_pipe_chain_three_stages) {
    // A success threads through every stage of a longer error-pipe chain.
    const auto result = eval("function result<integer> parse(string s) { return success(2) }\n"
                             "function result<integer> inc(integer x) { return success(x + 1) }\n"
                             "function integer triple(integer x) { return x * 3 }\n"
                             "function result<integer> pipeline() {\n"
                             "    return parse(\"2\") !> inc() !> triple()\n"
                             "}\n"
                             "result<integer> r = pipeline()\n"
                             "integer v = match r {\n"
                             "    success(s) { s }\n"
                             "    failure(e) { 0 }\n"
                             "}\n"
                             "v");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 9);
}

// ─── Optional chaining tests ───

LUMA_TEST(vm_optional_chaining_field) {
    const auto result = eval("record Point { integer x, integer y }\n"
                             "function integer f() {\n"
                             "    optional<Point> p = some(Point { x = 42, y = 10 })\n"
                             "    optional<integer> v = p?.x\n"
                             "    return v ?? 0\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 42);
}

LUMA_TEST(vm_optional_chaining_none) {
    const auto result = eval("record Point { integer x, integer y }\n"
                             "function integer f() {\n"
                             "    optional<Point> p = none\n"
                             "    optional<integer> v = p?.x\n"
                             "    return v ?? -1\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), -1);
}

LUMA_TEST(vm_optional_index) {
    const auto result = eval("function integer f() {\n"
                             "    optional<array<integer>> a = some([10, 20, 30])\n"
                             "    optional<integer> v = a?[1]\n"
                             "    return v ?? 0\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 20);
}

LUMA_TEST(vm_optional_index_none) {
    const auto result = eval("function integer f() {\n"
                             "    optional<array<integer>> a = none\n"
                             "    optional<integer> v = a?[0]\n"
                             "    return v ?? -1\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), -1);
}

LUMA_TEST(vm_optional_propagation_some) {
    // The '?' operator unwraps a some value and execution continues.
    const auto result = eval("function optional<integer> add_one(optional<integer> x) {\n"
                             "    integer v = x?\n"
                             "    return some(v + 1)\n"
                             "}\n"
                             "function integer f() {\n"
                             "    optional<integer> r = add_one(some(5))\n"
                             "    return r ?? -1\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 6);
}

LUMA_TEST(vm_optional_propagation_none) {
    // The '?' operator short-circuits, returning none from the enclosing function.
    const auto result = eval("function optional<integer> add_one(optional<integer> x) {\n"
                             "    integer v = x?\n"
                             "    return some(v + 1)\n"
                             "}\n"
                             "function integer f() {\n"
                             "    optional<integer> r = add_one(none)\n"
                             "    return r ?? -1\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), -1);
}

// ─── Record with tests ───

LUMA_TEST(vm_record_with) {
    const auto result = eval("record Point { integer x, integer y }\n"
                             "function integer f() {\n"
                             "    Point p1 = Point { x = 1, y = 2 }\n"
                             "    Point p2 = p1 with { x = 10 }\n"
                             "    return p2.x * 100 + p2.y\n"
                             "}\n"
                             "f()");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 1002); // x=10, y=2
}

// ─── Downcast and is-type tests ───

LUMA_TEST(vm_downcast_success) {
    const auto result = eval("function result<string> f(integer x) { return downcast<string>(x) }\n"
                             "result<string> r = f(42)\n"
                             "boolean ok = match r {\n"
                             "    success(s) { true }\n"
                             "    failure(e) { false }\n"
                             "}\n"
                             "# downcast from integer to string should fail\n"
                             "!ok");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

LUMA_TEST(vm_is_type) {
    const auto result = eval("is<integer>(42)");

    ASSERT_TRUE(result.is_bool());
    ASSERT_TRUE(result.as_bool());
}

// ─── Null coalescing with some value ───

LUMA_TEST(vm_null_coalescing_some) {
    const auto result = eval("optional<integer> a = some(99)\n"
                             "integer x = a ?? 0\n"
                             "x");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 99);
}

// ─── Main ───

int main() {
    LUMA_RUN_ALL();
}
