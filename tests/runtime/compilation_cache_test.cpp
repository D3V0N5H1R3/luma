// Compilation cache unit tests.

#include <string>

#include "runtime/compiler/compilation_cache.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Helpers ───

static CompileArtifact make_result(const std::string& name) {
    CompileArtifact artifact;
    CompiledFunction func;
    func.name = name;
    artifact.functions.push_back(std::move(func));
    return artifact;
}

// ─── Basic put/get ───

static void test_cache_put_get() {
    CompilationCache cache;
    auto result = make_result("main");

    cache.put("file.luma", "let x = 1", result);

    auto cached = cache.get("file.luma", "let x = 1");
    ASSERT_TRUE(cached.has_value());
    ASSERT_EQ(cached->functions.size(), 1U);
    ASSERT_EQ(cached->functions[0].name, "main");
}

// ─── Cache miss on content change ───

static void test_cache_miss_on_content_change() {
    CompilationCache cache;
    auto result = make_result("main");

    cache.put("file.luma", "let x = 1", result);

    auto cached = cache.get("file.luma", "let x = 2");
    ASSERT_FALSE(cached.has_value());
}

// ─── Cache miss on unknown file ───

static void test_cache_miss_unknown_file() {
    CompilationCache cache;

    auto cached = cache.get("unknown.luma", "content");
    ASSERT_FALSE(cached.has_value());
}

// ─── Invalidate file ───

static void test_cache_invalidate() {
    CompilationCache cache;
    auto result = make_result("main");

    cache.put("file.luma", "let x = 1", result);
    ASSERT_EQ(cache.size(), 1U);

    cache.invalidate("file.luma");
    ASSERT_EQ(cache.size(), 0U);

    auto cached = cache.get("file.luma", "let x = 1");
    ASSERT_FALSE(cached.has_value());
}

// ─── Clear ───

static void test_cache_clear() {
    CompilationCache cache;
    cache.put("a.luma", "a", make_result("a"));
    cache.put("b.luma", "b", make_result("b"));

    ASSERT_EQ(cache.size(), 2U);

    cache.clear();
    ASSERT_EQ(cache.size(), 0U);
}

// ─── Options differentiation ───

static void test_cache_options_differentiation() {
    CompilationCache cache;
    auto result_opt = make_result("optimized");
    auto result_dbg = make_result("debug");

    cache.put("file.luma", "code", result_opt, {true, false});
    cache.put("file.luma", "code", result_dbg, {true, true});

    ASSERT_EQ(cache.size(), 2U);

    auto cached_opt = cache.get("file.luma", "code", {true, false});
    auto cached_dbg = cache.get("file.luma", "code", {true, true});

    ASSERT_TRUE(cached_opt.has_value());
    ASSERT_TRUE(cached_dbg.has_value());
    ASSERT_EQ(cached_opt->functions[0].name, "optimized");
    ASSERT_EQ(cached_dbg->functions[0].name, "debug");
}

// ─── LRU eviction ───

static void test_cache_lru_eviction() {
    CompilationCache cache{3}; // Max 3 entries.

    cache.put("a.luma", "a", make_result("a"));
    cache.put("b.luma", "b", make_result("b"));
    cache.put("c.luma", "c", make_result("c"));

    ASSERT_EQ(cache.size(), 3U);

    // Adding a 4th should evict the LRU (a.luma).
    cache.put("d.luma", "d", make_result("d"));

    ASSERT_EQ(cache.size(), 3U);

    // a.luma should be evicted.
    auto cached_a = cache.get("a.luma", "a");
    ASSERT_FALSE(cached_a.has_value());

    // b, c, d should still be present.
    ASSERT_TRUE(cache.get("b.luma", "b").has_value());
    ASSERT_TRUE(cache.get("c.luma", "c").has_value());
    ASSERT_TRUE(cache.get("d.luma", "d").has_value());
}

