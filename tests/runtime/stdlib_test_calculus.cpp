// Standard library tests: Calculus module.

#include "common/resource_limits.hpp"
#include "stdlib_test_helpers.hpp"

LUMA_TEST(calculus_derivative) {
    const auto v = eval("Calculus.derivative(3.0, (number x) -> x * x)");

    // Returns raw number, not result.
    const auto n = v.as_number();

    ASSERT_TRUE(n > 5.9 && n < 6.1);
}

LUMA_TEST(calculus_integrate) {
    const auto v = eval("Calculus.integrate(0.0, 2.0, (number x) -> x)");

    // Returns raw number, not result.
    const auto n = v.as_number();

    ASSERT_TRUE(n > 1.9 && n < 2.1);
}

LUMA_TEST(calculus_limit) {
    const auto v = eval("Calculus.limit(1.0, (number x) -> (x * x - 1.0) / (x - 1.0))");

    ASSERT_RESULT_SUCCESS(v);

    const auto n = v.as_result()->owned_inner->as_number();

    ASSERT_TRUE(n > 1.9 && n < 2.1);
}

LUMA_TEST(calculus_module) {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Calculus.derivative"));
    ASSERT_TRUE(env->has("Calculus.derivative_with"));
    ASSERT_TRUE(env->has("Calculus.second_derivative"));
    ASSERT_TRUE(env->has("Calculus.gradient"));
    ASSERT_TRUE(env->has("Calculus.integrate"));
    ASSERT_TRUE(env->has("Calculus.integrate_with"));
    ASSERT_TRUE(env->has("Calculus.root"));
    ASSERT_TRUE(env->has("Calculus.newton"));
    ASSERT_TRUE(env->has("Calculus.minimize"));
    ASSERT_TRUE(env->has("Calculus.maximize"));
    ASSERT_TRUE(env->has("Calculus.limit"));
    ASSERT_TRUE(env->has("Calculus.sum_series"));
    ASSERT_TRUE(env->has("Calculus.partial_derivative"));
    ASSERT_TRUE(env->has("Calculus.divergence"));
    ASSERT_TRUE(env->has("Calculus.curl"));
    ASSERT_TRUE(env->has("Calculus.convolution"));
}

LUMA_TEST(calculus_root) {
    const auto v = eval("Calculus.root(1.0, 3.0, (number x) -> x * x - 4.0)");

    ASSERT_RESULT_SUCCESS(v);

    const auto n = v.as_result()->owned_inner->as_number();

    ASSERT_TRUE(n > 1.9 && n < 2.1);
}

LUMA_TEST(calculus_sum_series) {
    const auto v = eval("Calculus.sum_series(0, 10, (integer n) -> 1.0 / "
                        "Result.unwrap_or(Math.power(2.0, "
                        "Result.unwrap_or(Converter.to_number(n), 0.0)), 1.0))");

    ASSERT_TRUE(v.is_number());
    ASSERT_TRUE(v.as_number() > 1.99 && v.as_number() < 2.01);
}

// Regression: the term-count cap must be inclusive so that requesting exactly
// k_max_series_terms (1'000'000) is accepted, matching the "maximum is N"
// error message and the sibling Calculus.integrate_with's `> k_max` step cap.
// Before the fix the guard used `>=`, so the stated maximum was itself rejected
// as "too many terms" — eval() would then throw here instead of returning 0.
LUMA_TEST(calculus_sum_series_allows_exactly_max_terms) {
    const auto v = eval("Calculus.sum_series(0, 1000000, (integer _n) -> 0.0)");

    ASSERT_TRUE(v.is_number());
    ASSERT_EQ(v.as_number(), 0.0);
}

// One past the inclusive maximum is still rejected (the cap continues to bound
// work); the throw propagates through eval, so ASSERT_THROWS is required.
LUMA_TEST(calculus_sum_series_rejects_beyond_max_terms) {
    ASSERT_THROWS(eval("Calculus.sum_series(0, 1000001, (integer _n) -> 0.0)"));
}

LUMA_TEST(calculus_partial_derivative) {
    // f(x,y) = x^2 + y^2, ∂f/∂x at (3,4) should be ~6
    const auto v = eval("Calculus.partial_derivative([3.0, 4.0], 0, "
                        "(array<number> p) -> p[0] * p[0] + p[1] * p[1])");

    ASSERT_TRUE(v.is_number());
    ASSERT_TRUE(v.as_number() > 5.9 && v.as_number() < 6.1);
}

LUMA_TEST(calculus_partial_derivative_y) {
    // f(x,y) = x^2 + y^2, ∂f/∂y at (3,4) should be ~8
    const auto v = eval("Calculus.partial_derivative([3.0, 4.0], 1, "
                        "(array<number> p) -> p[0] * p[0] + p[1] * p[1])");

    ASSERT_TRUE(v.is_number());
    ASSERT_TRUE(v.as_number() > 7.9 && v.as_number() < 8.1);
}

