// Characterization tests for the insertion-ordered, lazily hash-indexed
// string->Value stores backing DictionaryValue and RecordValue.
//
// These pin down the observable behaviour (insertion-order preservation,
// find/set/erase semantics, lazy index build, and rebuild-after-direct-mutation)
// so the shared EntryIndex extraction can be verified as behaviour-preserving.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "runtime/interpreter/value.hpp"
#include "test_framework.hpp"

using luma::DictionaryValue;
using luma::RecordValue;
using luma::SetValue;
using luma::Value;

// ─── DictionaryValue::set / find ───

static void test_dict_set_and_find() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    dict.set("b", Value{2});

    const auto* const a = dict.find("a");
    const auto* const b = dict.find("b");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(a->as_integer(), 1);
    ASSERT_EQ(b->as_integer(), 2);
}

static void test_dict_find_missing_returns_null() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    ASSERT_EQ(dict.find("missing"), nullptr);
}

static void test_dict_set_overwrite_updates_in_place() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    dict.set("a", Value{99});

    ASSERT_EQ(dict.entries.size(), 1);
    const auto* const a = dict.find("a");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->as_integer(), 99);
}

static void test_dict_preserves_insertion_order() {
    DictionaryValue dict;
    dict.set("zebra", Value{1});
    dict.set("apple", Value{2});
    dict.set("mango", Value{3});

    ASSERT_EQ(dict.entries.size(), 3);
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

    ASSERT_EQ(dict.entries.size(), 3);
    ASSERT_EQ(dict.entries[1].first, std::string{"y"});
    ASSERT_EQ(dict.entries[1].second.as_integer(), 20);
}

// Overwriting an existing key *after* the lazy index has been built exercises
// the in-place update branch (map lookup hit), distinct from the linear-scan
// path taken before the index exists.
static void test_dict_overwrite_after_index_built() {
    DictionaryValue dict;
    dict.set("x", Value{1});
    dict.set("y", Value{2});
    ASSERT_NE(dict.find("x"), nullptr); // build the index

    dict.set("y", Value{20}); // overwrite via the built-index path

    ASSERT_EQ(dict.entries.size(), 2);
    const auto* const y = dict.find("y");
    ASSERT_NE(y, nullptr);
    ASSERT_EQ(y->as_integer(), 20);
    ASSERT_EQ(dict.entries[1].first, std::string{"y"});
}

// ─── DictionaryValue::erase ───

static void test_dict_erase_removes_key() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    dict.set("b", Value{2});
    dict.set("c", Value{3});
    dict.erase("b");

    ASSERT_EQ(dict.find("b"), nullptr);
    ASSERT_EQ(dict.entries.size(), 2);
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
    ASSERT_NE(dict.find("a"), nullptr);

    dict.erase("a");
    ASSERT_EQ(dict.find("a"), nullptr);

    const auto* const c = dict.find("c");
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(c->as_integer(), 3);
}

// ─── DictionaryValue: lazy index + set after build ───

static void test_dict_set_after_index_built() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    ASSERT_NE(dict.find("a"), nullptr); // builds the index

    dict.set("b", Value{2}); // exercises the built-index insert path
    const auto* const b = dict.find("b");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->as_integer(), 2);
}

// ─── DictionaryValue: rebuild after direct mutation ───

static void test_dict_rebuild_after_direct_mutation() {
    DictionaryValue dict;
    dict.set("a", Value{1});
    ASSERT_NE(dict.find("a"), nullptr); // build the index

    // Bypass set() with a direct push_back, then honour the documented
    // contract by rebuilding the index.
    dict.entries.emplace_back("b", Value{2});
    dict.rebuild_index();

    const auto* const b = dict.find("b");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->as_integer(), 2);
}

// ─── DictionaryValue: non-const find yields a mutable pointer ───

static void test_dict_non_const_find_mutates() {
    DictionaryValue dict;
    dict.set("a", Value{1});

    Value* const a = dict.find("a");
    ASSERT_NE(a, nullptr);
    *a = Value{42};

    const auto& const_dict = dict;
    const auto* const a2 = const_dict.find("a");
    ASSERT_NE(a2, nullptr);
    ASSERT_EQ(a2->as_integer(), 42);
}

// ─── RecordValue::find_field ───

static void test_record_find_field() {
    RecordValue rec;
    rec.type_name = "Point";
    rec.fields.emplace_back("x", Value{10});
    rec.fields.emplace_back("y", Value{20});

    const auto* const x = rec.find_field("x");
    const auto* const y = rec.find_field("y");
    ASSERT_NE(x, nullptr);
    ASSERT_NE(y, nullptr);
    ASSERT_EQ(x->as_integer(), 10);
    ASSERT_EQ(y->as_integer(), 20);
}