// ─── LRU: access refreshes entry ───

static void test_cache_lru_access_refreshes() {
    CompilationCache cache{3}; // Max 3 entries.

    cache.put("a.luma", "a", make_result("a"));
    cache.put("b.luma", "b", make_result("b"));
    cache.put("c.luma", "c", make_result("c"));

    // Access a.luma to make it most recent.
    auto cached = cache.get("a.luma", "a");
    ASSERT_TRUE(cached.has_value());

    // Now add d.luma — b.luma should be evicted (it's the LRU now).
    cache.put("d.luma", "d", make_result("d"));

    ASSERT_EQ(cache.size(), 3U);
    ASSERT_FALSE(cache.get("b.luma", "b").has_value()); // Evicted.
    ASSERT_TRUE(cache.get("a.luma", "a").has_value());  // Refreshed, still present.
    ASSERT_TRUE(cache.get("c.luma", "c").has_value());
    ASSERT_TRUE(cache.get("d.luma", "d").has_value());
}

// ─── LRU: update refreshes entry ───

static void test_cache_lru_update_refreshes() {
    CompilationCache cache{3};

    cache.put("a.luma", "a", make_result("a"));
    cache.put("b.luma", "b", make_result("b"));
    cache.put("c.luma", "c", make_result("c"));

    // Re-put a.luma to refresh it.
    cache.put("a.luma", "a_v2", make_result("a_v2"));

    // Add d.luma — b.luma should be evicted.
    cache.put("d.luma", "d", make_result("d"));

    ASSERT_EQ(cache.size(), 3U);
    ASSERT_FALSE(cache.get("b.luma", "b").has_value());
    ASSERT_TRUE(cache.get("a.luma", "a_v2").has_value());
}

// ─── Max entries getter ───

static void test_cache_max_entries() {
    CompilationCache cache{50};
    ASSERT_EQ(cache.max_entries(), 50U);

    CompilationCache default_cache;
    ASSERT_EQ(default_cache.max_entries(), CompilationCache::default_max_entries);
}

// ─── Content-hash invalidation (TS-5) ───

static void test_cache_content_hash_invalidation_flow() {
    // Simulate the compile → edit → recompile flow.
    CompilationCache cache;
    auto result_v1 = make_result("v1");
    auto result_v2 = make_result("v2");

    // First compilation caches the result.
    const std::string file = "app.luma";
    const std::string source_v1 = "function integer f() { return 1 }";
    cache.put(file, source_v1, result_v1);

    auto cached_v1 = cache.get(file, source_v1);
    ASSERT_TRUE(cached_v1.has_value());
    ASSERT_EQ(cached_v1->functions[0].name, "v1");

    // Source content changes — cache should miss on old content.
    const std::string source_v2 = "function integer f() { return 2 }";
    auto stale = cache.get(file, source_v2);
    ASSERT_FALSE(stale.has_value());

    // Recompile with new content and verify new result is cached.
    cache.put(file, source_v2, result_v2);
    auto cached_v2 = cache.get(file, source_v2);
    ASSERT_TRUE(cached_v2.has_value());
    ASSERT_EQ(cached_v2->functions[0].name, "v2");

    // Old content should no longer be cached.
    auto old_lookup = cache.get(file, source_v1);
    ASSERT_FALSE(old_lookup.has_value());
}

// ─── main ───

int main() {
    RUN(test_cache_put_get);
    RUN(test_cache_miss_on_content_change);
    RUN(test_cache_miss_unknown_file);
    RUN(test_cache_invalidate);
    RUN(test_cache_clear);
    RUN(test_cache_options_differentiation);
    RUN(test_cache_lru_eviction);
    RUN(test_cache_lru_access_refreshes);
    RUN(test_cache_lru_update_refreshes);
    RUN(test_cache_max_entries);
    RUN(test_cache_content_hash_invalidation_flow);
    return SUMMARY();
}
