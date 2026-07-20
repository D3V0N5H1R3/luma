// LruCache — Bounded cache with LRU eviction.
//
// Use for fixed-capacity caches where the least recently used entry is
// evicted when the cache is full (e.g., compilation caches, symbol
// caches).  Provides O(1) get/put via a hash map + doubly-linked list.
//
// Thread safety: NOT thread-safe.  Callers must provide external
// synchronisation for concurrent access.

#ifndef LUMA_COMMON_LRU_CACHE_HPP
#define LUMA_COMMON_LRU_CACHE_HPP

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace luma {

// A fixed-capacity cache that evicts the least recently used entry when full.
// O(1) lookup, insertion, and eviction via a doubly-linked list (LRU order)
// paired with a hash map (key → list iterator).
//
// Thread safety: This class is NOT thread-safe.  Callers must provide
// external synchronisation if accessed from multiple threads.
template <typename Key, typename Value, typename Hash = std::hash<Key>>
    requires std::equality_comparable<Key> && std::is_invocable_r_v<std::size_t, Hash, const Key&>
class LruCache {
public:
    explicit LruCache(std::size_t max_size) : max_size_(max_size) {
        assert(max_size > 0 && "LruCache: max_size must be > 0");
    }

    // Insert or update a key-value pair. Returns reference to stored value.
    [[nodiscard]] Value& put(const Key& key, Value value) {
        auto it = map_.find(key);

        if (it != map_.end()) {
            // Update existing entry and promote to MRU (front).
            it->second->second = std::move(value);
            order_.splice(order_.begin(), order_, it->second);
            check_invariant();
            return it->second->second;
        }

        // Evict least recently used entry (back) if at capacity.
        if (map_.size() >= max_size_) {
            const auto& back_key = order_.back().first;
            map_.erase(back_key);
            order_.pop_back();
        }

        // Insert new entry at front (MRU position).  try_emplace avoids a
        // second hash+probe of the key: the find() above already proved the
        // key absent (and any eviction removed a different key), so this insert
        // always succeeds.
        order_.emplace_front(key, std::move(value));
        map_.try_emplace(key, order_.begin());
        check_invariant();
        return order_.front().second;
    }

    // Look up a key. Returns pointer to value if found (promotes to MRU), nullptr otherwise.
    [[nodiscard]] Value* get(const Key& key) {
        auto it = map_.find(key);

        if (it == map_.end()) {
            return nullptr;
        }

        // Promote to MRU (front).
        order_.splice(order_.begin(), order_, it->second);
        return &it->second->second;
    }

    // Check if key exists without promoting.
    [[nodiscard]] bool contains(const Key& key) const {
        return map_.find(key) != map_.end();
    }

    // Remove a specific key. Returns true if the key was found and removed.
    bool erase(const Key& key) {
        auto it = map_.find(key);

        if (it == map_.end()) {
            return false;
        }

        order_.erase(it->second);
        map_.erase(it);
        check_invariant();
        return true;
    }

    // Remove all entries.
    void clear() {
        order_.clear();
        map_.clear();
    }

    // Iterate all entries (MRU to LRU order) without modifying access order.
    template <typename Func>
        requires std::invocable<Func, const Key&, const Value&>
    void for_each(Func&& func) const {
        for (const auto& [key, value] : order_) {
            func(key, value);
        }
    }

    // Non-const overload: allows mutation of values but not keys.
    template <typename Func>
        requires std::invocable<Func, const Key&, Value&>
    void for_each(Func&& func) {
        for (auto& entry : order_) {
            func(std::as_const(entry.first), entry.second);
        }
    }

    [[nodiscard]] std::size_t size() const {
        return map_.size();
    }

    [[nodiscard]] std::size_t max_size() const {
        return max_size_;
    }

private:
    void check_invariant() const {
        assert(map_.size() == order_.size());
    }

    std::size_t max_size_;

    // Entries stored in LRU order: front = most recently used, back = least recently used.
    using Entry = std::pair<Key, Value>;
    std::list<Entry> order_;

    // Maps each key to its position in the order list for O(1) access.
    std::unordered_map<Key, typename std::list<Entry>::iterator, Hash> map_;
};

} // namespace luma

#endif // LUMA_COMMON_LRU_CACHE_HPP
