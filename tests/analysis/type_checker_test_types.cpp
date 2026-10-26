// Type checker unit tests — type system features: statistics, optional, literals,
// interface fields, interpolation, dictionary, unary, tuple index, bitwise,
// record defaults, match string, unused result, trusted_downcast, internal, error-pipe, ADTs.

#include <algorithm>
#include <cstddef>
#include <string>

#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "type_checker_test_helpers.hpp"

// ─── Statistics return-type signatures ───

// Statistics functions all return result<number>, not bare number.
// The type checker must expose result<number> so callers can use Result.unwrap().
static void test_statistics_mean_returns_result_number() {
    ASSERT_TRUE(passes("result<number> r = Statistics.mean([1, 2, 3])\n"));
}

static void test_statistics_median_returns_result_number() {
    ASSERT_TRUE(passes("result<number> r = Statistics.median([1, 2, 3])\n"));
}

static void test_statistics_mode_returns_result_number() {
    ASSERT_TRUE(passes("result<number> r = Statistics.mode([1, 2, 2])\n"));
}

static void test_statistics_variance_returns_result_number() {
    ASSERT_TRUE(passes("result<number> r = Statistics.variance([1.0, 2.0, 3.0])\n"));
}

static void test_statistics_standard_deviation_returns_result_number() {
    ASSERT_TRUE(passes("result<number> r = Statistics.standard_deviation([1.0, 2.0, 3.0])\n"));
}

// Stdlib signatures declare result<void>; the documentation spells it
// result<none>.  Both annotations accept either.
static void test_result_none_and_result_void_are_interchangeable() {
    ASSERT_TRUE(passes("result<none> r = Process.set_environment_variable(\"A\", \"1\")\n"));
    ASSERT_TRUE(passes("result<void> r = Process.set_environment_variable(\"A\", \"1\")\n"));
    ASSERT_TRUE(passes("result<void> r = success(none)\n"));
    ASSERT_TRUE(passes("function result<none> set() {\n"
                       "  return Process.set_environment_variable(\"A\", \"1\")\n"
                       "}\n"));
    // Only the "no value" spellings are interchangeable.
    ASSERT_TRUE(fails("result<integer> r = Process.set_environment_variable(\"A\", \"1\")\n"));
}

// ─── None type ───

// none is not assignable to a concrete type.
static void test_null_not_assignable_to_integer() {
    ASSERT_TRUE(fails("integer x = none"));
}

static void test_null_not_assignable_to_string() {
    ASSERT_TRUE(fails("string s = none"));
}

// none is assignable to optional<T>.
static void test_none_assignable_to_optional() {
    ASSERT_TRUE(passes("optional<integer> x = none"));
    ASSERT_TRUE(passes("optional<string> s = none"));
}

// some(v) produces optional<T>.
static void test_some_produces_optional() {
    ASSERT_TRUE(passes("optional<integer> x = some(42)"));
    ASSERT_TRUE(passes("optional<string> s = some(\"hello\")"));
}

// T is assignable to optional<T>.
static void test_value_assignable_to_optional() {
    ASSERT_TRUE(passes("optional<integer> x = 42"));
    ASSERT_TRUE(passes("optional<string> s = \"hello\""));
}

// some(none) is meaningless and must be rejected by the type checker — the
// inner value of some(...) may not itself be none.
static void test_some_none_is_meaningless() {
    ASSERT_TRUE(fails("optional<optional<integer>> n = some(none)"));
}

// ─── Empty literal assignability ───

// Empty array literal is assignable to any typed array variable.
static void test_empty_array_literal_assignable() {
    ASSERT_TRUE(passes("array<integer> xs = []"));
    ASSERT_TRUE(passes("array<string> ss = []"));
}

// Empty dict literal is assignable to a typed dictionary variable.
static void test_empty_dict_literal_assignable() {
    ASSERT_TRUE(passes("dictionary<integer> d = {}"));
}

// ─── Interface field access ───

static void test_interface_field_access_typed() {
    ASSERT_TRUE(passes("interface Named { string name }\n"
                       "function string greet(Named n) {\n"
                       "    return n.name\n"
                       "}\n"));
}

static void test_interface_field_access_type_mismatch() {
    ASSERT_TRUE(fails("interface Named { string name }\n"
                      "function integer greet(Named n) {\n"
                      "    return n.name\n"
                      "}\n"));
}

// ─── String interpolation ───

static void test_string_interpolation_valid() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    integer x = 42\n"
                       "    string s = \"value is ${x}\"\n"
                       "}\n"));
}

// ─── Dictionary type ───

static void test_dictionary_valid() {
    ASSERT_TRUE(passes("dictionary<integer> d = {\"a\": 1, \"b\": 2}\n"));
}

// ─── Unary operators ───

static void test_negation_requires_numeric() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    boolean b = -true\n"
                      "}\n"));
}

