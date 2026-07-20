#ifndef LUMA_LSP_PARAM_UTILS_HPP
#define LUMA_LSP_PARAM_UTILS_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luma::lsp::util {

// Advance pos past a matched bracket pair. On entry, text[pos] is an
// opening bracket ('<' or '('). Returns the index of the matching
// closing bracket. Handles nested brackets and treats '>' preceded by
// '-' as part of the '->' arrow, not a bracket close.
[[nodiscard]] inline std::size_t skip_nested_brackets(std::string_view text, std::size_t pos) {
    int depth{1};
    const std::size_t len = text.size();

    while (depth > 0 && ++pos < len) {
        const char c = text[pos];

        if (c == '<' || c == '(') {
            ++depth;
        } else if (c == '>' || c == ')') {
            const bool is_arrow = (c == '>' && pos >= 1 && text[pos - 1] == '-');

            if (!is_arrow) {
                --depth;
            }
        }
    }

    return pos;
}

// Split a parameter-list string like "(a: T, b: array<U>)" into
// individual parameter strings ["a: T", "b: array<U>"].
// Respects nested angle brackets so that commas inside generic type
// parameters are not treated as argument separators.
[[nodiscard]] inline std::vector<std::string> split_param_list(std::string_view params_sig) {
    if (params_sig.size() < 2 || params_sig.front() != '(' || params_sig.back() != ')') {
        return {};
    }

    std::vector<std::string> result;
    std::string current;

    for (std::size_t i = 1; i < params_sig.size() - 1; ++i) {
        const char c = params_sig[i];

        if (c == '<' || c == '(') {
            const std::size_t start = i;
            i = skip_nested_brackets(params_sig, i);
            // substr on string_view returns a string_view; append avoids temporaries.
            current.append(params_sig.substr(start, i - start + 1));
        } else if (c == ',') {
            const auto first = current.find_first_not_of(' ');
            const auto last = current.find_last_not_of(' ');

            if (first != std::string::npos) {
                result.push_back(current.substr(first, last - first + 1));
            }

            current.clear();
        } else {
            current += c;
        }
    }

    const auto first = current.find_first_not_of(' ');
    const auto last = current.find_last_not_of(' ');

    if (first != std::string::npos) {
        result.push_back(current.substr(first, last - first + 1));
    }

    return result;
}

// Extract the bare parameter name from one "name: type" fragment of a parameter
// signature (as produced by split_param_list), trimming any spaces before the
// colon. Returns nullopt when the fragment carries no colon and so has no name
// to surface as a hint.
[[nodiscard]] inline std::optional<std::string> extract_param_name(std::string_view part) {
    const auto colon = part.find(':');

    if (colon == std::string_view::npos) {
        return std::nullopt;
    }

    std::string name{part.substr(0, colon)};
    const auto last = name.find_last_not_of(' ');

    if (last != std::string::npos) {
        name = name.substr(0, last + 1);
    }

    return name;
}

} // namespace luma::lsp::util

#endif // LUMA_LSP_PARAM_UTILS_HPP
