// Compilation cache unit tests.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "analysis/source/source_manager.hpp"
#include "runtime/compiler/compilation_cache.hpp"
#include "runtime/compiler/source_dependency.hpp"
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

static void overwrite_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out << content;
}

// Load `root` and `include` through a SourceManager, as a compile would, and
// return the dependencies recorded for `root`.
static std::vector<SourceDependency> dependencies_of(const TempFile& root,
                                                     const TempFile& include) {
    SourceManager source_manager;
    const auto& root_file = source_manager.load(root.path_string());
    (void)source_manager.load(include.path_string());

    return collect_source_dependencies(source_manager, root_file.file_id);
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

// ─── Source dependencies (included files) ───

static void test_dependencies_skip_root_and_virtual_sources() {
    const TempFile root{"include \"helper.luma\"\n"};
    const TempFile include{"function integer helper() { return 1 }\n"};

    SourceManager source_manager;
    const auto& root_file = source_manager.load(root.path_string());
    (void)source_manager.load(include.path_string());
    (void)source_manager.load_virtual("<prelude>", "function integer injected() { return 0 }");

    const auto dependencies = collect_source_dependencies(source_manager, root_file.file_id);

    ASSERT_EQ(dependencies.size(), 1U);
    ASSERT_EQ(dependencies[0].path, source_manager.files()[1].path);
    ASSERT_TRUE(are_source_dependencies_current(dependencies));
}

static void test_dependencies_stale_after_edit() {
    const TempFile root{"include \"helper.luma\"\n"};
    const TempFile include{"function integer helper() { return 1 }\n"};
    const auto dependencies = dependencies_of(root, include);

    overwrite_file(include.path(), "function integer helper() { return 2 }\n");

    ASSERT_FALSE(are_source_dependencies_current(dependencies));
}

static void test_dependencies_stale_after_delete() {
    const TempFile root{"include \"helper.luma\"\n"};
    const TempFile include{"function integer helper() { return 1 }\n"};
    const auto dependencies = dependencies_of(root, include);

    std::filesystem::remove(include.path());

    ASSERT_FALSE(are_source_dependencies_current(dependencies));
}

// SourceManager strips a UTF-8 BOM before the text is hashed, so the freshness
// check must strip it too — otherwise every BOM-prefixed include would read as
// edited and never hit the cache.
static void test_dependencies_current_with_utf8_bom() {
    const TempFile root{"include \"helper.luma\"\n"};
    const TempFile include{"\xEF\xBB\xBF"
                           "function integer helper() { return 1 }\n"};

    ASSERT_TRUE(are_source_dependencies_current(dependencies_of(root, include)));
}

// A dependency path read from an untrusted .lumc file may name something that
// is not a regular file; it must read as stale rather than be opened.
static void test_dependencies_stale_for_non_regular_file() {
    const TempDir dir;
    const std::vector<SourceDependency> dependencies{
        {.path = dir.path_string(), .content_hash = 0}};

    ASSERT_FALSE(are_source_dependencies_current(dependencies));
}

static void test_cache_miss_when_dependency_changes() {
    const TempFile root{"include \"helper.luma\"\n"};
    const TempFile include{"function integer helper() { return 1 }\n"};
    CompilationCache cache;

    cache.put("app.luma", "source", make_result("v1"), {}, dependencies_of(root, include));
    ASSERT_TRUE(cache.get("app.luma", "source").has_value());

    // The entry point is unchanged, but an included file was edited.
    overwrite_file(include.path(), "function integer helper() { return 2 }\n");

    ASSERT_FALSE(cache.get("app.luma", "source").has_value());
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
    RUN(test_dependencies_skip_root_and_virtual_sources);
    RUN(test_dependencies_stale_after_edit);
    RUN(test_dependencies_stale_after_delete);
    RUN(test_dependencies_current_with_utf8_bom);
    RUN(test_dependencies_stale_for_non_regular_file);
    RUN(test_cache_miss_when_dependency_changes);
    return SUMMARY();
}
