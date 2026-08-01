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

using luma::numeric::from_vec;
using luma::numeric::to_vec;
using namespace luma::linalg_detail;

namespace luma {

namespace {

// ─── Vector operation templates ──────────────────────────────────────────────

// Returns a "dimension mismatch" failure Value when the two operands differ in
// length, or nullopt when they match.  Shared by apply_binary_vec_op and by the
// scalar-returning reductions (dot, distance, angle) that cannot use it.
[[nodiscard]] std::optional<Value> require_same_dimension(const std::vector<double>& a,
                                                          const std::vector<double>& b,
                                                          std::string_view name) {
    if (a.size() != b.size()) {
        return make_failure_value(std::format("{}: dimension mismatch", name));
    }

    return std::nullopt;
}

template <typename BinaryOp>
[[nodiscard]] Value apply_binary_vec_op(std::span<const Value> args, std::string_view name,
                                        const SourceLocation& loc, BinaryOp op) {
    auto a = to_vec(args[0], name, loc);
    auto b = to_vec(args[1], name, loc);

    if (auto mismatch = require_same_dimension(a, b, name)) {
        return *mismatch;
    }

    std::vector<double> result(a.size());

    for (std::size_t i{0}; i < a.size(); ++i) {
        result[i] = op(a[i], b[i]);
    }

    return make_success_value(from_vec(result));
}

// Unlike apply_binary_vec_op, this returns a raw array Value rather than a
// result: the unary vector ops (scale, negate) cannot fail, so wrapping them in
// a result would force callers to unwrap a value that is always a success.
template <typename UnaryOp>
[[nodiscard]] Value apply_unary_vec_op(std::span<const Value> args, std::string_view name,
                                       const SourceLocation& loc, UnaryOp op) {
    auto v = to_vec(args[0], name, loc);

    for (auto& x : v) {
        x = op(x);
    }

    return Value{from_vec(v)};
}

// ─── Core operations ─────────────────────────────────────────────────────────

[[nodiscard]] double dot_product(const std::vector<double>& a, const std::vector<double>& b) {
    return std::inner_product(a.begin(), a.end(), b.begin(), 0.0);
}

[[nodiscard]] double vector_norm(const std::vector<double>& v) {
    return std::sqrt(dot_product(v, v));
}

} // namespace

// Vector construction and operations.
void register_linearalgebra_vectors(const EnvPtr& env) {
    ModuleBuilder{"LinearAlgebra", env}
        .func("zero_vector", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "LinearAlgebra.zero_vector", loc);

            if (n <= 0) {
                throw RuntimeError{"LinearAlgebra.zero_vector: size must be positive", loc,
                                   "size parameter must be greater than zero"};
            }

            if (static_cast<std::uint64_t>(n) > ResourceLimits::max_array_size) {
                throw RuntimeError{error_msg("LinearAlgebra", "zero_vector",
                                             std::format("size {} exceeds maximum array size ({})",
                                                         n, ResourceLimits::max_array_size)),
                                   loc, "reduce the vector size"};
            }

            auto arr = std::make_shared<ArrayValue>();

            for (std::int64_t i{0}; i < n; ++i) {
                arr->elements->emplace_back(0.0);
            }

            return Value{std::move(arr)};
        })
        .func("unit_vector", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto n = expect_integer(args[0], "LinearAlgebra.unit_vector", loc);
            const auto idx = expect_integer(args[1], "LinearAlgebra.unit_vector", loc);

            if (n <= 0 || idx < 0 || idx >= n) {
                throw RuntimeError{"LinearAlgebra.unit_vector: invalid dimensions", loc,
                                   "size must be positive and index must be in range [0, size)"};
            }

            if (static_cast<std::uint64_t>(n) > ResourceLimits::max_array_size) {
                throw RuntimeError{error_msg("LinearAlgebra", "unit_vector",
                                             std::format("size {} exceeds maximum array size ({})",
                                                         n, ResourceLimits::max_array_size)),
                                   loc, "reduce the vector size"};
            }

            auto arr = std::make_shared<ArrayValue>();

            for (std::int64_t i{0}; i < n; ++i) {
                arr->elements->emplace_back(i == idx ? 1.0 : 0.0);
            }

            return Value{std::move(arr)};
        })
        .func("add", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return apply_binary_vec_op(args, "LinearAlgebra.add", loc,
                                       [](double a, double b) { return a + b; });
        })
        .func("subtract", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return apply_binary_vec_op(args, "LinearAlgebra.subtract", loc,
                                       [](double a, double b) { return a - b; });
        })
        .func("scale", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto s = expect_numeric(args[1], "LinearAlgebra.scale", loc);
            return apply_unary_vec_op(args, "LinearAlgebra.scale", loc,
                                      [s](double x) { return x * s; });
        })
        .func("negate", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return apply_unary_vec_op(args, "LinearAlgebra.negate", loc,
                                      [](double x) { return -x; });
        })
        .func("dot", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_vec(args[0], "LinearAlgebra.dot", loc);
            auto b = to_vec(args[1], "LinearAlgebra.dot", loc);

            if (auto mismatch = require_same_dimension(a, b, "LinearAlgebra.dot")) {
                return *mismatch;
            }

            return make_success_value(Value{dot_product(a, b)});
        })
        .func("cross", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_vec(args[0], "LinearAlgebra.cross", loc);
            auto b = to_vec(args[1], "LinearAlgebra.cross", loc);

            if (a.size() != 3 || b.size() != 3) {
                return make_failure_value("LinearAlgebra.cross: requires 3D vectors");
            }

            const std::vector<double> result{(a[1] * b[2]) - (a[2] * b[1]),
                                             (a[2] * b[0]) - (a[0] * b[2]),
                                             (a[0] * b[1]) - (a[1] * b[0])};

            return make_success_value(from_vec(result));
        })
        .func("norm", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto v = to_vec(args[0], "LinearAlgebra.norm", loc);

            return Value{vector_norm(v)};
        })
        .func("normalize", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto v = to_vec(args[0], "LinearAlgebra.normalize", loc);

            const auto n = vector_norm(v);

            if (n < k_singularity_threshold) {
                return make_failure_value("LinearAlgebra.normalize: zero vector");
            }

            for (auto& x : v) {
                x /= n;
            }

            return make_success_value(from_vec(v));
        })
        .func("distance", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_vec(args[0], "LinearAlgebra.distance", loc);
            auto b = to_vec(args[1], "LinearAlgebra.distance", loc);

            if (auto mismatch = require_same_dimension(a, b, "LinearAlgebra.distance")) {
                return *mismatch;
            }

            double sum{0.0};

            for (std::size_t i{0}; i < a.size(); ++i) {
                const auto d = a[i] - b[i];

                sum += d * d;
            }

            return make_success_value(Value{std::sqrt(sum)});
        })
        .func("dimension", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{static_cast<std::int64_t>(
                expect_array(args[0], "LinearAlgebra.dimension", loc)->elements->size())};
        })
        .func("angle", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_vec(args[0], "LinearAlgebra.angle", loc);
            auto b = to_vec(args[1], "LinearAlgebra.angle", loc);

            if (auto mismatch = require_same_dimension(a, b, "LinearAlgebra.angle")) {
                return *mismatch;
            }

            const auto na = vector_norm(a);
            const auto nb = vector_norm(b);

            if (na < k_singularity_threshold || nb < k_singularity_threshold) {
                return make_failure_value("LinearAlgebra.angle: zero vector");
            }

            auto cos_angle = dot_product(a, b) / (na * nb);
            cos_angle = std::clamp(cos_angle, -1.0, 1.0);

            return make_success_value(Value{std::acos(cos_angle)});
        })
        .func("is_orthogonal", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_vec(args[0], "LinearAlgebra.is_orthogonal", loc);
            auto b = to_vec(args[1], "LinearAlgebra.is_orthogonal", loc);

            if (a.size() != b.size()) {
                return Value{false};
            }

            return Value{std::fabs(dot_product(a, b)) < k_orthogonality_tolerance};
        })
        .func("hadamard", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return apply_binary_vec_op(args, "LinearAlgebra.hadamard", loc,
                                       [](double a, double b) { return a * b; });
        })
        .func("project", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_vec(args[0], "LinearAlgebra.project", loc);
            auto b = to_vec(args[1], "LinearAlgebra.project", loc);

            if (auto mismatch = require_same_dimension(a, b, "LinearAlgebra.project")) {
                return *mismatch;
            }

            const auto bb = dot_product(b, b);

            if (bb < k_singularity_threshold) {
                return make_failure_value("LinearAlgebra.project: zero vector");
            }

            const auto scale = dot_product(a, b) / bb;
            std::vector<double> result(b.size());

            for (std::size_t i{0}; i < b.size(); ++i) {
                result[i] = scale * b[i];
            }

            return make_success_value(from_vec(result));
        })
        .func("reject", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_vec(args[0], "LinearAlgebra.reject", loc);
            auto b = to_vec(args[1], "LinearAlgebra.reject", loc);

            if (auto mismatch = require_same_dimension(a, b, "LinearAlgebra.reject")) {
                return *mismatch;
            }

            const auto bb = dot_product(b, b);

            if (bb < k_singularity_threshold) {
                return make_failure_value("LinearAlgebra.reject: zero vector");
            }

            const auto scale = dot_product(a, b) / bb;
            std::vector<double> result(a.size());

            for (std::size_t i{0}; i < a.size(); ++i) {
                result[i] = a[i] - (scale * b[i]);
            }

            return make_success_value(from_vec(result));
        })
        .func("linear_interpolation", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_vec(args[0], "LinearAlgebra.linear_interpolation", loc);
            auto b = to_vec(args[1], "LinearAlgebra.linear_interpolation", loc);
            const auto t = expect_numeric(args[2], "LinearAlgebra.linear_interpolation", loc);

            if (auto mismatch =
                    require_same_dimension(a, b, "LinearAlgebra.linear_interpolation")) {
                return *mismatch;
            }

            std::vector<double> result(a.size());

            for (std::size_t i{0}; i < a.size(); ++i) {
                result[i] = a[i] + (t * (b[i] - a[i]));
            }

            return make_success_value(from_vec(result));
        })
        .func("outer", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = to_vec(args[0], "LinearAlgebra.outer", loc);
            auto b = to_vec(args[1], "LinearAlgebra.outer", loc);

            if (a.empty() || b.empty()) {
                return make_failure_value("LinearAlgebra.outer: vectors must be non-empty");
            }

            std::vector<std::vector<double>> result(a.size(), std::vector<double>(b.size(), 0.0));

            for (std::size_t i{0}; i < a.size(); ++i) {
                for (std::size_t j{0}; j < b.size(); ++j) {
                    result[i][j] = a[i] * b[j];
                }
            }

            return make_success_value(numeric::from_mat(result));
        })
        .func("sum", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto v = to_vec(args[0], "LinearAlgebra.sum", loc);

            return Value{std::accumulate(v.begin(), v.end(), 0.0)};
        })
        .func("mean", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto v = to_vec(args[0], "LinearAlgebra.mean", loc);

            if (v.empty()) {
                return make_failure_value("LinearAlgebra.mean: empty vector");
            }

            return make_success_value(
                Value{std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size())});
        })
        .func("clamp", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto v = to_vec(args[0], "LinearAlgebra.clamp", loc);
            const auto lo = expect_numeric(args[1], "LinearAlgebra.clamp", loc);
            const auto hi = expect_numeric(args[2], "LinearAlgebra.clamp", loc);

            const auto low = std::min(lo, hi);
            const auto high = std::max(lo, hi);

            for (auto& x : v) {
                x = std::clamp(x, low, high);
            }

            return Value{from_vec(v)};
        });
}

} // namespace luma
