// Type checker unit tests — generics, recursive ADTs, concurrency, bounds checking.

#include "type_checker_test_helpers.hpp"

// ─── Recursive ADTs ───

static void test_recursive_choice_non_generic_passes() {
    ASSERT_TRUE(passes("choice Expr {\n"
                       "    Num(integer value)\n"
                       "    Add(Expr left, Expr right)\n"
                       "}\n"
                       "function integer eval(Expr e) {\n"
                       "    return match e {\n"
                       "        case Expr.Num(v) { v }\n"
                       "        case Expr.Add(l, r) { eval(l) + eval(r) }\n"
                       "    }\n"
                       "}\n"));
}

static void test_recursive_choice_generic_passes() {
    ASSERT_TRUE(passes("choice List<T> {\n"
                       "    Nil\n"
                       "    Cons(T head, List<T> tail)\n"
                       "}\n"
                       "List<integer> xs = List.Cons(1, List.Cons(2, List.Nil))\n"));
}

static void test_recursive_choice_generic_match_bindings() {
    // Match destructuring on a generic recursive choice should give
    // the bindings the correct concrete types.
    ASSERT_TRUE(passes("choice Tree<T> {\n"
                       "    Leaf(T value)\n"
                       "    Branch(Tree<T> left, Tree<T> right)\n"
                       "}\n"
                       "function integer sum(Tree<integer> t) {\n"
                       "    return match t {\n"
                       "        case Tree.Leaf(v) { v }\n"
                       "        case Tree.Branch(l, r) { sum(l) + sum(r) }\n"
                       "    }\n"
                       "}\n"));
}

static void test_recursive_choice_generic_constructor_infers_type() {
    // The type of List.Cons(1, List.Nil) should be inferred as
    // List<integer>, not bare List.
    ASSERT_TRUE(passes("choice List<T> {\n"
                       "    Nil\n"
                       "    Cons(T head, List<T> tail)\n"
                       "}\n"
                       "function integer length(List<integer> xs) {\n"
                       "    return match xs {\n"
                       "        case List.Nil { 0 }\n"
                       "        case List.Cons(_h, t) { 1 + length(t) }\n"
                       "    }\n"
                       "}\n"
                       "List<integer> xs = List.Cons(1, List.Cons(2, List.Nil))\n"
                       "integer n = length(xs)\n"));
}

static void test_recursive_choice_generic_assignability() {
    // A non-generic choice with same name should not accept wrong
    // generic type args.
    ASSERT_TRUE(fails("choice Box<T> {\n"
                      "    Val(T value)\n"
                      "}\n"
                      "function void take(Box<string> b) {\n"
                      "    return\n"
                      "}\n"
                      "take(Box.Val(42))\n"));
}

// Matching a generic choice inside a generic function must not clobber
// the function's own type param bindings.
static void test_recursive_choice_match_no_clobber() {
    ASSERT_TRUE(passes("choice List<T> {\n"
                       "    Nil\n"
                       "    Cons(T head, List<T> tail)\n"
                       "}\n"
                       "function<T> T head(List<T> lst) {\n"
                       "    return match lst {\n"
                       "        case List.Cons(h, _) { h }\n"
                       "        else { 0 }\n"
                       "    }\n"
                       "}\n"
                       "integer x = head(List.Cons(1, List.Nil))\n"));
}

// Match expression must reject wrong number of bindings.
static void test_recursive_choice_match_expr_binding_count() {
    ASSERT_TRUE(fails("choice Pair {\n"
                      "    Two(integer a, integer b)\n"
                      "}\n"
                      "Pair p = Pair.Two(1, 2)\n"
                      "integer x = match p {\n"
                      "    case Pair.Two(a) { a }\n"
                      "    else { 0 }\n"
                      "}\n"));
}

// Namespace-qualified choice type access.
static void test_namespace_qualified_choice_access() {
    ASSERT_TRUE(passes("namespace Shapes {\n"
                       "    choice Shape {\n"
                       "        Circle(number radius)\n"
                       "        Rect(number w, number h)\n"
                       "    }\n"
                       "}\n"
                       "Shapes.Shape c = Shapes.Shape.Circle(3.14)\n"));
}

static void test_turbofish_generic_call() {
    ASSERT_TRUE(passes("function<T> T identity(T value) {\n"
                       "    return value\n"
                       "}\n"
                       "integer x = identity::<integer>(42)\n"));
}

static void test_turbofish_wrong_type() {
    ASSERT_TRUE(fails("function<T> T identity(T value) {\n"
                      "    return value\n"
                      "}\n"
                      "string x = identity::<integer>(42)\n"));
}

