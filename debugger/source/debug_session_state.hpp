#ifndef LUMA_DAP_DEBUG_SESSION_STATE_HPP
#define LUMA_DAP_DEBUG_SESSION_STATE_HPP

// ─────────────────────────────────────────────────────────────────────────────
// WatchCache — caches evaluated watch results to avoid redundant re-evaluation
// while the debugger is paused.
// ─────────────────────────────────────────────────────────────────────────────

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace luma::dap {

// ─── Watch expression cache ───
// Caches evaluated watch results to avoid redundant re-evaluation
// while the debugger is paused.
//
// Note: Watch evaluation and cache access are single-threaded — all
// requests are processed sequentially on the DAP protocol thread.
// No synchronisation is required for the cache map itself.  Only the
// generation counter is atomic because invalidate() may be called
// from the execution thread on continue/step events.

class WatchCache {
public:
    struct WatchKey {
        int frame_id{0};
        std::string expression;

        bool operator==(const WatchKey&) const = default;
    };

    struct WatchKeyHash {
        std::size_t operator()(const WatchKey& key) const noexcept {
            // Combine frame_id and expression hashes.
            const std::size_t h1 = std::hash<int>{}(key.frame_id);
            const std::size_t h2 = std::hash<std::string>{}(key.expression);
            return h1 ^ (h2 << 1);
        }
    };

    struct Entry {
        std::string expression;
        int frame_id{0};
        std::string result;
        std::string type;
        int variables_reference{0};
        int generation{0}; // Generation when this entry was stored.
    };

    // Look up a cached result by frame and expression.
    // Returns std::nullopt if not found or stale.
    // Returns by value so the caller owns a snapshot; the underlying map entry
    // may be replaced by a later put() call on the same DAP message-loop thread.
    // NOTE: get() and put() are only ever called from the DAP message-loop
    // thread; invalidate() is the only cross-thread operation and it only
    // touches the atomic generation counter, never the map.
    [[nodiscard]] std::optional<Entry> get(int frame_id, const std::string& expression) const {
        auto it = cache_.find(WatchKey{frame_id, expression});
        if (it == cache_.end() ||
            it->second.generation != generation_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        return it->second;
    }

    // Store a result in the cache, keyed by frame and expression.
    void put(int frame_id, const std::string& expression, Entry entry) {
        entry.generation = generation_.load(std::memory_order_acquire);

        // Bound the cache.  invalidate() only bumps the generation — it must not
        // touch the map because it may run on the execution thread — so entries
        // from superseded generations linger here and, over a long session with
        // many distinct expressions, would grow without limit.  When the map
        // reaches the cap, first drop the dead (stale-generation) entries; if it
        // is still full (many live entries in one stop), clear it wholesale.  A
        // cleared live entry is merely re-evaluated on next access.  get()/put()
        // are single-threaded, so mutating the map here is safe.
        if (cache_.size() >= k_max_entries) {
            const int current = entry.generation;
            std::erase_if(cache_,
                          [current](const auto& kv) { return kv.second.generation != current; });
            if (cache_.size() >= k_max_entries) {
                cache_.clear();
            }
        }

        cache_[WatchKey{frame_id, expression}] = std::move(entry);
    }

    // Invalidate all cached results (called on continue/step).
    // Increments generation rather than clearing the map so that
    // the same expressions can be quickly re-cached after stopping.
    void invalidate() {
        generation_.fetch_add(1, std::memory_order_release);
    }

private:
    // Upper bound on cached entries.  Reached only after many distinct
    // frame/expression pairs accumulate across generations; see put().
    static constexpr std::size_t k_max_entries = 1024;

    std::unordered_map<WatchKey, Entry, WatchKeyHash> cache_;
    std::atomic<int> generation_{0};
};

} // namespace luma::dap

#endif // LUMA_DAP_DEBUG_SESSION_STATE_HPP
