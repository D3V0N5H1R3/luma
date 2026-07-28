#include "runtime/stdlib/math/calculus_module.hpp"

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <numbers>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/math/numeric_converters.hpp"

namespace luma {

// Import shared numeric conversion helpers.
using luma::numeric::from_mat;
using luma::numeric::from_vec;
using luma::numeric::to_vec;

namespace {

// ─── Helper: call a Luma function value with a single numeric argument ───────

[[nodiscard]] double result_to_numeric(const Value& result, const SourceLocation& loc) {
    if (result.is_result()) {
        const auto& res = *result.as_result();

        if (res.is_success) {
            return res.owned_inner->to_numeric();
        }

        throw RuntimeError{
            "Calculus: callback returned a fail result", loc,
            "the function passed to the calculus operation must return a number, not a failure"};
    }

    return result.to_numeric();
}

[[nodiscard]] double call_fn(const Value& fn, double x, const SourceLocation& loc) {
    std::vector<Value> call_args{Value{x}};

    return result_to_numeric(invoke_callable(fn, call_args, loc), loc);
}

[[nodiscard]] double call_multi_fn(const Value& fn, const std::vector<double>& point,
                                   const SourceLocation& loc) {
    std::vector<Value> call_args{from_vec(point)};
    return result_to_numeric(invoke_callable(fn, call_args, loc), loc);
}

// ─── Numerical differentiation (central difference) ──────────────────────────

// ─── Numerical method defaults named to document their purpose. ──────────────
constexpr double k_default_step_size = 1e-8;
constexpr double k_default_second_deriv_step = 1e-5;
constexpr double k_root_tolerance = 1e-12;
constexpr double k_minimize_tolerance = 1e-10;
constexpr double k_near_zero_threshold = 1e-14;
constexpr int k_max_root_iterations = CompileTimeLimits::max_root_iterations;
constexpr std::int64_t k_default_integration_steps = 1000;
// Upper bound on user-supplied Simpson subdivisions.  Caps callback work
// (preventing a DoS hang on huge counts) and keeps `steps` well clear of the
// signed-integer maximum so the even-rounding `steps = n + 1` cannot overflow.
// 1e6 subdivisions already far exceed double-precision accuracy for Simpson.
constexpr std::int64_t k_max_integration_steps = 1000000;
// Limit estimation: initial offset from the target point (halved each
// iteration) and the iteration cap before giving up on convergence.
constexpr double k_limit_initial_step = 0.1;
constexpr int k_limit_max_iterations = 30;
// Taylor coefficient count bounds; the finite-difference stencil stays usable
// within this inclusive range of terms.
constexpr std::int64_t k_taylor_min_terms = 1;
constexpr std::int64_t k_taylor_max_terms = 20;
// Upper bound on the number of terms sum_series evaluates, bounding callback
// work for adversarial counts.
constexpr std::int64_t k_max_series_terms = 1000000;

[[nodiscard]] double derivative(const Value& fn, double x, double h, const SourceLocation& loc) {
    const auto f_plus = call_fn(fn, x + h, loc);
    const auto f_minus = call_fn(fn, x - h, loc);

    return (f_plus - f_minus) / (2.0 * h);
}

// Central-difference estimate of the partial derivative of the multivariable
// callback `fn` along one `axis` at `point`, using step `h`: perturbs that
// single coordinate by ±h and evaluates the callback at each perturbed point.
[[nodiscard]] double central_diff_partial(const Value& fn, const std::vector<double>& point,
                                          std::size_t axis, double h, const SourceLocation& loc) {
    auto plus = point;
    plus[axis] += h;

    auto minus = point;
    minus[axis] -= h;

    const auto f_plus = call_multi_fn(fn, plus, loc);
    const auto f_minus = call_multi_fn(fn, minus, loc);

    return (f_plus - f_minus) / (2.0 * h);
}

// Finite-difference estimate of the k-th derivative of `fn` at `center` using
// step `h`.  Applies a central, binomial-weighted, sign-alternating stencil of
// k + 1 samples spanning center ± (k / 2)·h, divided by hᵏ.
[[nodiscard]] double nth_derivative(const Value& fn, double center, std::int64_t k, double h,
                                    const SourceLocation& loc) {
    double dk{0.0};

    for (std::int64_t j{0}; j <= k; ++j) {
        const auto x = center + ((static_cast<double>(j) - (static_cast<double>(k) / 2.0)) * h);

        auto binom = 1.0;

        for (std::int64_t m{0}; m < j; ++m) {
            binom *= static_cast<double>(k - m) / static_cast<double>(m + 1);
        }

        const auto sign = ((k - j) % 2 == 0) ? 1.0 : -1.0;

        dk += sign * binom * call_fn(fn, x, loc);
    }

    return dk / std::pow(h, static_cast<double>(k));
}

// ─── Numerical integration (Simpson's rule) ──────────────────────────────────

// Composite Simpson's rule quadrature of `f` over [a, b] using `n`
// subdivisions.  `f` is any callable taking and returning a double.  `n` is
// rounded up to the next even value so the 4/2-weighted stencil is well formed.
template <typename Integrand>
[[nodiscard]] double simpson_rule(Integrand&& f, double a, double b, std::int64_t n) {
    // n must be even.
    const auto steps = (n % 2 != 0) ? n + 1 : n;

    const auto h = (b - a) / static_cast<double>(steps);

    auto sum = f(a) + f(b);

    for (std::int64_t i{1}; i < steps; ++i) {
        const auto x = a + (static_cast<double>(i) * h);

        sum += (i % 2 == 0 ? 2.0 : 4.0) * f(x);
    }

    return sum * h / 3.0;
}

[[nodiscard]] double integrate_simpson(const Value& fn, double a, double b, const std::int64_t n,
                                       const SourceLocation& loc) {
    return simpson_rule([&](double x) { return call_fn(fn, x, loc); }, a, b, n);
}

// ─── Golden-section search (shared by minimize / maximize) ───────────────────

// Selects whether golden_section searches for a minimum or a maximum.
enum class Extremum {
    Minimum,
    Maximum
};

// Locates the extremum of `fn` on [a, b] by golden-section search.  For
// Extremum::Maximum the objective is negated during the search, turning the
// minimiser into a maximiser; the returned value is the extremum's location.
[[nodiscard]] double golden_section(double a, double b, const Value& fn, Extremum extremum,
                                    const SourceLocation& loc) {
    const auto tol = k_minimize_tolerance;
    const auto phi = std::numbers::phi;
    const auto resphi = 2.0 - phi;

    const auto sample = [&](double x) {
        const auto raw = call_fn(fn, x, loc);
        return extremum == Extremum::Maximum ? -raw : raw;
    };

    auto x1 = a + (resphi * (b - a));
    auto x2 = b - (resphi * (b - a));
    auto f1 = sample(x1);
    auto f2 = sample(x2);

    while (std::fabs(b - a) > tol) {
        if (f1 < f2) {
            b = x2;
            x2 = x1;
            f2 = f1;
            x1 = a + (resphi * (b - a));
            f1 = sample(x1);
        } else {
            a = x1;
            x1 = x2;
            f1 = f2;
            x2 = b - (resphi * (b - a));
            f2 = sample(x2);
        }
    }

    return (a + b) / 2.0;
}

} // namespace

// ─── Registration ────────────────────────────────────────────────────────────

static void register_calculus_derivatives(const EnvPtr& env);
static void register_calculus_solvers(const EnvPtr& env);
static void register_calculus_analysis(const EnvPtr& env);

void register_calculus_ns(const EnvPtr& env) {
    register_calculus_derivatives(env);
    register_calculus_solvers(env);
    register_calculus_analysis(env);
}

// Numerical differentiation and integration.
static void register_calculus_derivatives(const EnvPtr& env) {
    ModuleBuilder{"Calculus", env}
        .func("derivative", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = args[0].to_numeric();
            const auto h = k_default_step_size;

            return Value{derivative(args[1], x, h, loc)};
        })
        .func("derivative_with", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = args[0].to_numeric();
            const auto h = args[1].to_numeric();

            if (h <= 0.0) {
                throw RuntimeError{error_msg("Calculus", "derivative_with",
                                             std::format("step must be positive, got {}", h)),
                                   loc, "pass a positive number as the step size"};
            }

            return Value{derivative(args[2], x, h, loc)};
        })
        .func("second_derivative", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = args[0].to_numeric();
            const auto h = k_default_second_deriv_step;
            const auto f_plus = call_fn(args[1], x + h, loc);
            const auto f_mid = call_fn(args[1], x, loc);
            const auto f_minus = call_fn(args[1], x - h, loc);

            return Value{(f_plus - (2.0 * f_mid) + f_minus) / (h * h)};
        })
        .func("nth_derivative", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = args[0].to_numeric();
            const auto order = expect_integer(args[1], "Calculus.nth_derivative", loc);

            if (order < 0) {
                throw RuntimeError{
                    error_msg("Calculus", "nth_derivative",
                              std::format("order must be zero or greater, got {}", order)),
                    loc, "pass a non-negative integer order"};
            }
            if (order == 0) {
                return Value{call_fn(args[2], x, loc)};
            }
            if (order > k_taylor_max_terms) {
                throw RuntimeError{error_msg("Calculus", "nth_derivative",
                                             std::format("order must be at most {}, got {}",
                                                         k_taylor_max_terms, order)),
                                   loc,
                                   "high-order finite differences lose accuracy; reduce the order"};
            }

            return Value{nth_derivative(args[2], x, order, k_default_second_deriv_step, loc)};
        })
        .func("differentiate", 1)
        .raw_body([](std::span<const Value> args, SourceLocation /*loc*/) -> Value {
            // Return the derivative as a first-class function that numerically
            // differentiates the captured `fn` at its argument.
            Value fn = args[0];
            return Value{std::make_shared<NativeFunctionValue>(
                "Calculus.differentiate.fn",
                [fn](std::span<const Value> inner_args, SourceLocation inner_loc) -> Value {
                    const auto x = inner_args[0].to_numeric();
                    return Value{derivative(fn, x, k_default_step_size, inner_loc)};
                })};
        })
        .func("gradient", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto point = to_vec(args[0], "Calculus.gradient", loc);

            const auto h = k_default_step_size;

            std::vector<double> result;
            result.reserve(point.size());

            for (std::size_t i{0}; i < point.size(); ++i) {
                result.push_back(central_diff_partial(args[1], point, i, h, loc));
            }

            return from_vec(result);
        })
        .func("integrate", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = args[0].to_numeric();
            const auto b = args[1].to_numeric();
            const auto n = k_default_integration_steps;

            return Value{integrate_simpson(args[2], a, b, n, loc)};
        })
        .func("integrate_with", 4)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = args[0].to_numeric();
            const auto b = args[1].to_numeric();

            const auto n = expect_integer(args[2], "Calculus.integrate_with", loc);

            if (n <= 0) {
                throw RuntimeError{error_msg("Calculus", "integrate_with",
                                             std::format("steps must be positive, got {}", n)),
                                   loc, "pass a positive integer as the step count"};
            }

            if (n > k_max_integration_steps) {
                throw RuntimeError{error_msg("Calculus", "integrate_with",
                                             std::format("steps must be at most {}, got {}",
                                                         k_max_integration_steps, n)),
                                   loc, "reduce the subdivision count"};
            }

            return Value{integrate_simpson(args[3], a, b, n, loc)};
        })
        .func("arc_length", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = args[0].to_numeric();
            const auto b = args[1].to_numeric();

            // Length of y = f(x) over [a, b] is ∫ √(1 + f'(x)²) dx, integrated
            // with the same Simpson rule behind Calculus.integrate.
            const auto integrand = [&](double x) {
                const auto slope = derivative(args[2], x, k_default_step_size, loc);
                return std::sqrt(1.0 + (slope * slope));
            };

            return Value{simpson_rule(integrand, a, b, k_default_integration_steps)};
        });
}

