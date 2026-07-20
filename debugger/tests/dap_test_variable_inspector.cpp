// DAP variable inspector tests — ref allocation, frame registration, generations.

#include <set>
#include <string>
#include <thread>
#include <vector>

#include "dap_session_types.hpp"
#include "dap_types.hpp"
#include "json/json.hpp"
#include "runtime/interpreter/value.hpp"
#include "test_framework.hpp"
#include "variable_inspector.hpp"

using namespace luma::dap;
using luma::json::JsonValue;

namespace {

// ─── ValueRef shared_ptr safety ────────────────────────────────────

void test_value_ref_shared_ptr() {
    // ValueRef should use shared_ptr for memory safety.
    auto val = std::make_shared<luma::Value>(static_cast<std::int64_t>(42));
    ValueRef ref{val};

    ASSERT_NE(ref.value, nullptr);
    ASSERT_EQ(ref.value.use_count(), 2); // val + ref.value
}

// ─── Variable inspector ref allocation ─────────────────────────────

void test_variable_inspector_alloc_ref_unique() {
    VariableInspector inspector;
    auto ref1 = inspector.alloc_ref(ScopeRef{.frame_id = 1, .scope_type = ScopeType::Local});
    auto ref2 = inspector.alloc_ref(ScopeRef{.frame_id = 2, .scope_type = ScopeType::Global});
    ASSERT_NE(ref1, ref2);
}

void test_variable_inspector_register_frame() {
    VariableInspector inspector;
    auto frame_id = inspector.register_frame(1, 0, nullptr);
    auto mapping = inspector.resolve_frame(frame_id);
    ASSERT_TRUE(mapping.has_value());
    ASSERT_EQ(mapping->thread_id, 1);
    ASSERT_EQ(mapping->frame_index, 0);
    ASSERT_TRUE(mapping->vm == nullptr);
}

void test_variable_inspector_resolve_frame_invalid() {
    VariableInspector inspector;
    auto mapping = inspector.resolve_frame(9999);
    ASSERT_FALSE(mapping.has_value());
}

void test_variable_inspector_invalidate_refs() {
    VariableInspector inspector;
    (void)inspector.alloc_ref(ScopeRef{.frame_id = 1, .scope_type = ScopeType::Local});
    inspector.invalidate_refs();
    // After invalidation, new refs should still be allocatable.
    auto ref = inspector.alloc_ref(ScopeRef{.frame_id = 2, .scope_type = ScopeType::Global});
    ASSERT_TRUE(ref > 0);
}

void test_variable_inspector_purge_on_generation_interval() {
    VariableInspector inspector;
    inspector.set_purge_generation_interval(3);

    // Allocate refs, then advance generations without purging.
    (void)inspector.alloc_ref(ScopeRef{.frame_id = 1, .scope_type = ScopeType::Local});
    auto ref2 = inspector.alloc_ref(ScopeRef{.frame_id = 2, .scope_type = ScopeType::Global});

    // First two invalidations should NOT purge (interval is 3).
    inspector.invalidate_refs();
    inspector.invalidate_refs();

    // Stale refs should still resolve to nothing (generation mismatch),
    // but new allocations should work and get increasing IDs (no ID reset).
    auto ref3 = inspector.alloc_ref(ScopeRef{.frame_id = 3, .scope_type = ScopeType::Local});
    ASSERT_TRUE(ref3 > ref2);

    // Third invalidation triggers purge — stale entries are removed.
    inspector.invalidate_refs();

    // New allocation after purge should still work.
    auto ref4 = inspector.alloc_ref(ScopeRef{.frame_id = 4, .scope_type = ScopeType::Local});
    ASSERT_TRUE(ref4 > 0);
}

void test_variable_inspector_purge_on_entry_threshold() {
    VariableInspector inspector;
    inspector.set_purge_entry_threshold(5);

    // Allocate 4 refs, then invalidate so they become stale.
    for (int i = 0; i < 4; ++i) {
        (void)inspector.alloc_ref(ScopeRef{.frame_id = i, .scope_type = ScopeType::Local});
    }
    inspector.invalidate_refs();

    // Allocate more refs — the 5th total entry should trigger a purge
    // that removes the 4 stale entries from the previous generation.
    auto ref_a = inspector.alloc_ref(ScopeRef{.frame_id = 10, .scope_type = ScopeType::Local});
    ASSERT_TRUE(ref_a > 0);

    // After purge, further allocations should still succeed.
    auto ref_b = inspector.alloc_ref(ScopeRef{.frame_id = 11, .scope_type = ScopeType::Global});
    ASSERT_TRUE(ref_b > ref_a);
}

void test_variable_inspector_stale_ref_not_resolved() {
    VariableInspector inspector;
    auto frame_id = inspector.register_frame(1, 0, nullptr);

    // Frame should resolve before invalidation.
    ASSERT_TRUE(inspector.resolve_frame(frame_id).has_value());

    // After invalidation (generation bump), old frame is stale.
    inspector.invalidate_refs();
    ASSERT_FALSE(inspector.resolve_frame(frame_id).has_value());
}

void test_variable_inspector_generation_counter_increments() {
    VariableInspector inspector;

    auto frame1 = inspector.register_frame(1, 0, nullptr);
    ASSERT_TRUE(inspector.resolve_frame(frame1).has_value());

    inspector.invalidate_refs();
    ASSERT_FALSE(inspector.resolve_frame(frame1).has_value());

    auto frame2 = inspector.register_frame(2, 0, nullptr);
    ASSERT_TRUE(inspector.resolve_frame(frame2).has_value());
    ASSERT_FALSE(inspector.resolve_frame(frame1).has_value());
}

void test_variable_inspector_multiple_invalidation_cycles() {
    VariableInspector inspector;

    for (int cycle = 0; cycle < 5; ++cycle) {
        auto frame = inspector.register_frame(cycle, 0, nullptr);
        ASSERT_TRUE(inspector.resolve_frame(frame).has_value());
        inspector.invalidate_refs();
        ASSERT_FALSE(inspector.resolve_frame(frame).has_value());
    }

    // After 5 cycles, new registrations should still work.
    auto final_frame = inspector.register_frame(99, 0, nullptr);
    ASSERT_TRUE(inspector.resolve_frame(final_frame).has_value());
}

void test_variable_inspector_mixed_scope_and_value_refs() {
    VariableInspector inspector;

    auto scope_ref = inspector.alloc_ref(ScopeRef{.frame_id = 1, .scope_type = ScopeType::Local});
    auto value_ref =
        inspector.alloc_ref(ValueRef{std::make_shared<luma::Value>(static_cast<std::int64_t>(42))});

    // Both should have unique IDs.
    ASSERT_NE(scope_ref, value_ref);
    ASSERT_TRUE(scope_ref > 0);
    ASSERT_TRUE(value_ref > 0);
}

void test_variable_inspector_stale_scope_ref_after_invalidation() {
    VariableInspector inspector;
    auto ref1 = inspector.alloc_ref(ScopeRef{.frame_id = 1, .scope_type = ScopeType::Local});
    auto ref2 = inspector.alloc_ref(ScopeRef{.frame_id = 2, .scope_type = ScopeType::Global});

    inspector.invalidate_refs();

    // Both refs are from prior generation — allocating new refs should
    // produce IDs greater than the old ones (no ID reset occurred).
    auto ref3 = inspector.alloc_ref(ScopeRef{.frame_id = 3, .scope_type = ScopeType::Closure});
    ASSERT_TRUE(ref3 > ref1);
    ASSERT_TRUE(ref3 > ref2);
}

void test_variable_inspector_frame_and_ref_independent_ids() {
    VariableInspector inspector;

    // Frame IDs and ref IDs use separate counters.
    auto frame_id = inspector.register_frame(1, 0, nullptr);
    auto ref_id =
        inspector.alloc_ref(ScopeRef{.frame_id = frame_id, .scope_type = ScopeType::Local});

    // Both should be positive — they may or may not be equal since they
    // use independent counters, but both should be valid.
    ASSERT_TRUE(frame_id > 0);
    ASSERT_TRUE(ref_id > 0);

    // Frame should resolve correctly.
    auto mapping = inspector.resolve_frame(frame_id);
    ASSERT_TRUE(mapping.has_value());
    ASSERT_EQ(mapping->thread_id, 1);
    ASSERT_EQ(mapping->frame_index, 0);
}

void test_variable_inspector_generational_entry_is_stale() {
    GenerationalEntry<int> entry{.value = 42, .generation = 3};

    ASSERT_FALSE(entry.is_stale(3)); // Same generation.
    ASSERT_TRUE(entry.is_stale(4));  // Newer generation.
    ASSERT_TRUE(entry.is_stale(5));  // Even newer.
}

// ─── Concurrent ref allocation ─────────────────────────────────────

void test_variable_inspector_concurrent_alloc_ref() {
    VariableInspector inspector;
    constexpr int num_threads = 10;
    constexpr int allocs_per_thread = 50;
    std::vector<std::thread> threads;
    std::vector<std::vector<int>> results(num_threads);

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&inspector, &results, i]() {
            for (int j = 0; j < allocs_per_thread; ++j) {
                auto ref = inspector.alloc_ref(ScopeRef{.frame_id = i * allocs_per_thread + j,
                                                        .scope_type = ScopeType::Local});
                results[i].push_back(ref);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Collect all IDs and verify no duplicates.
    std::set<int> all_ids;
    for (const auto& thread_results : results) {
        for (int id : thread_results) {
            ASSERT_TRUE(id > 0);
            auto [_, inserted] = all_ids.insert(id);
            ASSERT_TRUE(inserted); // No duplicate IDs.
        }
    }

    // Total count should match expected.
    ASSERT_EQ(all_ids.size(), static_cast<std::size_t>(num_threads * allocs_per_thread));
}

// ─── parse_value tests ─────────────────────────────────────────────

void test_parse_value_boolean_true() {
    auto val = VariableInspector::parse_value("true");
    ASSERT_TRUE(val.is_bool());
    ASSERT_TRUE(val.as_bool());
}

void test_parse_value_boolean_false() {
    auto val = VariableInspector::parse_value("false");
    ASSERT_TRUE(val.is_bool());
    ASSERT_FALSE(val.as_bool());
}

void test_parse_value_integer() {
    auto val = VariableInspector::parse_value("42");
    ASSERT_TRUE(val.is_integer());
    ASSERT_EQ(val.as_integer(), 42);
}

void test_parse_value_negative_integer() {
    auto val = VariableInspector::parse_value("-7");
    ASSERT_TRUE(val.is_integer());
    ASSERT_EQ(val.as_integer(), -7);
}

void test_parse_value_double() {
    auto val = VariableInspector::parse_value("3.14");
    ASSERT_TRUE(val.is_number());
}

void test_parse_value_quoted_string() {
    auto val = VariableInspector::parse_value("\"hello\"");
    ASSERT_TRUE(val.is_string());
    ASSERT_EQ(val.as_string(), "hello");
}

void test_parse_value_bare_string() {
    auto val = VariableInspector::parse_value("xyz");
    ASSERT_TRUE(val.is_string());
    ASSERT_EQ(val.as_string(), "xyz");
}

// ─── parse_value_typed (type-aware setVariable coercion) ───────────

void test_parse_value_typed_integer_accepts_integer() {
    const luma::Value current{static_cast<std::int64_t>(0)};
    auto val = VariableInspector::parse_value_typed("42", current);
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->is_integer());
    ASSERT_EQ(val->as_integer(), 42);
}

void test_parse_value_typed_integer_rejects_decimal() {
    const luma::Value current{static_cast<std::int64_t>(0)};
    auto val = VariableInspector::parse_value_typed("2.5", current);
    ASSERT_FALSE(val.has_value());
}

void test_parse_value_typed_integer_rejects_text() {
    const luma::Value current{static_cast<std::int64_t>(0)};
    auto val = VariableInspector::parse_value_typed("abc", current);
    ASSERT_FALSE(val.has_value());
}

void test_parse_value_typed_number_accepts_decimal() {
    const luma::Value current{0.0};
    auto val = VariableInspector::parse_value_typed("2.5", current);
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->is_number());
}