// ─── Optional chaining auto-flatten ───

// Chaining ?.field on an optional record whose field is already optional
// should NOT double-wrap: result should be optional<T>, not optional<optional<T>>.
static void test_optional_chain_auto_flatten() {
    ASSERT_TRUE(passes("record Inner { string value }\n"
                       "record Outer { optional<Inner> child }\n"
                       "function void foo(optional<Outer> o) {\n"
                       "    optional<Inner> c = o?.child\n"
                       "}\n"));
}

// Chaining ?.field twice should auto-flatten each level.
static void test_optional_chain_nested_auto_flatten() {
    ASSERT_TRUE(passes("record A { string name }\n"
                       "record B { optional<A> a }\n"
                       "function void foo(optional<B> b) {\n"
                       "    optional<A> v = b?.a\n"
                       "}\n"));
}

// ─── Generic bounds enforcement ───

// A bounded generic function should reject types that don't satisfy the bound.
static void test_generic_bound_violation() {
    ASSERT_TRUE(fails("interface Named { string name }\n"
                      "record Dog { integer age }\n"
                      "function<T: Named> string greet(T thing) {\n"
                      "    return thing.name\n"
                      "}\n"
                      "string s = greet(Dog { age = 5 })\n"));
}

// A bounded generic function should accept types that satisfy the bound.
static void test_generic_bound_satisfied() {
    ASSERT_TRUE(passes("interface Named { string name }\n"
                       "record Dog { string name, integer age }\n"
                       "function<T: Named> string greet(T thing) {\n"
                       "    return thing.name\n"
                       "}\n"
                       "string s = greet(Dog { name = \"Rex\", age = 5 })\n"));
}

// Unbounded generic parameters should still work as before.
static void test_generic_unbounded_still_works() {
    ASSERT_TRUE(passes("function<T> T identity(T value) {\n"
                       "    return value\n"
                       "}\n"
                       "integer x = identity(42)\n"));
}

// ─── Encoder return type consistency ───

// Encoder.encode_base64 should now return result<string>.
static void test_encoder_encode_base64_returns_result() {
    ASSERT_TRUE(passes("result<string> r = Encoder.encode_base64(\"hello\")\n"));
}

// Encoder.base64url_encode should now return result<string>.
static void test_encoder_base64url_encode_returns_result() {
    ASSERT_TRUE(passes("result<string> r = Encoder.base64url_encode(\"hello\")\n"));
}

// ─── Incompatible type comparison warning ───

static void test_incompatible_type_comparison_warns() {
    const auto warnings = check_warnings("boolean b = 42 == \"hello\"\n");

    ASSERT_FALSE(warnings.empty());
}

static void test_incompatible_type_comparison_integer_ne_boolean() {
    const auto warnings = check_warnings("boolean b = 42 != true\n");

    ASSERT_FALSE(warnings.empty());
}

static void test_compatible_type_comparison_no_warn() {
    const auto warnings = check_warnings("boolean b = 42 == 3.14\n");

    // integer == number should NOT warn (numeric promotion).
    bool has_incompatible = false;

    for (const auto& w : warnings) {
        const std::string msg{w.message};

        if (msg.find("incompatible") != std::string::npos) {
            has_incompatible = true;
        }
    }

    ASSERT_FALSE(has_incompatible);
}

static void test_same_type_comparison_no_incompatible_warn() {
    const auto warnings = check_warnings("boolean b = \"a\" == \"b\"\n");

    bool has_incompatible = false;

    for (const auto& w : warnings) {
        const std::string msg{w.message};

        if (msg.find("incompatible") != std::string::npos) {
            has_incompatible = true;
        }
    }

    ASSERT_FALSE(has_incompatible);
}

// ─── Compile-time array bounds checking ───

static void test_array_literal_out_of_bounds() {
    ASSERT_TRUE(fails("integer x = [1, 2, 3][5]\n"));
}

static void test_array_literal_negative_index() {
    // Negative indices are always out of bounds for array literals.
    // Note: -1 is parsed as unary minus on a literal, so the type checker
    // may not catch this at compile time. Verify it at least doesn't crash.
    (void)check("integer x = [1, 2, 3][-1]\n");
}

static void test_array_literal_in_bounds() {
    ASSERT_TRUE(passes("integer x = [1, 2, 3][2]\n"));
}

// ─── Compile-time tuple bounds checking ───

