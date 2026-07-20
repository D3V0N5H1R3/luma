#ifndef LUMA_COMPILER_COMPILATION_CACHE_HPP
#define LUMA_COMPILER_COMPILATION_CACHE_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "common/lru_cache.hpp"
#include "runtime/compiler/compile_result.hpp"

namespace luma {

// In-memory compilation cache that avoids re-compiling unchanged files.
// Keyed by (absolute path, compilation options, content hash). When a file's
// content changes or options differ, the cached entry is not returned.
// Uses LRU eviction when the cache exceeds max_entries.
//
// Thread safety
// -------------
// The underlying LruCache is intentionally not thread-safe, so all public
// methods (get, put, invalidate, clear, size) acquire `mutex_` before touching
// `cache_`.  The cache is therefore safe to share across threads (e.g.
// concurrent compilations) without external synchronisation.  `mutex_` is
// marked `mutable` so that the const accessor `size()` can also lock.
class CompilationCache {
public:
    static constexpr std::size_t default_max_entries{128};

    // Options that affect compilation output.
    struct Options {
        bool optimized;
        bool debug_info;

        constexpr Options(bool opt = true, bool dbg = false) : optimized{opt}, debug_info{dbg} {}

        [[nodiscard]] bool operator==(const Options&) const = default;
    };

    explicit CompilationCache(std::size_t max_entries = default_max_entries)
        : cache_{max_entries} {}

    // Look up a cached compilation result for the given file path and content.
    // Returns nullopt if the file is not in the cache or the content has changed.
    [[nodiscard]] std::optional<CompileArtifact>
    get(const std::string& path, const std::string& content, const Options& options = Options{});

    // Store a compiled artifact for the given file path and content.
    void put(const std::string& path, const std::string& content, const CompileArtifact& artifact,
             const Options& options = Options{});

    // Invalidate all cache entries for a specific file (across all option variants).
    void invalidate(const std::string& path);

    // Clear the entire cache.
    void clear();

    // Return the number of cached entries.
    [[nodiscard]] std::size_t size() const;

    // Return the maximum number of entries.
    [[nodiscard]] std::size_t max_entries() const {
        return cache_.max_size();
    }

private:
    // FNV-1a hash of file content (fast, no crypto needed).
    // Note: FNV-1a has a negligible collision probability for typical source
    // files (64-bit hash space).  A collision would cause a stale cache hit —
    // the cached result from a different file content would be returned.
    // In practice this is harmless (a re-compile fixes it), and adding a
    // runtime collision check would require storing the full content string
    // which defeats the purpose of hashing.  If paranoia is needed, switch
    // to a stronger hash (e.g. SHA-256) rather than adding collision detection.
    [[nodiscard]] static std::uint64_t hash_content(const std::string& content);

    struct CacheKey {
        std::string path;
        Options options;

        [[nodiscard]] bool operator==(const CacheKey&) const = default;
    };

    struct CacheKeyHash {
        [[nodiscard]] std::size_t operator()(const CacheKey& k) const noexcept {
            std::size_t h = std::hash<std::string>{}(k.path);
            h ^= std::hash<bool>{}(k.options.optimized) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<bool>{}(k.options.debug_info) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    // LRU eviction, access-order bookkeeping, and the map/list invariant are
    // all encapsulated by LruCache; this class only adds the mutex and the
    // content-hash freshness check on top.
    struct CacheEntry {
        std::uint64_t content_hash{0};
        CompileArtifact artifact;
    };

    mutable std::mutex mutex_;
    LruCache<CacheKey, CacheEntry, CacheKeyHash> cache_;
};

} // namespace luma

#endif // LUMA_COMPILER_COMPILATION_CACHE_HPP