// Root-finding and optimisation.
static void register_calculus_solvers(const EnvPtr& env) {
    ModuleBuilder{"Calculus", env}
        .func("root", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto a = args[0].to_numeric();
            auto b = args[1].to_numeric();

            const auto tol = k_root_tolerance;
            const auto max_iter = k_max_root_iterations;

            auto fa = call_fn(args[2], a, loc);
            auto fb = call_fn(args[2], b, loc);

            if (fa * fb > 0) {
                return make_failure_value("Calculus.root: function must have opposite signs "
                                          "at interval endpoints");
            }

            for (int i{0}; i < max_iter; ++i) {
                const auto mid = (a + b) / 2.0;
                const auto fm = call_fn(args[2], mid, loc);

                if (std::fabs(fm) < tol || (b - a) / 2.0 < tol) {
                    return make_success_value(Value{mid});
                }

                if (fa * fm < 0) {
                    b = mid;
                    fb = fm;
                } else {
                    a = mid;
                    fa = fm;
                }
            }

            return make_success_value(Value{(a + b) / 2.0});
        })
        .func("newton", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto x = args[0].to_numeric();

            const auto tol = k_root_tolerance;
            const auto h = k_default_step_size;
            const auto max_iter = k_max_root_iterations;

            for (int i{0}; i < max_iter; ++i) {
                const auto fx = call_fn(args[1], x, loc);

                if (std::fabs(fx) < tol) {
                    return make_success_value(Value{x});
                }

                const auto dfx = derivative(args[1], x, h, loc);

                if (std::fabs(dfx) < k_near_zero_threshold) {
                    return make_failure_value("Calculus.newton: derivative is zero");
                }

                x -= fx / dfx;
            }

            return make_success_value(Value{x});
        })
        .func("minimize", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = args[0].to_numeric();
            const auto b = args[1].to_numeric();

            const auto x_min = golden_section(a, b, args[2], Extremum::Minimum, loc);

            auto tuple = std::make_shared<TupleValue>();
            tuple->elements.emplace_back(x_min);
            tuple->elements.emplace_back(call_fn(args[2], x_min, loc));

            return Value{std::move(tuple)};
        })
        .func("maximize", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = args[0].to_numeric();
            const auto b = args[1].to_numeric();

            const auto x_max = golden_section(a, b, args[2], Extremum::Maximum, loc);

            auto tuple = std::make_shared<TupleValue>();
            tuple->elements.emplace_back(x_max);
            tuple->elements.emplace_back(call_fn(args[2], x_max, loc));

            return Value{std::move(tuple)};
        });
}