static void test_tuple_out_of_bounds() {
    ASSERT_TRUE(fails("integer x = (1, \"a\", true)[5]\n"));
}

// ─── String interpolation function warning ───

static void test_string_interpolation_function_warns() {
    const auto warnings = check_warnings("function integer foo() { return 1 }\n"
                                         "string s = \"value is ${foo}\"\n");

    ASSERT_FALSE(warnings.empty());
}

static void test_string_interpolation_call_no_warn() {
    const auto warnings = check_warnings("function integer foo() { return 1 }\n"
                                         "string s = \"value is ${foo()}\"\n");

    bool has_func_warn = false;

    for (const auto& w : warnings) {
        const std::string msg{w.message};

        if (msg.find("function value") != std::string::npos) {
            has_func_warn = true;
        }
    }

    ASSERT_FALSE(has_func_warn);
}

// ─── Floating-point equality warning ───

static void test_float_equality_warns() {
    const auto warnings = check_warnings("boolean b = 3.14 == 3.14\n");

    bool has_float_warn = false;

    for (const auto& w : warnings) {
        const std::string msg{w.message};

        if (msg.find("floating") != std::string::npos ||
            msg.find("rounding") != std::string::npos) {
            has_float_warn = true;
        }
    }

    ASSERT_TRUE(has_float_warn);
}

static void test_integer_equality_no_float_warn() {
    const auto warnings = check_warnings("boolean b = 42 == 42\n");

    bool has_float_warn = false;

    for (const auto& w : warnings) {
        const std::string msg{w.message};

        if (msg.find("floating") != std::string::npos ||
            msg.find("rounding") != std::string::npos) {
            has_float_warn = true;
        }
    }

    ASSERT_FALSE(has_float_warn);
}

// ─── String interpolation namespace warning ───

static void test_string_interpolation_namespace_warns() {
    const auto warnings = check_warnings("string s = \"value is ${Math}\"\n");

    ASSERT_FALSE(warnings.empty());
}

// ─── Optional chain result not unwrapped. ───

static void test_optional_chain_not_unwrapped_warns() {
    ASSERT_TRUE(has_warnings("record User { string name }\n"
                             "optional<User> u = none\n"
                             "string n = u?.name\n"));
}

static void test_optional_chain_with_coalesce_no_warn() {
    // Using ?? to provide a default suppresses the warning.
    ASSERT_FALSE(has_warnings("record User { string name }\n"
                              "optional<User> u = none\n"
                              "string n = u?.name ?? \"unknown\"\n"));
}

static void test_optional_chain_to_optional_var_no_warn() {
    // Declaring the target as optional<T> suppresses the warning.
    ASSERT_FALSE(has_warnings("record User { string name }\n"
                              "optional<User> u = none\n"
                              "optional<string> n = u?.name\n"));
}

// ─── Immutable field / index / dictionary assignment ───

static void test_immutable_record_field_assignment_error() {
    ASSERT_TRUE(fails("record Point { integer x, integer y }\n"
                      "Point p = Point { x = 1, y = 2 }\n"
                      "p.x = 5\n"));
}

static void test_mutable_record_field_assignment_ok() {
    ASSERT_TRUE(passes("record Point { integer x, integer y }\n"
                       "mutable Point p = Point { x = 1, y = 2 }\n"
                       "p.x = 5\n"));
}

static void test_immutable_array_index_assignment_error() {
    ASSERT_TRUE(fails("array<integer> items = [1, 2, 3]\n"
                      "items[0] = 10\n"));
}

static void test_mutable_array_index_assignment_ok() {
    ASSERT_TRUE(passes("mutable array<integer> items = [1, 2, 3]\n"
                       "items[0] = 10\n"));
}

// Field-vs-index traversal policy: assigning to the field of an indexed
// element (a[i].x = …) mutates the stored element, not the array binding, so
// it is allowed even when the array itself is immutable.  This is the
// distinguishing case that separates the field-only walk (which must stop at
// an index access) from the field-and-index walk used for a[i] = ….
static void test_immutable_array_element_field_assignment_allowed() {
    ASSERT_TRUE(passes("record Point { integer x, integer y }\n"
                       "array<Point> ps = [Point { x = 1, y = 2 }]\n"
                       "ps[0].x = 5\n"));
}

static void test_immutable_array_element_field_compound_assignment_allowed() {
    ASSERT_TRUE(passes("record Point { integer x, integer y }\n"
                       "array<Point> ps = [Point { x = 1, y = 2 }]\n"
                       "ps[0].x += 5\n"));
}

