// LSP analysis cache tests — LRU eviction, transactional updates, and the
// cross-file symbol reverse index.
//
// LspAnalysisCache is deliberately NOT thread-safe (the server serialises
// access through its state mutex), which makes it straightforward to test its
// data-structure invariants directly. These tests pin down the LRU ordering,
// the RAII CacheTransaction commit/rollback semantics, and — most importantly —
// the reverse-index invariant that a lookup_symbol miss authoritatively means
// "not defined in any cached file" after every insert/remove/evict.

#include <stdexcept>
#include <string>
#include <utility>

#include "analysis/source/source_location.hpp"
#include "lsp_analysis_cache.hpp"
#include "lsp_analysis_result.hpp"
#include "test_framework.hpp"

using luma::SourceLocation;
using luma::lsp::AnalysisResult;
using luma::lsp::LspAnalysisCache;
using luma::lsp::SymbolDefinition;
using luma::lsp::UserFunctionInfo;

namespace {

// ─── Fixtures ──────────────────────────────────────────────────────

[[nodiscard]] AnalysisResult make_result_with_definition(const std::string& name, int line) {
    AnalysisResult result;
    result.semantic.symbols.definitions[name] =
        SymbolDefinition{SourceLocation{.file_id = 0, .line = line, .column = 1}, "record", false};
    return result;
}

[[nodiscard]] AnalysisResult make_result_with_function(const std::string& name, int line) {
    AnalysisResult result;
    UserFunctionInfo info;
    info.location = SourceLocation{.file_id = 0, .line = line, .column = 1};
    result.semantic.symbols.user_functions[name] = std::move(info);
    return result;
}

// A predicate that always permits eviction.
const auto always_evict = [](const std::string&) {
    return true;
};

// ─── Basic storage ─────────────────────────────────────────────────

void test_insert_find_contains_size() {
    LspAnalysisCache cache;
    cache.insert("uri://a", make_result_with_definition("Foo", 1));

    ASSERT_TRUE(cache.contains("uri://a"));
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(1));

    const auto found = cache.find("uri://a");
    ASSERT_TRUE(found.has_value());
}

void test_find_missing_returns_empty() {
    const LspAnalysisCache cache;
    const auto found = cache.find("uri://missing");
    ASSERT_FALSE(found.has_value());
}

void test_at_throws_on_missing() {
    LspAnalysisCache cache;
    ASSERT_THROWS_AS(cache.at("uri://missing"), std::out_of_range);
}

// ─── LRU eviction ──────────────────────────────────────────────────

void test_evict_one_removes_least_recently_used() {
    LspAnalysisCache cache;
    cache.insert("uri://a", make_result_with_definition("A", 1));
    cache.insert("uri://b", make_result_with_definition("B", 1));
    cache.insert("uri://c", make_result_with_definition("C", 1));

    ASSERT_TRUE(cache.evict_one(always_evict));

    // "a" was inserted first (least recently used) → evicted.
    ASSERT_FALSE(cache.contains("uri://a"));
    ASSERT_TRUE(cache.contains("uri://b"));
    ASSERT_TRUE(cache.contains("uri://c"));
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(2));
}

void test_touch_updates_recency() {
    LspAnalysisCache cache;
    cache.insert("uri://a", make_result_with_definition("A", 1));
    cache.insert("uri://b", make_result_with_definition("B", 1));
    cache.insert("uri://c", make_result_with_definition("C", 1));

    cache.touch("uri://a"); // a becomes most-recently-used → b is now LRU

    ASSERT_TRUE(cache.evict_one(always_evict));
    ASSERT_FALSE(cache.contains("uri://b"));
    ASSERT_TRUE(cache.contains("uri://a"));
    ASSERT_TRUE(cache.contains("uri://c"));
}

void test_evict_one_skips_pinned() {
    LspAnalysisCache cache;
    cache.insert("uri://a", make_result_with_definition("A", 1));
    cache.insert("uri://b", make_result_with_definition("B", 1));
    cache.insert("uri://c", make_result_with_definition("C", 1));

    // Pin the LRU entry ("a"); eviction must skip it and take the next one.
    const auto evicted = cache.evict_one([](const std::string& uri) { return uri != "uri://a"; });
    ASSERT_TRUE(evicted);
    ASSERT_TRUE(cache.contains("uri://a"));
    ASSERT_FALSE(cache.contains("uri://b"));
    ASSERT_TRUE(cache.contains("uri://c"));
}

void test_evict_one_returns_false_when_all_pinned() {
    LspAnalysisCache cache;
    cache.insert("uri://a", make_result_with_definition("A", 1));

    const auto evicted = cache.evict_one([](const std::string&) { return false; });
    ASSERT_FALSE(evicted);
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(1));
}

