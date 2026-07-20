#ifndef LUMA_SOURCE_SOURCE_LOCATION_HPP
#define LUMA_SOURCE_SOURCE_LOCATION_HPP

#include <compare>

namespace luma {

/// Unique identifier for a loaded source file.
/// 0 is reserved for synthetic/unknown locations (e.g. "<input>", "<repl>").
using FileId = int;

/// Represents a position in a source file (file, line, column).
struct SourceLocation {
    FileId file_id{0};
    int line{1};
    int column{1};

    /// Returns true if this location carries a usable position.
    ///
    /// A location counts as valid when it names a real file (file_id != 0) OR
    /// a real line (line != 0).  The `|| line != 0` half is deliberate:
    /// synthetic sources such as the REPL and "<input>" legitimately use
    /// file_id == 0 together with a genuine 1-based line, so file_id == 0 alone
    /// must NOT be read as "no location".
    ///
    /// IMPORTANT: a *default-constructed* `SourceLocation{}` is `{0, 1, 1}`, so
    /// `line == 1` and it is therefore VALID (truthy).  Do not treat a
    /// default-constructed location as an "absent" sentinel —
    /// `SourceLocation loc = {}; if (loc.is_valid())` is true, not false.  When
    /// you need to represent the *absence* of a location, use
    /// `std::optional<SourceLocation>` rather than relying on this conversion.
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return file_id != 0 || line != 0;
    }

    /// Explicit bool conversion, equivalent to is_valid().  See is_valid() for
    /// the crucial caveat: a default-constructed location is truthy.
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return is_valid();
    }

    [[nodiscard]] auto operator<=>(const SourceLocation&) const = default;
};

} // namespace luma

#endif // LUMA_SOURCE_SOURCE_LOCATION_HPP