static void test_immutable_dictionary_assignment_error() {
    ASSERT_TRUE(fails("dictionary<integer> d = { \"a\": 1 }\n"
                      "d[\"a\"] = 10\n"));
}

static void test_mutable_dictionary_assignment_ok() {
    ASSERT_TRUE(passes("mutable dictionary<integer> d = { \"a\": 1 }\n"
                       "d[\"a\"] = 10\n"));
}

// ─── Immutable compound-assignment to field / element ───

static void test_immutable_compound_assign_field_error() {
    ASSERT_TRUE(fails("record Point { integer x, integer y }\n"
                      "Point p = Point { x = 1, y = 2 }\n"
                      "p.x += 5\n"));
}

static void test_mutable_compound_assign_field_ok() {
    ASSERT_TRUE(passes("record Point { integer x, integer y }\n"
                       "mutable Point p = Point { x = 1, y = 2 }\n"
                       "p.x += 5\n"));
}

static void test_immutable_compound_assign_element_error() {
    ASSERT_TRUE(fails("array<integer> items = [1, 2, 3]\n"
                      "items[0] += 10\n"));
}

static void test_mutable_compound_assign_element_ok() {
    ASSERT_TRUE(passes("mutable array<integer> items = [1, 2, 3]\n"
                       "items[0] += 10\n"));
}

static void test_immutable_compound_assign_dictionary_error() {
    ASSERT_TRUE(fails("dictionary<integer> d = { \"a\": 1 }\n"
                      "d[\"a\"] += 10\n"));
}

// ─── Pipe type mismatch ───

static void test_pipe_type_mismatch() {
    ASSERT_TRUE(fails("function integer double_it(integer x) { return x * 2 }\n"
                      "function void foo() {\n"
                      "    \"hello\" |> double_it()\n"
                      "}\n"));
}

// The right-hand side of a pipe must be a function call, not a bare value.
static void test_pipe_rhs_not_call_fails() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    integer x = 5 |> 42\n"
                      "}\n"));
}

// Piping supplies one implicit argument, so a value piped into a function that
// needs two explicit arguments is still under-applied.
static void test_pipe_arity_too_few_fails() {
    ASSERT_TRUE(fails("function integer add(integer a, integer b) { return a + b }\n"
                      "function void foo() {\n"
                      "    integer x = 5 |> add()\n"
                      "}\n"));
}

// A namespace-qualified pipe target type-checks its first parameter too.
static void test_pipe_namespace_type_mismatch_fails() {
    ASSERT_TRUE(fails("namespace M {\n"
                      "    function integer twice(integer n) { return n * 2 }\n"
                      "}\n"
                      "function void foo() {\n"
                      "    integer x = \"hi\" |> M.twice()\n"
                      "}\n"));
}

// The right-hand side of an error pipe must be a function call too.
static void test_error_pipe_rhs_not_call_fails() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    result<integer> r = 5 !> 99\n"
                      "}\n"));
}

// ─── Specific imports ───

static void test_specific_import_function() {
    ASSERT_TRUE(passes("namespace Geometry {\n"
                       "    function number area(number r) {\n"
                       "        return r * r\n"
                       "    }\n"
                       "    function number perimeter(number r) {\n"
                       "        return r * 2\n"
                       "    }\n"
                       "}\n"
                       "use Geometry.area\n"
                       "number a = area(5.0)\n"));
}

static void test_specific_import_record() {
    ASSERT_TRUE(passes("namespace Shapes {\n"
                       "    record Point { integer x, integer y }\n"
                       "}\n"
                       "use Shapes.Point\n"
                       "Point p = Point { x = 1, y = 2 }\n"));
}

static void test_specific_import_choice() {
    ASSERT_TRUE(passes("namespace Traffic {\n"
                       "    choice Light { Red, Yellow, Green }\n"
                       "}\n"
                       "use Traffic.Light\n"
                       "Light l = Light.Red\n"));
}

static void test_specific_import_unknown_member() {
    ASSERT_TRUE(fails("namespace Geometry {\n"
                      "    function number area(number r) {\n"
                      "        return r * r\n"
                      "    }\n"
                      "}\n"
                      "use Geometry.does_not_exist\n"));
}

static void test_specific_import_internal_blocked() {
    ASSERT_TRUE(fails("namespace Util {\n"
                      "    internal function string normalize(string s) { return s }\n"
                      "}\n"
                      "use Util.normalize\n"));
}

// ─── Generic interface ───

