#ifndef LUMA_SOURCE_SOURCE_LOCATION_HPP
#define LUMA_SOURCE_SOURCE_LOCATION_HPP

#include <compare>

namespace luma {

/// Unique identifier for a loaded source file.
/// 0 is reserved for synthetic/unknown locations (e.g. "<input>", "<repl>").
using FileId = int;

/// Represents a position in a source file (file, line, column).
///
/// A location copied from a token points ONE PAST the token's last character
/// (`line`/`column`), and also records where that token begins
/// (`start_line`/`start_column`).  AST nodes, the compiler's source maps and the
/// language server all copy token locations, so the start travels with them and
/// diagnostics can put the caret on the token itself (see start()).  A
/// `start_line` of 0 means "start unknown" — locations built by hand rather
/// than copied from a token.
struct SourceLocation {
    FileId file_id{0};
    int line{1};
    int column{1};
    int start_line{0};
    int start_column{0};

    /// Returns true if this location records where its token begins.
    [[nodiscard]] constexpr bool has_start() const noexcept {
        return start_line > 0 && start_column > 0;
    }

    /// The position of the token's first character, or this location itself
    /// when the start is unknown.  The result carries no start of its own.
    [[nodiscard]] constexpr SourceLocation start() const noexcept {
        if (!has_start()) {
            return {.file_id = file_id, .line = line, .column = column};
        }

        return {.file_id = file_id, .line = start_line, .column = start_column};
    }

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

    // The start is display metadata derived from the same token, so identity and
    // ordering are by (file, line, column) alone: a hand-built location equals
    // the token location it names.
    [[nodiscard]] constexpr bool operator==(const SourceLocation& other) const noexcept {
        return file_id == other.file_id && line == other.line && column == other.column;
    }

    [[nodiscard]] constexpr std::strong_ordering
    operator<=>(const SourceLocation& other) const noexcept {
        if (const auto order = file_id <=> other.file_id; order != 0) {
            return order;
        }

        if (const auto order = line <=> other.line; order != 0) {
            return order;
        }

        return column <=> other.column;
    }
};

} // namespace luma

#endif // LUMA_SOURCE_SOURCE_LOCATION_HPP
