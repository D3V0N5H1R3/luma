// Unit tests for StringInterner and InternedString.

#include "runtime/compiler/string_interner.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Basic intern and resolve ───

static void test_intern_and_resolve() {
    StringInterner interner;
    const auto a = interner.intern("hello");
    ASSERT_TRUE(a.valid());
    ASSERT_EQ(interner.resolve(a), std::string_view{"hello"});
}

// ─── Idempotency: interning the same string twice returns the same handle ───

static void test_intern_idempotent() {
    StringInterner interner;
    const auto a = interner.intern("world");
    const auto b = interner.intern("world");
    ASSERT_TRUE(a == b);
    ASSERT_EQ(a.id(), b.id());
}

// ─── Different strings get distinct handles ───

static void test_different_strings_distinct_ids() {
    StringInterner interner;
    const auto a = interner.intern("alpha");
    const auto b = interner.intern("beta");
    ASSERT_TRUE(a != b);
}

// ─── Size tracking ───

static void test_size_tracking() {
    StringInterner interner;
    ASSERT_EQ(interner.size(), 0U);

    (void)interner.intern("x");
    ASSERT_EQ(interner.size(), 1U);

    (void)interner.intern("y");
    ASSERT_EQ(interner.size(), 2U);

    // Re-interning the same string must not grow the pool.
    (void)interner.intern("x");
    ASSERT_EQ(interner.size(), 2U);
}

// ─── Intern from string_view (no allocation for known strings) ───

static void test_intern_from_string_view() {
    StringInterner interner;
    const std::string s{"identifier"};
    const std::string_view sv{s};
    const auto a = interner.intern(sv);
    const auto b = interner.intern("identifier");
    ASSERT_TRUE(a == b);
    ASSERT_EQ(interner.resolve(a), std::string_view{"identifier"});
}

// ─── Default-constructed InternedString is invalid ───

static void test_default_invalid() {
    const InternedString s;
    ASSERT_FALSE(s.valid());
    ASSERT_EQ(s.id(), 0U);
}

// ─── Equality and inequality operators ───

static void test_equality_operators() {
    StringInterner interner;
    const auto a = interner.intern("foo");
    const auto b = interner.intern("foo");
    const auto c = interner.intern("bar");

    ASSERT_TRUE(a == b);
    ASSERT_FALSE(a != b);
    ASSERT_FALSE(a == c);
    ASSERT_TRUE(a != c);
}

// ─── Many strings interned in sequence ───

static void test_many_strings() {
    StringInterner interner;
    constexpr int n = 100;

    for (int i = 0; i < n; ++i) {
        (void)interner.intern("str_" + std::to_string(i));
    }

    ASSERT_EQ(interner.size(), static_cast<std::size_t>(n));

    // All re-interns must hit existing entries.
    for (int i = 0; i < n; ++i) {
        const auto handle = interner.intern("str_" + std::to_string(i));
        ASSERT_TRUE(handle.valid());
        ASSERT_EQ(interner.resolve(handle), "str_" + std::to_string(i));
    }

    ASSERT_EQ(interner.size(), static_cast<std::size_t>(n));
}

// ─── InternedString can be used as unordered_map key ───

static void test_as_map_key() {
    StringInterner interner;
    std::unordered_map<InternedString, int> counts;

    const auto key = interner.intern("result");
    counts[key] = 42;

    ASSERT_EQ(counts[interner.intern("result")], 42);
}

// ─── Empty string can be interned ───

static void test_empty_string() {
    StringInterner interner;
    const auto empty = interner.intern("");
    ASSERT_TRUE(empty.valid());
    ASSERT_EQ(interner.resolve(empty), std::string_view{""});
    ASSERT_EQ(interner.size(), 1U);
}

// ─── Runner ───

int main() {
    RUN(test_intern_and_resolve);
    RUN(test_intern_idempotent);
    RUN(test_different_strings_distinct_ids);
    RUN(test_size_tracking);
    RUN(test_intern_from_string_view);
    RUN(test_default_invalid);
    RUN(test_equality_operators);
    RUN(test_many_strings);
    RUN(test_as_map_key);
    RUN(test_empty_string);
    return SUMMARY();
}