void test_evict_to_limit_reduces_to_max() {
    LspAnalysisCache cache{2};
    cache.insert("uri://a", make_result_with_definition("A", 1));
    cache.insert("uri://b", make_result_with_definition("B", 1));
    cache.insert("uri://c", make_result_with_definition("C", 1));

    cache.evict_to_limit(always_evict);

    ASSERT_EQ(cache.size(), static_cast<std::size_t>(2));
    ASSERT_FALSE(cache.contains("uri://a")); // LRU evicted first
    ASSERT_TRUE(cache.contains("uri://b"));
    ASSERT_TRUE(cache.contains("uri://c"));
}

void test_evict_to_limit_respects_pinned() {
    LspAnalysisCache cache{1};
    cache.insert("uri://a", make_result_with_definition("A", 1));
    cache.insert("uri://b", make_result_with_definition("B", 1));

    // "a" is over the limit and the LRU, but pinned — the eviction must rotate
    // past it and drop the unpinned "b" instead.
    cache.evict_to_limit([](const std::string& uri) { return uri != "uri://a"; });

    ASSERT_TRUE(cache.contains("uri://a"));
    ASSERT_FALSE(cache.contains("uri://b"));
    ASSERT_EQ(cache.size(), static_cast<std::size_t>(1));
}

void test_evict_to_limit_terminates_when_all_pinned() {
    LspAnalysisCache cache{1};
    cache.insert("uri://a", make_result_with_definition("A", 1));
    cache.insert("uri://b", make_result_with_definition("B", 1));

    // Every entry is pinned; the loop must give up rather than spin forever.
    cache.evict_to_limit([](const std::string&) { return false; });

    ASSERT_EQ(cache.size(), static_cast<std::size_t>(2));
}

// ─── Transactions ──────────────────────────────────────────────────

void test_transaction_commit_inserts() {
    LspAnalysisCache cache;
    {
        auto txn = cache.begin_update("uri://a");
        txn.set_result(make_result_with_definition("Foo", 3));
        txn.commit();
        ASSERT_TRUE(txn.is_finished());
    }
    ASSERT_TRUE(cache.contains("uri://a"));
    const auto found = cache.find("uri://a");
    ASSERT_TRUE(found.has_value());
}

void test_transaction_implicit_rollback_on_scope_exit() {
    LspAnalysisCache cache;
    {
        auto txn = cache.begin_update("uri://a");
        txn.set_result(make_result_with_definition("Foo", 3));
        // No commit() — leaving scope discards the staged result.
    }
    ASSERT_FALSE(cache.contains("uri://a"));
}

void test_transaction_explicit_rollback() {
    LspAnalysisCache cache;
    auto txn = cache.begin_update("uri://a");
    txn.set_result(make_result_with_definition("Foo", 3));
    txn.rollback();
    ASSERT_TRUE(txn.is_finished());
    ASSERT_FALSE(cache.contains("uri://a"));
}

void test_transaction_commit_applies_include_deps() {
    LspAnalysisCache cache;
    {
        auto txn = cache.begin_update("uri://main");
        txn.set_result(make_result_with_definition("Foo", 1));
        txn.add_include_dependent("uri://lib");
        txn.commit();
    }
    const auto deps = cache.get_dependents("uri://lib");
    ASSERT_TRUE(deps.has_value());
    ASSERT_TRUE(deps->contains("uri://main"));
}

void test_transaction_finished_operations_throw() {
    LspAnalysisCache cache;
    auto txn = cache.begin_update("uri://a");
    txn.commit();

    ASSERT_THROWS_AS(txn.set_result(make_result_with_definition("Foo", 1)), std::logic_error);
    ASSERT_THROWS_AS(txn.add_include_dependent("uri://lib"), std::logic_error);
    ASSERT_THROWS_AS(txn.commit(), std::logic_error);
}

void test_transaction_moved_from_does_not_apply() {
    LspAnalysisCache cache;

    // Moving then dropping both without commit must leave the cache untouched.
    {
        auto txn = cache.begin_update("uri://a");
        txn.set_result(make_result_with_definition("Foo", 1));
        auto moved = std::move(txn);
        ASSERT_TRUE(txn.is_finished()); // moved-from is neutralised
    }
    ASSERT_FALSE(cache.contains("uri://a"));

    // Committing the moved-to transaction applies exactly once.
    {
        auto txn = cache.begin_update("uri://b");
        txn.set_result(make_result_with_definition("Bar", 1));
        auto moved = std::move(txn);
        moved.commit();
    }
    ASSERT_TRUE(cache.contains("uri://b"));
}

// ─── Cross-file symbol reverse index ───────────────────────────────

void test_symbol_index_empty_by_default() {
    const LspAnalysisCache cache;
    ASSERT_FALSE(cache.has_symbol_index());
}

