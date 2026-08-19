#ifndef LUMA_DAP_VARIABLE_REFERENCE_REGISTRY_HPP
#define LUMA_DAP_VARIABLE_REFERENCE_REGISTRY_HPP

#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

#include "debugger_config.hpp"

namespace luma::dap {

// ─── Generational entry wrapper ───
// Combines a value with a generation counter to detect stale references
// without maintaining a separate generation map.
template <typename T> struct GenerationalEntry {
    T value;
    int generation{0};

    [[nodiscard]] bool is_stale(int current_gen) const {
        return generation != current_gen;
    }
};

// ─── Purge configuration defaults ───
// Controls how often stale entries are evicted from generational registries.

// Maximum number of entries across both registries before a purge is triggered.
inline constexpr int default_purge_entry_threshold =
    config::variable::k_default_purge_entry_threshold;

// Number of generation advances between automatic purges.
inline constexpr int default_purge_generation_interval =
    config::variable::k_default_purge_generation_interval;

// ─── Generation-based invalidation ───
// When execution resumes after a stop, all variable references become stale
// (the program state may have changed).  Instead of clearing the entire map
// — which is O(n) and loses entries that may still be useful if the client
// re-requests them — we advance a generation counter.  This makes
// invalidation O(1):
//
//   • allocate() stamps each entry with the current generation.
//   • lookup() rejects entries whose generation != current generation.
//   • purge_stale() lazily removes old entries when memory thresholds
//     are reached (amortised cost spread over many operations).
//
// This design is safe because variable references are inherently ephemeral:
// they map DAP integer IDs to Values captured at stop time.  Stale entries
// are harmless (lookup returns nullopt) and evicted periodically.

// ─── Variable reference registry ───
// Reusable container that pairs values with a generation counter for
// O(1) bulk invalidation.  Stale entries are lazily purged.
//
// Thread safety: This class is NOT thread-safe.  Callers must hold an
// external mutex (e.g. the DAP session mutex) before calling any method.
template <typename T> class VariableReferenceRegistry {
public:
    [[nodiscard]] int allocate(T value) {
        if (next_id_ == std::numeric_limits<int>::max()) {
            next_id_ = 1; // wrap safely instead of UB
            entries_.clear();
        }

        const int id = next_id_++;
        entries_[id] = GenerationalEntry<T>{std::move(value), generation_};
        return id;
    }

    [[nodiscard]] std::optional<T> lookup(int id) const {
        auto it = entries_.find(id);

        if (it == entries_.end() || it->second.is_stale(generation_)) {
            return std::nullopt;
        }

        return it->second.value;
    }

    void advance_generation() {
        generation_++;
    }

    void purge_stale() {
        std::erase_if(entries_,
                      [this](const auto& pair) { return pair.second.is_stale(generation_); });
        last_purge_generation_ = generation_;
    }

    // Clear all entries and reset the ID counter.  Advances the generation
    // so any outstanding references become stale.
    void clear_and_reset() {
        entries_.clear();
        next_id_ = 1;
        generation_++;
        last_purge_generation_ = generation_;
    }

    [[nodiscard]] int size() const {
        return static_cast<int>(entries_.size());
    }

    [[nodiscard]] int next_id() const {
        return next_id_;
    }

    [[nodiscard]] int generation() const {
        return generation_;
    }

    [[nodiscard]] int generations_since_purge() const {
        return generation_ - last_purge_generation_;
    }

private:
    // Protected by external mutex (callers must hold lock).
    std::unordered_map<int, GenerationalEntry<T>> entries_;
    int next_id_{1};
    int generation_{0};
    int last_purge_generation_{0};
};

} // namespace luma::dap

#endif // LUMA_DAP_VARIABLE_REFERENCE_REGISTRY_HPP