void test_parse_value_typed_number_accepts_integer_literal() {
    const luma::Value current{0.0};
    auto val = VariableInspector::parse_value_typed("5", current);
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->is_number());
}

void test_parse_value_typed_number_rejects_text() {
    const luma::Value current{0.0};
    auto val = VariableInspector::parse_value_typed("abc", current);
    ASSERT_FALSE(val.has_value());
}

void test_parse_value_typed_boolean_accepts_bool() {
    const luma::Value current{true};
    auto val = VariableInspector::parse_value_typed("false", current);
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->is_bool());
    ASSERT_FALSE(val->as_bool());
}

void test_parse_value_typed_boolean_rejects_number() {
    const luma::Value current{true};
    auto val = VariableInspector::parse_value_typed("1", current);
    ASSERT_FALSE(val.has_value());
}

void test_parse_value_typed_string_accepts_any_text() {
    const luma::Value current{std::string{"orig"}};
    auto val = VariableInspector::parse_value_typed("99", current);
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->is_string());
    ASSERT_EQ(val->as_string(), "99");
}

void test_parse_value_typed_string_strips_quotes() {
    const luma::Value current{std::string{"orig"}};
    auto val = VariableInspector::parse_value_typed("\"hi\"", current);
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(val->is_string());
    ASSERT_EQ(val->as_string(), "hi");
}

