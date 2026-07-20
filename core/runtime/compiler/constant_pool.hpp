#ifndef LUMA_COMPILER_CONSTANT_POOL_HPP
#define LUMA_COMPILER_CONSTANT_POOL_HPP

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/string_hash.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// Manages the constant pool for a bytecode chunk.
//
// Constants are deduplicated by value: identical integer, string, and
// floating-point literals share a single pool slot.  Doubles are keyed
// by bit pattern (via std::bit_cast) so that 0.0 / -0.0 and distinct
// NaN encodings remain separate entries.
//
// The pool is capped at 65 535 entries: the .lumc format serialises the
// per-chunk constant count as a u16, so a 65 536th entry would wrap the count
// to 0 on write.
class ConstantPool {
public:
    static constexpr std::size_t max_size{CompilerLimits::k_max_constants};

    // Add a constant, deduplicating integers, strings, and doubles.
    // Returns the u16 pool index.
    [[nodiscard]] std::uint16_t add(Value value) {
        if (value.is_integer()) {
            return add_dedup(value, value.as_integer(), int_indices_);
        }

        if (value.is_string()) {
            return add_dedup(value, value.as_string(), string_indices_);
        }

        if (value.is_number()) {
            // Use bit_cast to distinguish 0.0 from -0.0 (and different NaN
            // bit patterns), since IEEE 754 equality treats them as equal.
            const auto bits = std::bit_cast<std::uint64_t>(value.as_number());
            return add_dedup(value, bits, double_indices_);
        }

        ensure_space();

        values_.push_back(std::move(value));

        return static_cast<std::uint16_t>(values_.size() - 1);
    }

    // --- element access --------------------------------------------------

    [[nodiscard]] const Value& operator[](std::size_t index) const {
        return values_[index];
    }

    [[nodiscard]] Value& operator[](std::size_t index) {
        return values_[index];
    }

    // --- capacity --------------------------------------------------------

    [[nodiscard]] std::size_t size() const noexcept {
        return values_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return values_.empty();
    }

    void reserve(std::size_t capacity) {
        values_.reserve(capacity);
    }

    // --- modifiers (non-dedup) -------------------------------------------

    // Append a value without deduplication.  Used by the bytecode
    // deserialiser, which has already-serialised pool entries.
    void push_back(Value value) {
        ensure_space();
        values_.push_back(std::move(value));
    }

    // --- iteration -------------------------------------------------------

    [[nodiscard]] auto begin() const noexcept {
        return values_.begin();
    }

    [[nodiscard]] auto end() const noexcept {
        return values_.end();
    }

    [[nodiscard]] auto begin() noexcept {
        return values_.begin();
    }

    [[nodiscard]] auto end() noexcept {
        return values_.end();
    }

private:
    void ensure_space() {
        if (values_.size() >= max_size) {
            throw std::overflow_error{
                std::format("Constant pool overflow (more than {} constants)", max_size)};
        }
    }

    // Deduplication helper: look up a key in the given map, returning the
    // existing index if found, otherwise appending the value to the pool.
    template <typename Key, typename Map>
    [[nodiscard]] std::uint16_t add_dedup(Value& value, const Key& key, Map& dedup_map) {
        if (auto it = dedup_map.find(key); it != dedup_map.end()) {
            return it->second;
        }

        ensure_space();

        auto idx = static_cast<std::uint16_t>(values_.size());

        // Record the map entry before moving the value, since `key` may
        // be a reference into `value` (e.g. for strings).
        dedup_map[key] = idx;
        values_.push_back(std::move(value));

        return idx;
    }

    std::vector<Value> values_;
    std::unordered_map<std::int64_t, std::uint16_t> int_indices_;
    // String constant deduplication: identical string literals share one pool
    // entry, so Op::Constant on the same string literal only copies the Value
    // struct (not the string content).
    StringMap<std::uint16_t> string_indices_;
    std::unordered_map<std::uint64_t, std::uint16_t> double_indices_; // Keyed by bit pattern.
};

// The .lumc format serialises the constant count as a u16 (bytecode_serializer.cpp),
// so the pool cap must fit that field; a larger cap would wrap the count on write.
static_assert(ConstantPool::max_size <= std::numeric_limits<std::uint16_t>::max(),
              "constant pool cap must fit the u16 count field in the .lumc format");

} // namespace luma

#endif // LUMA_COMPILER_CONSTANT_POOL_HPP
