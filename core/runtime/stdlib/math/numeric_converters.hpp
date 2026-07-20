#ifndef LUMA_STDLIB_NUMERIC_CONVERTERS_HPP
#define LUMA_STDLIB_NUMERIC_CONVERTERS_HPP

// ═══════════════════════════════════════════════════════════
// Numeric array/matrix conversion helpers
// ═══════════════════════════════════════════════════════════
//
// Shared helpers for converting between Luma array Values and
// C++ numeric vectors/matrices.  Used by LinearAlgebra and
// Calculus modules.

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <string_view>
#include <vector>

#include "analysis/errors/error.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/native_function_validation.hpp"

namespace luma::numeric {

// Convert a Luma array Value to a std::vector<double>.
// Throws RuntimeError if the value is not an array of numbers.
[[nodiscard]] inline std::vector<double> to_vec(const Value& v, std::string_view name,
                                                const SourceLocation& loc) {
    if (!v.is_array()) {
        throw RuntimeError{
            std::format("{}: expected array of numbers, got '{}'", name, v.display_type_name()),
            loc, "ensure the input is an array containing only numbers"};
    }

    const auto& elems = *v.as_array()->elements;

    std::vector<double> result;
    result.reserve(elems.size());

    std::ranges::transform(elems, std::back_inserter(result), [&](const Value& e) {
        return expect_numeric(e, std::format("{}: element", name), loc);
    });

    return result;
}

// Convert a Luma 2D array Value to a std::vector<std::vector<double>>.
// Validates that the matrix is rectangular.
[[nodiscard]] inline std::vector<std::vector<double>> to_mat(const Value& v, std::string_view name,
                                                             const SourceLocation& loc) {
    if (!v.is_array()) {
        throw RuntimeError{std::format("{}: expected matrix (array of arrays), got '{}'", name,
                                       v.display_type_name()),
                           loc, "pass a 2D array (array of arrays)"};
    }

    const auto& rows = *v.as_array()->elements;

    std::vector<std::vector<double>> result;
    result.reserve(rows.size());

    for (const auto& row : rows) {
        result.push_back(to_vec(row, name, loc));
    }

    if (!result.empty()) {
        const auto cols = result[0].size();

        for (std::size_t i{1}; i < result.size(); ++i) {
            if (result[i].size() != cols) {
                throw RuntimeError{std::format("{}: matrix rows have different lengths", name), loc,
                                   "all matrix rows must have the same number of columns"};
            }
        }
    }

    return result;
}

/// Convert a C++ vector to a Luma array Value.
/// Callers must validate that v.size() <= ResourceLimits::max_array_size before calling.
[[nodiscard]] inline Value from_vec(const std::vector<double>& v) {
    auto arr = std::make_shared<ArrayValue>();

    for (const auto x : v) {
        arr->elements->push_back(Value{x});
    }

    return Value{std::move(arr)};
}

/// Convert a C++ matrix to a Luma 2D array Value.
/// Callers must validate that m.size() <= ResourceLimits::max_array_size before calling.
[[nodiscard]] inline Value from_mat(const std::vector<std::vector<double>>& m) {
    auto arr = std::make_shared<ArrayValue>();

    for (const auto& row : m) {
        arr->elements->push_back(from_vec(row));
    }

    return Value{std::move(arr)};
}

} // namespace luma::numeric

#endif // LUMA_STDLIB_NUMERIC_CONVERTERS_HPP