// ─── set_variable stale reference ─────────────────────────────────

void test_set_variable_stale_reference() {
    VariableInspector inspector;
    // Reference 999 doesn't exist.
    auto resolver = [](int) -> std::shared_ptr<ThreadState> {
        return nullptr;
    };
    auto result = inspector.set_variable(999, "x", "42", resolver);
    ASSERT_EQ(result.type, "error");
}

// ─── Scope enumeration: names, ordering, hints, and reference mapping ──

void test_get_scopes_names_order_and_refs() {
    VariableInspector inspector;
    // No live VM: resolve_frame/resolve_vm yield null, so the optional Closure
    // scope is omitted and only the always-present Local + Global remain.
    auto resolver = [](int) -> std::shared_ptr<ThreadState> {
        return nullptr;
    };

    auto scopes = inspector.get_scopes(42, resolver);

    ASSERT_EQ(scopes.size(), static_cast<std::size_t>(2));

    // Local comes first, is cheap, and hints "locals".
    ASSERT_EQ(scopes[0].name, std::string("Local"));
    ASSERT_FALSE(scopes[0].expensive);
    ASSERT_EQ(scopes[0].presentation_hint, std::string("locals"));

    // Global comes last, is expensive, and hints "globals".
    ASSERT_EQ(scopes[1].name, std::string("Global"));
    ASSERT_TRUE(scopes[1].expensive);
    ASSERT_EQ(scopes[1].presentation_hint, std::string("globals"));

    // Each scope is addressable through a distinct, non-zero reference.
    ASSERT_TRUE(scopes[0].variables_reference != 0);
    ASSERT_TRUE(scopes[1].variables_reference != 0);
    ASSERT_NE(scopes[0].variables_reference, scopes[1].variables_reference);
}

