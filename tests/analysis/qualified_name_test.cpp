// Unit tests for shared/symbols/qualified_name.hpp.
//
// Characterises the qualified-name helpers that centralise the "Module.member"
// build/split logic used across the compiler, type checker, and language
// server. The edge cases (no dot, trailing dot, leading dot, multi-dot, empty)
// are pinned here because the migrated call sites rely on the precise
// first-vs-last-dot semantics of each helper.

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "symbols/qualified_name.hpp"
#include "test_framework.hpp"

using namespace luma;

// ═══════════════════════════════════════════════════════════
// make_qualified
// ═══════════════════════════════════════════════════════════

static void test_make_qualified_basic() {
    ASSERT_EQ(make_qualified("Math", "floor"), std::string{"Math.floor"});
}

static void test_make_qualified_empty_namespace() {
    // No namespace → the bare member, with no leading dot.
    ASSERT_EQ(make_qualified("", "floor"), std::string{"floor"});
}

static void test_make_qualified_empty_member() {
    ASSERT_EQ(make_qualified("Math", ""), std::string{"Math."});
}

static void test_make_qualified_nested() {
    ASSERT_EQ(make_qualified("A.B", "C"), std::string{"A.B.C"});
}

// ═══════════════════════════════════════════════════════════
// is_qualified_name
// ═══════════════════════════════════════════════════════════

static void test_is_qualified_name_true() {
    ASSERT_TRUE(is_qualified_name("Math.floor"));
    ASSERT_TRUE(is_qualified_name("A.B.C"));
    ASSERT_TRUE(is_qualified_name("Math."));  // trailing dot still counts
    ASSERT_TRUE(is_qualified_name(".floor")); // leading dot still counts
}

static void test_is_qualified_name_false() {
    ASSERT_FALSE(is_qualified_name("floor"));
    ASSERT_FALSE(is_qualified_name(""));
}

// ═══════════════════════════════════════════════════════════
// qualified_module — head before the FIRST dot
// ═══════════════════════════════════════════════════════════

static void test_qualified_module_basic() {
    ASSERT_EQ(qualified_module("Math.floor"), std::string_view{"Math"});
}

static void test_qualified_module_no_dot() {
    // Unqualified → the whole name.
    ASSERT_EQ(qualified_module("floor"), std::string_view{"floor"});
    ASSERT_EQ(qualified_module(""), std::string_view{""});
}

static void test_qualified_module_multi_dot() {
    // First dot is the split point: "A.B.C" → "A".
    ASSERT_EQ(qualified_module("A.B.C"), std::string_view{"A"});
}

static void test_qualified_module_leading_dot() {
    // Leading dot → empty head.
    ASSERT_EQ(qualified_module(".floor"), std::string_view{""});
}

static void test_qualified_module_trailing_dot() {
    ASSERT_EQ(qualified_module("Math."), std::string_view{"Math"});
}

// ═══════════════════════════════════════════════════════════
// qualified_member — tail after the LAST dot
// ═══════════════════════════════════════════════════════════

static void test_qualified_member_basic() {
    ASSERT_EQ(qualified_member("Math.floor"), std::string_view{"floor"});
}

static void test_qualified_member_no_dot() {
    // Unqualified → the whole name.
    ASSERT_EQ(qualified_member("floor"), std::string_view{"floor"});
    ASSERT_EQ(qualified_member(""), std::string_view{""});
}

static void test_qualified_member_multi_dot() {
    // Last dot is the split point: "A.B.C" → "C".
    ASSERT_EQ(qualified_member("A.B.C"), std::string_view{"C"});
}

static void test_qualified_member_trailing_dot() {
    // Trailing dot → empty tail.
    ASSERT_EQ(qualified_member("Math."), std::string_view{""});
}

static void test_qualified_member_leading_dot() {
    ASSERT_EQ(qualified_member(".floor"), std::string_view{"floor"});
}

// ═══════════════════════════════════════════════════════════
// split_module — first-dot split, nullopt when unqualified
// ═══════════════════════════════════════════════════════════

static void test_split_module_basic() {
    const auto split = split_module("Math.floor");
    ASSERT_TRUE(split.has_value());
    ASSERT_EQ(split->first, std::string_view{"Math"});
    ASSERT_EQ(split->second, std::string_view{"floor"});
}

