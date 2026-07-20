#ifndef LUMA_LSP_STRING_UTILS_HPP
#define LUMA_LSP_STRING_UTILS_HPP

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

#include "common/narrow_int.hpp"
#include "common/utf8.hpp"
#include "json/json_helpers.hpp"
#include "symbols/qualified_name.hpp"

// Focused headers split from this file — include them directly when
// only type-annotation rendering or parameter-list parsing is needed.
#include "lsp_param_utils.hpp"
#include "lsp_type_formatter.hpp"

namespace luma::lsp::util {

// Re-export the shared UTF-8 continuation check for convenience.
using luma::is_utf8_continuation;

// Re-export the shared integer narrowing helper.
// LSP line and character positions are always non-negative and small.
using luma::clamp_to_int;

// Re-export the shared qualified-name predicate. A name is namespace-qualified
// when it contains a '.', e.g. "Module.member" or "Choice.Variant" — used to
// distinguish top-level symbols from members nested under a namespace prefix.
using luma::is_qualified_name;

// Convert a string to lowercase (ASCII-only). Returns a new string.
[[nodiscard]] inline std::string to_lower(std::string s) {
    std::ranges::transform(s, s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Case-insensitive substring test with no allocation.  `needle_lower` must
// already be lowercase (ASCII); `haystack` is lowered on the fly during the
// comparison.  Returns true when `haystack` contains `needle_lower`.
[[nodiscard]] inline bool contains_ci_lower(std::string_view haystack,
                                            std::string_view needle_lower) {
    if (needle_lower.empty()) {
        return true;
    }
    if (needle_lower.size() > haystack.size()) {
        return false;
    }
    const auto it = std::ranges::search(haystack, needle_lower, [](char h, char n) {
                        return static_cast<char>(std::tolower(static_cast<unsigned char>(h))) == n;
                    }).begin();
    return it != haystack.end();
}

// Validate that `name` is a legal Luma identifier, mirroring the lexer's
// identifier rules (core/analysis/lexer/lexer.hpp is_alpha/is_alnum): an ASCII
// letter or underscore start, then ASCII letters, digits, or underscores — plus
// any UTF-8 multibyte byte (>= 0x80) in either position so Unicode letters
// (e.g. `café`, `π`, `名前`) are accepted just as the lexer accepts them in
// source. Used by rename to reject illegal new names before editing.
[[nodiscard]] inline bool is_valid_identifier(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    const auto is_ident_start = [](unsigned char c) {
        return std::isalpha(c) != 0 || c == '_' || c >= 0x80;
    };
    const auto is_ident_continue = [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c >= 0x80;
    };
    if (!is_ident_start(static_cast<unsigned char>(name.front()))) {
        return false;
    }
    for (std::size_t i{1}; i < name.size(); ++i) {
        if (!is_ident_continue(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace luma::lsp::util

#endif // LUMA_LSP_STRING_UTILS_HPP
