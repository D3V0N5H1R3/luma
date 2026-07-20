#ifndef LUMA_COMPILER_NAME_TABLE_HPP
#define LUMA_COMPILER_NAME_TABLE_HPP

#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "common/string_hash.hpp"
#include "runtime/compiler/compiler_limits.hpp"

namespace luma {

// ─────────────────────────────────────────────────────────────────────────────
// NameTable — interned table of global and field names referenced by bytecode.
//
// Extracted from Chunk (TODO refactor/C5).  Owns the name vector together with
// the O(1) deduplication index that was previously a private Chunk member, so
// the two can no longer drift out of sync.  Indices are stable 16-bit handles
// emitted directly into the bytecode stream.
// ─────────────────────────────────────────────────────────────────────────────
class NameTable {
public:
    // Maximum number of entries — bounded by the 16-bit index space.
    static constexpr std::size_t k_max_size{CompilerLimits::k_max_names};

    // Intern `name`, returning its stable index.  Deduplicates: an already
    // present name maps to its existing index.  Throws std::overflow_error when
    // the table is full.
    [[nodiscard]] std::uint16_t add(std::string_view name) {
        if (auto it = index_.find(name); it != index_.end()) {
            return it->second;
        }

        if (names_.size() >= k_max_size) {
            throw std::overflow_error{
                std::format("Name table overflow (more than {} names)", k_max_size)};
        }

        const auto idx = static_cast<std::uint16_t>(names_.size());
        names_.emplace_back(name);
        index_[names_.back()] = idx;
        return idx;
    }

    [[nodiscard]] const std::string& operator[](std::size_t i) const {
        return names_[i];
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return names_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return names_.empty();
    }

    void reserve(std::size_t n) {
        names_.reserve(n);
    }

    [[nodiscard]] auto begin() const noexcept {
        return names_.begin();
    }

    [[nodiscard]] auto end() const noexcept {
        return names_.end();
    }

private:
    std::vector<std::string> names_;
    StringMap<std::uint16_t> index_; // O(1) dedup index.
};

// The .lumc format serialises the name count as a u16 (bytecode_serializer.cpp),
// so the table cap must fit that field; a larger cap would wrap the count on write.
static_assert(NameTable::k_max_size <= std::numeric_limits<std::uint16_t>::max(),
              "name table cap must fit the u16 count field in the .lumc format");

} // namespace luma

#endif // LUMA_COMPILER_NAME_TABLE_HPP
