#ifndef LUMA_COMMON_STRING_UTILS_HPP
#define LUMA_COMMON_STRING_UTILS_HPP

// Prevent Windows min/max macros from interfering with std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace luma {

// Case-insensitive equality comparison for two string views (ASCII).
[[nodiscard]] inline bool case_insensitive_equal(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

// Case-insensitive substring search (ASCII).
// Returns the position of the first occurrence of needle in haystack,
// or std::string_view::npos if not found.
[[nodiscard]] std::size_t case_insensitive_find(std::string_view haystack,
                                                std::string_view needle) noexcept;

/// Returns true if @p haystack contains @p needle (case-insensitive).
[[nodiscard]] inline bool case_insensitive_contains(std::string_view haystack,
                                                    std::string_view needle) noexcept {
    return case_insensitive_find(haystack, needle) != std::string_view::npos;
}

// Return true when `token` occurs in `haystack` as a standalone identifier —
// that is, not immediately preceded or followed by an identifier character
// ([A-Za-z0-9_]).  Used to gate name-triggered behaviour (e.g. built-in prelude
// injection) on a whole-word match so that a longer identifier such as
// `mySolarisHelper` does not falsely match `Solaris`.  This is a
// deliberately cheap lexical heuristic, not a tokenizer: the word still matches
// inside a comment or string literal, which is acceptable for its gating use.
[[nodiscard]] inline bool contains_identifier_token(std::string_view haystack,
                                                    std::string_view token) noexcept {
    if (token.empty()) {
        return false;
    }

    const auto is_ident = [](char c) noexcept {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    };

    for (std::size_t pos = haystack.find(token); pos != std::string_view::npos;
         pos = haystack.find(token, pos + 1)) {
        const bool left_ok = pos == 0 || !is_ident(haystack[pos - 1]);
        const std::size_t after = pos + token.size();
        const bool right_ok = after >= haystack.size() || !is_ident(haystack[after]);

        if (left_ok && right_ok) {
            return true;
        }
    }

    return false;
}

// Compute the Levenshtein (edit) distance between two strings.
// Reusable utility for typo suggestions across modules.
// When max_distance is provided, returns max_distance + 1 if the true
// distance would exceed the threshold (early exit optimisation).
[[nodiscard]] std::size_t levenshtein_distance(std::string_view a, std::string_view b,
                                               std::size_t max_distance = std::string_view::npos);

// Find the closest match to `needle` among `candidates` using Levenshtein distance.
// Returns a formatted "did you mean '...'?" string, or empty if no close match exists.
// Threshold: distance <= 1 for short names (length <= 4), <= 2 otherwise.
[[nodiscard]] std::string suggest_name(const std::vector<std::string_view>& candidates,
                                       std::string_view needle);

// Split a string by a delimiter character into segments.
[[nodiscard]] std::vector<std::string> split_string(std::string_view s, char delimiter);

// Split a string by a delimiter, invoking a callback for each segment
// instead of allocating a vector.  The callback receives std::string_view
// slices into the original string.
template <typename F> void for_each_split(std::string_view str, char delimiter, F&& callback) {
    std::size_t start = 0;

    while (true) {
        const auto pos = str.find(delimiter, start);

        if (pos == std::string_view::npos) {
            callback(str.substr(start));
            break;
        }

        callback(str.substr(start, pos - start));
        start = pos + 1;
    }
}

// Return a lowercase copy of a string (ASCII).
[[nodiscard]] inline std::string to_lower_copy(std::string_view sv) {
    std::string result{sv};
    std::ranges::transform(result, result.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Returns true if the first character is an ASCII uppercase letter. Used to
// distinguish type/record names (Capitalised) from variable names.
[[nodiscard]] inline bool starts_with_uppercase(std::string_view sv) noexcept {
    return !sv.empty() && std::isupper(static_cast<unsigned char>(sv.front())) != 0;
}

// Returns true if the first character is an ASCII letter or digit. Used to
// validate field names, which must begin with an identifier character or a
// tuple-index digit.
[[nodiscard]] inline bool first_is_letter_or_digit(std::string_view sv) noexcept {
    return !sv.empty() && (std::isalpha(static_cast<unsigned char>(sv.front())) != 0 ||
                           std::isdigit(static_cast<unsigned char>(sv.front())) != 0);
}

// Split text into lines at '\n' boundaries, stripping trailing '\r' for CRLF compatibility.
// Uses find + substr for O(N) performance without stream overhead.
[[nodiscard]] std::vector<std::string> split_lines(std::string_view text);

// Strip a UTF-8 Byte Order Mark (BOM) from the beginning of a string, if present.
void strip_utf8_bom(std::string& text);

// Trim leading and trailing ASCII whitespace, returning a view into the
// original string.  No allocation.
[[nodiscard]] inline std::string_view trim_view(std::string_view sv) noexcept {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }

    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }

    return sv;
}

// Trim leading and trailing ASCII whitespace from a string_view.
[[nodiscard]] inline std::string trim(std::string_view sv) {
    return std::string{trim_view(sv)};
}

// Glob-style pattern matching supporting '*' (any sequence, including empty) and
// '?' (exactly one character).  All other characters match literally, byte for
// byte.  This is a linear, backtracking-free matcher (the single remembered '*'
// position bounds the work at O(pattern * text)), so it is safe to run on
// untrusted patterns and subjects — unlike translating the glob to a std::regex,
// which is vulnerable to catastrophic backtracking (ReDoS).
[[nodiscard]] inline bool glob_match(std::string_view pattern, std::string_view text) noexcept {
    std::size_t pi{0};
    std::size_t ti{0};
    std::size_t star_p{std::string_view::npos};
    std::size_t star_t{0};

    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            ++pi;
            ++ti;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star_p = pi;
            star_t = ti;
            ++pi;
        } else if (star_p != std::string_view::npos) {
            pi = star_p + 1;
            ++star_t;
            ti = star_t;
        } else {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*') {
        ++pi;
    }

    return pi == pattern.size();
}

} // namespace luma

#endif // LUMA_COMMON_STRING_UTILS_HPP