// ─── Stress test: rapid reference allocation ──────────────────────

void test_variable_inspector_stress_alloc() {
    VariableInspector inspector;

    // Allocate 10000 references rapidly.
    std::set<int> ids;
    for (int i = 0; i < 10000; ++i) {
        auto val = std::make_shared<luma::Value>(static_cast<std::int64_t>(i));
        int id = inspector.alloc_ref(ValueRef{std::move(val), 0});
        ids.insert(id);
    }

    // All IDs should be unique.
    ASSERT_EQ(static_cast<int>(ids.size()), 10000);
}

// ─── Max depth limiting ───────────────────────────────────────────

void test_variable_inspector_max_depth_respected() {
    VariableInspector inspector;

    // Create an array value (structured type).
    auto arr = std::make_shared<luma::ArrayValue>();
    arr->elements->push_back(luma::Value{static_cast<std::int64_t>(1)});
    luma::Value nested_array{arr};

    // At depth 31 (< 32), should get a reference.
    auto var_shallow = inspector.make_variable("arr", nested_array, false, 31);
    ASSERT_TRUE(var_shallow.variables_reference != 0);

    // At depth 32 (== max), should NOT get a reference.
    auto var_deep = inspector.make_variable("arr", nested_array, false, 32);
    ASSERT_EQ(var_deep.variables_reference, 0);
}

// ─── Array paging (start/count) ───────────────────────────────────

