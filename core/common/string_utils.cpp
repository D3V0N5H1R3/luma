#include "common/string_utils.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <vector>

namespace luma {

namespace {

// Levenshtein (edit) distance via the classic two-row dynamic programming
// algorithm.  Allocates two vectors of size (n+1) for the previous and current
// rows.
//
// When Bounded is true the per-row minimum is tracked and the function returns
// max_dist + 1 as soon as the distance is guaranteed to exceed max_dist,
// enabling early termination for "did you mean?" suggestions.  When Bounded is
// false the row-minimum bookkeeping is discarded at compile time, so the
// unbounded path carries no early-exit overhead and max_dist is ignored.
template <bool Bounded>
std::size_t levenshtein_impl(std::string_view a, std::string_view b, std::size_t max_dist) {
    const auto m = a.size();
    const auto n = b.size();

    if constexpr (Bounded) {
        if ((m > n && m - n > max_dist) || (n > m && n - m > max_dist)) {
            return max_dist + 1;
        }
    }

    std::vector<std::size_t> prev(n + 1);
    std::vector<std::size_t> curr(n + 1);

    for (std::size_t j = 0; j <= n; ++j) {
        prev[j] = j;
    }

    for (std::size_t i = 1; i <= m; ++i) {
        curr[0] = i;
        [[maybe_unused]] std::size_t row_min = curr[0];

        for (std::size_t j = 1; j <= n; ++j) {
            const std::size_t cost = (a[i - 1] == b[j - 1]) ? 0U : 1U;
            curr[j] = (std::min)({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});

            if constexpr (Bounded) {
                row_min = (std::min)(row_min, curr[j]);
            }
        }

        if constexpr (Bounded) {
            if (row_min > max_dist) {
                return max_dist + 1;
            }
        }

        std::swap(prev, curr);
    }

    return prev[n];
}

} // anonymous namespace

std::size_t levenshtein_distance(std::string_view a, std::string_view b, std::size_t max_distance) {
    if (max_distance == std::string_view::npos) {
        return levenshtein_impl<false>(a, b, max_distance);
    }

    return levenshtein_impl<true>(a, b, max_distance);
}

std::string suggest_name(const std::vector<std::string_view>& candidates, std::string_view needle) {
    const auto max_dist = needle.size() <= 4 ? std::size_t{1} : std::size_t{2};
    std::string best;
    std::size_t best_dist = max_dist + 1;

    for (const auto candidate : candidates) {
        if (candidate == needle) {
            continue;
        }

        const auto d = levenshtein_distance(needle, candidate, max_dist);

        if (d < best_dist) {
            best_dist = d;
            best = std::string{candidate};
        }
    }

    if (best_dist <= max_dist) {
        return std::format("did you mean '{}'?", best);
    }

    return {};
}

std::size_t case_insensitive_find(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.size() > haystack.size()) {
        return std::string_view::npos;
    }

    const auto result = std::ranges::search(haystack, needle, [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
    });

    if (result.empty()) {
        return std::string_view::npos;
    }

    return static_cast<std::size_t>(result.begin() - haystack.begin());
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines{};
    std::size_t start{0};

    auto push_line = [&lines](std::string_view line) {
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        lines.emplace_back(line);
    };

    while (start <= text.size()) {
        const auto pos = text.find('\n', start);

        if (pos == std::string_view::npos) {
            push_line(text.substr(start));
            break;
        }

        push_line(text.substr(start, pos - start));
        start = pos + 1;
    }

    return lines;
}

void strip_utf8_bom(std::string& text) {
    constexpr std::string_view k_utf8_bom = "\xEF\xBB\xBF";

    if (text.size() >= k_utf8_bom.size() && text.starts_with(k_utf8_bom)) {
        text.erase(0, k_utf8_bom.size());
    }
}

std::vector<std::string> split_string(std::string_view s, char delimiter) {
    std::vector<std::string> parts;

    for_each_split(s, delimiter,
                   [&parts](std::string_view segment) { parts.emplace_back(segment); });

    return parts;
}

} // namespace luma
