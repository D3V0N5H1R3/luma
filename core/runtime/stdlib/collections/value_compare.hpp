#ifndef LUMA_STDLIB_VALUE_COMPARE_HPP
#define LUMA_STDLIB_VALUE_COMPARE_HPP

// ═══════════════════════════════════════════════════════════
// Value comparison helpers for stdlib modules
// ═══════════════════════════════════════════════════════════
//
// Shared comparison logic used by Array.sort, Array.binary_search,
// BinaryTree, and other modules that need to order Luma values.

#include <cmath>
#include <format>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// Compare two Values, returning negative/zero/positive like strcmp.
// Supports integer, number, string, boolean.
// Throws RuntimeError for incomparable types.
[[nodiscard]] inline int compare_values(const Value& a, const Value& b, const SourceLocation& loc,
                                        std::string_view context = "compare") {
    // String comparison.
    if (a.is_string() && b.is_string()) {
        return a.as_string().compare(b.as_string());
    }

    // Numeric comparison (integer and/or number).
    if ((a.is_integer() || a.is_number()) && (b.is_integer() || b.is_number())) {
        // Fast path: both integers.
        if (a.is_integer() && b.is_integer()) {
            const auto av = a.as_integer();
            const auto bv = b.as_integer();

            if (av < bv) {
                return -1;
            }
            if (av > bv) {
                return 1;
            }
            return 0;
        }

        const auto av = a.to_numeric();
        const auto bv = b.to_numeric();

        if (std::isnan(av) || std::isnan(bv)) {
            throw RuntimeError{std::format("{}: NaN is not comparable", context), loc,
                               "NaN values cannot be compared"};
        }

        if (av < bv) {
            return -1;
        }
        if (av > bv) {
            return 1;
        }
        return 0;
    }

    // Boolean comparison (false < true).
    if (a.is_bool() && b.is_bool()) {
        const int av = a.as_bool() ? 1 : 0;
        const int bv = b.as_bool() ? 1 : 0;
        return av - bv;
    }

    throw RuntimeError{std::format("{}: only comparable types (integer, number, string, boolean) "
                                   "are supported, got '{}' and '{}'",
                                   context, a.display_type_name(), b.display_type_name()),
                       loc, "use integer, number, string, or boolean values"};
}

} // namespace luma

#endif // LUMA_STDLIB_VALUE_COMPARE_HPP