static void test_split_module_no_dot() {
    ASSERT_FALSE(split_module("floor").has_value());
    ASSERT_FALSE(split_module("").has_value());
}

static void test_split_module_multi_dot() {
    // Split at the FIRST dot: remainder keeps the rest verbatim.
    const auto split = split_module("A.B.C");
    ASSERT_TRUE(split.has_value());
    ASSERT_EQ(split->first, std::string_view{"A"});
    ASSERT_EQ(split->second, std::string_view{"B.C"});
}

static void test_split_module_trailing_dot() {
    const auto split = split_module("Math.");
    ASSERT_TRUE(split.has_value());
    ASSERT_EQ(split->first, std::string_view{"Math"});
    ASSERT_EQ(split->second, std::string_view{""});
}

static void test_split_module_leading_dot() {
    const auto split = split_module(".floor");
    ASSERT_TRUE(split.has_value());
    ASSERT_EQ(split->first, std::string_view{""});
    ASSERT_EQ(split->second, std::string_view{"floor"});
}

// The returned views alias the argument's storage without allocating.
static void test_split_module_views_alias_argument() {
    const std::string name = "Module.member";
    const auto split = split_module(name);
    ASSERT_TRUE(split.has_value());
    ASSERT_EQ(split->first.data(), name.data());
    ASSERT_EQ(split->second.data(), name.data() + 7);
}

// ═══════════════════════════════════════════════════════════
// Round-trip: make_qualified ∘ split is identity on qualified names
// ═══════════════════════════════════════════════════════════

static void test_round_trip_first_dot() {
    const std::string original = "Math.floor";
    const auto split = split_module(original);
    ASSERT_TRUE(split.has_value());
    ASSERT_EQ(make_qualified(split->first, split->second), original);
}

// ═══════════════════════════════════════════════════════════
// QualifiedName::parse — last-dot split into owned halves
// ═══════════════════════════════════════════════════════════

static void test_parse_qualified() {
    const auto qn = QualifiedName::parse("Math.floor");
    ASSERT_EQ(qn.namespace_part, std::string{"Math"});
    ASSERT_EQ(qn.member_part, std::string{"floor"});
}

static void test_parse_unqualified() {
    const auto qn = QualifiedName::parse("floor");
    ASSERT_EQ(qn.namespace_part, std::string{""});
    ASSERT_EQ(qn.member_part, std::string{"floor"});
}

static void test_parse_nested_uses_last_dot() {
    // parse splits on the LAST dot (unlike split_module).
    const auto qn = QualifiedName::parse("A.B.C");
    ASSERT_EQ(qn.namespace_part, std::string{"A.B"});
    ASSERT_EQ(qn.member_part, std::string{"C"});
}

static void test_parse_trailing_dot() {
    const auto qn = QualifiedName::parse("Math.");
    ASSERT_EQ(qn.namespace_part, std::string{"Math"});
    ASSERT_EQ(qn.member_part, std::string{""});
}

int main() {
    using namespace luma::test;
    print_suite_header("qualified_name");

    RUN(test_make_qualified_basic);
    RUN(test_make_qualified_empty_namespace);
    RUN(test_make_qualified_empty_member);
    RUN(test_make_qualified_nested);

    RUN(test_is_qualified_name_true);
    RUN(test_is_qualified_name_false);

    RUN(test_qualified_module_basic);
    RUN(test_qualified_module_no_dot);
    RUN(test_qualified_module_multi_dot);
    RUN(test_qualified_module_leading_dot);
    RUN(test_qualified_module_trailing_dot);

    RUN(test_qualified_member_basic);
    RUN(test_qualified_member_no_dot);
    RUN(test_qualified_member_multi_dot);
    RUN(test_qualified_member_trailing_dot);
    RUN(test_qualified_member_leading_dot);

    RUN(test_split_module_basic);
    RUN(test_split_module_no_dot);
    RUN(test_split_module_multi_dot);
    RUN(test_split_module_trailing_dot);
    RUN(test_split_module_leading_dot);
    RUN(test_split_module_views_alias_argument);

    RUN(test_round_trip_first_dot);

    RUN(test_parse_qualified);
    RUN(test_parse_unqualified);
    RUN(test_parse_nested_uses_last_dot);
    RUN(test_parse_trailing_dot);

    return SUMMARY();
}