// ─── Tuple index inference ───

// tuple[literal] at a known index yields the declared element type.
static void test_tuple_index_typed_integer() {
    ASSERT_TRUE(passes("function void foo() {\n"
                       "    (integer, string) t = (1, \"hi\")\n"
                       "    integer n = t[0]\n"
                       "    string s = t[1]\n"
                       "}\n"));
}

// Assigning the first element of (string, integer) to integer is a type error.
static void test_tuple_index_typed_mismatch() {
    ASSERT_TRUE(fails("function void foo() {\n"
                      "    (string, integer) t = (\"hi\", 1)\n"
                      "    integer n = t[0]\n"
                      "}\n"));
}

// ─── Integer division and bitwise operators. ───

static void test_integer_division_requires_integer() {
    ASSERT_TRUE(fails("number x = 3.0 // 2.0\n"));
}

static void test_integer_division_returns_integer() {
    ASSERT_TRUE(passes("integer x = 10 // 3\n"));
}

// (Bitwise operators were removed from the language surface — R06. Bit
// manipulation now lives in the Bits stdlib module; its type signatures are
// exercised by the catalog conformance test and bits_functions.luma.)

// ─── Comparison and membership operators. ───

// Comparing incompatible operand types (string with numeric) is a type error.
static void test_comparison_mismatch_string_numeric() {
    ASSERT_TRUE(fails("boolean b = \"a\" < 1\n"));
}

// Comparing operands of the same ordered type is valid.
static void test_comparison_same_type_passes() {
    ASSERT_TRUE(passes("boolean b = 1 < 2\nboolean c = \"a\" < \"b\"\n"));
}

// The `in` operator requires a container (array/string/dictionary) on the right.
static void test_in_requires_container() {
    ASSERT_TRUE(fails("boolean b = 5 in 10\n"));
}

// `in` over an array, string, and dictionary all type-check and yield boolean.
static void test_in_container_passes() {
    ASSERT_TRUE(passes("boolean a = 3 in [1, 2, 3]\n"
                       "boolean b = \"x\" in \"xyz\"\n"
                       "boolean c = \"k\" in {\"k\": 1}\n"));
}

// `in` over a range type-checks and yields boolean, for both bound forms.
static void test_in_range_passes() {
    ASSERT_TRUE(passes("boolean a = 5 in 1..=100\n"
                       "boolean b = 5 in 1..100\n"));
}

// `in` on a range requires an integer left operand.
static void test_in_range_requires_integer() {
    ASSERT_TRUE(fails("boolean b = \"x\" in 1..=100\n"));
    ASSERT_TRUE(fails("boolean b = 1.5 in 1..=100\n"));
}

// ─── Record default field values. ───

static void test_record_default_field_valid() {
    ASSERT_TRUE(passes("record Config { string host = \"localhost\", integer port = 8080 }\n"
                       "Config c = Config {}\n"));
}

static void test_record_default_field_partial_override_valid() {
    ASSERT_TRUE(passes("record Config { string host = \"localhost\", integer port = 8080 }\n"
                       "Config c = Config { port = 443 }\n"));
}

static void test_record_required_field_still_required() {
    ASSERT_TRUE(fails("record User { string name, integer age = 0 }\n"
                      "User u = User {}\n" // name has no default → error
                      ));
}

static void test_record_default_type_mismatch_error() {
    ASSERT_TRUE(fails("record Box { integer size = \"oops\" }\n" // string assigned to integer
                      "Box b = Box {}\n"));
}

// ─── Match string literal arms without == prefix. ───

static void test_match_string_literal_arm_valid() {
    ASSERT_TRUE(passes("string s = \"hi\"\n"
                       "match s {\n"
                       "    case \"hi\"    { integer x = 1 }\n"
                       "    case \"hello\" { integer x = 2 }\n"
                       "    else          { integer x = 3 }\n"
                       "}\n"));
}

static void test_match_string_literal_arm_requires_else() {
    ASSERT_TRUE(fails("string s = \"hi\"\n"
                      "match s {\n"
                      "    case \"hi\" { integer x = 1 }\n"
                      "}\n" // no else arm — should be required
                      ));
}

// ─── Nested match / if in value position (regression). ───
// A match or if used as the tail value of an enclosing match arm must
// contribute its result type; previously it was inferred as 'void', which
// broke assignment of the enclosing match to a typed variable.

static void test_match_nested_match_arm_value_typed() {
    ASSERT_TRUE(passes("choice E { A  B }\n"
                       "result<integer, E> r = failure(E.A)\n"
                       "string label = match r {\n"
                       "    success(v) { \"ok\" }\n"
                       "    failure(e) {\n"
                       "        match e {\n"
                       "        case E.A { \"a\" }\n"
                       "        case E.B { \"b\" }\n"
                       "        }\n"
                       "    }\n"
                       "}\n"));
}

