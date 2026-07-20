// Name resolver unit tests.

#include <string>
#include <vector>

#include "analysis/resolver/resolver.hpp"
#include "lex_parse_util.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Helpers ───

static std::vector<Diagnostic> resolve(const std::string& source) {
    auto program = luma::test::lex_and_parse(source);

    NameResolver resolver;

    return resolver.resolve(program);
}

static bool resolves(const std::string& source) {
    return resolve(source).empty();
}

// ─── ResolveScope tests ───

static void test_scope_define_and_lookup() {
    auto scope = std::make_shared<ResolveScope>();
    (void)scope->define("x", false);

    const auto result = scope->lookup("x");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->slot_index, 0);
    ASSERT_EQ(result->is_mutable, false);
}

static void test_scope_multiple_defines() {
    auto scope = std::make_shared<ResolveScope>();
    auto slot_a = scope->define("a", false);
    auto slot_b = scope->define("b", true);

    ASSERT_EQ(slot_a, 0);
    ASSERT_EQ(slot_b, 1);
    ASSERT_EQ(scope->local_count(), 2);
}

static void test_scope_lookup_not_found() {
    auto scope = std::make_shared<ResolveScope>();
    (void)scope->define("x", false);

    const auto result = scope->lookup("y");
    ASSERT_TRUE(!result.has_value());
}

static void test_scope_parent_lookup() {
    auto parent = std::make_shared<ResolveScope>();
    (void)parent->define("x", true);

    auto child = std::make_shared<ResolveScope>(parent);
    (void)child->define("y", false);

    // Child can see its own variable.
    const auto y = child->lookup("y");
    ASSERT_TRUE(y.has_value());
    ASSERT_EQ(y->frame_depth, 0);

    // Child can see parent's variable.
    const auto x = child->lookup("x");
    ASSERT_TRUE(x.has_value());
    ASSERT_EQ(x->is_mutable, true);
}

static void test_scope_has_local() {
    auto parent = std::make_shared<ResolveScope>();
    (void)parent->define("x", false);

    auto child = std::make_shared<ResolveScope>(parent);

    ASSERT_FALSE(child->has_local("x")); // x is in parent, not local.
    (void)child->define("y", false);
    ASSERT_TRUE(child->has_local("y"));

    // Verify has_local remains correct after cross-scope lookup caches
    // the parent variable into child's variables_ map.
    auto found = child->lookup("x");
    ASSERT_TRUE(found.has_value());
    ASSERT_FALSE(child->has_local("x")); // Still not locally defined.
}

static void test_scope_define_if_absent() {
    auto scope = std::make_shared<ResolveScope>();
    (void)scope->define("x", false);

    // define_if_absent should not create a duplicate.
    ASSERT_FALSE(scope->define_if_absent("x", true));
    ASSERT_EQ(scope->local_count(), 1);

    // define_if_absent should create a new variable.
    ASSERT_TRUE(scope->define_if_absent("y", false));
    ASSERT_EQ(scope->local_count(), 2);
}

static void test_scope_define_if_absent_from_parent() {
    auto parent = std::make_shared<ResolveScope>();
    (void)parent->define("x", false);

    auto child = std::make_shared<ResolveScope>(parent);
    ASSERT_FALSE(child->define_if_absent("x", true));
    ASSERT_FALSE(child->has_local("x")); // Should not be local.
    ASSERT_EQ(child->local_count(), 0);  // No new slot allocated.
}

// ─── Full resolver tests ───

static void test_resolve_simple_variable() {
    ASSERT_TRUE(resolves("integer x = 42\n"));
}

static void test_resolve_function_call() {
    ASSERT_TRUE(resolves("function string greet() {\n"
                         "    return \"hello\"\n"
                         "}\n"
                         "string msg = greet()\n"));
}

static void test_resolve_nested_scopes() {
    ASSERT_TRUE(resolves("function integer outer() {\n"
                         "    integer x = 1\n"
                         "    return x\n"
                         "}\n"));
}

static void test_resolve_record_access() {
    ASSERT_TRUE(resolves("record Point {\n"
                         "    integer x\n"
                         "    integer y\n"
                         "}\n"
                         "Point p = Point(1, 2)\n"));
}