static void test_record_find_field_missing_returns_null() {
    RecordValue rec;
    rec.fields.emplace_back("x", Value{10});
    ASSERT_EQ(rec.find_field("z"), nullptr);
}

static void test_record_preserves_field_order() {
    RecordValue rec;
    rec.fields.emplace_back("gamma", Value{1});
    rec.fields.emplace_back("alpha", Value{2});
    rec.fields.emplace_back("beta", Value{3});

    ASSERT_EQ(rec.fields.size(), 3);
    ASSERT_EQ(rec.fields[0].first, std::string{"gamma"});
    ASSERT_EQ(rec.fields[1].first, std::string{"alpha"});
    ASSERT_EQ(rec.fields[2].first, std::string{"beta"});
}

static void test_record_non_const_find_field_mutates() {
    RecordValue rec;
    rec.fields.emplace_back("x", Value{10});

    Value* const x = rec.find_field("x");
    ASSERT_NE(x, nullptr);
    *x = Value{55};

    const auto& const_rec = rec;
    const auto* const x2 = const_rec.find_field("x");
    ASSERT_NE(x2, nullptr);
    ASSERT_EQ(x2->as_integer(), 55);
}

// Repeated lookups after the lazy index builds must stay consistent.
static void test_record_repeated_lookup_consistent() {
    RecordValue rec;
    rec.fields.emplace_back("a", Value{1});
    rec.fields.emplace_back("b", Value{2});
    rec.fields.emplace_back("c", Value{3});

    for (int i = 0; i < 3; ++i) {
        ASSERT_NE(rec.find_field("a"), nullptr);
        ASSERT_NE(rec.find_field("b"), nullptr);
        ASSERT_NE(rec.find_field("c"), nullptr);
        ASSERT_EQ(rec.find_field("d"), nullptr);
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
    constexpr int count = 20; // well past linear_scan_threshold (8)
    for (int i = 0; i < count; ++i) {
        dict.set("key_" + std::to_string(i), Value{i});
    }

    for (int i = 0; i < count; ++i) {
        const auto* const v = dict.find("key_" + std::to_string(i));
        ASSERT_NE(v, nullptr);
        ASSERT_EQ(v->as_integer(), i);
    }
    ASSERT_EQ(dict.find("key_missing"), nullptr);
}

// Inserting keys one at a time across the threshold, and looking up every prior
// key after each insertion, must stay consistent whether the lookup is served
// by the linear scan (small) or the freshly built hash index (large).
static void test_dict_growth_across_threshold_stays_consistent() {
    DictionaryValue dict;
    constexpr int count = 16;
    for (int i = 0; i < count; ++i) {
        dict.set("k" + std::to_string(i), Value{i * 10});
        for (int j = 0; j <= i; ++j) {
            const auto* const v = dict.find("k" + std::to_string(j));
            ASSERT_NE(v, nullptr);
            ASSERT_EQ(v->as_integer(), j * 10);
        }
    }
}

// Overwriting a key after the hash index is built must update in place through
// the built-index path without disturbing later lookups.
static void test_dict_overwrite_after_index_built_large() {
    DictionaryValue dict;
    constexpr int count = 12;
    for (int i = 0; i < count; ++i) {
        dict.set("o" + std::to_string(i), Value{i});
    }
    ASSERT_NE(dict.find("o5"), nullptr); // build the index

    dict.set("o5", Value{500}); // overwrite via the built-index path
    ASSERT_EQ(dict.entries.size(), count);
    const auto* const v = dict.find("o5");
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->as_integer(), 500);
}

// Building the index (size past the threshold) then erasing back below it must
// keep lookups correct: the still-consistent index is reused even though the
// container is now small.
static void test_dict_erase_below_threshold_after_index_built() {
    DictionaryValue dict;
    constexpr int count = 12;
    for (int i = 0; i < count; ++i) {
        dict.set("e" + std::to_string(i), Value{i});
    }
    ASSERT_NE(dict.find("e0"), nullptr); // force the hash index to build

    for (int i = 0; i < count - 2; ++i) {
        dict.erase("e" + std::to_string(i));
    }

    ASSERT_EQ(dict.entries.size(), 2);
    for (int i = 0; i < count - 2; ++i) {
        ASSERT_EQ(dict.find("e" + std::to_string(i)), nullptr);
    }
    const auto* const a = dict.find("e10");
    const auto* const b = dict.find("e11");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(a->as_integer(), 10);
    ASSERT_EQ(b->as_integer(), 11);
}

