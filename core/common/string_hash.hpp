#ifndef LUMA_COMMON_STRING_HASH_HPP
#define LUMA_COMMON_STRING_HASH_HPP

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace luma {

// Transparent hash functor for std::string keys.
//
// When used with std::equal_to<> (the transparent comparator), this
// enables heterogeneous lookup on std::unordered_map/set — allowing
// std::string_view keys to be passed to find(), contains(), count(),
// etc. without constructing a temporary std::string.
//
// Uses std::hash internally (not fnv1a_hash) for compatibility with
// standard library container requirements.
//
// Usage:
//   StringMap<int> counts;          // unordered_map<string, int>
//   counts["hello"] = 1;
//   counts.find(some_string_view);  // works without allocation
//
// See also:
//   common/hash.hpp   — fnv1a_hash() for internal hash tables and cache keys
//   common/crc32.hpp  — crc32_hash() for protocol checksums
struct StringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }

    [[nodiscard]] std::size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string>{}(s);
    }

    [[nodiscard]] std::size_t operator()(const char* s) const noexcept {
        return std::hash<std::string_view>{}(std::string_view{s});
    }
};

// Convenience aliases for string-keyed containers with transparent lookup.
template <typename V>
using StringMap = std::unordered_map<std::string, V, StringHash, std::equal_to<>>;

using StringSet = std::unordered_set<std::string, StringHash, std::equal_to<>>;

} // namespace luma

#endif // LUMA_COMMON_STRING_HASH_HPP
