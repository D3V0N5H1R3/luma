#ifndef LUMA_COMPILER_SOURCE_MAP_HPP
#define LUMA_COMPILER_SOURCE_MAP_HPP

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "analysis/source/source_location.hpp"

namespace luma {

// ─────────────────────────────────────────────────────────────────────────────
// SourceMap — instruction-offset → source-location mapping.
//
// Extracted from Chunk (TODO refactor/C5) so the bytecode container delegates
// source-location bookkeeping to a focused type.  Stores one entry per opcode
// (not per operand byte) — a sparse map resolved by binary search, saving ~60%
// memory versus a per-byte table.
// ─────────────────────────────────────────────────────────────────────────────
class SourceMap {
public:
    using Entry = std::pair<std::size_t, SourceLocation>;

    SourceMap() = default;

    // Construct from a prebuilt entry list (used by optimizer compaction, which
    // rewrites every offset in one pass).
    explicit SourceMap(std::vector<Entry> entries) : entries_{std::move(entries)} {}

    // Append a mapping for the instruction beginning at byte `offset`.
    void append(std::size_t offset, SourceLocation loc) {
        entries_.emplace_back(offset, loc);
    }

    // Resolve the source location for a given instruction byte offset.
    // Uses binary search on the sparse map; returns a default-constructed
    // location when the map is empty.
    [[nodiscard]] SourceLocation location_at(std::size_t offset) const {
        if (entries_.empty()) {
            return {};
        }

        // Find the last entry with byte_offset <= offset.
        auto it =
            std::upper_bound(entries_.begin(), entries_.end(), offset,
                             [](std::size_t val, const Entry& entry) { return val < entry.first; });

        if (it == entries_.begin()) {
            return entries_.front().second;
        }

        --it;
        return it->second;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return entries_.empty();
    }

    void reserve(std::size_t n) {
        entries_.reserve(n);
    }

    void clear() noexcept {
        entries_.clear();
    }

    [[nodiscard]] auto begin() const noexcept {
        return entries_.begin();
    }

    [[nodiscard]] auto end() const noexcept {
        return entries_.end();
    }

private:
    std::vector<Entry> entries_;
};

} // namespace luma

#endif // LUMA_COMPILER_SOURCE_MAP_HPP