// Limits, series, and multivariable/vector calculus.
static void register_calculus_analysis(const EnvPtr& env) {
    ModuleBuilder{"Calculus", env}
        .func("limit", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto target = args[0].to_numeric();

            double h{k_limit_initial_step};
            double prev{std::numeric_limits<double>::quiet_NaN()};

            for (int i{0}; i < k_limit_max_iterations; ++i) {
                const auto left = call_fn(args[1], target - h, loc);
                const auto right = call_fn(args[1], target + h, loc);

                if (std::isnan(left) && std::isnan(right)) {
                    h /= 2.0;

                    continue;
                }

                double estimate = std::numeric_limits<double>::quiet_NaN();

                if (std::isnan(left)) {
                    estimate = right;
                } else if (std::isnan(right)) {
                    estimate = left;
                } else {
                    estimate = (left + right) / 2.0;
                }

                if (!std::isnan(prev) && std::fabs(estimate - prev) < k_minimize_tolerance) {
                    return make_success_value(Value{estimate});
                }

                prev = estimate;

                h /= 2.0;
            }

            if (std::isnan(prev)) {
                return make_failure_value("Calculus.limit: could not converge");
            }

            return make_success_value(Value{prev});
        })
        .func("sum_series", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto start = expect_integer(args[0], "Calculus.sum_series", loc);
            const auto count = expect_integer(args[1], "Calculus.sum_series", loc);

            // The second argument is the term COUNT n (per the documented
            // contract): sum fn(start) + fn(start + 1) + ... + fn(start + n - 1).
            // A non-positive count is an empty sum.  Cap the count to bound work;
            // the maximum is inclusive (n == k_max_series_terms is allowed), which
            // makes the "maximum is N" message truthful and matches the sibling
            // Calculus.integrate_with, whose step cap also uses `> k_max`.
            if (count > k_max_series_terms) {
                throw RuntimeError{std::format("Calculus.sum_series: too many terms, maximum is {}",
                                               k_max_series_terms),
                                   loc, "reduce the term count"};
            }

            double sum{0.0};

            // Index by an offset k in [0, count) and form i = start + k with
            // unsigned arithmetic so neither the loop counter nor the index can
            // trip signed-overflow UB for adversarial start/count values.
            const auto base = static_cast<std::uint64_t>(start);
            for (std::int64_t k{0}; k < count; ++k) {
                const auto i = static_cast<std::int64_t>(base + static_cast<std::uint64_t>(k));
                std::vector<Value> call_args{Value{i}};

                sum += result_to_numeric(invoke_callable(args[2], call_args, loc), loc);
            }

            return Value{sum};
        })
        .func("partial_derivative", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& point_arr = expect_array(args[0], "Calculus.partial_derivative", loc);
            const auto idx = expect_integer(args[1], "Calculus.partial_derivative", loc);

            const auto n = static_cast<std::int64_t>(point_arr->elements->size());
            if (idx < 0 || idx >= n) {
                throw RuntimeError{"Calculus.partial_derivative: index out of range", loc};
            }

            const auto point = to_vec(args[0], "Calculus.partial_derivative", loc);

            constexpr double h = k_default_step_size;
            const auto ui = static_cast<std::size_t>(idx);

            return Value{central_diff_partial(args[2], point, ui, h, loc)};
        })
        .func("hessian", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto point = to_vec(args[0], "Calculus.hessian", loc);
            const auto n = point.size();

            if (n == 0) {
                return Value{std::make_shared<ArrayValue>()};
            }

            // The Hessian is an n×n matrix built with 4·n² callback evaluations;
            // guard the element count so an untrusted point length cannot drive
            // an unbounded allocation / quadratic blow-up (matches the matrix
            // constructors in the LinearAlgebra module).
            if (n > ResourceLimits::max_array_size / n) {
                throw RuntimeError{
                    error_msg("Calculus", "hessian",
                              std::format("size {} exceeds maximum matrix element count ({})", n,
                                          ResourceLimits::max_array_size)),
                    loc, "reduce the number of point components"};
            }

            constexpr double h = k_default_second_deriv_step;

            std::vector<std::vector<double>> result(n, std::vector<double>(n));

            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t j = 0; j < n; ++j) {
                    auto pp = point;
                    pp[i] += h;
                    pp[j] += h;
                    auto pm = point;
                    pm[i] += h;
                    pm[j] -= h;
                    auto mp = point;
                    mp[i] -= h;
                    mp[j] += h;
                    auto mm = point;
                    mm[i] -= h;
                    mm[j] -= h;

                    const auto fpp = call_multi_fn(args[1], pp, loc);
                    const auto fpm = call_multi_fn(args[1], pm, loc);
                    const auto fmp = call_multi_fn(args[1], mp, loc);
                    const auto fmm = call_multi_fn(args[1], mm, loc);

                    result[i][j] = (fpp - fpm - fmp + fmm) / (4.0 * h * h);
                }
            }

            return from_mat(result);
        })
        .func("divergence", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& point_arr = expect_array(args[0], "Calculus.divergence", loc);
            const auto& fields_arr = expect_array(args[1], "Calculus.divergence", loc);

            const auto n = point_arr->elements->size();
            if (fields_arr->elements->size() != n) {
                throw RuntimeError{
                    "Calculus.divergence: number of fields must match dimension of point", loc};
            }

            const auto point = to_vec(args[0], "Calculus.divergence", loc);

            constexpr double h = k_default_step_size;
            double div = 0.0;

            for (std::size_t i = 0; i < n; ++i) {
                div += central_diff_partial((*fields_arr->elements)[i], point, i, h, loc);
            }

            return Value{div};
        })
        .func("curl", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& point_arr = expect_array(args[0], "Calculus.curl", loc);
            const auto& fields_arr = expect_array(args[1], "Calculus.curl", loc);

            if (point_arr->elements->size() != 3 || fields_arr->elements->size() != 3) {
                return make_failure_value("Calculus.curl: requires exactly 3 dimensions");
            }

            const auto point = to_vec(args[0], "Calculus.curl", loc);

            auto pd = [&](std::size_t fi, std::size_t vi) {
                return central_diff_partial((*fields_arr->elements)[fi], point, vi,
                                            k_default_step_size, loc);
            };

            const std::vector<double> result{pd(2, 1) - pd(1, 2), pd(0, 2) - pd(2, 0),
                                             pd(1, 0) - pd(0, 1)};

            return make_success_value(from_vec(result));
        })
        .func("jacobian", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& point_arr = expect_array(args[0], "Calculus.jacobian", loc);
            const auto& fields_arr = expect_array(args[1], "Calculus.jacobian", loc);

            const auto rows = fields_arr->elements->size();
            const auto cols = point_arr->elements->size();

            if (rows == 0 || cols == 0) {
                return make_failure_value("Calculus.jacobian: point and fields must be non-empty");
            }

            // Guard the element count so an untrusted size cannot drive an
            // unbounded allocation (matches the hessian guard).
            if (rows > ResourceLimits::max_array_size / cols) {
                return make_failure_value(
                    "Calculus.jacobian: matrix exceeds the maximum element count");
            }

            const auto point = to_vec(args[0], "Calculus.jacobian", loc);

            constexpr double h = k_default_step_size;
            std::vector<std::vector<double>> result(rows, std::vector<double>(cols));

            for (std::size_t i = 0; i < rows; ++i) {
                for (std::size_t j = 0; j < cols; ++j) {
                    // Entry (i, j) = ∂fields[i]/∂x[j] at point.
                    result[i][j] =
                        central_diff_partial((*fields_arr->elements)[i], point, j, h, loc);
                }
            }

            return make_success_value(from_mat(result));
        })
        .func("laplacian", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto point = to_vec(args[0], "Calculus.laplacian", loc);
            const auto n = point.size();

            constexpr double h = k_default_second_deriv_step;
            const auto f_mid = call_multi_fn(args[1], point, loc);

            double laplacian = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                auto plus = point;
                plus[i] += h;
                auto minus = point;
                minus[i] -= h;

                const auto f_plus = call_multi_fn(args[1], plus, loc);
                const auto f_minus = call_multi_fn(args[1], minus, loc);

                // Sum of unmixed second partials ∂²f/∂xᵢ² (trace of the Hessian).
                laplacian += (f_plus - (2.0 * f_mid) + f_minus) / (h * h);
            }

            return Value{laplacian};
        })
        .func("product_series", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto start = expect_integer(args[0], "Calculus.product_series", loc);
            const auto count = expect_integer(args[1], "Calculus.product_series", loc);

            // The ∏ analogue of sum_series: product fn(start) · … · fn(start + n - 1);
            // an empty product (count ≤ 0) is 1.
            if (count > k_max_series_terms) {
                throw RuntimeError{
                    std::format("Calculus.product_series: too many terms, maximum is {}",
                                k_max_series_terms),
                    loc, "reduce the term count"};
            }

            double product{1.0};

            const auto base = static_cast<std::uint64_t>(start);
            for (std::int64_t k{0}; k < count; ++k) {
                const auto i = static_cast<std::int64_t>(base + static_cast<std::uint64_t>(k));
                std::vector<Value> call_args{Value{i}};

                product *= result_to_numeric(invoke_callable(args[2], call_args, loc), loc);
            }

            return Value{product};
        })
        .func("convolution", 5)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto t = args[2].to_numeric();
            const auto a = args[3].to_numeric();
            const auto b = args[4].to_numeric();

            if (a >= b) {
                throw RuntimeError{
                    "Calculus.convolution: lower bound must be less than upper bound", loc};
            }

            const auto result = simpson_rule(
                [&](double tau) {
                    const auto fv = call_fn(args[0], tau, loc);
                    const auto gv = call_fn(args[1], t - tau, loc);
                    return fv * gv;
                },
                a, b, k_default_integration_steps);

            return Value{result};
        })
        .func("taylor", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto center = args[0].to_numeric();

            const auto n_terms = expect_integer(args[1], "Calculus.taylor", loc);

            if (n_terms < k_taylor_min_terms || n_terms > k_taylor_max_terms) {
                throw RuntimeError{
                    error_msg("Calculus", "taylor",
                              std::format("terms must be between {} and {}, got {}",
                                          k_taylor_min_terms, k_taylor_max_terms, n_terms)),
                    loc,
                    std::format("pass an integer between {} and {} as the number of terms",
                                k_taylor_min_terms, k_taylor_max_terms)};
            }

            auto coeffs = std::make_shared<ArrayValue>();

            const auto h = k_default_second_deriv_step;

            double factorial{1.0};

            for (std::int64_t k{0}; k < n_terms; ++k) {
                if (k > 0) {
                    factorial *= static_cast<double>(k);
                }

                const auto dk = nth_derivative(args[2], center, k, h, loc);

                coeffs->elements->emplace_back(dk / factorial);
            }

            return Value{std::move(coeffs)};
        });
}

} // namespace luma
