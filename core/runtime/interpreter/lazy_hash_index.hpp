#ifndef LUMA_INTERPRETER_LAZY_HASH_INDEX_HPP
#define LUMA_INTERPRETER_LAZY_HASH_INDEX_HPP

#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace luma {

/// A lazily-built hash index over a vector of key-value pairs.
/// Rebuilds automatically when invalidated.
template <typename Key, typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
class LazyHashIndex {
public:
    using index_type = std::unordered_map<Key, std::size_t, Hash, Equal>;

    /// Marks the index as stale. Next lookup will trigger a rebuild.
    void invalidate() noexcept {
        built_ = false;
    }

    /// Returns true if the index needs rebuilding.
    [[nodiscard]] bool is_stale() const noexcept {
        return !built_;
    }

    /// Returns true if the index has been built and is current.
    [[nodiscard]] bool is_built() const noexcept {
        return built_;
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
        if (!built_) {
            // Thrown when the hash index is modified (insert/erase) while
            // iterating. This is a programmer error.
            throw std::logic_error(
                "LazyHashIndex::insert() called on stale index; call rebuild() first");
        }
        index_.emplace(key, idx);
    }

    /// Provides direct access to the underlying map for in-place updates.
    [[nodiscard]] index_type& raw() noexcept {
        return index_;
    }

    [[nodiscard]] const index_type& raw() const noexcept {
        return index_;
    }

    /// Clears the index and marks it as stale.
    void clear() noexcept {
        index_.clear();
        built_ = false;
    }

    /// Forces a rebuild using the provided callable.
    template <typename RebuildFn> void rebuild(RebuildFn&& rebuild) const {
        index_.clear();
        std::forward<RebuildFn>(rebuild)(index_);
        built_ = true;
    }

private:
    mutable index_type index_;
    mutable bool built_{false};

    template <typename RebuildFn> void ensure_built(RebuildFn&& rebuild) const {
        if (!built_) {
            index_.clear();
            std::forward<RebuildFn>(rebuild)(index_);
            built_ = true;
        }
    }
};

} // namespace luma

#endif // LUMA_INTERPRETER_LAZY_HASH_INDEX_HPP