static void test_match_nested_if_arm_value_typed() {
    ASSERT_TRUE(passes("result<integer> r = success(5)\n"
                       "string label = match r {\n"
                       "    success(v) { if v > 0 { \"pos\" } else { \"nonpos\" } }\n"
                       "    failure(e) { \"err\" }\n"
                       "}\n"));
}

// The nested match's type is propagated, so a mismatch between it and the
// sibling arm is detected (would not be caught when it inferred as void).
static void test_match_nested_match_arm_value_type_mismatch_fails() {
    ASSERT_TRUE(fails("choice E { A  B }\n"
                      "result<integer, E> r = failure(E.A)\n"
                      "string label = match r {\n"
                      "    success(v) { \"ok\" }\n"
                      "    failure(e) {\n"
                      "        match e {\n"
                      "        case E.A { 1 }\n"
                      "        case E.B { 2 }\n"
                      "        }\n"
                      "    }\n"
                      "}\n"));
}

// ─── Unused result<T> warning. ───

static void test_unused_result_errors() {
    // Silently discarding a result<T> is a hard error by default.
    ASSERT_TRUE(fails("function result<string> get() { return success(\"hi\") }\n"
                      "get()\n"));
}

static void test_unused_result_assigned_no_warn() {
    ASSERT_FALSE(has_warnings("function result<string> get() { return success(\"hi\") }\n"
                              "result<string> r = get()\n"));
}

static void test_unused_result_suppressed_with_wildcard() {
    ASSERT_FALSE(has_warnings("function result<string> get() { return success(\"hi\") }\n"
                              "result<string> _ = get()\n"));
}

static void test_unused_result_suppressed_with_discard_assignment() {
    // '_ = expr' with no '_' in scope discards the value without declaring it.
    const std::string source = "function result<string> get() { return success(\"hi\") }\n"
                               "function void main() {\n"
                               "    _ = get()\n"
                               "}\n";
    ASSERT_FALSE(fails(source));
    ASSERT_FALSE(has_warnings(source));
}

static void test_discard_assignment_still_checks_value() {
    ASSERT_TRUE(fails("function void main() {\n"
                      "    _ = missing_name\n"
                      "}\n"));
}

static void test_declared_underscore_assignment_is_type_checked() {
    // A declared mutable '_' keeps ordinary assignment semantics, including type checks.
    ASSERT_TRUE(fails("function void main() {\n"
                      "    mutable integer _ = 1\n"
                      "    _ = \"text\"\n"
                      "}\n"));
}

static void test_discard_assignment_inside_underscore_loop() {
    // An immutable '_' (here the loop variable) cannot be assigned, so '_ = expr'
    // discards instead of reporting an immutable assignment.
    const std::string source = "function result<string> get() { return success(\"hi\") }\n"
                               "function void main() {\n"
                               "    for _ in 0..3 {\n"
                               "        _ = get()\n"
                               "    }\n"
                               "}\n";
    ASSERT_FALSE(fails(source));
    ASSERT_FALSE(has_warnings(source));
}

static void test_unused_void_no_warn() {
    ASSERT_FALSE(has_warnings("function void side_effect() { print(\"hi\") }\n"
                              "side_effect()\n"));
}

// ─── trusted_downcast on StdlibAny emits warning. ───

static void test_trusted_downcast_on_any_warns() {
    ASSERT_TRUE(has_warnings("function result<integer> get_value() { return success(42) }\n"
                             "function void main() {\n"
                             "    result<integer> r = get_value()\n"
                             "    integer n = trusted_downcast<integer>(Result.unwrap(r))\n"
                             "}\n"));
}

static void test_trusted_downcast_on_concrete_no_warn() {
    // trusted_downcast on a concrete type warns only for redundant cast.
    // The StdlibAny warning should not appear for concrete types.
    const auto warnings = check_warnings("integer val = 42\n"
                                         "integer n = trusted_downcast<integer>(val)\n");

    bool has_stdlib_any_warn = false;

    for (const auto& w : warnings) {
        if (w.message.find("StdlibAny") != std::string::npos) {
            has_stdlib_any_warn = true;
        }
    }

    ASSERT_FALSE(has_stdlib_any_warn);
}

// ─── internal keyword access control. ───

static void test_internal_function_callable_from_within_namespace() {
    ASSERT_TRUE(passes("namespace Util {\n"
                       "    function string process(string s) {\n"
                       "        return Util.normalize(s)\n"
                       "    }\n"
                       "    internal function string normalize(string s) {\n"
                       "        return s\n"
                       "    }\n"
                       "}\n"));
}

static void test_internal_function_blocked_from_outside() {
    ASSERT_TRUE(fails("namespace Util {\n"
                      "    internal function string normalize(string s) { return s }\n"
                      "}\n"
                      "string r = Util.normalize(\"hello\")\n"));
}