// Records share the same EntryIndex: a record with more fields than the
// threshold must build its index and resolve every field, with a miss null.
static void test_record_large_lookup_builds_index() {
    RecordValue rec;
    rec.type_name = "Wide";
    constexpr int count = 20;
    for (int i = 0; i < count; ++i) {
        rec.fields.emplace_back("f" + std::to_string(i), Value{i});
    }

    for (int i = 0; i < count; ++i) {
        const auto* const v = rec.find_field("f" + std::to_string(i));
        ASSERT_NE(v, nullptr);
        ASSERT_EQ(v->as_integer(), i);
    }
    ASSERT_EQ(rec.find_field("f_absent"), nullptr);
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
    const auto src = std::make_shared<DictionaryValue>();
    constexpr int count = 50; // well past linear_scan_threshold (8)
    for (int i = 0; i < count; ++i) {
        src->set("k" + std::to_string(i), Value{i});
    }

    const Value copy = Value{src}.deep_copy();
    ASSERT_TRUE(copy.is_dictionary());
    const auto& dst = copy.as_dictionary();

    ASSERT_EQ(dst->entries.size(), count);
    for (int i = 0; i < count; ++i) {
        ASSERT_EQ(dst->entries[static_cast<std::size_t>(i)].first, "k" + std::to_string(i));
        const auto* const v = dst->find("k" + std::to_string(i));
        ASSERT_NE(v, nullptr);
        ASSERT_EQ(v->as_integer(), i);
    }
    ASSERT_EQ(dst->find("k_absent"), nullptr);
}

// Mutating the copy — including a nested dictionary value — must leave the
// source untouched, proving the copy is deep rather than structurally shared.
static void test_dict_deep_copy_is_independent() {
    const auto inner = std::make_shared<DictionaryValue>();
    inner->set("n", Value{1});

    const auto src = std::make_shared<DictionaryValue>();
    src->set("a", Value{10});
    src->set("inner", Value{inner});

    const Value copy = Value{src}.deep_copy();
    const auto& dst = copy.as_dictionary();

    Value* const a = dst->find("a");
    ASSERT_NE(a, nullptr);
    *a = Value{999};

    Value* const nested = dst->find("inner");
    ASSERT_NE(nested, nullptr);
    ASSERT_TRUE(nested->is_dictionary());
    nested->as_dictionary()->set("n", Value{888});

    const auto* const src_a = src->find("a");
    ASSERT_NE(src_a, nullptr);
    ASSERT_EQ(src_a->as_integer(), 10);

    const auto* const src_inner = src->find("inner");
    ASSERT_NE(src_inner, nullptr);
    ASSERT_TRUE(src_inner->is_dictionary());
    const auto* const src_n = src_inner->as_dictionary()->find("n");
    ASSERT_NE(src_n, nullptr);
    ASSERT_EQ(src_n->as_integer(), 1);
}

// ─── Concurrent lookups on a shared container (review R1) ───
//
// Dictionaries, records, and sets are shared read-only between task threads
// (a spawned task's global snapshot is shared by its siblings).  The first
// lookup on a container larger than the linear-scan threshold builds its lazy
// hash index; that build used to write a plain `bool` and the map with no
// synchronisation, so two tasks indexing the same 9-entry global raced.  Run
// many rounds so each fresh container's first build is contended.

static void test_concurrent_lookups_on_shared_containers() {
    constexpr int k_rounds = 200;
    constexpr int k_threads = 8;
    constexpr int k_entries = 24; // Above EntryIndex::linear_scan_threshold.

    std::atomic<int> failures{0};

    for (int round = 0; round < k_rounds; ++round) {
        auto dict = std::make_shared<DictionaryValue>();
        auto record = std::make_shared<RecordValue>();
        auto set = std::make_shared<SetValue>();

        for (int i = 0; i < k_entries; ++i) {
            dict->set("k" + std::to_string(i), Value{i});
            record->fields.emplace_back("f" + std::to_string(i), Value{i});
            set->elements.emplace_back(Value{static_cast<std::int64_t>(i)});
        }

        std::latch start{k_threads};

        {
            std::vector<std::jthread> threads;
            threads.reserve(k_threads);

            for (int t = 0; t < k_threads; ++t) {
                threads.emplace_back([&, t] {
                    start.arrive_and_wait();

                    for (int i = 0; i < k_entries; ++i) {
                        const int key = (i + t) % k_entries;
                        const auto* const value =
                            std::as_const(*dict).find("k" + std::to_string(key));
                        const auto* const field =
                            std::as_const(*record).find_field("f" + std::to_string(key));

                        if (value == nullptr || !value->is_integer() ||
                            value->as_integer() != key) {
                            failures.fetch_add(1);
                        }

                        if (field == nullptr || !field->is_integer() ||
                            field->as_integer() != key) {
                            failures.fetch_add(1);
                        }

                        if (!set->contains(Value{static_cast<std::int64_t>(key)})) {
                            failures.fetch_add(1);
                        }
                    }
                });
            }
        } // jthreads join here.
    }

    ASSERT_EQ(failures.load(), 0);
}