void test_get_variables_array_paging() {
    VariableInspector inspector;

    // Build an array [0, 1, 2, …, 19].
    auto arr = std::make_shared<luma::ArrayValue>();
    for (std::int64_t i = 0; i < 20; ++i) {
        arr->elements->push_back(luma::Value{i});
    }
    luma::Value array_value{arr};

    auto var = inspector.make_variable("arr", array_value, false, 0);
    ASSERT_TRUE(var.variables_reference != 0);

    const VariableInspector::ThreadResolver resolver{};

    // A middle page (start=10, count=5) must return exactly elements [10]..[14].
    // A prior double-paging bug sliced the already-paged result a second time,
    // so any start > 0 returned an empty list.
    auto page = inspector.get_variables(var.variables_reference, 10, 5, "", resolver);
    ASSERT_EQ(page.size(), 5U);
    ASSERT_EQ(page[0].name, "[10]");
    ASSERT_EQ(page[4].name, "[14]");

    // count == 0 means "from start to the end".
    auto tail = inspector.get_variables(var.variables_reference, 15, 0, "", resolver);
    ASSERT_EQ(tail.size(), 5U);
    ASSERT_EQ(tail[0].name, "[15]");
    ASSERT_EQ(tail[4].name, "[19]");

    // The first page (start == 0) still works.
    auto head = inspector.get_variables(var.variables_reference, 0, 3, "", resolver);
    ASSERT_EQ(head.size(), 3U);
    ASSERT_EQ(head[0].name, "[0]");
    ASSERT_EQ(head[2].name, "[2]");
}

// ─── Collection expansion (queue, set, hash_set, linked_list, binary_tree, ───
// ─── graph, key_value_store, range, reference) ───────────────────────────────

void test_get_variables_queue() {
    VariableInspector inspector;

    auto queue = std::make_shared<luma::QueueValue>();
    queue->elements.push_back(luma::Value{static_cast<std::int64_t>(10)});
    queue->elements.push_back(luma::Value{static_cast<std::int64_t>(20)});
    luma::Value queue_value{queue};

    auto var = inspector.make_variable("q", queue_value, false, 0);
    ASSERT_TRUE(var.variables_reference != 0);
    ASSERT_EQ(var.indexed_variables, 2);

    const VariableInspector::ThreadResolver resolver{};
    auto children = inspector.get_variables(var.variables_reference, 0, 0, "", resolver);
    ASSERT_EQ(children.size(), 2U);
    ASSERT_EQ(children[0].name, "[0]");
    ASSERT_EQ(children[0].value, "10");
    ASSERT_EQ(children[1].name, "[1]");
    ASSERT_EQ(children[1].value, "20");
}

void test_get_variables_set() {
    VariableInspector inspector;

    auto set = std::make_shared<luma::SetValue>();
    set->elements.push_back(luma::Value{static_cast<std::int64_t>(7)});
    set->elements.push_back(luma::Value{static_cast<std::int64_t>(8)});
    luma::Value set_value{set};

    auto var = inspector.make_variable("s", set_value, false, 0);
    ASSERT_EQ(var.indexed_variables, 2);

    const VariableInspector::ThreadResolver resolver{};
    auto children = inspector.get_variables(var.variables_reference, 0, 0, "", resolver);
    ASSERT_EQ(children.size(), 2U);
    ASSERT_EQ(children[0].value, "7");
    ASSERT_EQ(children[1].value, "8");
}

void test_get_variables_hash_set() {
    VariableInspector inspector;

    auto hash_set = std::make_shared<luma::HashSetValue>();
    hash_set->buckets[1].push_back(luma::Value{static_cast<std::int64_t>(100)});
    hash_set->buckets[2].push_back(luma::Value{static_cast<std::int64_t>(200)});
    hash_set->count_ = 2;
    luma::Value hash_set_value{hash_set};

    auto var = inspector.make_variable("hs", hash_set_value, false, 0);
    ASSERT_EQ(var.indexed_variables, 2);

    const VariableInspector::ThreadResolver resolver{};
    auto children = inspector.get_variables(var.variables_reference, 0, 0, "", resolver);
    ASSERT_EQ(children.size(), 2U);

    // Bucket iteration order is unspecified; assert on the set of values.
    std::set<std::string> values;
    for (const auto& child : children) {
        values.insert(child.value);
    }
    ASSERT_TRUE(values.contains("100"));
    ASSERT_TRUE(values.contains("200"));
}

void test_get_variables_linked_list() {
    VariableInspector inspector;

    auto list = std::make_shared<luma::LinkedListValue>();
    list->head = std::make_shared<luma::LinkedListNode>(luma::Value{static_cast<std::int64_t>(1)});
    list->head->next =
        std::make_shared<luma::LinkedListNode>(luma::Value{static_cast<std::int64_t>(2)});
    list->count_ = 2;
    luma::Value list_value{list};

    auto var = inspector.make_variable("ll", list_value, false, 0);
    ASSERT_EQ(var.indexed_variables, 2);

    const VariableInspector::ThreadResolver resolver{};
    auto children = inspector.get_variables(var.variables_reference, 0, 0, "", resolver);
    ASSERT_EQ(children.size(), 2U);
    ASSERT_EQ(children[0].value, "1"); // head first
    ASSERT_EQ(children[1].value, "2");
}