void test_symbol_lookup_finds_definitions_and_functions() {
    LspAnalysisCache cache;
    cache.insert("uri://a", make_result_with_definition("Foo", 7));
    cache.insert("uri://b", make_result_with_function("bar", 12));

    ASSERT_TRUE(cache.has_symbol_index());

    const auto foo = cache.lookup_symbol("Foo");
    ASSERT_TRUE(foo.has_value());
    ASSERT_EQ(foo->uri, std::string("uri://a"));
    ASSERT_EQ(foo->location.line, 7);

    const auto bar = cache.lookup_symbol("bar");
    ASSERT_TRUE(bar.has_value());
    ASSERT_EQ(bar->uri, std::string("uri://b"));
    ASSERT_EQ(bar->location.line, 12);

    ASSERT_FALSE(cache.lookup_symbol("Missing").has_value());
}

void test_symbol_lookup_excludes_uri() {
    LspAnalysisCache cache;
    cache.insert("uri://a", make_result_with_definition("Shared", 1));
    cache.insert("uri://b", make_result_with_definition("Shared", 2));

    // Excluding "a" yields the other definer.
    const auto other = cache.lookup_symbol("Shared", "uri://a");
    ASSERT_TRUE(other.has_value());
    ASSERT_EQ(other->uri, std::string("uri://b"));

    // A symbol defined only in the excluded file resolves to nothing.
    cache.insert("uri://c", make_result_with_definition("OnlyC", 1));
    ASSERT_FALSE(cache.lookup_symbol("OnlyC", "uri://c").has_value());
}

void test_symbol_index_updated_on_remove() {
    LspAnalysisCache cache;
    cache.insert("uri://a", make_result_with_definition("Foo", 1));
    cache.insert("uri://b", make_result_with_definition("Bar", 1));

    cache.remove("uri://a");

    ASSERT_FALSE(cache.lookup_symbol("Foo").has_value()); // gone with its file
    ASSERT_TRUE(cache.lookup_symbol("Bar").has_value());
}

void test_symbol_index_updated_on_reinsert() {
    LspAnalysisCache cache;
    cache.insert("uri://a", make_result_with_definition("Old", 1));
    // Re-analysing the same file replaces its contributed symbols.
    cache.insert("uri://a", make_result_with_definition("New", 1));

    ASSERT_FALSE(cache.lookup_symbol("Old").has_value());
    ASSERT_TRUE(cache.lookup_symbol("New").has_value());
}

void test_symbol_index_updated_on_evict() {
    LspAnalysisCache cache{1};
    cache.insert("uri://a", make_result_with_definition("Foo", 1));
    cache.insert("uri://b", make_result_with_definition("Bar", 1));

    cache.evict_to_limit(always_evict); // drops "a"

    ASSERT_FALSE(cache.lookup_symbol("Foo").has_value());
    ASSERT_TRUE(cache.lookup_symbol("Bar").has_value());
}

// ─── Include dependency tracking ───────────────────────────────────

void test_include_dependents_add_get_remove() {
    LspAnalysisCache cache;
    cache.add_include_dependent("uri://lib", "uri://a");
    cache.add_include_dependent("uri://lib", "uri://b");

    const auto deps = cache.get_dependents("uri://lib");
    ASSERT_TRUE(deps.has_value());
    ASSERT_EQ(deps->size(), static_cast<std::size_t>(2));
    ASSERT_TRUE(deps->contains("uri://a"));
    ASSERT_TRUE(deps->contains("uri://b"));

    ASSERT_FALSE(cache.get_dependents("uri://unknown").has_value());

    cache.remove_dependent("uri://a");
    const auto after = cache.get_dependents("uri://lib");
    ASSERT_TRUE(after.has_value());
    ASSERT_FALSE(after->contains("uri://a"));
    ASSERT_TRUE(after->contains("uri://b"));
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
    RUN(test_insert_find_contains_size);
    RUN(test_find_missing_returns_empty);
    RUN(test_at_throws_on_missing);
    RUN(test_evict_one_removes_least_recently_used);
    RUN(test_touch_updates_recency);
    RUN(test_evict_one_skips_pinned);
    RUN(test_evict_one_returns_false_when_all_pinned);
    RUN(test_evict_to_limit_reduces_to_max);
    RUN(test_evict_to_limit_respects_pinned);
    RUN(test_evict_to_limit_terminates_when_all_pinned);
    RUN(test_transaction_commit_inserts);
    RUN(test_transaction_implicit_rollback_on_scope_exit);
    RUN(test_transaction_explicit_rollback);
    RUN(test_transaction_commit_applies_include_deps);
    RUN(test_transaction_finished_operations_throw);
    RUN(test_transaction_moved_from_does_not_apply);
    RUN(test_symbol_index_empty_by_default);
    RUN(test_symbol_lookup_finds_definitions_and_functions);
    RUN(test_symbol_lookup_excludes_uri);
    RUN(test_symbol_index_updated_on_remove);
    RUN(test_symbol_index_updated_on_reinsert);
    RUN(test_symbol_index_updated_on_evict);
    RUN(test_include_dependents_add_get_remove);

    return SUMMARY();
}