// A copy starts with an unbuilt index (copying the map would race with a
// concurrent build of the shared source) but must still answer lookups.
static void test_copied_container_rebuilds_its_own_index() {
    DictionaryValue source;

    for (int i = 0; i < 20; ++i) {
        source.set("k" + std::to_string(i), Value{i});
    }

    ASSERT_NE(source.find("k3"), nullptr); // Builds the source's index.

    DictionaryValue copy{source};
    copy.set("extra", Value{100});

    const auto* const copied = copy.find("k17");
    ASSERT_NE(copied, nullptr);
    ASSERT_EQ(copied->as_integer(), 17);
    ASSERT_NE(copy.find("extra"), nullptr);
    ASSERT_EQ(source.find("extra"), nullptr);

    DictionaryValue moved{std::move(copy)};
    const auto* const after_move = moved.find("k5");
    ASSERT_NE(after_move, nullptr);
    ASSERT_EQ(after_move->as_integer(), 5);
}

// ─── Iterative teardown of deep values (review R2) ───
//
// Dropping the last reference to a long recursive value destroyed one level
// per native stack frame chain, so a 1,000,000-node list crashed with a stack
// overflow (0xC00000FD).  Each test builds the chain on a fresh thread (a
// 1 MiB stack on Windows) with a sentinel at the bottom and checks that the
// whole chain is freed.

namespace {

constexpr int k_deep_chain_length = 1'000'000;

// Build a chain of `length` levels on top of a sentinel, drop it, and return
// whether the sentinel was freed.  `wrap` turns the current chain into the
// next, deeper level.
template <typename Wrap> bool drop_deep_chain(Wrap wrap) {
    std::weak_ptr<luma::DecimalValue> sentinel_alive;

    {
        std::jthread worker{[&sentinel_alive, &wrap] {
            auto sentinel = std::make_shared<luma::DecimalValue>();
            sentinel_alive = sentinel;
            Value chain{std::move(sentinel)};

            for (int i = 0; i < k_deep_chain_length; ++i) {
                chain = wrap(std::move(chain), i);
            }
        }};
    }

    return sentinel_alive.expired();
}

} // namespace

static void test_dropping_deep_choice_chain_does_not_overflow() {
    ASSERT_TRUE(drop_deep_chain([](Value tail, int i) {
        auto node = std::make_shared<luma::ChoiceValue>();
        node->type_name = "IntList";
        node->variant = "Node";
        node->fields.emplace_back(static_cast<std::int64_t>(i));
        node->fields.push_back(std::move(tail));
        return Value{std::move(node)};
    }));
}

static void test_dropping_deep_array_chain_does_not_overflow() {
    ASSERT_TRUE(drop_deep_chain([](Value inner, int /*i*/) {
        auto array = std::make_shared<luma::ArrayValue>();
        array->elements->push_back(std::move(inner));
        return Value{std::move(array)};
    }));
}

static void test_dropping_deep_result_and_record_chain_does_not_overflow() {
    ASSERT_TRUE(drop_deep_chain([](Value inner, int i) {
        if (i % 2 == 0) {
            return Value{luma::ResultValue::success(std::move(inner))};
        }

        auto record = std::make_shared<RecordValue>();
        record->type_name = "Box";
        record->fields.emplace_back("value", std::move(inner));
        return Value{std::move(record)};
    }));
}

static void test_dropping_deep_closure_chain_does_not_overflow() {
    ASSERT_TRUE(drop_deep_chain([](Value inner, int /*i*/) {
        auto closure = std::make_shared<luma::FunctionValue>();
        closure->upvalues.push_back(std::move(inner));
        closure->upvalue_cells.emplace_back();
        return Value{std::move(closure)};
    }));
}