static void test_internal_record_blocked_from_outside() {
    ASSERT_TRUE(fails("namespace Util {\n"
                      "    internal record Config { string name }\n"
                      "}\n"
                      "Util.Config c = Util.Config { name = \"x\" }\n"));
}

static void test_internal_enum_blocked_from_outside() {
    ASSERT_TRUE(fails("namespace Util {\n"
                      "    internal choice State { Active, Inactive }\n"
                      "}\n"
                      "Util.State s = Util.State.Active\n"));
}

static void test_internal_interface_blocked_from_outside() {
    // An internal interface used as a qualified type annotation from outside
    // the namespace must be rejected, just like internal records and choices.
    ASSERT_TRUE(fails("namespace Util {\n"
                      "    internal interface Shape { number area }\n"
                      "}\n"
                      "function number describe(Util.Shape s) { return s.area }\n"));
}

static void test_internal_type_alias_blocked_from_outside() {
    // An internal type alias is private to its namespace: qualified access
    // from outside must be a type error.
    ASSERT_TRUE(fails("namespace Util {\n"
                      "    internal type Distance = number\n"
                      "}\n"
                      "Util.Distance d = 4.0\n"));
}

static void test_internal_interface_usable_within_namespace() {
    // Inside the owning namespace, an internal interface resolves normally.
    ASSERT_TRUE(passes("namespace Util {\n"
                       "    internal interface Shape { number area }\n"
                       "    record Circle { number area }\n"
                       "    function number describe(Shape s) { return s.area }\n"
                       "}\n"));
}

static void test_internal_type_alias_usable_within_namespace() {
    // Inside the owning namespace, an internal type alias resolves normally.
    ASSERT_TRUE(passes("namespace Util {\n"
                       "    internal type Distance = number\n"
                       "    function Distance origin() { return 0.0 }\n"
                       "}\n"));
}

static void test_internal_not_imported_by_use() {
    ASSERT_TRUE(fails("namespace Util {\n"
                      "    internal function string normalize(string s) { return s }\n"
                      "}\n"
                      "use Util\n"
                      "string r = normalize(\"hello\")\n"));
}

static void test_internal_at_top_level_is_syntax_error() {
    DiagnosticCollector discarded;
    Lexer lexer{"internal function string foo() { return \"x\" }\n", discarded};
    auto tokens = lexer.tokenize();

    Parser parser{std::move(tokens)};
    (void)parser.parse();

    ASSERT_FALSE(parser.get_errors().empty());
}

static void test_non_internal_members_still_accessible() {
    ASSERT_TRUE(passes("namespace Util {\n"
                       "    function string process(string s) { return s }\n"
                       "    internal function string secret(string s) { return s }\n"
                       "}\n"
                       "string r = Util.process(\"hello\")\n"));
}

// ─── Regression: nested namespaces resolve their own members ───

static void test_nested_namespace_member_types_resolve() {
    // Members of a nested namespace are registered under the full path
    // (Outer.Inner.Item); a bare sibling name used to be retried as
    // Inner.Item and reported as an unknown type.
    ASSERT_TRUE(passes("namespace Outer {\n"
                       "    namespace Inner {\n"
                       "        record Item { integer n }\n"
                       "        function integer get(Item it) { return it.n }\n"
                       "    }\n"
                       "}\n"));
}

static void test_nested_namespace_internal_type_usable_inside() {
    ASSERT_TRUE(passes("namespace Outer {\n"
                       "    namespace Inner {\n"
                       "        internal record Secret { integer n }\n"
                       "        function integer reveal(Secret s) { return s.n }\n"
                       "        function integer reveal_all(array<Outer.Inner.Secret> s) {\n"
                       "            return Array.length(s)\n"
                       "        }\n"
                       "    }\n"
                       "}\n"));
}

static void test_nested_namespace_internal_type_blocked_outside() {
    ASSERT_TRUE(fails_with_message("namespace Outer {\n"
                                   "    namespace Inner {\n"
                                   "        internal record Secret { integer n }\n"
                                   "    }\n"
                                   "}\n"
                                   "array<Outer.Inner.Secret> s = []\n",
                                   "is internal to namespace 'Outer.Inner'"));
}

// ─── Regression: an internal-access error is reported once, where it occurs ───

static void test_internal_type_in_annotation_reported_once_with_location() {
    const auto diagnostics = check("namespace Geo {\n"
                                   "    internal record Secret { integer n }\n"
                                   "}\n"
                                   "array<Geo.Secret> s = []\n");

    const auto is_internal = [](const Diagnostic& d) {
        return d.message.find("is internal to namespace 'Geo'") != std::string::npos;
    };

    ASSERT_EQ(std::ranges::count_if(diagnostics, is_internal), std::ptrdiff_t{1});

    const auto it = std::ranges::find_if(diagnostics, is_internal);
    ASSERT_TRUE(it != diagnostics.end());
    ASSERT_EQ(it->primary_location().line, 4);
}