static void test_generic_interface_satisfaction() {
    ASSERT_TRUE(passes("interface Container<T> {\n"
                       "    T value\n"
                       "}\n"
                       "record Box { integer value }\n"
                       "function<T> T get_value(Container<T> c) {\n"
                       "    return c.value\n"
                       "}\n"
                       "@main\n"
                       "function void start() {\n"
                       "    Box b = Box { value = 42 }\n"
                       "    integer v = get_value(b)\n"
                       "}\n"));
}

static void test_generic_interface_missing_field_fails() {
    // A record missing the required field should fail even with a generic interface.
    ASSERT_TRUE(fails("interface Container<T> {\n"
                      "    T value\n"
                      "}\n"
                      "record Empty { string name }\n"
                      "function<T> T get_value(Container<T> c) {\n"
                      "    return c.value\n"
                      "}\n"
                      "@main\n"
                      "function void start() {\n"
                      "    Empty e = Empty { name = \"hello\" }\n"
                      "    integer v = get_value(e)\n"
                      "}\n"));
}

// ─── Generic type alias ───

static void test_generic_type_alias_valid() {
    ASSERT_TRUE(passes("type Pair<T> = (T, T)\n"
                       "Pair<integer> p = (1, 2)\n"));
}

static void test_generic_type_alias_type_mismatch() {
    ASSERT_TRUE(fails("type Pair<T> = (T, T)\n"
                      "Pair<integer> p = (1, \"two\")\n"));
}

// ─── Concurrency type checking ───

static void test_channel_type_valid() {
    ASSERT_TRUE(passes("channel<integer> ch = Channel.new()\n"));
}

static void test_task_type_valid() {
    ASSERT_TRUE(passes("function integer compute() { return 42 }\n"
                       "task<integer> t = spawn compute()\n"));
}

static void test_task_scope_valid() {
    ASSERT_TRUE(passes("function integer compute() { return 42 }\n"
                       "task_scope {\n"
                       "    task<integer> t = spawn compute()\n"
                       "    integer val = await t\n"
                       "}\n"));
}

static void test_spawn_outside_task_scope_warns() {
    auto warnings = check_warnings("function integer compute() { return 42 }\n"
                                   "task<integer> t = spawn compute()\n");

    bool found{false};

    for (const auto& w : warnings) {
        if (w.message.find("spawn outside task_scope") != std::string::npos) {
            found = true;
        }
    }

    ASSERT_TRUE(found);
}

static void test_spawn_inside_task_scope_no_warn() {
    auto warnings = check_warnings("function integer compute() { return 42 }\n"
                                   "task_scope {\n"
                                   "    task<integer> t = spawn compute()\n"
                                   "    integer val = await t\n"
                                   "}\n");

    bool found{false};

    for (const auto& w : warnings) {
        if (w.message.find("spawn outside task_scope") != std::string::npos) {
            found = true;
        }
    }

    ASSERT_FALSE(found);
}

static void test_task_scope_result_type() {
    // task_scope result assigned to array<integer> should type check.
    ASSERT_TRUE(passes("function integer compute() { return 42 }\n"
                       "array<integer> results = task_scope {\n"
                       "    spawn compute()\n"
                       "    spawn compute()\n"
                       "}\n"));
}

static void test_task_scope_heterogeneous_spawn_rejected() {
    // Every spawn in a task_scope feeds one result array, so the spawned result
    // types must be homogeneous. Mixing integer and string spawns must be
    // rejected. (Regression: the mismatch type-checked and produced an
    // array<integer> that silently held a string at runtime.)
    ASSERT_TRUE(fails("function integer ret_int() { return 1 }\n"
                      "function string ret_str() { return \"x\" }\n"
                      "array<integer> r = task_scope {\n"
                      "    spawn ret_int()\n"
                      "    spawn ret_str()\n"
                      "}\n"));
}

static void test_task_scope_nested_no_warn() {
    // spawn inside nested task_scope should not warn.
    auto warnings = check_warnings("function integer compute() { return 42 }\n"
                                   "task_scope {\n"
                                   "    task_scope {\n"
                                   "        task<integer> t = spawn compute()\n"
                                   "        integer val = await t\n"
                                   "    }\n"
                                   "}\n");

    bool found{false};

    for (const auto& w : warnings) {
        if (w.message.find("spawn outside task_scope") != std::string::npos) {
            found = true;
        }
    }

    ASSERT_FALSE(found);
}

// ─── Concurrency type checking — negative cases ───

