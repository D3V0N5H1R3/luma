#ifndef LUMA_STDLIB_GRAPHICALUI_CSS_HPP
#define LUMA_STDLIB_GRAPHICALUI_CSS_HPP

#include <optional>
#include <string>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "common/string_utils.hpp"

namespace luma::gui_detail {

// Check if a CSS property name (in underscore form) is a known CSS property.
// Handles reserved keys (class, id, role, aria_*, on_*), pseudo-class prefixes
// (hover_, focus_, active_, disabled_, focus_within_), and custom properties (--*).
[[nodiscard]] bool is_known_css_property(const std::string& key);

// Find the closest matching CSS property for typo suggestions.
// Returns an empty string if no close match (Levenshtein distance >= 4) is found.
[[nodiscard]] std::string suggest_css_property(const std::string& key);

// Sanitise user-loaded CSS using allowlist-based token filtering.
// Strips dangerous at-rules, unknown CSS functions, and unsafe url() schemes.
[[nodiscard]] std::string sanitise_loaded_css(const std::string& css);

// Reject inline CSS (from GraphicalUi.stylesheet) that contains dangerous
// substrings: <script, javascript:/vbscript: URL schemes, expression(),
// -moz-binding:, url(), and @import.  Matching is case-insensitive.  Throws a
// RuntimeError naming the offending pattern when a match is found.
void validate_inline_css(const std::string& css, SourceLocation loc);

// Validate a stylesheet path passed to GraphicalUi.load_stylesheet.  The path
// must end in ".css", be relative (no absolute paths), contain no URL scheme,
// and contain no ".." traversal sequence.  Throws a RuntimeError on the first
// violation; returns normally when the path is safe.
void validate_stylesheet_path(const std::string& path, SourceLocation loc);

// The MIME type and CSS src format() token for a bundled font file, derived from
// its extension.  Both members reference static storage and stay valid for the
// program's lifetime.
struct FontFormat {
    std::string_view mime;   // e.g. "font/woff2"
    std::string_view format; // e.g. "woff2" (the CSS src ... format("...") token)
};

// Map a font path's extension to its FontFormat.  Recognises .woff2, .woff,
// .ttf, and .otf (case-insensitively).  Returns std::nullopt for any other
// extension.  This is the single source of truth shared by validate_font_path
// (which rejects unknown extensions) and the font_face command handler (which
// builds the data: URI).
[[nodiscard]] std::optional<FontFormat> font_format_for_path(const std::string& path);

// Validate a font path passed to GraphicalUi.font_face.  The path must end in a
// supported font extension (see font_format_for_path), be relative (no absolute
// paths), contain no URL scheme, and contain no ".." traversal sequence.  Throws
// a RuntimeError on the first violation; returns normally when the path is safe.
void validate_font_path(const std::string& path, SourceLocation loc);

// Validate a font-family name passed to GraphicalUi.font_face.  Only letters,
// digits, spaces, hyphens, and underscores are allowed, and the name must be
// non-empty and at most 64 characters.  This keeps the name safe to embed inside
// a quoted CSS string.  Throws a RuntimeError when the name is invalid.
void validate_font_family(const std::string& family, SourceLocation loc);

// Validate a font-weight value passed to GraphicalUi.font_face.  Accepts the CSS
// keywords normal/bold/bolder/lighter and numeric weights (digits and spaces,
// e.g. "400" or a "100 900" variable-font range).  Throws a RuntimeError on an
// unrecognised value.
void validate_font_weight(const std::string& weight, SourceLocation loc);

// Validate a font-style value passed to GraphicalUi.font_face.  Accepts the CSS
// keywords normal/italic/oblique.  Throws a RuntimeError on an unrecognised
// value.
void validate_font_style(const std::string& style, SourceLocation loc);

} // namespace luma::gui_detail

#endif // LUMA_STDLIB_GRAPHICALUI_CSS_HPP