static void test_internal_type_in_signature_reported_once_with_location() {
    const auto diagnostics = check("namespace Geo {\n"
                                   "    internal record Secret { integer n }\n"
                                   "}\n"
                                   "function integer peek(Geo.Secret s) { return 0 }\n");

    const auto is_internal = [](const Diagnostic& d) {
        return d.message.find("is internal to namespace 'Geo'") != std::string::npos;
    };

    ASSERT_EQ(std::ranges::count_if(diagnostics, is_internal), std::ptrdiff_t{1});

    const auto it = std::ranges::find_if(diagnostics, is_internal);
    ASSERT_TRUE(it != diagnostics.end());
    ASSERT_EQ(it->primary_location().line, 4);
}

// ─── !> error-pipe operator type checking ───

static void test_error_pipe_infers_result_type() {
    // A plain function after !> wraps the return in result<T>.
    ASSERT_TRUE(passes("function integer double_it(integer n) { return n * 2 }\n"
                       "result<integer> r = 21 !> double_it()\n"));
}

static void test_error_pipe_chain_infers_result_type() {
    ASSERT_TRUE(passes("function result<integer> parse(string s) { return success(1) }\n"
                       "function integer times2(integer n) { return n * 2 }\n"
                       "result<integer> r = success(\"x\") !> parse() !> times2()\n"));
}

// ─── Choice (ADT) type checking ───

static void test_choice_declaration_valid() {
    ASSERT_TRUE(passes("choice Shape {\n"
                       "  Circle(number radius)\n"
                       "  Rectangle(number width, number height)\n"
                       "  Point\n"
                       "}\n"));
}

static void test_choice_variant_field_unknown_type() {
    ASSERT_TRUE(fails("choice Bad {\n"
                      "  Variant(foo_type x)\n"
                      "}\n"));
}

// Calling a unit variant like a data-variant constructor is an error.
static void test_choice_unit_variant_called_as_function() {
    ASSERT_TRUE(fails("choice Hue { Red  Green  Blue }\n"
                      "Hue c = Hue.Red(5)\n"));
}

// A data variant must be constructed with the exact declared arity.
static void test_choice_data_variant_too_many_args() {
    ASSERT_TRUE(fails("choice Shape {\n"
                      "  Circle(number radius)\n"
                      "  Point\n"
                      "}\n"
                      "Shape s = Shape.Circle(1.0, 2.0)\n"));
}

static void test_choice_data_variant_too_few_args() {
    ASSERT_TRUE(fails("choice Shape {\n"
                      "  Rectangle(number width, number height)\n"
                      "  Point\n"
                      "}\n"
                      "Shape s = Shape.Rectangle(1.0)\n"));
}

// A data variant argument must match the declared field type.
static void test_choice_data_variant_wrong_arg_type() {
    ASSERT_TRUE(fails("choice Shape {\n"
                      "  Circle(number radius)\n"
                      "  Point\n"
                      "}\n"
                      "Shape s = Shape.Circle(\"big\")\n"));
}

// A data variant used without its argument list is not a value of the
// choice type (it is the constructor, not an instance).
static void test_choice_data_variant_missing_args_as_value() {
    ASSERT_TRUE(fails("choice Shape {\n"
                      "  Circle(number radius)\n"
                      "  Point\n"
                      "}\n"
                      "Shape s = Shape.Circle\n"));
}

// A unit variant carries no fields, so destructuring it in a match arm
// is an error.
static void test_choice_match_destructure_unit_variant() {
    ASSERT_TRUE(fails("choice Color { Red  Green  Blue }\n"
                      "Color c = Color.Red\n"
                      "integer x = match c {\n"
                      "  case Color.Red(n) { n }\n"
                      "  else { 0 }\n"
                      "}\n"));
}

// A value of one choice type cannot be assigned to a variable of another.
static void test_choice_assign_wrong_choice_type() {
    ASSERT_TRUE(fails("choice Color { Red  Green }\n"
                      "choice Mood { Happy  Sad }\n"
                      "Color c = Mood.Happy\n"));
}

// ─── Record destructuring ───

static void test_record_destructuring_valid() {
    ASSERT_TRUE(passes("record Point { integer x, integer y }\n"
                       "Point p = Point { x = 1, y = 2 }\n"
                       "Point { x, y } = p\n"
                       "print(x + y)\n"));
}

static void test_record_destructuring_subset_valid() {
    ASSERT_TRUE(passes("record Point { integer x, integer y, integer z }\n"
                       "Point p = Point { x = 1, y = 2, z = 3 }\n"
                       "Point { x } = p\n"
                       "print(x)\n"));
}

