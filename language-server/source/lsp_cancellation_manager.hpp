#ifndef LUMA_LSP_CANCELLATION_MANAGER_HPP
#define LUMA_LSP_CANCELLATION_MANAGER_HPP

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_set>

#include "json/json.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════════════════
// CancellationManager — thread-safe tracking of cancelled request IDs.
//
// The LSP client may send $/cancelRequest notifications before the
// corresponding request has been dispatched.  CancellationManager
// records these IDs in a bounded set and lets the dispatch layer
// check (and clear) them atomically.
//
// Owns its own mutex — independent of state_mutex_ / write_mutex_.
// ═══════════════════════════════════════════════════════════════════════

class CancellationManager {
public:
    // Record that a request ID was cancelled by the client.
    void cancel(const luma::json::JsonValue& id) {
        auto id_str = normalise_id(id);
        const std::lock_guard lock(mutex_);
        cancelled_ids_.insert(std::move(id_str));
        evict_if_needed();
    }

    // Check if a request was cancelled and clear the entry.
    // Returns true if the ID was found (and removed).
    bool check_and_clear(const luma::json::JsonValue& id) {
        auto id_str = normalise_id(id);
        const std::lock_guard lock(mutex_);
        auto it = cancelled_ids_.find(id_str);
        if (it != cancelled_ids_.end()) {
            cancelled_ids_.erase(it);
            return true;
        }
        return false;
    }

    // Number of outstanding cancelled IDs.
    [[nodiscard]] std::size_t size() const {
        const std::lock_guard lock(mutex_);
        return cancelled_ids_.size();
    }

private:
    static constexpr std::size_t k_max_id_length = 256;
    static constexpr std::size_t k_max_cancelled_ids = 64;

    [[nodiscard]] static std::string normalise_id(const luma::json::JsonValue& id) {
        auto id_str = id.to_string();
        if (id_str.size() > k_max_id_length) {
            id_str.resize(k_max_id_length);
        }
        return id_str;
    }

    void evict_if_needed() {
        while (cancelled_ids_.size() > k_max_cancelled_ids) {
            cancelled_ids_.erase(cancelled_ids_.begin());
        }
    }

    mutable std::mutex mutex_;
    std::unordered_set<std::string> cancelled_ids_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_CANCELLATION_MANAGER_HPP
