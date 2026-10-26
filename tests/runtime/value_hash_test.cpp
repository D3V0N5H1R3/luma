// ValueHash unit tests — structural hashing for Value.
//
// Exercises basic type hashing, consistency, cross-type normalization,
// array/dictionary/tuple hashing, and order-independence for dictionaries.

#include <cmath>
#include <cstdint>
#include <future>
#include <limits>
#include <memory>
#include <vector>

#include "runtime/concurrency/channel.hpp"
#include "runtime/concurrency/task_completion.hpp"
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

// ─── Review regressions: equality must be an equivalence relation ───

// Review #15: integer/number equality promoted the integer to double, so
// int(2^53) and int(2^53 + 1) both equalled 2^53.0 while differing from each
// other — equality was not transitive.  Comparison is now exact.
static void test_equals_integer_number_exact_beyond_2_pow_53() {
    const std::int64_t big = std::int64_t{1} << 53;
    const Value exact_int{big};
    const Value next_int{big + 1};
    const Value as_number{static_cast<double>(big)};

    ASSERT_TRUE(exact_int.equals(as_number));
    ASSERT_TRUE(as_number.equals(exact_int));
    ASSERT_FALSE(next_int.equals(as_number));
    ASSERT_FALSE(as_number.equals(next_int));
    ASSERT_FALSE(exact_int.equals(next_int));

    // Equal values still hash identically.
    ValueHash hasher;
    ASSERT_EQ(hasher(exact_int), hasher(as_number));

    // Non-integral, non-finite, and out-of-range numbers never equal an integer.
    ASSERT_FALSE(Value{std::int64_t{3}}.equals(Value{3.5}));
    ASSERT_FALSE(Value{std::int64_t{0}}.equals(Value{std::nan("")}));
    ASSERT_FALSE(
        Value{std::numeric_limits<std::int64_t>::max()}.equals(Value{9223372036854775808.0}));
    ASSERT_TRUE(
        Value{std::numeric_limits<std::int64_t>::min()}.equals(Value{-9223372036854775808.0}));
    ASSERT_TRUE(Value{std::int64_t{0}}.equals(Value{-0.0}));
}

// Review #12: functions, tasks, channels and by-reference collections compared
// unequal even to themselves, so a Set could never find them and duplicates
// piled up.  They now use identity equality, consistent with ValueHash.
static void test_equals_identity_types_are_reflexive() {
    const Value function{std::make_shared<FunctionValue>()};
    const Value other_function{std::make_shared<FunctionValue>()};
    ASSERT_TRUE(function.equals(function));
    ASSERT_FALSE(function.equals(other_function));

    const Value native{std::make_shared<NativeFunctionValue>()};
    ASSERT_TRUE(native.equals(native));
    ASSERT_FALSE(native.equals(function));

    // Channels: a deep copy shares the channel, so it is the same channel.
    const Value channel{std::make_shared<ChannelValue>(std::make_shared<Channel>())};
    const Value other_channel{std::make_shared<ChannelValue>(std::make_shared<Channel>())};
    ASSERT_TRUE(channel.equals(channel));
    ASSERT_TRUE(channel.equals(channel.deep_copy()));
    ASSERT_FALSE(channel.equals(other_channel));

    // Tasks: a deep copy clones the handle but is still the same task.
    std::promise<Value> promise;
    const Value task{std::make_shared<TaskValue>(promise.get_future().share(), nullptr,
                                                 std::make_shared<TaskCompletion>())};
    ASSERT_TRUE(task.equals(task));
    ASSERT_TRUE(task.equals(task.deep_copy()));
    promise.set_value(Value{1});

    // By-reference collections: identity, never structural.
    const Value store{std::make_shared<KeyValueStoreValue>()};
    const Value other_store{std::make_shared<KeyValueStoreValue>()};
    ASSERT_TRUE(store.equals(store));
    ASSERT_FALSE(store.equals(other_store));

    // Usable as set elements: inserting the same value twice keeps one entry.
    ValueSet set;
    set.insert(function);
    set.insert(function);
    set.insert(channel);
    set.insert(channel);
    set.insert(other_function);
    ASSERT_EQ(set.size(), static_cast<std::size_t>(3));
    ASSERT_TRUE(set.contains(function));
}

// Code review (function identity): deep_copy builds a new FunctionValue, so a
// value-semantics copy (a `mutable` binding, a spawned task's argument)
// compared unequal to the function it was copied from, while an immutable
// alias compared equal.  The identity now survives deep_copy — including a
// copy of a closure whose mutable upvalue cell is cloned — and stays
// consistent with ValueHash.
static void test_function_equals_its_deep_copy() {
    auto closure = std::make_shared<FunctionValue>();
    closure->upvalue_cells.push_back(std::make_shared<Value>(std::int64_t{7}));
    const Value function{closure};
    const Value copy = function.deep_copy();

    ASSERT_TRUE(copy.as_function().get() != function.as_function().get());
    ASSERT_TRUE(function.equals(copy));
    ASSERT_TRUE(copy.equals(function));
    ASSERT_TRUE(copy.equals(copy.deep_copy()));
    ASSERT_EQ(ValueHash{}(function), ValueHash{}(copy));

    // A separately created closure — even of the same code — is a different
    // function, and so is a copy of it.
    const Value other{std::make_shared<FunctionValue>()};
    ASSERT_FALSE(function.equals(other));
    ASSERT_FALSE(copy.equals(other.deep_copy()));

    ValueSet set;
    set.insert(function);
    set.insert(copy);
    ASSERT_EQ(set.size(), static_cast<std::size_t>(1));
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

    // Equality as an equivalence relation (review regressions).
    RUN(test_equals_integer_number_exact_beyond_2_pow_53);
    RUN(test_equals_identity_types_are_reflexive);
    RUN(test_function_equals_its_deep_copy);

    return SUMMARY();
}
