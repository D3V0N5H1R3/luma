#include "runtime/compiler/compilation_cache.hpp"

#include <mutex>
#include <vector>

#include "common/hash.hpp"

namespace luma {

std::optional<CompileArtifact>
CompilationCache::get(const std::string& path, const std::string& content, const Options& options) {
    const std::scoped_lock lock(mutex_);

    // LruCache::get promotes the entry to most-recently-used on any key match,
    // including the stale-content case below where we then return a miss. This
    // is intentional and unobservable in practice: a stale hit is always
    // followed by a put() of the same key (the caller recompiles), which
    // re-promotes the entry regardless. The only divergence from a
    // promote-after-freshness-check would be the LRU eviction order after a
    // terminal stale lookup, which never affects the correctness of a returned
    // artifact — only the cache hit rate.
    auto* entry = cache_.get(CacheKey{.path = path, .options = options});

    if (entry == nullptr) {
        return std::nullopt;
    }

    if (entry->content_hash != hash_content(content)) {
        return std::nullopt;
    }

    return entry->artifact;
}

void CompilationCache::put(const std::string& path, const std::string& content,
                           const CompileArtifact& artifact, const Options& options) {
    const std::scoped_lock lock(mutex_);

    // LruCache::put refreshes an existing key in place (promoting it to most
    // recently used) or inserts a new entry, evicting the least recently used
    // entry when at capacity.
    static_cast<void>(
        cache_.put(CacheKey{.path = path, .options = options},
                   CacheEntry{.content_hash = hash_content(content), .artifact = artifact}));
}

void CompilationCache::invalidate(const std::string& path) {
    const std::scoped_lock lock(mutex_);

    // Collect the matching keys first, then erase them: mutating the cache
    // while iterating it inside for_each would invalidate the traversal.
    std::vector<CacheKey> stale_keys;
    cache_.for_each([&](const CacheKey& key, const CacheEntry&) {
        if (key.path == path) {
            stale_keys.push_back(key);
        }
    });

    for (const auto& key : stale_keys) {
        static_cast<void>(cache_.erase(key));
    }
}

void CompilationCache::clear() {
    const std::scoped_lock lock(mutex_);
    cache_.clear();
}

std::size_t CompilationCache::size() const {
    const std::scoped_lock lock(mutex_);
    return cache_.size();
}

std::uint64_t CompilationCache::hash_content(const std::string& content) {
    return fnv1a_hash(content);
}

} // namespace luma