LUMA_TEST(calculus_divergence) {
    // F = (x^2, y^2), div F = 2x + 2y at (1,1) = 4
    const auto v = eval("Calculus.divergence([1.0, 1.0], ["
                        "(array<number> p) -> p[0] * p[0],"
                        "(array<number> p) -> p[1] * p[1]])");

    ASSERT_TRUE(v.is_number());
    ASSERT_TRUE(v.as_number() > 3.9 && v.as_number() < 4.1);
}

LUMA_TEST(calculus_curl) {
    // F = (y, -x, 0), curl F = (0, 0, -2)
    const auto v = eval("Calculus.curl([0.0, 0.0, 0.0], ["
                        "(array<number> p) -> p[1],"
                        "(array<number> p) -> 0.0 - p[0],"
                        "(array<number> p) -> 0.0]) |> Result.unwrap()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);

    // z-component should be ~ -2
    const auto z = (*v.as_array()->elements)[2].as_number();
    ASSERT_TRUE(z > -2.1 && z < -1.9);
}

LUMA_TEST(calculus_curl_non_3d) {
    ASSERT_EVAL_FAILURE("Calculus.curl([1.0, 2.0], ["
                        "(array<number> p) -> p[0],"
                        "(array<number> p) -> p[1]])");
}

LUMA_TEST(calculus_convolution) {
    // Convolution of identity with identity over [0, 1] at t=1:
    // ∫₀¹ τ * (1 - τ) dτ = 1/6 ≈ 0.1667
    const auto v = eval("Calculus.convolution("
                        "(number x) -> x,"
                        "(number x) -> x,"
                        "1.0, 0.0, 1.0)");

    ASSERT_TRUE(v.is_number());
    ASSERT_TRUE(v.as_number() > 0.16 && v.as_number() < 0.17);
}

LUMA_TEST(calculus_derivative_with) {
    // d/dx (x^2) at x=5 with a custom step should be ~10.
    const auto v = eval("Calculus.derivative_with(5.0, 0.0001, (number x) -> x * x)");

    ASSERT_TRUE(v.is_number());
    ASSERT_TRUE(v.as_number() > 9.9 && v.as_number() < 10.1);
}

LUMA_TEST(calculus_second_derivative) {
    // f(x) = x^3 → f''(x) = 6x, so f''(2) = 12.
    const auto v = eval("Calculus.second_derivative(2.0, (number x) -> x * x * x)");

    ASSERT_TRUE(v.is_number());
    ASSERT_TRUE(v.as_number() > 11.9 && v.as_number() < 12.1);
}

LUMA_TEST(calculus_gradient) {
    // ∇(x^2 + y^2) at (3, 4) = [6, 8].
    const auto v = eval("Calculus.gradient([3.0, 4.0], "
                        "(array<number> p) -> p[0] * p[0] + p[1] * p[1])");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);

    const auto gx = (*v.as_array()->elements)[0].as_number();
    const auto gy = (*v.as_array()->elements)[1].as_number();

    ASSERT_TRUE(gx > 5.9 && gx < 6.1);
    ASSERT_TRUE(gy > 7.9 && gy < 8.1);
}

LUMA_TEST(calculus_integrate_with) {
    // ∫₀¹ x^2 dx = 1/3 with an explicit subdivision count.
    const auto v = eval("Calculus.integrate_with(0.0, 1.0, 100, (number x) -> x * x)");

    ASSERT_TRUE(v.is_number());
    ASSERT_TRUE(v.as_number() > 0.33 && v.as_number() < 0.34);
}

LUMA_TEST(calculus_newton) {
    // Newton's method on x^2 - 2 from x0 = 2 converges to √2 ≈ 1.41421.
    const auto v = eval("Calculus.newton(2.0, (number x) -> x * x - 2.0)");

    ASSERT_RESULT_SUCCESS(v);

    const auto n = v.as_result()->owned_inner->as_number();

    ASSERT_TRUE(n > 1.41 && n < 1.42);
}

LUMA_TEST(calculus_minimize) {
    // min of (x - 1)^2 on [-2, 2] is at x = 1 with f = 0; result is (x, f).
    const auto v = eval("Calculus.minimize(-2.0, 2.0, (number x) -> (x - 1.0) * (x - 1.0))");

    ASSERT_TRUE(v.is_tuple());
    ASSERT_EQ(v.as_tuple()->elements.size(), 2U);

    const auto x = v.as_tuple()->elements[0].as_number();
    const auto fx = v.as_tuple()->elements[1].as_number();

    ASSERT_TRUE(x > 0.99 && x < 1.01);
    ASSERT_TRUE(fx > -0.01 && fx < 0.01);
}