void test_get_variables_binary_tree() {
    VariableInspector inspector;

    // BST with root 2, left 1, right 3 → in-order traversal yields 1, 2, 3.
    auto tree = std::make_shared<luma::BinaryTreeValue>();
    tree->root = std::make_shared<luma::BinaryTreeNode>(luma::Value{static_cast<std::int64_t>(2)});
    tree->root->left =
        std::make_shared<luma::BinaryTreeNode>(luma::Value{static_cast<std::int64_t>(1)});
    tree->root->right =
        std::make_shared<luma::BinaryTreeNode>(luma::Value{static_cast<std::int64_t>(3)});
    tree->count_ = 3;
    luma::Value tree_value{tree};

    auto var = inspector.make_variable("bt", tree_value, false, 0);
    ASSERT_EQ(var.indexed_variables, 3);

    const VariableInspector::ThreadResolver resolver{};
    auto children = inspector.get_variables(var.variables_reference, 0, 0, "", resolver);
    ASSERT_EQ(children.size(), 3U);
    ASSERT_EQ(children[0].value, "1");
    ASSERT_EQ(children[1].value, "2");
    ASSERT_EQ(children[2].value, "3");
}

void test_get_variables_graph() {
    VariableInspector inspector;

    auto graph = std::make_shared<luma::GraphValue>();
    graph->directed = true;
    graph->adjacency["a"].push_back(luma::GraphEdge{.to = "b", .weight = 1.0});
    graph->adjacency["b"] = {};
    luma::Value graph_value{graph};

    auto var = inspector.make_variable("g", graph_value, false, 0);
    ASSERT_EQ(var.named_variables, 2);

    const VariableInspector::ThreadResolver resolver{};
    auto children = inspector.get_variables(var.variables_reference, 0, 0, "", resolver);
    ASSERT_EQ(children.size(), 2U);

    // Adjacency iteration order is unspecified; assert on the set of vertices.
    std::set<std::string> names;
    for (const auto& child : children) {
        names.insert(child.name);
    }
    ASSERT_TRUE(names.contains("a"));
    ASSERT_TRUE(names.contains("b"));
}

void test_get_variables_key_value_store() {
    VariableInspector inspector;

    auto store = std::make_shared<luma::KeyValueStoreValue>();
    store->entries["name"] = "luma";
    luma::Value store_value{store};

    auto var = inspector.make_variable("kv", store_value, false, 0);
    ASSERT_EQ(var.named_variables, 1);

    const VariableInspector::ThreadResolver resolver{};
    auto children = inspector.get_variables(var.variables_reference, 0, 0, "", resolver);
    ASSERT_EQ(children.size(), 1U);
    ASSERT_EQ(children[0].name, "name");
    ASSERT_EQ(children[0].value, "luma");
}

void test_get_variables_range() {
    VariableInspector inspector;

    auto range = std::make_shared<luma::RangeValue>();
    range->start = 1;
    range->end = 5;
    range->inclusive = true;
    luma::Value range_value{range};

    auto var = inspector.make_variable("r", range_value, false, 0);
    ASSERT_EQ(var.named_variables, 3);

    const VariableInspector::ThreadResolver resolver{};
    auto children = inspector.get_variables(var.variables_reference, 0, 0, "", resolver);
    ASSERT_EQ(children.size(), 3U);
    ASSERT_EQ(children[0].name, "start");
    ASSERT_EQ(children[0].value, "1");
    ASSERT_EQ(children[1].name, "end");
    ASSERT_EQ(children[1].value, "5");
    ASSERT_EQ(children[2].name, "inclusive");
    ASSERT_EQ(children[2].value, "true");
}

