#ifndef LUMA_COMMON_ENUM_NAME_HPP
#define LUMA_COMMON_ENUM_NAME_HPP

#include <cstddef>
#include <string_view>

namespace luma {

/// Maps an enumerator to its display name via an ordered name table indexed by
/// the enumerator's underlying value.  Returns "Unknown" for out-of-range
/// values.
///
/// The name table must list one entry per enumerator, in declaration order.
/// Pair each call with a `static_assert` that ties the table size to the enum
/// so that inserting or removing an enumerator without updating the table fails
/// to compile.
template <typename Enum, std::size_t N>
[[nodiscard]] constexpr std::string_view enum_name(Enum kind,
                                                   const std::string_view (&names)[N]) noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < N ? names[index] : "Unknown";
}

} // namespace luma

#endif // LUMA_COMMON_ENUM_NAME_HPP
