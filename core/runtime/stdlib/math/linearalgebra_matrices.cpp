#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/math/linearalgebra_internal.hpp"
#include "runtime/stdlib/math/linearalgebra_module.hpp"
#include "runtime/stdlib/math/numeric_converters.hpp"

using luma::numeric::from_mat;
using luma::numeric::from_vec;
using luma::numeric::to_mat;
using luma::numeric::to_vec;
using namespace luma::linalg_detail;

namespace luma {

namespace {

[[nodiscard]] std::vector<std::vector<double>>
mat_multiply(const std::vector<std::vector<double>>& a, const std::vector<std::vector<double>>& b) {
    const auto m = a.size();
    const auto n = b[0].size();
    const auto k = b.size();

    std::vector<std::vector<double>> result(m, std::vector<double>(n, 0.0));

    for (std::size_t i{0}; i < m; ++i) {
        for (std::size_t j{0}; j < n; ++j) {
            for (std::size_t p{0}; p < k; ++p) {
                result[i][j] += a[i][p] * b[p][j];
            }
        }
    }

    return result;
}

[[nodiscard]] std::vector<std::vector<double>>
mat_transpose(const std::vector<std::vector<double>>& m) {
    if (m.empty()) {
        return {};
    }

    const auto rows = m.size();
    const auto cols = m[0].size();

    std::vector<std::vector<double>> result(cols, std::vector<double>(rows, 0.0));

    for (std::size_t i{0}; i < rows; ++i) {
        for (std::size_t j{0}; j < cols; ++j) {
            result[j][i] = m[i][j];
        }
    }

    return result;
}

// Gaussian elimination with partial pivoting, shared by determinant, inverse,
// and solve.
struct LuDecomp {
    std::vector<std::vector<double>> lu;
    std::vector<std::size_t> pivot;
    int sign{1};
    bool singular{false};
};

// Factor `matrix` into P·A = L·U.  The decomposition is recomputed on every
// call: caching it across operations would require mutable state or a matrix
// wrapper, which adds complexity not worth it for a teaching language.
[[nodiscard]] LuDecomp lu_decompose(const std::vector<std::vector<double>>& matrix) {
    const auto n = matrix.size();

    LuDecomp result;
    result.lu = matrix;

    result.pivot.resize(n);

    for (std::size_t i{0}; i < n; ++i) {
        result.pivot[i] = i;
    }

    for (std::size_t k{0}; k < n; ++k) {
        // Find pivot.
        double max_val{0.0};
        std::size_t max_idx{k};

        for (std::size_t i{k}; i < n; ++i) {
            const auto abs_val = std::fabs(result.lu[i][k]);

            if (abs_val > max_val) {
                max_val = abs_val;
                max_idx = i;
            }
        }

        if (max_val < k_singularity_threshold) {
            result.singular = true;

            return result;
        }

        if (max_idx != k) {
            std::swap(result.lu[k], result.lu[max_idx]);
            std::swap(result.pivot[k], result.pivot[max_idx]);

            result.sign = -result.sign;
        }

        for (std::size_t i{k + 1}; i < n; ++i) {
            result.lu[i][k] /= result.lu[k][k];

            for (std::size_t j{k + 1}; j < n; ++j) {
                result.lu[i][j] -= result.lu[i][k] * result.lu[k][j];
            }
        }
    }

    return result;
}

// Solve A·x = rhs for x using a completed, non-singular LU decomposition.
// Applies the stored pivot permutation to rhs, then forward substitution through
// the unit lower-triangular factor and back substitution through the upper one.
[[nodiscard]] std::vector<double> lu_solve(const LuDecomp& lu, const std::vector<double>& rhs) {
    const auto n = lu.lu.size();

    std::vector<double> x(n);

    for (std::size_t i{0}; i < n; ++i) {
        x[i] = rhs[lu.pivot[i]];
    }

    for (std::size_t i{1}; i < n; ++i) {
        for (std::size_t j{0}; j < i; ++j) {
            x[i] -= lu.lu[i][j] * x[j];
        }
    }

    for (auto i = static_cast<std::ptrdiff_t>(n) - 1; i >= 0; --i) {
        const auto ui = static_cast<std::size_t>(i);

        for (std::size_t j{ui + 1}; j < n; ++j) {
            x[ui] -= lu.lu[ui][j] * x[j];
        }

        x[ui] /= lu.lu[ui][ui];
    }

    return x;
}

// True when the matrix is non-empty and has equal row and column counts.
[[nodiscard]] bool is_square_mat(const std::vector<std::vector<double>>& m) {
    return !m.empty() && m.size() == m[0].size();
}

// Returns a "matrix must be square" failure Value when m is not square, or
// nullopt when it is.  Lets the square-only operations share one guard message.
[[nodiscard]] std::optional<Value> require_square(const std::vector<std::vector<double>>& m,
                                                  std::string_view name) {
    if (!is_square_mat(m)) {
        return make_failure_value(std::format("{}: matrix must be square", name));
    }

    return std::nullopt;
}

// Guard a matrix allocation of rows × cols elements against the array-size
// limit.  The comparison divides rather than multiplies so the product can
// never overflow.  from_mat requires callers to validate the element count
// first, so every matrix constructor — and multiply, whose result is
// rows(a) × cols(b) — routes through this one guard.
void require_matrix_within_limit(std::uint64_t rows, std::uint64_t cols, std::string_view function,
                                 std::string_view subject, std::string_view hint,
                                 const SourceLocation& loc) {
    if (rows != 0 && cols > ResourceLimits::max_array_size / rows) {
        throw RuntimeError{error_msg("LinearAlgebra", function,
                                     std::format("{} exceeds maximum matrix element count ({})",
                                                 subject, ResourceLimits::max_array_size)),
                           loc, std::string{hint}};
    }
}

} // namespace

// Matrix construction and operations.
void register_linearalgebra_matrices(const EnvPtr& env) {
    ModuleBuilder{"LinearAlgebra", env}
        .func("identity", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "LinearAlgebra.identity", loc);

            if (n <= 0) {
                throw RuntimeError{"LinearAlgebra.identity: size must be positive", loc,
                                   "matrix size must be greater than zero"};
            }

            require_matrix_within_limit(static_cast<std::uint64_t>(n),
                                        static_cast<std::uint64_t>(n), "identity",
                                        std::format("size {}", n), "reduce the matrix size", loc);

            std::vector<std::vector<double>> m(
                static_cast<std::size_t>(n), std::vector<double>(static_cast<std::size_t>(n), 0.0));

            for (std::int64_t i{0}; i < n; ++i) {
                m[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)] = 1.0;
            }