static void test_resolve_namespace_function() {
    ASSERT_TRUE(resolves("namespace Util {\n"
                         "    function integer add(integer a, integer b) {\n"
                         "        return a + b\n"
                         "    }\n"
                         "}\n"
                         "integer result = Util.add(1, 2)\n"));
}

static void test_resolve_mutable_variable() {
    ASSERT_TRUE(resolves("mutable integer x = 10\n"
                         "x = 20\n"));
}

static void test_resolve_choice_variant() {
    ASSERT_TRUE(resolves("choice Color {\n"
                         "    Red\n"
                         "    Green\n"
                         "    Blue\n"
                         "}\n"
                         "Color c = Color.Red\n"));
}

// ─── Use-declaration import tests (characterization) ───
// The resolver registers namespace members and processes `use` imports but
// currently emits no diagnostics for well-formed programs.  These cases pin
// the wildcard, specific, choice, and internal-member-filtering import paths
// so the member-registration refactor stays behaviour-preserving.

static void test_resolve_use_wildcard_import() {
    ASSERT_TRUE(resolves("namespace Geometry {\n"
                         "    function number square(number n) {\n"
                         "        return n * n\n"
                         "    }\n"
                         "    function number cube(number n) {\n"
                         "        return n * n * n\n"
                         "    }\n"
                         "}\n"
                         "use Geometry\n"
                         "number a = square(2.0)\n"
                         "number b = cube(3.0)\n"));
}

static void test_resolve_use_specific_function_import() {
    ASSERT_TRUE(resolves("namespace MathUtils {\n"
                         "    function integer double_it(integer x) {\n"
                         "        return x * 2\n"
                         "    }\n"
                         "    function integer triple_it(integer x) {\n"
                         "        return x * 3\n"
                         "    }\n"
                         "}\n"
                         "use MathUtils.double_it\n"
                         "integer val = double_it(5)\n"));
}

static void test_resolve_use_specific_choice_import() {
    ASSERT_TRUE(resolves("namespace Palette {\n"
                         "    choice Direction {\n"
                         "        Up\n"
                         "        Down\n"
                         "    }\n"
                         "}\n"
                         "use Palette.Direction\n"
                         "Direction d = Direction.Up\n"));
}

static void test_resolve_use_wildcard_choice_import() {
    ASSERT_TRUE(resolves("namespace Signals {\n"
                         "    choice State {\n"
                         "        On\n"
                         "        Off\n"
                         "    }\n"
                         "    function integer code(integer n) {\n"
                         "        return n\n"
                         "    }\n"
                         "}\n"
                         "use Signals\n"
                         "State s = State.On\n"
                         "integer c = code(7)\n"));
}

static void test_resolve_use_wildcard_skips_internal_member() {
    ASSERT_TRUE(resolves("namespace Secret {\n"
                         "    function integer visible() {\n"
                         "        return 1\n"
                         "    }\n"
                         "    internal function integer hidden() {\n"
                         "        return 2\n"
                         "    }\n"
                         "}\n"
                         "use Secret\n"
                         "integer x = visible()\n"));
}

// ─── main ───

int main() {
    // ResolveScope unit tests.
    RUN(test_scope_define_and_lookup);
    RUN(test_scope_multiple_defines);
    RUN(test_scope_lookup_not_found);
    RUN(test_scope_parent_lookup);
    RUN(test_scope_has_local);
    RUN(test_scope_define_if_absent);
    RUN(test_scope_define_if_absent_from_parent);

    // Full resolver integration tests.
    RUN(test_resolve_simple_variable);
    RUN(test_resolve_function_call);
    RUN(test_resolve_nested_scopes);
    RUN(test_resolve_record_access);
    RUN(test_resolve_namespace_function);
    RUN(test_resolve_mutable_variable);
    RUN(test_resolve_choice_variant);

    // Use-declaration import characterization tests.
    RUN(test_resolve_use_wildcard_import);
    RUN(test_resolve_use_specific_function_import);
    RUN(test_resolve_use_specific_choice_import);
    RUN(test_resolve_use_wildcard_choice_import);
    RUN(test_resolve_use_wildcard_skips_internal_member);
    return SUMMARY();
}
