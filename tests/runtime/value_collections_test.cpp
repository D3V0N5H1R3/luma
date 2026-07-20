// Characterization tests for the insertion-ordered, lazily hash-indexed
// string->Value stores backing DictionaryValue and RecordValue.
//
// These pin down the observable behaviour (insertion-order preservation,
// find/set/erase semantics, lazy index build, and rebuild-after-direct-mutation)
// so the shared EntryIndex extraction can be verified as behaviour-preserving.

#include <memory>
#include <string>

#include "runtime/interpreter/value.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── DictionaryValue::set / find ───

static void test_dict_set_and_find() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    dict.set("b", Value{2});

    const auto* a = dict.find("a");
    const auto* b = dict.find("b");
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    ASSERT_EQ(a->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(b->as_integer(), static_cast<std::int64_t>(2));
}

static void test_dict_find_missing_returns_null() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    ASSERT_TRUE(dict.find("missing") == nullptr);
}

static void test_dict_set_overwrite_updates_in_place() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    dict.set("a", Value{99});

    ASSERT_EQ(dict.entries.size(), static_cast<std::size_t>(1));
    const auto* a = dict.find("a");
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->as_integer(), static_cast<std::int64_t>(99));
}

static void test_dict_preserves_insertion_order() {
    DictionaryValue dict;
    dict.set("zebra", Value{1});
    dict.set("apple", Value{2});
    dict.set("mango", Value{3});

    ASSERT_EQ(dict.entries.size(), static_cast<std::size_t>(3));
    ASSERT_EQ(dict.entries[0].first, std::string{"zebra"});
    ASSERT_EQ(dict.entries[1].first, std::string{"apple"});
    ASSERT_EQ(dict.entries[2].first, std::string{"mango"});
}

// Overwriting an existing key must not reorder or grow the entries vector.
static void test_dict_overwrite_preserves_order() {
    DictionaryValue dict;
    dict.set("x", Value{1});
    dict.set("y", Value{2});
    dict.set("z", Value{3});
    dict.set("y", Value{20}); // overwrite the middle key

    ASSERT_EQ(dict.entries.size(), static_cast<std::size_t>(3));
    ASSERT_EQ(dict.entries[1].first, std::string{"y"});
    ASSERT_EQ(dict.entries[1].second.as_integer(), static_cast<std::int64_t>(20));
}

// Overwriting an existing key *after* the lazy index has been built exercises
// the in-place update branch (map lookup hit), distinct from the linear-scan
// path taken before the index exists.
static void test_dict_overwrite_after_index_built() {
    DictionaryValue dict;
    dict.set("x", Value{1});
    dict.set("y", Value{2});
    ASSERT_TRUE(dict.find("x") != nullptr); // build the index

    dict.set("y", Value{20}); // overwrite via the built-index path

    ASSERT_EQ(dict.entries.size(), static_cast<std::size_t>(2));
    const auto* y = dict.find("y");
    ASSERT_TRUE(y != nullptr);
    ASSERT_EQ(y->as_integer(), static_cast<std::int64_t>(20));
    ASSERT_EQ(dict.entries[1].first, std::string{"y"});
}

// ─── DictionaryValue::erase ───

static void test_dict_erase_removes_key() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    dict.set("b", Value{2});
    dict.set("c", Value{3});
    dict.erase("b");

    ASSERT_TRUE(dict.find("b") == nullptr);
    ASSERT_EQ(dict.entries.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(dict.entries[0].first, std::string{"a"});
    ASSERT_EQ(dict.entries[1].first, std::string{"c"});
}

// Erase after the index was built (via find) must keep lookups consistent.
static void test_dict_erase_after_index_built() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    dict.set("b", Value{2});
    dict.set("c", Value{3});

    // Force the lazy index to build.
    ASSERT_TRUE(dict.find("a") != nullptr);

    dict.erase("a");
    ASSERT_TRUE(dict.find("a") == nullptr);

    const auto* c = dict.find("c");
    ASSERT_TRUE(c != nullptr);
    ASSERT_EQ(c->as_integer(), static_cast<std::int64_t>(3));
}

