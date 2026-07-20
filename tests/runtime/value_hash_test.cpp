// ValueHash unit tests — structural hashing for Value.
//
// Exercises basic type hashing, consistency, cross-type normalization,
// array/dictionary/tuple hashing, and order-independence for dictionaries.

#include <memory>
#include <vector>

#include "runtime/interpreter/value.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Basic type hashing ───

static void test_hash_integer() {
    ValueHash hasher;
    const auto h = hasher(Value{42});
    // Hash should be deterministic (non-zero for a non-trivial value).
    ASSERT_TRUE(h != 0 || hasher(Value{0}) == 0);
}

static void test_hash_number() {
    ValueHash hasher;
    const auto h = hasher(Value{3.14});
    (void)h; // Just verify it doesn't crash.
}

static void test_hash_string() {
    ValueHash hasher;
    const auto h = hasher(Value{"hello"});
    ASSERT_TRUE(h != 0);
}

static void test_hash_boolean_true() {
    ValueHash hasher;
    const auto h = hasher(Value{true});
    (void)h;
}

static void test_hash_boolean_false() {
    ValueHash hasher;
    const auto h = hasher(Value{false});
    (void)h;
}

static void test_hash_none() {
    ValueHash hasher;
    const auto h = hasher(Value{NullValue{}});
    ASSERT_EQ(h, static_cast<std::size_t>(0));
}

// ─── Hash consistency ───

static void test_hash_consistency_integer() {
    ValueHash hasher;
    ASSERT_EQ(hasher(Value{42}), hasher(Value{42}));
}

static void test_hash_consistency_string() {
    ValueHash hasher;
    ASSERT_EQ(hasher(Value{"hello"}), hasher(Value{"hello"}));
}

static void test_hash_consistency_boolean() {
    ValueHash hasher;
    ASSERT_EQ(hasher(Value{true}), hasher(Value{true}));
    ASSERT_EQ(hasher(Value{false}), hasher(Value{false}));
}

static void test_hash_consistency_number() {
    ValueHash hasher;
    ASSERT_EQ(hasher(Value{2.718}), hasher(Value{2.718}));
}

// ─── Cross-type normalization (int/number) ───

static void test_hash_cross_type_int_number() {
    // int(42) and number(42.0) compare equal, so must hash the same.
    ValueHash hasher;
    ASSERT_EQ(hasher(Value{static_cast<std::int64_t>(42)}), hasher(Value{42.0}));
}

static void test_hash_cross_type_zero() {
    ValueHash hasher;
    ASSERT_EQ(hasher(Value{static_cast<std::int64_t>(0)}), hasher(Value{0.0}));
}

static void test_hash_cross_type_negative() {
    ValueHash hasher;
    ASSERT_EQ(hasher(Value{static_cast<std::int64_t>(-7)}), hasher(Value{-7.0}));
}

static void test_hash_cross_type_large_integer() {
    // 2^53 is the largest integer every double represents exactly; beyond it
    // the int64<->double mapping is lossy. integer(2^53) still compares equal
    // to number(2^53.0), so the two MUST hash identically. (Regression: the
    // hash previously branched at this boundary and gave them distinct hashes,
    // breaking ValueMap/ValueSet lookups for large integer keys.)
    ValueHash hasher;
    const std::int64_t big = std::int64_t{1} << 53;
    ASSERT_EQ(hasher(Value{big}), hasher(Value{static_cast<double>(big)}));
}

// ─── Different values produce different hashes (probabilistic) ───

static void test_hash_different_integers() {
    ValueHash hasher;
    ASSERT_NE(hasher(Value{1}), hasher(Value{2}));
}

static void test_hash_different_strings() {
    ValueHash hasher;
    ASSERT_NE(hasher(Value{"foo"}), hasher(Value{"bar"}));
}

static void test_hash_true_vs_false() {
    ValueHash hasher;
    ASSERT_NE(hasher(Value{true}), hasher(Value{false}));
}

// ─── Array hashing ───

static void test_hash_array_same_elements() {
    ValueHash hasher;

    auto arr1 = std::make_shared<ArrayValue>();
    arr1->elements->push_back(Value{1});
    arr1->elements->push_back(Value{2});
    arr1->elements->push_back(Value{3});

    auto arr2 = std::make_shared<ArrayValue>();
    arr2->elements->push_back(Value{1});
    arr2->elements->push_back(Value{2});
    arr2->elements->push_back(Value{3});

    ASSERT_EQ(hasher(Value{std::move(arr1)}), hasher(Value{std::move(arr2)}));
}

static void test_hash_array_different_elements() {
    ValueHash hasher;

    auto arr1 = std::make_shared<ArrayValue>();
    arr1->elements->push_back(Value{1});
    arr1->elements->push_back(Value{2});

    auto arr2 = std::make_shared<ArrayValue>();
    arr2->elements->push_back(Value{3});
    arr2->elements->push_back(Value{4});

    ASSERT_NE(hasher(Value{std::move(arr1)}), hasher(Value{std::move(arr2)}));
}

static void test_hash_array_different_length() {
    ValueHash hasher;

    auto arr1 = std::make_shared<ArrayValue>();
    arr1->elements->push_back(Value{1});

    auto arr2 = std::make_shared<ArrayValue>();
    arr2->elements->push_back(Value{1});
    arr2->elements->push_back(Value{2});

    ASSERT_NE(hasher(Value{std::move(arr1)}), hasher(Value{std::move(arr2)}));
}