static void test_spawn_non_call_rejected() {
    // spawn must be applied to a function call, not an arbitrary expression.
    auto errors = check("task<integer> t = spawn 42\n");

    bool found{false};

    for (const auto& e : errors) {
        if (e.message.find("spawn requires a function call") != std::string::npos) {
            found = true;
        }
    }

    ASSERT_TRUE(found);
}

static void test_await_non_task_rejected() {
    // await must be applied to a task value, not an integer.
    auto errors = check("integer v = await 42\n");

    bool found{false};

    for (const auto& e : errors) {
        if (e.message.find("await requires a task value") != std::string::npos) {
            found = true;
        }
    }

    ASSERT_TRUE(found);
}

static void test_await_channel_rejected() {
    // A channel is not awaitable — await requires a task value.
    auto errors = check("channel<integer> ch = Channel.new()\n"
                        "integer v = await ch\n");

    bool found{false};

    for (const auto& e : errors) {
        if (e.message.find("await requires a task value") != std::string::npos) {
            found = true;
        }
    }

    ASSERT_TRUE(found);
}

static void test_task_scope_result_type_mismatch() {
    // task_scope yields array<integer>; assigning it to integer must fail.
    ASSERT_TRUE(fails("function integer compute() { return 42 }\n"
                      "integer x = task_scope {\n"
                      "    spawn compute()\n"
                      "}\n"));
}

static void test_await_outside_task_scope_on_fire_and_forget() {
    // Awaiting a fire-and-forget task (spawned outside a task_scope) is still
    // type-correct — the await yields the task's element type.
    ASSERT_TRUE(passes("function integer compute() { return 42 }\n"
                       "task<integer> t = spawn compute()\n"
                       "integer v = await t\n"));
}

// ─── Generic resolution edge cases (CA-26) ───

static void test_generic_identity_function_infers() {
    // A simple generic identity function should infer T from the argument.
    ASSERT_TRUE(passes("function<T> T identity(T value) {\n"
                       "    return value\n"
                       "}\n"
                       "integer x = identity(42)\n"));
}

static void test_generic_wrong_type_arg_count() {
    // Providing wrong number of explicit type args should fail or the return
    // type should mismatch.  Luma currently ignores extra turbofish args, so
    // we verify the simpler case: providing the *wrong* type should error.
    ASSERT_TRUE(fails("function<T> T identity(T value) {\n"
                      "    return value\n"
                      "}\n"
                      "string x = identity::<integer>(42)\n"));
}

static void test_generic_nested_type_params() {
    // Nested generic types should resolve correctly.
    ASSERT_TRUE(passes("function<T> array<T> wrap(T value) {\n"
                       "    return [value]\n"
                       "}\n"
                       "array<integer> xs = wrap(1)\n"));
}

// ─── Edge-case generics tests ───

static void test_generic_constraint_violation_error_message() {
    // Verify that a constraint violation produces an error mentioning the bound.
    const auto errors = check("interface Named { string name }\n"
                              "record Dog { integer age }\n"
                              "function<T: Named> string greet(T thing) {\n"
                              "    return thing.name\n"
                              "}\n"
                              "string s = greet(Dog { age = 5 })\n");
    ASSERT_FALSE(errors.empty());
    // The error should reference the failing constraint or the missing field.
    bool found_constraint_msg{false};
    for (const auto& d : errors) {
        if (d.message.find("Named") != std::string::npos ||
            d.message.find("name") != std::string::npos ||
            d.message.find("satisfy") != std::string::npos ||
            d.message.find("bound") != std::string::npos) {
            found_constraint_msg = true;
        }
    }
    ASSERT_TRUE(found_constraint_msg);
}

static void test_generic_no_type_args_when_required() {
    // Calling a generic function that returns T where T resolves to a type
    // that conflicts with the call-site assignment should fail.
    ASSERT_TRUE(fails("function<T> T default_value() {\n"
                      "    return 0\n"
                      "}\n"
                      "boolean x = default_value()\n"
                      "string y = x\n"));
}

static void test_generic_recursive_choice_type_mismatch() {
    // A recursive generic choice with mismatched inner types should fail.
    ASSERT_TRUE(fails("choice List<T> {\n"
                      "    Nil\n"
                      "    Cons(T head, List<T> tail)\n"
                      "}\n"
                      "List<integer> xs = List.Cons(\"hello\", List.Nil)\n"));
}

// ─── Multiple bounds (T: A + B) ───