// ─── DictionaryValue: lazy index + set after build ───

static void test_dict_set_after_index_built() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    ASSERT_TRUE(dict.find("a") != nullptr); // builds the index

    dict.set("b", Value{2}); // exercises the built-index insert path
    const auto* b = dict.find("b");
    ASSERT_TRUE(b != nullptr);
    ASSERT_EQ(b->as_integer(), static_cast<std::int64_t>(2));
}

// ─── DictionaryValue: rebuild after direct mutation ───

static void test_dict_rebuild_after_direct_mutation() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    ASSERT_TRUE(dict.find("a") != nullptr); // build the index

    // Bypass set() with a direct push_back, then honour the documented
    // contract by rebuilding the index.
    dict.entries.emplace_back("b", Value{2});
    dict.rebuild_index();

    const auto* b = dict.find("b");
    ASSERT_TRUE(b != nullptr);
    ASSERT_EQ(b->as_integer(), static_cast<std::int64_t>(2));
}

// ─── DictionaryValue: non-const find yields a mutable pointer ───

static void test_dict_non_const_find_mutates() {
    DictionaryValue dict;
    dict.set("a", Value{1});

    Value* a = dict.find("a");
    ASSERT_TRUE(a != nullptr);
    *a = Value{42};

    const auto& const_dict = dict;
    const auto* a2 = const_dict.find("a");
    ASSERT_TRUE(a2 != nullptr);
    ASSERT_EQ(a2->as_integer(), static_cast<std::int64_t>(42));
}

// ─── RecordValue::find_field ───

static void test_record_find_field() {
    RecordValue rec;
    rec.type_name = "Point";
    rec.fields.emplace_back("x", Value{10});
    rec.fields.emplace_back("y", Value{20});

    const auto* x = rec.find_field("x");
    const auto* y = rec.find_field("y");
    ASSERT_TRUE(x != nullptr);
    ASSERT_TRUE(y != nullptr);
    ASSERT_EQ(x->as_integer(), static_cast<std::int64_t>(10));
    ASSERT_EQ(y->as_integer(), static_cast<std::int64_t>(20));
}

static void test_record_find_field_missing_returns_null() {
    RecordValue rec;
    rec.fields.emplace_back("x", Value{10});
    ASSERT_TRUE(rec.find_field("z") == nullptr);
}

static void test_record_preserves_field_order() {
    RecordValue rec;
    rec.fields.emplace_back("gamma", Value{1});
    rec.fields.emplace_back("alpha", Value{2});
    rec.fields.emplace_back("beta", Value{3});

    ASSERT_EQ(rec.fields.size(), static_cast<std::size_t>(3));
    ASSERT_EQ(rec.fields[0].first, std::string{"gamma"});
    ASSERT_EQ(rec.fields[1].first, std::string{"alpha"});
    ASSERT_EQ(rec.fields[2].first, std::string{"beta"});
}

static void test_record_non_const_find_field_mutates() {
    RecordValue rec;
    rec.fields.emplace_back("x", Value{10});

    Value* x = rec.find_field("x");
    ASSERT_TRUE(x != nullptr);
    *x = Value{55};

    const auto& const_rec = rec;
    const auto* x2 = const_rec.find_field("x");
    ASSERT_TRUE(x2 != nullptr);
    ASSERT_EQ(x2->as_integer(), static_cast<std::int64_t>(55));
}

// Repeated lookups after the lazy index builds must stay consistent.
static void test_record_repeated_lookup_consistent() {
    RecordValue rec;
    rec.fields.emplace_back("a", Value{1});
    rec.fields.emplace_back("b", Value{2});
    rec.fields.emplace_back("c", Value{3});

    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(rec.find_field("a") != nullptr);
        ASSERT_TRUE(rec.find_field("b") != nullptr);
        ASSERT_TRUE(rec.find_field("c") != nullptr);
        ASSERT_TRUE(rec.find_field("d") == nullptr);
    }
}

// ─── EntryIndex linear-scan / hash-index threshold (P01) ───
//
// Small containers are served by a linear scan while the lazy hash index stays
// unbuilt; once a container grows past the threshold the index is built and
// used.  These tests pin both regimes and the transition between them so the
// small-size fast path cannot silently break large-container lookups.