void test_get_variables_xml() {
    VariableInspector inspector;

    auto root = std::make_shared<luma::XmlValue>();
    root->node_type = luma::XmlValue::NodeType::Element;
    root->tag_or_content = "note";
    root->attributes.emplace_back("id", "1");

    auto child = std::make_shared<luma::XmlValue>();
    child->node_type = luma::XmlValue::NodeType::Element;
    child->tag_or_content = "to";
    root->children.push_back(child);

    luma::Value xml_value{root};

    auto var = inspector.make_variable("doc", xml_value, false, 0);
    // node_type + tag leaves, plus one attribute and one child node.
    ASSERT_EQ(var.named_variables, 4);

    const VariableInspector::ThreadResolver resolver{};
    auto children = inspector.get_variables(var.variables_reference, 0, 0, "", resolver);
    ASSERT_EQ(children.size(), 4U);
    ASSERT_EQ(children[0].name, "node_type");
    ASSERT_EQ(children[0].value, "element");
    ASSERT_EQ(children[1].name, "tag");
    ASSERT_EQ(children[1].value, "note");
    ASSERT_EQ(children[2].name, "@id");
    ASSERT_EQ(children[2].value, "1");
    ASSERT_EQ(children[3].name, "[0]");
    // The nested element is itself structured, so it gets its own reference.
    ASSERT_TRUE(children[3].variables_reference != 0);
}

void test_get_variables_reference() {
    VariableInspector inspector;

    auto reference =
        std::make_shared<luma::ReferenceValue>(luma::Value{static_cast<std::int64_t>(42)});
    luma::Value reference_value{reference};

    auto var = inspector.make_variable("ref", reference_value, false, 0);
    ASSERT_EQ(var.named_variables, 1);

    const VariableInspector::ThreadResolver resolver{};
    auto children = inspector.get_variables(var.variables_reference, 0, 0, "", resolver);
    ASSERT_EQ(children.size(), 1U);
    ASSERT_EQ(children[0].name, "value");
    ASSERT_EQ(children[0].value, "42");
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Variable Inspector Tests");

    RUN(test_value_ref_shared_ptr);
    RUN(test_variable_inspector_alloc_ref_unique);
    RUN(test_variable_inspector_register_frame);
    RUN(test_variable_inspector_resolve_frame_invalid);
    RUN(test_variable_inspector_invalidate_refs);
    RUN(test_variable_inspector_purge_on_generation_interval);
    RUN(test_variable_inspector_purge_on_entry_threshold);
    RUN(test_variable_inspector_stale_ref_not_resolved);
    RUN(test_variable_inspector_generation_counter_increments);
    RUN(test_variable_inspector_multiple_invalidation_cycles);
    RUN(test_variable_inspector_mixed_scope_and_value_refs);
    RUN(test_variable_inspector_stale_scope_ref_after_invalidation);
    RUN(test_variable_inspector_frame_and_ref_independent_ids);
    RUN(test_variable_inspector_generational_entry_is_stale);
    RUN(test_variable_inspector_concurrent_alloc_ref);

    // parse_value.
    RUN(test_parse_value_boolean_true);
    RUN(test_parse_value_boolean_false);
    RUN(test_parse_value_integer);
    RUN(test_parse_value_negative_integer);
    RUN(test_parse_value_double);
    RUN(test_parse_value_quoted_string);
    RUN(test_parse_value_bare_string);

    // parse_value_typed (type-aware setVariable coercion).
    RUN(test_parse_value_typed_integer_accepts_integer);
    RUN(test_parse_value_typed_integer_rejects_decimal);
    RUN(test_parse_value_typed_integer_rejects_text);
    RUN(test_parse_value_typed_number_accepts_decimal);
    RUN(test_parse_value_typed_number_accepts_integer_literal);
    RUN(test_parse_value_typed_number_rejects_text);
    RUN(test_parse_value_typed_boolean_accepts_bool);
    RUN(test_parse_value_typed_boolean_rejects_number);
    RUN(test_parse_value_typed_string_accepts_any_text);
    RUN(test_parse_value_typed_string_strips_quotes);

    // set_variable.
    RUN(test_set_variable_stale_reference);
    RUN(test_get_scopes_names_order_and_refs);

    // Stress.
    RUN(test_variable_inspector_stress_alloc);

    // Depth limiting.
    RUN(test_variable_inspector_max_depth_respected);
    RUN(test_get_variables_array_paging);

    // Collection expansion for the remaining structured value kinds.
    RUN(test_get_variables_queue);
    RUN(test_get_variables_set);
    RUN(test_get_variables_hash_set);
    RUN(test_get_variables_linked_list);
    RUN(test_get_variables_binary_tree);
    RUN(test_get_variables_graph);
    RUN(test_get_variables_key_value_store);
    RUN(test_get_variables_xml);
    RUN(test_get_variables_range);
    RUN(test_get_variables_reference);

    return SUMMARY();
}