static void test_record_destructuring_unknown_field_fails() {
    ASSERT_TRUE(fails("record Point { integer x, integer y }\n"
                      "Point p = Point { x = 1, y = 2 }\n"
                      "Point { x, w } = p\n"));
}

static void test_record_destructuring_duplicate_field_fails() {
    ASSERT_TRUE(fails("record Point { integer x, integer y }\n"
                      "Point p = Point { x = 1, y = 2 }\n"
                      "Point { x, x } = p\n"));
}

static void test_record_destructuring_unknown_type_fails() {
    ASSERT_TRUE(fails("Nope { x } = 1\n"));
}

static void test_record_destructuring_wrong_initializer_fails() {
    ASSERT_TRUE(fails("record Point { integer x, integer y }\n"
                      "record Other { integer a }\n"
                      "Other o = Other { a = 1 }\n"
                      "Point { x } = o\n"));
}

static void test_record_destructuring_field_type_used() {
    // The bound field keeps its declared type: a string field is not numeric.
    ASSERT_TRUE(fails("record Named { string name }\n"
                      "Named n = Named { name = \"Ada\" }\n"
                      "Named { name } = n\n"
                      "print(name + 1)\n"));
}

static void test_record_match_pattern_valid() {
    ASSERT_TRUE(passes("record Named { string name, integer age }\n"
                       "Named n = Named { name = \"Ada\", age = 36 }\n"
                       "match n {\n"
                       "    case Named { name, age } { print(name) print(age) }\n"
                       "}\n"));
}

static void test_record_match_pattern_unknown_field_fails() {
    ASSERT_TRUE(fails("record Named { string name, integer age }\n"
                      "Named n = Named { name = \"Ada\", age = 36 }\n"
                      "match n {\n"
                      "    case Named { nope } { print(nope) }\n"
                      "}\n"));
}

static void test_record_match_pattern_wrong_type_fails() {
    // The arm's record type must match the subject's record type.
    ASSERT_TRUE(fails("record Point { integer x }\n"
                      "record Other { integer a }\n"
                      "Point p = Point { x = 1 }\n"
                      "match p {\n"
                      "    case Other { a } { print(a) }\n"
                      "}\n"));
}

static void test_record_match_pattern_on_non_record_fails() {
    ASSERT_TRUE(fails("record Point { integer x }\n"
                      "integer n = 5\n"
                      "match n {\n"
                      "    case Point { x } { print(x) }\n"
                      "}\n"));
}

static void test_record_match_pattern_generic_valid() {
    ASSERT_TRUE(passes("record Box<T> { T value, integer tag }\n"
                       "Box<integer> b = Box<integer> { value = 1, tag = 2 }\n"
                       "integer got = match b {\n"
                       "    case Box { value, tag } { value + tag }\n"
                       "}\n"
                       "print(got)\n"));
}

// ─── Regression: integer compound assignment with a number value ───

static void test_integer_compound_assignment_with_number_fails() {
    ASSERT_TRUE(fails_with("mutable integer x = 1\n"
                           "x += 0.5\n",
                           DiagnosticCode::TypeMismatch));
    ASSERT_TRUE(fails("mutable integer y = 4\n"
                      "y *= 1.5\n"));
    ASSERT_TRUE(passes("mutable integer z = 1\n"
                       "z += 2\n"
                       "mutable number w = 1.0\n"
                       "w += 1\n"));
}

// ─── Regression: element type of queue/stack/set/key_value_store ───

static void test_builtin_container_type_argument_is_kept() {
    ASSERT_TRUE(fails("queue<integer> q = Queue.new()\n"
                      "queue<string> r = q\n"));
    ASSERT_TRUE(fails("set<integer> s = Set.new()\n"
                      "set<string> t = s\n"));
    ASSERT_TRUE(passes("stack<integer> s = Stack.new()\n"
                       "stack<integer> t = s\n"));
}

// ─── Regression: match expression arm patterns are checked ───

static void test_match_expression_integer_case_on_string_subject_fails() {
    ASSERT_TRUE(fails_with_message("string name = \"a\"\n"
                                   "string r = match name {\n"
                                   "    case 5 { \"five\" }\n"
                                   "    else { \"other\" }\n"
                                   "}\n",
                                   "integer case but subject is 'string'"));
}

static void test_match_expression_range_on_number_subject_fails() {
    ASSERT_TRUE(fails_with_message("number v = 1.5\n"
                                   "string r = match v {\n"
                                   "    case 0..=9 { \"low\" }\n"
                                   "    else { \"high\" }\n"
                                   "}\n",
                                   "range case but subject is 'number'"));
}

// ─── Regression: '?' checks the propagated error type ───