// A dictionary grown well past the threshold must build its index and keep
// every key findable, with a miss still returning null.
static void test_dict_large_lookup_builds_index() {
    DictionaryValue dict;
    const int count = 20; // well past linear_scan_threshold (8)
    for (int i = 0; i < count; ++i) {
        dict.set("key_" + std::to_string(i), Value{i});
    }

    for (int i = 0; i < count; ++i) {
        const auto* v = dict.find("key_" + std::to_string(i));
        ASSERT_TRUE(v != nullptr);
        ASSERT_EQ(v->as_integer(), static_cast<std::int64_t>(i));
    }
    ASSERT_TRUE(dict.find("key_missing") == nullptr);
}

// Inserting keys one at a time across the threshold, and looking up every prior
// key after each insertion, must stay consistent whether the lookup is served
// by the linear scan (small) or the freshly built hash index (large).
static void test_dict_growth_across_threshold_stays_consistent() {
    DictionaryValue dict;
    const int count = 16;
    for (int i = 0; i < count; ++i) {
        dict.set("k" + std::to_string(i), Value{i * 10});
        for (int j = 0; j <= i; ++j) {
            const auto* v = dict.find("k" + std::to_string(j));
            ASSERT_TRUE(v != nullptr);
            ASSERT_EQ(v->as_integer(), static_cast<std::int64_t>(j * 10));
        }
    }
}

// Overwriting a key after the hash index is built must update in place through
// the built-index path without disturbing later lookups.
static void test_dict_overwrite_after_index_built_large() {
    DictionaryValue dict;
    const int count = 12;
    for (int i = 0; i < count; ++i) {
        dict.set("o" + std::to_string(i), Value{i});
    }
    ASSERT_TRUE(dict.find("o5") != nullptr); // build the index

    dict.set("o5", Value{500}); // overwrite via the built-index path
    ASSERT_EQ(dict.entries.size(), static_cast<std::size_t>(count));
    const auto* v = dict.find("o5");
    ASSERT_TRUE(v != nullptr);
    ASSERT_EQ(v->as_integer(), static_cast<std::int64_t>(500));
}

// Building the index (size past the threshold) then erasing back below it must
// keep lookups correct: the still-consistent index is reused even though the
// container is now small.
static void test_dict_erase_below_threshold_after_index_built() {
    DictionaryValue dict;
    const int count = 12;
    for (int i = 0; i < count; ++i) {
        dict.set("e" + std::to_string(i), Value{i});
    }
    ASSERT_TRUE(dict.find("e0") != nullptr); // force the hash index to build

    for (int i = 0; i < count - 2; ++i) {
        dict.erase("e" + std::to_string(i));
    }

    ASSERT_EQ(dict.entries.size(), static_cast<std::size_t>(2));
    for (int i = 0; i < count - 2; ++i) {
        ASSERT_TRUE(dict.find("e" + std::to_string(i)) == nullptr);
    }
    const auto* a = dict.find("e10");
    const auto* b = dict.find("e11");
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    ASSERT_EQ(a->as_integer(), static_cast<std::int64_t>(10));
    ASSERT_EQ(b->as_integer(), static_cast<std::int64_t>(11));
}

// Records share the same EntryIndex: a record with more fields than the
// threshold must build its index and resolve every field, with a miss null.
static void test_record_large_lookup_builds_index() {
    RecordValue rec;
    rec.type_name = "Wide";
    const int count = 20;
    for (int i = 0; i < count; ++i) {
        rec.fields.emplace_back("f" + std::to_string(i), Value{i});
    }

    for (int i = 0; i < count; ++i) {
        const auto* v = rec.find_field("f" + std::to_string(i));
        ASSERT_TRUE(v != nullptr);
        ASSERT_EQ(v->as_integer(), static_cast<std::int64_t>(i));
    }
    ASSERT_TRUE(rec.find_field("f_absent") == nullptr);
}