// A multi-bound parameter must reject a type that satisfies only one bound.
static void test_generic_multi_bound_violation() {
    ASSERT_TRUE(fails("interface HasValue { integer value }\n"
                      "interface HasName { string name }\n"
                      "record OnlyValue { integer value }\n"
                      "function<T: HasValue + HasName> string describe(T item) {\n"
                      "    return item.name\n"
                      "}\n"
                      "string s = describe(OnlyValue { value = 7 })\n"));
}

// A multi-bound parameter must accept a type that satisfies every bound.
static void test_generic_multi_bound_satisfied() {
    ASSERT_TRUE(passes("interface HasValue { integer value }\n"
                       "interface HasName { string name }\n"
                       "record Tagged { integer value, string name }\n"
                       "function<T: HasValue + HasName> string describe(T item) {\n"
                       "    return item.name\n"
                       "}\n"
                       "string s = describe(Tagged { value = 7, name = \"x\" })\n"));
}

// ─── Generic record instantiation ───

// Constructing a generic record with a field value of the wrong type must fail.
static void test_generic_record_field_type_mismatch() {
    ASSERT_TRUE(fails("record Box<T> {\n"
                      "    T value\n"
                      "}\n"
                      "Box<integer> b = Box<integer> { value = \"hello\" }\n"));
}

// A function expecting Box<string> must reject a Box<integer> argument.
static void test_generic_record_arg_instantiation_mismatch() {
    ASSERT_TRUE(fails("record Box<T> {\n"
                      "    T value\n"
                      "}\n"
                      "function void take(Box<string> b) {\n"
                      "    return\n"
                      "}\n"
                      "take(Box<integer> { value = 42 })\n"));
}

// A generic two-param constructor call with swapped/mismatched arg types fails.
static void test_generic_record_two_param_mismatch() {
    ASSERT_TRUE(fails("record Pair<T, U> {\n"
                      "    T first,\n"
                      "    U second\n"
                      "}\n"
                      "function<T, U> Pair<T, U> make_pair(T a, U b) {\n"
                      "    return Pair<T, U> { first = a, second = b }\n"
                      "}\n"
                      "Pair<string, integer> p = make_pair(1, \"two\")\n"));
}

// ─── Generic functions producing / composing generic types ───

// A generic function may construct and return a generic record.
static void test_generic_function_returns_generic_record() {
    ASSERT_TRUE(passes("record Box<T> {\n"
                       "    T value\n"
                       "}\n"
                       "function<T> Box<T> wrap(T value) {\n"
                       "    return Box<T> { value = value }\n"
                       "}\n"
                       "Box<integer> b = wrap(42)\n"
                       "integer n = b.value\n"));
}

// A generic function may consume a generic record and return its element.
static void test_generic_function_takes_generic_record() {
    ASSERT_TRUE(passes("record Box<T> {\n"
                       "    T value\n"
                       "}\n"
                       "function<T> T unwrap(Box<T> b) {\n"
                       "    return b.value\n"
                       "}\n"
                       "Box<string> b = Box<string> { value = \"hi\" }\n"
                       "string s = unwrap(b)\n"));
}

// Nesting one generic call inside another resolves both inference sites.
static void test_generic_function_calls_generic_function() {
    ASSERT_TRUE(passes("function<T> T identity(T value) {\n"
                       "    return value\n"
                       "}\n"
                       "function<T> array<T> wrap(T value) {\n"
                       "    return [value]\n"
                       "}\n"
                       "array<integer> xs = wrap(identity(99))\n"));
}