static void test_question_mark_propagates_incompatible_error_type_fails() {
    ASSERT_TRUE(fails_with("record MathError { string reason }\n"
                           "function result<integer, string> parse() { return success(1) }\n"
                           "function result<integer, MathError> compute() {\n"
                           "    integer n = parse()?\n"
                           "    return success(n)\n"
                           "}\n",
                           DiagnosticCode::TypeMismatch));
    ASSERT_TRUE(passes("record MathError { string reason }\n"
                       "function result<integer, MathError> parse() { return success(1) }\n"
                       "function result<integer, MathError> compute() {\n"
                       "    integer n = parse()?\n"
                       "    return success(n)\n"
                       "}\n"));
}

// ─── Regression: the default error type of result<T> is string ───

static void test_result_default_error_type_is_string() {
    ASSERT_TRUE(fails("result<integer> r = failure(42)\n"));
    ASSERT_TRUE(passes("result<integer> r = failure(\"bad input\")\n"
                       "result<integer, string> same = r\n"));
    ASSERT_TRUE(fails("result<integer> r = failure(\"bad input\")\n"
                      "match r {\n"
                      "    case success(v) { print(v) }\n"
                      "    case failure(e) { integer n = e }\n"
                      "}\n"));
}

// ─── Regression: plain '.' and '[ ]' on an optional ───

static void test_plain_field_access_on_optional_fails() {
    ASSERT_TRUE(fails_with_message("record User { string name }\n"
                                   "optional<User> u = none\n"
                                   "string n = u.name\n",
                                   "may be none"));
    ASSERT_TRUE(passes("record User { string name }\n"
                       "optional<User> u = none\n"
                       "string n = u?.name ?? \"nobody\"\n"));
}

static void test_plain_index_access_on_optional_fails() {
    ASSERT_TRUE(fails_with_message("optional<array<integer>> xs = none\n"
                                   "integer first = xs[0]\n",
                                   "may be none"));
    ASSERT_TRUE(passes("optional<array<integer>> xs = none\n"
                       "integer first = xs?[0] ?? 0\n"));
}

// ─── Regression: destructuring a value that is not a tuple ───

static void test_tuple_destructuring_non_tuple_fails() {
    ASSERT_TRUE(fails_with_message("(integer a, integer b) = 5\n", "cannot destructure"));
}

static void test_for_destructuring_non_tuple_elements_fails() {
    ASSERT_TRUE(fails_with_message("for (a, b) in [1, 2] {\n"
                                   "    print(a)\n"
                                   "}\n",
                                   "cannot destructure"));
}

// ─── Regression: '??' needs an optional or result on the left ───

static void test_null_coalescing_on_plain_value_fails() {
    ASSERT_TRUE(fails_with("integer x = 5 ?? 0\n", DiagnosticCode::InvalidOperand));
}

static void test_null_coalescing_chain_widens_right_side() {
    // The right optional's integer is widened when it lands in optional<number>.
    const auto program = parse("optional<number> a = none\n"
                               "optional<integer> b = some(1)\n"
                               "optional<number> c = a ?? b\n");
    TypeChecker checker;
    ASSERT_TRUE(checker.check(program, false).empty());

    const auto& decl = static_cast<const VariableDeclStatement&>(*program.statements[2]);
    const auto& coalesce = static_cast<const BinaryExpression&>(*decl.initializer);
    ASSERT_TRUE(coalesce.right->numeric_widening != nullptr);
}

