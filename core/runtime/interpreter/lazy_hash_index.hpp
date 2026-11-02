#ifndef LUMA_INTERPRETER_LAZY_HASH_INDEX_HPP
#define LUMA_INTERPRETER_LAZY_HASH_INDEX_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace luma {

namespace detail {

// Striped locks serialising the install step of LazyHashIndex builds (never
// the build itself — see ensure_built).  An install happens at most a few
// times per index, so contention is negligible; striping keeps the per-object
// cost at zero bytes instead of a std::mutex in every dictionary, record, and
// set.  No other lock is ever taken while one is held.  Leaked on purpose so a
// lookup during static destruction never touches a dead mutex.
[[nodiscard]] inline std::mutex& lazy_index_build_mutex(const void* owner) noexcept {
    static constexpr std::size_t k_stripes{64};
    static auto* const mutexes = new std::array<std::mutex, k_stripes>{};
    const auto address = reinterpret_cast<std::uintptr_t>(owner);

    // Drop the low bits (allocation alignment) before picking a stripe.
    return (*mutexes)[(address >> 4U) % k_stripes];
}

} // namespace detail

/// A lazily-built hash index over a vector of key-value pairs.
/// Rebuilds automatically when invalidated.
///
/// ── Thread safety ──
/// The owning value (dictionary, record, set) is shared between task threads:
/// a spawned task's global snapshot is shared by its siblings, and values reach
/// other tasks through Reference.get or task results.  Two tasks can therefore
/// look up keys in the same container concurrently, and the first lookup on a
/// large container builds the index.  `built_` is read with acquire ordering
/// (the hot lookup path — one load).  A missing index is built into a local
/// map without any lock (building may hash and compare compound keys, which
/// look up other containers and build *their* indexes), then installed under a
/// striped mutex and published with a release store; a racing duplicate build
/// is discarded.
///
/// Mutation (insert, raw, invalidate, clear) is only legal on an owner that is
/// exclusively held — the VM's copy-on-write discipline (make_exclusive /
/// ensure_unique / clone_*) guarantees no other thread can observe it then.
template <typename Key, typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
class LazyHashIndex {
public:
    using index_type = std::unordered_map<Key, std::size_t, Hash, Equal>;

    LazyHashIndex() = default;
    ~LazyHashIndex() = default;

    // A copy starts unbuilt: its owner copies the storage vector, and the copy
    // rebuilds its own index on first lookup.  Copying the source's map would
    // race with a concurrent lazy build of the (shared) source.
    LazyHashIndex(const LazyHashIndex& /*other*/) {}

    LazyHashIndex& operator=(const LazyHashIndex& other) {
        if (this != &other) {
            clear();
        }
        return *this;
    }

    // A move transfers the built map.  Only an exclusively owned index is ever
    // moved from, so no lookup can race with it.
    LazyHashIndex(LazyHashIndex&& other) noexcept(std::is_nothrow_move_constructible_v<index_type>)
        : index_{std::move(other.index_)},
          built_{other.built_.exchange(false, std::memory_order_relaxed)} {}

    LazyHashIndex&
    operator=(LazyHashIndex&& other) noexcept(std::is_nothrow_move_assignable_v<index_type>) {
        if (this != &other) {
            index_ = std::move(other.index_);
            built_.store(other.built_.exchange(false, std::memory_order_relaxed),
                         std::memory_order_relaxed);
        }
        return *this;
    }

    /// Marks the index as stale. Next lookup will trigger a rebuild.
    /// Precondition: the owner is exclusively held.
    void invalidate() noexcept {
        built_.store(false, std::memory_order_relaxed);
    }

    /// Returns true if the index needs rebuilding.
    [[nodiscard]] bool is_stale() const noexcept {
        return !built_.load(std::memory_order_acquire);
    }

    /// Returns true if the index has been built and is current.
    [[nodiscard]] bool is_built() const noexcept {
        return built_.load(std::memory_order_acquire);
    }