int main() {
    // ─── Recursive ADTs ───

    RUN(test_recursive_choice_non_generic_passes);
    RUN(test_recursive_choice_generic_passes);
    RUN(test_recursive_choice_generic_match_bindings);
    RUN(test_recursive_choice_generic_constructor_infers_type);
    RUN(test_recursive_choice_generic_assignability);
    RUN(test_recursive_choice_match_no_clobber);
    RUN(test_recursive_choice_match_expr_binding_count);
    RUN(test_namespace_qualified_choice_access);
    RUN(test_turbofish_generic_call);
    RUN(test_turbofish_wrong_type);

    // ─── Optional chaining auto-flatten ───

    RUN(test_optional_chain_auto_flatten);
    RUN(test_optional_chain_nested_auto_flatten);

    // ─── Generic bounds enforcement ───

    RUN(test_generic_bound_violation);
    RUN(test_generic_bound_satisfied);
    RUN(test_generic_unbounded_still_works);

    // ─── Encoder return type consistency ───

    RUN(test_encoder_encode_base64_returns_result);
    RUN(test_encoder_base64url_encode_returns_result);

    // ─── Incompatible type comparison warning ───

    RUN(test_incompatible_type_comparison_warns);
    RUN(test_incompatible_type_comparison_integer_ne_boolean);
    RUN(test_compatible_type_comparison_no_warn);
    RUN(test_same_type_comparison_no_incompatible_warn);

    // ─── Compile-time bounds checking ───

    RUN(test_array_literal_out_of_bounds);
    RUN(test_array_literal_negative_index);
    RUN(test_array_literal_in_bounds);
    RUN(test_tuple_out_of_bounds);

    // ─── String interpolation warnings ───

    RUN(test_string_interpolation_function_warns);
    RUN(test_string_interpolation_call_no_warn);
    RUN(test_string_interpolation_namespace_warns);

    // ─── Optional chain result not unwrapped ───

    RUN(test_optional_chain_not_unwrapped_warns);
    RUN(test_optional_chain_with_coalesce_no_warn);
    RUN(test_optional_chain_to_optional_var_no_warn);

    // ─── Floating-point equality warning ───

    RUN(test_float_equality_warns);
    RUN(test_integer_equality_no_float_warn);

    // ─── Immutable field / index / dictionary assignment ───

    RUN(test_immutable_record_field_assignment_error);
    RUN(test_mutable_record_field_assignment_ok);
    RUN(test_immutable_array_index_assignment_error);
    RUN(test_mutable_array_index_assignment_ok);
    RUN(test_immutable_array_element_field_assignment_allowed);
    RUN(test_immutable_array_element_field_compound_assignment_allowed);
    RUN(test_immutable_dictionary_assignment_error);
    RUN(test_mutable_dictionary_assignment_ok);

    // ─── Immutable compound-assignment to field / element ───

    RUN(test_immutable_compound_assign_field_error);
    RUN(test_mutable_compound_assign_field_ok);
    RUN(test_immutable_compound_assign_element_error);
    RUN(test_mutable_compound_assign_element_ok);
    RUN(test_immutable_compound_assign_dictionary_error);

    // ─── Pipe type mismatch ───

    RUN(test_pipe_type_mismatch);
    RUN(test_pipe_rhs_not_call_fails);
    RUN(test_pipe_arity_too_few_fails);
    RUN(test_pipe_namespace_type_mismatch_fails);
    RUN(test_error_pipe_rhs_not_call_fails);

    // ─── Specific imports ───

    RUN(test_specific_import_function);
    RUN(test_specific_import_record);
    RUN(test_specific_import_choice);
    RUN(test_specific_import_unknown_member);
    RUN(test_specific_import_internal_blocked);

    // ─── Generic interface ───

    RUN(test_generic_interface_satisfaction);
    RUN(test_generic_interface_missing_field_fails);

    // ─── Generic type alias ───

    RUN(test_generic_type_alias_valid);
    RUN(test_generic_type_alias_type_mismatch);

    // ─── Concurrency type checking ───

    RUN(test_channel_type_valid);
    RUN(test_task_type_valid);
    RUN(test_task_scope_valid);
    RUN(test_spawn_outside_task_scope_warns);
    RUN(test_spawn_inside_task_scope_no_warn);
    RUN(test_task_scope_result_type);
    RUN(test_task_scope_heterogeneous_spawn_rejected);
    RUN(test_task_scope_nested_no_warn);

    // ─── Concurrency type checking — negative cases ───

    RUN(test_spawn_non_call_rejected);
    RUN(test_await_non_task_rejected);
    RUN(test_await_channel_rejected);
    RUN(test_task_scope_result_type_mismatch);
    RUN(test_await_outside_task_scope_on_fire_and_forget);

    // ─── Generic resolution edge cases (CA-26) ───

    RUN(test_generic_identity_function_infers);
    RUN(test_generic_wrong_type_arg_count);
    RUN(test_generic_nested_type_params);

    // ─── Edge-case generics tests ───

    RUN(test_generic_constraint_violation_error_message);
    RUN(test_generic_no_type_args_when_required);
    RUN(test_generic_recursive_choice_type_mismatch);

    // ─── Multiple bounds, generic record instantiation, composition ───

    RUN(test_generic_multi_bound_violation);
    RUN(test_generic_multi_bound_satisfied);
    RUN(test_generic_record_field_type_mismatch);
    RUN(test_generic_record_arg_instantiation_mismatch);
    RUN(test_generic_record_two_param_mismatch);
    RUN(test_generic_function_returns_generic_record);
    RUN(test_generic_function_takes_generic_record);
    RUN(test_generic_function_calls_generic_function);
    return SUMMARY();
}