int main() {
    // ─── Statistics return-type signatures ───

    RUN(test_statistics_mean_returns_result_number);
    RUN(test_statistics_median_returns_result_number);
    RUN(test_statistics_mode_returns_result_number);
    RUN(test_statistics_variance_returns_result_number);
    RUN(test_statistics_standard_deviation_returns_result_number);
    RUN(test_result_none_and_result_void_are_interchangeable);

    // ─── None/Optional type ───

    RUN(test_null_not_assignable_to_integer);
    RUN(test_null_not_assignable_to_string);
    RUN(test_none_assignable_to_optional);
    RUN(test_some_produces_optional);
    RUN(test_value_assignable_to_optional);
    RUN(test_some_none_is_meaningless);

    // ─── Empty literal assignability ───

    RUN(test_empty_array_literal_assignable);
    RUN(test_empty_dict_literal_assignable);

    // ─── Interface field access ───

    RUN(test_interface_field_access_typed);
    RUN(test_interface_field_access_type_mismatch);

    // ─── String interpolation ───

    RUN(test_string_interpolation_valid);

    // ─── Dictionary ───

    RUN(test_dictionary_valid);

    // ─── Unary operators ───

    RUN(test_negation_requires_numeric);

    // ─── Integer division and bitwise operators ───

    RUN(test_integer_division_requires_integer);
    RUN(test_integer_division_returns_integer);

    // ─── Comparison and membership operators ───

    RUN(test_comparison_mismatch_string_numeric);
    RUN(test_comparison_same_type_passes);
    RUN(test_in_requires_container);
    RUN(test_in_container_passes);
    RUN(test_in_range_passes);
    RUN(test_in_range_requires_integer);

    // ─── Tuple index inference ───

    RUN(test_tuple_index_typed_integer);
    RUN(test_tuple_index_typed_mismatch);

    // ─── Record default field values ───

    RUN(test_record_default_field_valid);
    RUN(test_record_default_field_partial_override_valid);
    RUN(test_record_required_field_still_required);
    RUN(test_record_default_type_mismatch_error);

    // ─── Match string literal arms without == prefix ───

    RUN(test_match_string_literal_arm_valid);
    RUN(test_match_string_literal_arm_requires_else);
    RUN(test_match_nested_match_arm_value_typed);
    RUN(test_match_nested_if_arm_value_typed);
    RUN(test_match_nested_match_arm_value_type_mismatch_fails);

    // ─── Unused result<T> warning ───

    RUN(test_unused_result_errors);
    RUN(test_unused_result_assigned_no_warn);
    RUN(test_unused_result_suppressed_with_wildcard);
    RUN(test_unused_result_suppressed_with_discard_assignment);
    RUN(test_discard_assignment_still_checks_value);
    RUN(test_declared_underscore_assignment_is_type_checked);
    RUN(test_discard_assignment_inside_underscore_loop);
    RUN(test_unused_void_no_warn);

    // ─── trusted_downcast on StdlibAny emits warning ───

    RUN(test_trusted_downcast_on_any_warns);
    RUN(test_trusted_downcast_on_concrete_no_warn);

    // ─── internal keyword access control ───

    RUN(test_internal_function_callable_from_within_namespace);
    RUN(test_internal_function_blocked_from_outside);
    RUN(test_internal_record_blocked_from_outside);
    RUN(test_internal_enum_blocked_from_outside);
    RUN(test_internal_interface_blocked_from_outside);
    RUN(test_internal_type_alias_blocked_from_outside);
    RUN(test_internal_interface_usable_within_namespace);
    RUN(test_internal_type_alias_usable_within_namespace);
    RUN(test_internal_not_imported_by_use);
    RUN(test_internal_at_top_level_is_syntax_error);
    RUN(test_nested_namespace_member_types_resolve);
    RUN(test_nested_namespace_internal_type_usable_inside);
    RUN(test_nested_namespace_internal_type_blocked_outside);
    RUN(test_internal_type_in_annotation_reported_once_with_location);
    RUN(test_internal_type_in_signature_reported_once_with_location);
    RUN(test_non_internal_members_still_accessible);

    // ─── !> error-pipe operator type checking ───

    RUN(test_error_pipe_infers_result_type);
    RUN(test_error_pipe_chain_infers_result_type);

    // ─── Choice (ADT) type checking ───

    RUN(test_choice_declaration_valid);
    RUN(test_choice_variant_field_unknown_type);
    RUN(test_choice_unit_variant_called_as_function);
    RUN(test_choice_data_variant_too_many_args);
    RUN(test_choice_data_variant_too_few_args);
    RUN(test_choice_data_variant_wrong_arg_type);
    RUN(test_choice_data_variant_missing_args_as_value);
    RUN(test_choice_match_destructure_unit_variant);
    RUN(test_choice_assign_wrong_choice_type);

    // ─── Record destructuring ───
    RUN(test_record_destructuring_valid);
    RUN(test_record_destructuring_subset_valid);
    RUN(test_record_destructuring_unknown_field_fails);
    RUN(test_record_destructuring_duplicate_field_fails);
    RUN(test_record_destructuring_unknown_type_fails);
    RUN(test_record_destructuring_wrong_initializer_fails);
    RUN(test_record_destructuring_field_type_used);
    RUN(test_record_match_pattern_valid);
    RUN(test_record_match_pattern_unknown_field_fails);
    RUN(test_record_match_pattern_wrong_type_fails);
    RUN(test_record_match_pattern_on_non_record_fails);
    RUN(test_record_match_pattern_generic_valid);

    // ─── Regression: compound assignment, containers, match, results, optionals, destructuring ───

    RUN(test_integer_compound_assignment_with_number_fails);
    RUN(test_builtin_container_type_argument_is_kept);
    RUN(test_match_expression_integer_case_on_string_subject_fails);
    RUN(test_match_expression_range_on_number_subject_fails);
    RUN(test_question_mark_propagates_incompatible_error_type_fails);
    RUN(test_result_default_error_type_is_string);
    RUN(test_plain_field_access_on_optional_fails);
    RUN(test_plain_index_access_on_optional_fails);
    RUN(test_tuple_destructuring_non_tuple_fails);
    RUN(test_for_destructuring_non_tuple_elements_fails);
    RUN(test_null_coalescing_on_plain_value_fails);
    RUN(test_null_coalescing_chain_widens_right_side);
    return SUMMARY();
}