            return Value{from_mat(m)};
        })
        .func("zero_matrix", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto rows = expect_integer(args[0], "LinearAlgebra.zero_matrix", loc);
            const auto cols = expect_integer(args[1], "LinearAlgebra.zero_matrix", loc);

            if (rows <= 0 || cols <= 0) {
                throw RuntimeError{"LinearAlgebra.zero_matrix: dimensions must be positive", loc,
                                   "both rows and columns must be greater than zero"};
            }

            require_matrix_within_limit(
                static_cast<std::uint64_t>(rows), static_cast<std::uint64_t>(cols), "zero_matrix",
                std::format("dimensions {}x{}", rows, cols), "reduce the matrix dimensions", loc);

            const std::vector<std::vector<double>> m(
                static_cast<std::size_t>(rows),
                std::vector<double>(static_cast<std::size_t>(cols), 0.0));

            return Value{from_mat(m)};
        })
        .func("diagonal", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto diag = to_vec(args[0], "LinearAlgebra.diagonal", loc);

            const auto n = diag.size();

            require_matrix_within_limit(n, n, "diagonal", std::format("size {}", n),
                                        "reduce the vector length", loc);

            std::vector<std::vector<double>> m(n, std::vector<double>(n, 0.0));

            for (std::size_t i{0}; i < n; ++i) {
                m[i][i] = diag[i];
            }

            return Value{from_mat(m)};
        })
        // rows/columns/shape/is_square convert the whole matrix via to_mat
        // rather than reading the outer array length directly.  This is a
        // deliberate tradeoff: to_mat also validates that the matrix is
        // rectangular, so these queries reject ragged input consistently.
        .func("rows", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.rows", loc);

            return Value{static_cast<std::int64_t>(m.size())};
        })
        .func("columns", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.columns", loc);

            return Value{static_cast<std::int64_t>(m.empty() ? 0 : m[0].size())};
        })
        .func("shape", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.shape", loc);

            auto tuple = std::make_shared<TupleValue>();
            tuple->elements.emplace_back(static_cast<std::int64_t>(m.size()));
            tuple->elements.emplace_back(static_cast<std::int64_t>(m.empty() ? 0 : m[0].size()));

            return Value{std::move(tuple)};
        })
        .func("is_square", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.is_square", loc);

            return Value{is_square_mat(m)};
        })
        .func("transpose", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.transpose", loc);

            return Value{from_mat(mat_transpose(m))};
        })
        .func("multiply", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_mat(args[0], "LinearAlgebra.multiply", loc);
            auto b = to_mat(args[1], "LinearAlgebra.multiply", loc);

            if (a.empty() || b.empty() || a[0].empty() || b[0].empty() || a[0].size() != b.size()) {
                return make_failure_value("LinearAlgebra.multiply: dimension mismatch");
            }

            require_matrix_within_limit(
                a.size(), b[0].size(), "multiply",
                std::format("result dimensions {}x{}", a.size(), b[0].size()),
                "reduce the matrix size", loc);

            return make_success_value(from_mat(mat_multiply(a, b)));
        })
        .func("multiply_vector", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.multiply_vector", loc);
            auto v = to_vec(args[1], "LinearAlgebra.multiply_vector", loc);

            if (m.empty() || m[0].empty() || m[0].size() != v.size()) {
                return make_failure_value("LinearAlgebra.multiply_vector: dimension mismatch");
            }

            std::vector<double> result(m.size(), 0.0);

            for (std::size_t i{0}; i < m.size(); ++i) {
                for (std::size_t j{0}; j < v.size(); ++j) {
                    result[i] += m[i][j] * v[j];
                }
            }

            return make_success_value(from_vec(result));
        })
        .func("scale_matrix", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.scale_matrix", loc);

            const auto s = expect_numeric(args[1], "LinearAlgebra.scale_matrix", loc);

            for (auto& row : m) {
                for (auto& x : row) {
                    x *= s;
                }
            }

            return Value{from_mat(m)};
        })
        .func("add_matrix", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_mat(args[0], "LinearAlgebra.add_matrix", loc);
            auto b = to_mat(args[1], "LinearAlgebra.add_matrix", loc);

            if (a.size() != b.size() || (!a.empty() && a[0].size() != b[0].size())) {
                return make_failure_value("LinearAlgebra.add_matrix: dimension mismatch");
            }

            for (std::size_t i{0}; i < a.size(); ++i) {
                for (std::size_t j{0}; j < a[0].size(); ++j) {
                    a[i][j] += b[i][j];
                }
            }

            return make_success_value(from_mat(a));
        })
        .func("determinant", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.determinant", loc);

            if (auto not_square = require_square(m, "LinearAlgebra.determinant")) {
                return *not_square;
            }

            auto lu = lu_decompose(m);

            if (lu.singular) {
                return make_success_value(Value{0.0});
            }

            auto det = static_cast<double>(lu.sign);

            for (std::size_t i{0}; i < m.size(); ++i) {
                det *= lu.lu[i][i];
            }

            return make_success_value(Value{det});
        })
        .func("trace", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.trace", loc);

            if (auto not_square = require_square(m, "LinearAlgebra.trace")) {
                return *not_square;
            }

            double tr{0.0};

            for (std::size_t i{0}; i < m.size(); ++i) {
                tr += m[i][i];
            }

            return make_success_value(Value{tr});
        })
        .func("inverse", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.inverse", loc);

            if (auto not_square = require_square(m, "LinearAlgebra.inverse")) {
                return *not_square;
            }

            auto lu = lu_decompose(m);

            if (lu.singular) {
                return make_failure_value("LinearAlgebra.inverse: singular matrix");
            }

            const auto n = m.size();

            std::vector<std::vector<double>> inv(n, std::vector<double>(n, 0.0));

            for (std::size_t col{0}; col < n; ++col) {
                std::vector<double> unit(n, 0.0);
                unit[col] = 1.0;

                const auto column = lu_solve(lu, unit);

                for (std::size_t row{0}; row < n; ++row) {
                    inv[row][col] = column[row];
                }
            }

            return make_success_value(from_mat(inv));
        })
        .func("solve", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.solve", loc);
            auto b = to_vec(args[1], "LinearAlgebra.solve", loc);

            if (auto not_square = require_square(m, "LinearAlgebra.solve")) {
                return *not_square;
            }

            if (m.size() != b.size()) {
                return make_failure_value("LinearAlgebra.solve: dimension mismatch");
            }

            auto lu = lu_decompose(m);

            if (lu.singular) {
                return make_failure_value("LinearAlgebra.solve: singular matrix");
            }

            return make_success_value(from_vec(lu_solve(lu, b)));
        })
        .func("is_symmetric", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.is_symmetric", loc);

            if (!is_square_mat(m)) {
                return Value{false};
            }

            for (std::size_t i{0}; i < m.size(); ++i) {
                for (std::size_t j{i + 1}; j < m.size(); ++j) {
                    if (std::fabs(m[i][j] - m[j][i]) > k_comparison_tolerance) {
                        return Value{false};
                    }
                }
            }

            return Value{true};
        })
        .func("hadamard_matrix", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_mat(args[0], "LinearAlgebra.hadamard_matrix", loc);
            auto b = to_mat(args[1], "LinearAlgebra.hadamard_matrix", loc);

            if (a.size() != b.size() || (!a.empty() && a[0].size() != b[0].size())) {
                return make_failure_value("LinearAlgebra.hadamard_matrix: dimension mismatch");
            }

            for (std::size_t i{0}; i < a.size(); ++i) {
                for (std::size_t j{0}; j < a[0].size(); ++j) {
                    a[i][j] *= b[i][j];
                }
            }

            return make_success_value(from_mat(a));
        })
        .func("rank", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.rank", loc);

            if (m.empty() || m[0].empty()) {
                return Value{static_cast<std::int64_t>(0)};
            }

            const auto rows = m.size();
            const auto cols = m[0].size();
            std::size_t rank{0};

            for (std::size_t col{0}; col < cols && rank < rows; ++col) {
                // Find a pivot row at or below `rank` with the largest magnitude.
                std::size_t pivot{rank};
                double max_val{std::fabs(m[rank][col])};

                for (std::size_t i{rank + 1}; i < rows; ++i) {
                    if (std::fabs(m[i][col]) > max_val) {
                        max_val = std::fabs(m[i][col]);
                        pivot = i;
                    }
                }

                if (max_val < k_singularity_threshold) {
                    continue;
                }

                std::swap(m[rank], m[pivot]);

                for (std::size_t i{0}; i < rows; ++i) {
                    if (i == rank) {
                        continue;
                    }

                    const auto factor = m[i][col] / m[rank][col];

                    for (std::size_t j{col}; j < cols; ++j) {
                        m[i][j] -= factor * m[rank][j];
                    }
                }

                ++rank;
            }

            return Value{static_cast<std::int64_t>(rank)};
        })
        .func("is_identity", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.is_identity", loc);

            if (!is_square_mat(m)) {
                return Value{false};
            }

            for (std::size_t i{0}; i < m.size(); ++i) {
                for (std::size_t j{0}; j < m.size(); ++j) {
                    const auto expected = (i == j) ? 1.0 : 0.0;

                    if (std::fabs(m[i][j] - expected) > k_comparison_tolerance) {
                        return Value{false};
                    }
                }
            }

            return Value{true};
        })
        .func("is_diagonal", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto m = to_mat(args[0], "LinearAlgebra.is_diagonal", loc);

            if (!is_square_mat(m)) {
                return Value{false};
            }

            for (std::size_t i{0}; i < m.size(); ++i) {
                for (std::size_t j{0}; j < m.size(); ++j) {
                    if (i != j && std::fabs(m[i][j]) > k_comparison_tolerance) {
                        return Value{false};
                    }
                }
            }

            return Value{true};
        });
}

} // namespace luma
