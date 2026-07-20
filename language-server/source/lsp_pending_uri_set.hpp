#ifndef LUMA_LSP_PENDING_URI_SET_HPP
#define LUMA_LSP_PENDING_URI_SET_HPP

#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace luma::lsp {

// Thread-safe set of URIs pending re-analysis.
//
// Internally uses an unordered_set to deduplicate URIs automatically.
// All public methods acquire an internal mutex, so callers do not need
// to hold any external lock.
class PendingUriSet {
public:
    // Insert a URI into the pending set (no-op if already present).
    void insert(std::string uri) {
        const std::lock_guard lock(mutex_);
        uris_.insert(std::move(uri));
    }

    // Insert all URIs from `uris` into the pending set.
    void insert_all(std::vector<std::string> uris) {
        const std::lock_guard lock(mutex_);
        for (auto& uri : uris) {
            uris_.insert(std::move(uri));
        }
    }

    // Returns true if `uri` is currently in the pending set.
    [[nodiscard]] bool is_pending(const std::string& uri) const {
        const std::shared_lock lock(mutex_);
        return uris_.contains(uri);
    }

    // Remove `uri` from the pending set (no-op if not present).
    void remove(const std::string& uri) {
        const std::lock_guard lock(mutex_);
        uris_.erase(uri);
    }

    // Remove and return all pending URIs as a vector.
    // The set is empty after this call. The lock is held only for a cheap
    // swap; the URIs are moved into the result vector outside the lock so
    // that concurrent callers are not blocked during the copy.
    [[nodiscard]] std::vector<std::string> drain_all() {
        std::unordered_set<std::string> drained;
        {
            const std::lock_guard lock(mutex_);
            drained.swap(uris_);
        }

        std::vector<std::string> result;
        result.reserve(drained.size());
        while (!drained.empty()) {
            auto node = drained.extract(drained.begin());
            result.push_back(std::move(node.value()));
        }
        return result;
    }

    // Returns true if there are no pending URIs.
    [[nodiscard]] bool empty() const {
        const std::shared_lock lock(mutex_);
        return uris_.empty();
    }

    // Returns the number of pending URIs.
    [[nodiscard]] std::size_t size() const {
        const std::shared_lock lock(mutex_);
        return uris_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_set<std::string> uris_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_PENDING_URI_SET_HPP
