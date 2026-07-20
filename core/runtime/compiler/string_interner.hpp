#ifndef LUMA_RUNTIME_COMPILER_STRING_INTERNER_HPP
#define LUMA_RUNTIME_COMPILER_STRING_INTERNER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/string_hash.hpp"

namespace luma {

// Interned string identifier — a lightweight handle (4 bytes) that
// refers to a string stored in a StringInterner.  Two InternedStrings
// from the same interner can be compared for equality in O(1) via
// their numeric IDs, rather than comparing characters.
class InternedString {
public:
    constexpr InternedString() = default;

    [[nodiscard]] constexpr std::uint32_t id() const noexcept {
        return id_;
    }

    [[nodiscard]] constexpr bool operator==(const InternedString& other) const noexcept {
        return id_ == other.id_;
    }

    [[nodiscard]] constexpr bool operator!=(const InternedString& other) const noexcept {
        return id_ != other.id_;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id_ != 0;
    }

private:
    friend class StringInterner;

    explicit constexpr InternedString(std::uint32_t id) : id_{id} {}

    std::uint32_t id_{0}; // 0 = invalid / not interned.
};

// Deduplicating string storage.  Each unique string is stored exactly
// once and assigned a compact numeric ID.  Lookups and interning are
// O(1) amortised.  The interner owns the string data — all returned
// string_views remain valid for the lifetime of the interner.
//
// ┌──────────────────────────────────────────────────────────────────┐
// │  WARNING — NOT THREAD-SAFE                                      │
// │                                                                  │
// │  All operations (intern, resolve, size) must be externally       │
// │  synchronised if the interner is shared across threads.          │
// │  A typical approach is to hold a std::mutex while calling any    │
// │  method, or to use a per-thread interner.  Concurrent calls to   │
// │  intern() will corrupt internal data structures.                 │
// └──────────────────────────────────────────────────────────────────┘
class StringInterner {
public:
    StringInterner() {
        // Reserve ID 0 as the invalid sentinel.
        strings_.emplace_back();
    }

    // Intern a string: returns its unique handle.  If the string was
    // already interned, the existing handle is returned.
    [[nodiscard]] InternedString intern(std::string_view str) {
        if (auto it = lookup_.find(str); it != lookup_.end()) {
            return InternedString{it->second};
        }

        auto id = static_cast<std::uint32_t>(strings_.size());

        strings_.emplace_back(str);

        // Key on the stored string (stable lifetime for the map key).
        lookup_[strings_.back()] = id;

        return InternedString{id};
    }

    // Resolve an InternedString back to its text.
    [[nodiscard]] std::string_view resolve(InternedString s) const {
        if (s.id() < strings_.size()) {
            return strings_[s.id()];
        }

        return {};
    }

    // Number of unique strings interned (excluding the sentinel).
    [[nodiscard]] std::size_t size() const noexcept {
        return strings_.size() - 1;
    }

private:
    // Ordered storage — index == ID.
    std::vector<std::string> strings_;

    // Reverse map: string content → ID.  Keys are std::string copies
    // (not string_views) so they remain valid even when the strings_
    // vector reallocates.  StringMap's transparent StringHash allows
    // heterogeneous lookup by string_view without constructing a
    // temporary std::string.
    StringMap<std::uint32_t> lookup_;
};

} // namespace luma

// Hash support for InternedString so it can be used as a map key.
template <> struct std::hash<luma::InternedString> {
    [[nodiscard]] std::size_t operator()(const luma::InternedString& s) const noexcept {
        return std::hash<std::uint32_t>{}(s.id());
    }
};

#endif // LUMA_RUNTIME_COMPILER_STRING_INTERNER_HPP