// A subtree still referenced elsewhere must survive the teardown of one owner
// and be freed (iteratively) with the last one.
static void test_shared_subtree_survives_partial_teardown() {
    std::weak_ptr<luma::DecimalValue> sentinel_alive;
    Value shared_tail;

    {
        auto sentinel = std::make_shared<luma::DecimalValue>();
        sentinel_alive = sentinel;
        Value chain{std::move(sentinel)};

        for (int i = 0; i < k_deep_chain_length; ++i) {
            auto array = std::make_shared<luma::ArrayValue>();
            array->elements->push_back(std::move(chain));
            chain = Value{std::move(array)};

            if (i == k_deep_chain_length / 2) {
                shared_tail = chain;
            }
        }
    } // The outer half is destroyed; the inner half is still owned.

    ASSERT_FALSE(sentinel_alive.expired());
    ASSERT_TRUE(shared_tail.is_array());

    shared_tail = Value{};

    ASSERT_TRUE(sentinel_alive.expired());
}

// ─── Index builds of containers keyed by containers (review follow-up) ───
//
// Building a set's index hashes and compares its elements; comparing two
// sets (or two dictionaries with more than 8 keys) looks up the other one,
// which builds *its* index.  The build used to run under a striped lock, so
// that nested build took a second stripe lock while the first was held — a
// self-deadlock (MSVC: "resource deadlock would occur") when both mapped to
// the same stripe, and a lock-order inversion between threads.  Equal-content
// duplicates guarantee that the outer build compares elements.

namespace {

Value make_int_set(std::int64_t a, std::int64_t b) {
    auto set = std::make_shared<SetValue>();
    set->elements.emplace_back(a);
    set->elements.emplace_back(b);
    return Value{std::move(set)};
}

Value make_large_dict(std::int64_t seed) {
    auto dict = std::make_shared<DictionaryValue>();

    for (std::int64_t i = 0; i < 12; ++i) { // Above the linear-scan threshold.
        dict->set("k" + std::to_string(i), Value{seed + i});
    }

    return Value{std::move(dict)};
}

// A set whose elements come in equal-content pairs (distinct objects), so
// building its index must compare elements with each other.
std::shared_ptr<SetValue> make_outer_set(bool of_dictionaries, std::int64_t pairs) {
    auto outer = std::make_shared<SetValue>();

    for (std::int64_t i = 0; i < pairs; ++i) {
        for (int copy = 0; copy < 2; ++copy) {
            outer->elements.push_back(of_dictionaries ? make_large_dict(i * 100)
                                                      : make_int_set(i, i + 100'000));
        }
    }

    return outer;
}

} // namespace

static void test_set_of_sets_index_build_does_not_deadlock() {
    const auto outer = make_outer_set(false, 400);

    ASSERT_TRUE(outer->contains(make_int_set(5, 100'005)));
    ASSERT_FALSE(outer->contains(make_int_set(5, 6)));
}

static void test_set_of_large_dictionaries_index_build_does_not_deadlock() {
    const auto outer = make_outer_set(true, 400);

    ASSERT_TRUE(outer->contains(make_large_dict(700)));
    ASSERT_FALSE(outer->contains(make_large_dict(701)));
}

static void test_concurrent_index_builds_of_nested_containers() {
    constexpr int k_rounds = 20;
    constexpr int k_threads = 8;

    std::atomic<int> failures{0};

    for (int round = 0; round < k_rounds; ++round) {
        const auto sets = make_outer_set(false, 100);
        const auto dicts = make_outer_set(true, 100);
        std::latch start{k_threads};

        {
            std::vector<std::jthread> threads;
            threads.reserve(k_threads);

            for (int t = 0; t < k_threads; ++t) {
                threads.emplace_back([&, t] {
                    start.arrive_and_wait();

                    for (std::int64_t i = t; i < 100; i += 7) {
                        if (!sets->contains(make_int_set(i, i + 100'000)) ||
                            !dicts->contains(make_large_dict(i * 100))) {
                            failures.fetch_add(1);
                        }
                    }
                });
            }
        } // jthreads join here.
    }

    ASSERT_EQ(failures.load(), 0);
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

    RUN(test_concurrent_lookups_on_shared_containers);
    RUN(test_copied_container_rebuilds_its_own_index);

    RUN(test_dropping_deep_choice_chain_does_not_overflow);
    RUN(test_dropping_deep_array_chain_does_not_overflow);
    RUN(test_dropping_deep_result_and_record_chain_does_not_overflow);
    RUN(test_dropping_deep_closure_chain_does_not_overflow);
    RUN(test_shared_subtree_survives_partial_teardown);

    RUN(test_set_of_sets_index_build_does_not_deadlock);
    RUN(test_set_of_large_dictionaries_index_build_does_not_deadlock);
    RUN(test_concurrent_index_builds_of_nested_containers);

    return SUMMARY();
}