    /// Looks up a key, rebuilding the index first if stale.
    /// @param key     The key to find.
    /// @param rebuild A callable that populates the index: void(index_type&).
    /// @return        Pointer to the mapped value, or nullptr if not found.
    template <typename RebuildFn>
    [[nodiscard]] const std::size_t* find(const Key& key, RebuildFn&& rebuild) const {
        ensure_built(std::forward<RebuildFn>(rebuild));
        auto it = index_.find(key);
        return it != index_.end() ? &it->second : nullptr;
    }

    /// Heterogeneous overload — accepts any key type compatible with the transparent
    /// hasher/comparator (e.g. std::string_view when Key=std::string and Equal=std::equal_to<>).
    /// Avoids constructing a Key object for lookup-only operations.
    template <typename K, typename RebuildFn>
        requires requires {
            typename Equal::is_transparent;
            typename Hash::is_transparent;
        }
    [[nodiscard]] const std::size_t* find(const K& key, RebuildFn&& rebuild) const {
        ensure_built(std::forward<RebuildFn>(rebuild));
        auto it = index_.find(key);
        return it != index_.end() ? &it->second : nullptr;
    }

    /// Inserts a key-index pair into the index without a full rebuild.
    /// Precondition: the index must already be built (is_built() == true).
    /// Throws std::logic_error if the index is stale — call rebuild() first.
    void insert(const Key& key, std::size_t idx) {
        if (!is_built()) {
            // Thrown when the hash index is modified (insert/erase) while
            // iterating. This is a programmer error.
            throw std::logic_error(
                "LazyHashIndex::insert() called on stale index; call rebuild() first");
        }
        index_.emplace(key, idx);
    }

    /// Provides direct access to the underlying map for in-place updates.
    /// Precondition: the owner is exclusively held.
    [[nodiscard]] index_type& raw() noexcept {
        return index_;
    }

    [[nodiscard]] const index_type& raw() const noexcept {
        return index_;
    }

    /// Clears the index and marks it as stale.
    /// Precondition: the owner is exclusively held.
    void clear() noexcept {
        index_.clear();
        built_.store(false, std::memory_order_relaxed);
    }

    /// Forces a rebuild using the provided callable.
    template <typename RebuildFn> void rebuild(RebuildFn&& rebuild) const {
        index_type fresh;
        std::forward<RebuildFn>(rebuild)(fresh);
        publish(std::move(fresh), /*replace_built=*/true);
    }

private:
    mutable index_type index_;
    mutable std::atomic<bool> built_{false};

    // Double-checked lazy build: the acquire load is the whole cost of a
    // lookup once the index exists.
    //
    // The index is built into a local map with NO lock held: building hashes
    // and compares keys, and for compound keys (a set of sets, a set of large
    // dictionaries) equality looks up the *other* container, which builds that
    // container's index in turn.  Running that under a striped lock nested one
    // stripe lock inside another — a self-deadlock when both map to the same
    // stripe and a lock-order inversion between two threads.  The stripe lock
    // now only guards the brief install step; a thread that loses the race
    // discards its duplicate build.
    template <typename RebuildFn> void ensure_built(RebuildFn&& rebuild) const {
        if (built_.load(std::memory_order_acquire)) [[likely]] {
            return;
        }

        index_type fresh;
        std::forward<RebuildFn>(rebuild)(fresh);
        publish(std::move(fresh), /*replace_built=*/false);
    }

    // Install a freshly built map and release-store `built_`.  Only hashing-
    // free work (two map moves) runs under the stripe lock; the previous map
    // (a stale index or a losing duplicate build) is destroyed after it is
    // released, since destroying Value keys may run arbitrary teardown.
    void publish(index_type fresh, bool replace_built) const {
        index_type discarded;

        {
            const std::scoped_lock lock{detail::lazy_index_build_mutex(this)};

            if (!replace_built && built_.load(std::memory_order_relaxed)) {
                discarded = std::move(fresh); // Another thread installed it first.
            } else {
                discarded = std::move(index_);
                index_ = std::move(fresh);
                built_.store(true, std::memory_order_release);
            }
        }
    }
};

} // namespace luma

#endif // LUMA_INTERPRETER_LAZY_HASH_INDEX_HPP