LUMA_TEST(calculus_maximize) {
    // max of -(x - 1)^2 on [-2, 2] is at x = 1 with f = 0; result is (x, f).
    const auto v = eval("Calculus.maximize(-2.0, 2.0, "
                        "(number x) -> 0.0 - (x - 1.0) * (x - 1.0))");

    ASSERT_TRUE(v.is_tuple());
    ASSERT_EQ(v.as_tuple()->elements.size(), 2U);

    const auto x = v.as_tuple()->elements[0].as_number();
    const auto fx = v.as_tuple()->elements[1].as_number();

    ASSERT_TRUE(x > 0.99 && x < 1.01);
    ASSERT_TRUE(fx > -0.01 && fx < 0.01);
}

LUMA_TEST(calculus_root_no_sign_change) {
    // f(x) = x^2 + 1 is positive on [2, 3]: no bracketed root → failure result.
    ASSERT_EVAL_FAILURE("Calculus.root(2.0, 3.0, (number x) -> x * x + 1.0)");
}

LUMA_TEST(calculus_newton_zero_derivative) {
    // A constant function has a zero derivative everywhere → failure result.
    ASSERT_EVAL_FAILURE("Calculus.newton(0.0, (number _x) -> 5.0)");
}

LUMA_TEST(calculus_derivative_with_nonpositive_step) {
    ASSERT_THROWS(eval("Calculus.derivative_with(1.0, 0.0, (number x) -> x * x)"));
    ASSERT_THROWS(eval("Calculus.derivative_with(1.0, -0.5, (number x) -> x * x)"));
}

LUMA_TEST(calculus_integrate_with_nonpositive_steps) {
    ASSERT_THROWS(eval("Calculus.integrate_with(0.0, 1.0, 0, (number x) -> x)"));
    ASSERT_THROWS(eval("Calculus.integrate_with(0.0, 1.0, -5, (number x) -> x)"));
}

LUMA_TEST(calculus_integrate_with_steps_capped) {
    // A huge subdivision count must be rejected rather than overflow the
    // even-rounding `steps = n + 1` (signed-overflow UB) or loop ~2^63 times.
    ASSERT_THROWS(eval("Calculus.integrate_with(0.0, 1.0, 9223372036854775807, (number x) -> x)"));
    ASSERT_THROWS(eval("Calculus.integrate_with(0.0, 1.0, 2000000, (number x) -> x)"));
}

LUMA_TEST(calculus_sum_series_too_many_terms) {
    // The term count exceeds the one-million guard.
    ASSERT_THROWS(eval("Calculus.sum_series(0, 2000000, (integer _n) -> 1.0)"));

    // An adversarially huge term count must be rejected rather than loop ~2^63
    // times.
    ASSERT_THROWS(eval("Calculus.sum_series(-1, 9223372036854775807, (integer _n) -> 1.0)"));
}

LUMA_TEST(calculus_sum_series_counts_terms_from_start) {
    // Per the documented contract, the second argument is the term COUNT n:
    // sum_series(start, n, fn) = fn(start) + ... + fn(start + n - 1).
    // sum_series(5, 3, n -> n) must therefore sum 5 + 6 + 7 = 18 — not 0, which
    // the previous (inclusive-end) implementation returned for end < start.
    const auto v = eval("Calculus.sum_series(5, 3, (integer n) -> "
                        "Result.unwrap_or(Converter.to_number(n), 0.0))");

    ASSERT_TRUE(v.is_number());
    ASSERT_TRUE(v.as_number() > 17.999 && v.as_number() < 18.001);
}

LUMA_TEST(calculus_partial_derivative_index_out_of_range) {
    ASSERT_THROWS(eval("Calculus.partial_derivative([1.0, 2.0], 5, "
                       "(array<number> p) -> p[0] + p[1])"));
    ASSERT_THROWS(eval("Calculus.partial_derivative([1.0, 2.0], -1, "
                       "(array<number> p) -> p[0] + p[1])"));
}

LUMA_TEST(calculus_divergence_field_mismatch) {
    // A 3D point with a single component field is a dimension mismatch.
    ASSERT_THROWS(eval("Calculus.divergence([1.0, 2.0, 3.0], "
                       "[(array<number> p) -> p[0]])"));
}

LUMA_TEST(calculus_convolution_invalid_bounds) {
    // The lower bound must be strictly less than the upper bound.
    ASSERT_THROWS(eval("Calculus.convolution((number x) -> x, (number x) -> x, "
                       "1.0, 1.0, 0.0)"));
}

LUMA_TEST(calculus_callback_failure) {
    // A callback that yields a failure result must surface as a runtime error.
    ASSERT_THROWS(eval("Calculus.derivative(1.0, (number _x) -> failure(\"boom\"))"));
}

int main() {
    LUMA_RUN_ALL();
}