// ─── Value::deep_copy() for dictionaries (P01) ───
//
// A dictionary deep copy builds the copy's entries directly (bulk O(n)) rather
// than replaying set() per entry, which would linear-scan the unindexed vector
// to dedup on every insert (O(n^2)). These tests pin the observable result: a
// full, independent copy with identical key order and values, a working lazy
// index on the copy, and true value-semantics isolation from the source.

// The copy must preserve every key in insertion order, every value, and expose
// a lazy index that resolves each key (and returns null for a miss) — proving
// the directly-built entries vector is index-consistent.
static void test_dict_deep_copy_preserves_order_and_values() {
    auto src = std::make_shared<DictionaryValue>();
    const int count = 50; // well past linear_scan_threshold (8)
    for (int i = 0; i < count; ++i) {
        src->set("k" + std::to_string(i), Value{i});
    }

    const Value copy = Value{src}.deep_copy();
    ASSERT_TRUE(copy.is_dictionary());
    const auto& dst = copy.as_dictionary();

    ASSERT_EQ(dst->entries.size(), static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        ASSERT_EQ(dst->entries[static_cast<std::size_t>(i)].first, "k" + std::to_string(i));
        const auto* v = dst->find("k" + std::to_string(i));
        ASSERT_TRUE(v != nullptr);
        ASSERT_EQ(v->as_integer(), static_cast<std::int64_t>(i));
    }
    ASSERT_TRUE(dst->find("k_absent") == nullptr);
}

// Mutating the copy — including a nested dictionary value — must leave the
// source untouched, proving the copy is deep rather than structurally shared.
static void test_dict_deep_copy_is_independent() {
    auto inner = std::make_shared<DictionaryValue>();
    inner->set("n", Value{1});

    auto src = std::make_shared<DictionaryValue>();
    src->set("a", Value{10});
    src->set("inner", Value{inner});

    const Value copy = Value{src}.deep_copy();
    const auto& dst = copy.as_dictionary();

    Value* a = dst->find("a");
    ASSERT_TRUE(a != nullptr);
    *a = Value{999};

    Value* nested = dst->find("inner");
    ASSERT_TRUE(nested != nullptr);
    ASSERT_TRUE(nested->is_dictionary());
    nested->as_dictionary()->set("n", Value{888});

    const auto* src_a = src->find("a");
    ASSERT_TRUE(src_a != nullptr);
    ASSERT_EQ(src_a->as_integer(), static_cast<std::int64_t>(10));

    const auto* src_inner = src->find("inner");
    ASSERT_TRUE(src_inner != nullptr);
    ASSERT_TRUE(src_inner->is_dictionary());
    const auto* src_n = src_inner->as_dictionary()->find("n");
    ASSERT_TRUE(src_n != nullptr);
    ASSERT_EQ(src_n->as_integer(), static_cast<std::int64_t>(1));
}

// ─── Main ───

int main() {
    RUN(test_dict_set_and_find);
    RUN(test_dict_find_missing_returns_null);
    RUN(test_dict_set_overwrite_updates_in_place);
    RUN(test_dict_preserves_insertion_order);
    RUN(test_dict_overwrite_preserves_order);
    RUN(test_dict_overwrite_after_index_built);
    RUN(test_dict_erase_removes_key);
    RUN(test_dict_erase_after_index_built);
    RUN(test_dict_set_after_index_built);
    RUN(test_dict_rebuild_after_direct_mutation);
    RUN(test_dict_non_const_find_mutates);

    RUN(test_record_find_field);
    RUN(test_record_find_field_missing_returns_null);
    RUN(test_record_preserves_field_order);
    RUN(test_record_non_const_find_field_mutates);
    RUN(test_record_repeated_lookup_consistent);

    RUN(test_dict_large_lookup_builds_index);
    RUN(test_dict_growth_across_threshold_stays_consistent);
    RUN(test_dict_overwrite_after_index_built_large);
    RUN(test_dict_erase_below_threshold_after_index_built);
    RUN(test_record_large_lookup_builds_index);

    RUN(test_dict_deep_copy_preserves_order_and_values);
    RUN(test_dict_deep_copy_is_independent);

    return SUMMARY();
}