static void test_hash_empty_array() {
    ValueHash hasher;

    auto arr1 = std::make_shared<ArrayValue>();
    auto arr2 = std::make_shared<ArrayValue>();

    ASSERT_EQ(hasher(Value{std::move(arr1)}), hasher(Value{std::move(arr2)}));
}

// ─── Dictionary hashing (order-independent) ───

static void test_hash_dictionary_order_independent() {
    ValueHash hasher;

    auto dict1 = std::make_shared<DictionaryValue>();
    dict1->set("a", Value{1});
    dict1->set("b", Value{2});

    auto dict2 = std::make_shared<DictionaryValue>();
    dict2->set("b", Value{2});
    dict2->set("a", Value{1});

    ASSERT_EQ(hasher(Value{std::move(dict1)}), hasher(Value{std::move(dict2)}));
}

static void test_hash_dictionary_same_entries() {
    ValueHash hasher;

    auto dict1 = std::make_shared<DictionaryValue>();
    dict1->set("x", Value{"hello"});
    dict1->set("y", Value{42});

    auto dict2 = std::make_shared<DictionaryValue>();
    dict2->set("x", Value{"hello"});
    dict2->set("y", Value{42});

    ASSERT_EQ(hasher(Value{std::move(dict1)}), hasher(Value{std::move(dict2)}));
}

static void test_hash_dictionary_different_values() {
    ValueHash hasher;

    auto dict1 = std::make_shared<DictionaryValue>();
    dict1->set("a", Value{1});

    auto dict2 = std::make_shared<DictionaryValue>();
    dict2->set("a", Value{2});

    ASSERT_NE(hasher(Value{std::move(dict1)}), hasher(Value{std::move(dict2)}));
}

static void test_hash_empty_dictionary() {
    ValueHash hasher;

    auto dict1 = std::make_shared<DictionaryValue>();
    auto dict2 = std::make_shared<DictionaryValue>();

    ASSERT_EQ(hasher(Value{std::move(dict1)}), hasher(Value{std::move(dict2)}));
}

// ─── Tuple hashing ───

static void test_hash_tuple_same_elements() {
    ValueHash hasher;

    auto tup1 = std::make_shared<TupleValue>();
    tup1->elements.push_back(Value{1});
    tup1->elements.push_back(Value{"two"});

    auto tup2 = std::make_shared<TupleValue>();
    tup2->elements.push_back(Value{1});
    tup2->elements.push_back(Value{"two"});

    ASSERT_EQ(hasher(Value{std::move(tup1)}), hasher(Value{std::move(tup2)}));
}

static void test_hash_tuple_different_size() {
    ValueHash hasher;

    auto tup1 = std::make_shared<TupleValue>();
    tup1->elements.push_back(Value{1});

    auto tup2 = std::make_shared<TupleValue>();
    tup2->elements.push_back(Value{1});
    tup2->elements.push_back(Value{2});

    ASSERT_NE(hasher(Value{std::move(tup1)}), hasher(Value{std::move(tup2)}));
}

static void test_hash_tuple_different_elements() {
    ValueHash hasher;

    auto tup1 = std::make_shared<TupleValue>();
    tup1->elements.push_back(Value{1});
    tup1->elements.push_back(Value{2});

    auto tup2 = std::make_shared<TupleValue>();
    tup2->elements.push_back(Value{3});
    tup2->elements.push_back(Value{4});

    ASSERT_NE(hasher(Value{std::move(tup1)}), hasher(Value{std::move(tup2)}));
}

// ─── Empty array vs empty dict (different types, different hashes) ───

static void test_hash_empty_array_vs_empty_dict() {
    ValueHash hasher;

    auto arr = std::make_shared<ArrayValue>();
    auto dict = std::make_shared<DictionaryValue>();

    ASSERT_NE(hasher(Value{std::move(arr)}), hasher(Value{std::move(dict)}));
}

// ─── Main ───

int main() {
    // Basic type hashing.
    RUN(test_hash_integer);
    RUN(test_hash_number);
    RUN(test_hash_string);
    RUN(test_hash_boolean_true);
    RUN(test_hash_boolean_false);
    RUN(test_hash_none);

    // Hash consistency.
    RUN(test_hash_consistency_integer);
    RUN(test_hash_consistency_string);
    RUN(test_hash_consistency_boolean);
    RUN(test_hash_consistency_number);

    // Cross-type normalization.
    RUN(test_hash_cross_type_int_number);
    RUN(test_hash_cross_type_zero);
    RUN(test_hash_cross_type_negative);
    RUN(test_hash_cross_type_large_integer);

    // Different values, different hashes.
    RUN(test_hash_different_integers);
    RUN(test_hash_different_strings);
    RUN(test_hash_true_vs_false);

    // Array hashing.
    RUN(test_hash_array_same_elements);
    RUN(test_hash_array_different_elements);
    RUN(test_hash_array_different_length);
    RUN(test_hash_empty_array);

    // Dictionary hashing (order-independent).
    RUN(test_hash_dictionary_order_independent);
    RUN(test_hash_dictionary_same_entries);
    RUN(test_hash_dictionary_different_values);
    RUN(test_hash_empty_dictionary);

    // Tuple hashing.
    RUN(test_hash_tuple_same_elements);
    RUN(test_hash_tuple_different_size);
    RUN(test_hash_tuple_different_elements);

    // Cross-type container distinction.
    RUN(test_hash_empty_array_vs_empty_dict);

    return SUMMARY();
}
