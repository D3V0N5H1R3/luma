// Standard library tests: Statistics module.

#include <string>

#include "stdlib_test_helpers.hpp"

LUMA_TEST(statistics_mean) {
    const auto v = eval("Statistics.mean([2.0, 4.0, 6.0])");

    ASSERT_RESULT_SUCCESS(v);

    const auto d = v.as_result()->owned_inner->as_number();

    ASSERT_TRUE(d > 3.99 && d < 4.01);
}

LUMA_TEST(statistics_mean_empty) {
    ASSERT_EVAL_FAILURE("Statistics.mean([])");
}

LUMA_TEST(statistics_median) {
    const auto v = eval("Statistics.median([1.0, 3.0, 2.0])");

    ASSERT_RESULT_SUCCESS(v);

    const auto d = v.as_result()->owned_inner->as_number();

    ASSERT_TRUE(d > 1.99 && d < 2.01);
}

LUMA_TEST(statistics_median_empty) {
    ASSERT_EVAL_FAILURE("Statistics.median([])");
}

LUMA_TEST(statistics_mode) {
    ASSERT_EVAL_NUM("Statistics.mode([1.0, 2.0, 2.0, 3.0])", 2.0);
}

LUMA_TEST(statistics_mode_empty) {
    ASSERT_EVAL_FAILURE("Statistics.mode([])");
}

LUMA_TEST(statistics_standard_deviation) {
    const auto v = eval("Statistics.standard_deviation([2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() > 1.9);
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() < 2.1);
}

LUMA_TEST(statistics_standard_deviation_empty) {
    ASSERT_EVAL_FAILURE("Statistics.standard_deviation([])");
}

LUMA_TEST(statistics_variance) {
    const auto v = eval("Statistics.variance([2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() > 3.9);
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() < 4.1);
}

LUMA_TEST(statistics_variance_empty) {
    ASSERT_EVAL_FAILURE("Statistics.variance([])");
}

// Regression: a plain running sum overflowed to inf for values near DBL_MAX and
// was reported as success(inf).
LUMA_TEST(statistics_mean_near_max_does_not_overflow) {
    ASSERT_EVAL_NUM("Statistics.mean([1e308, 1e308])", 1e308);
    ASSERT_EVAL_NUM("Statistics.mean([1e308, -1e308])", 0.0);
}

LUMA_TEST(statistics_median_near_max_does_not_overflow) {
    ASSERT_EVAL_NUM("Statistics.median([1e308, 1e308])", 1e308);
}

// Regression: non-finite statistics are failures, not success(inf/nan).
LUMA_TEST(statistics_non_finite_results_are_failures) {
    ASSERT_EVAL_FAILURE("Statistics.mean([Math.infinity, 1.0])");
    ASSERT_EVAL_FAILURE("Statistics.median([Math.infinity])");
    ASSERT_EVAL_FAILURE("Statistics.mode([Math.infinity])");
    ASSERT_EVAL_FAILURE("Statistics.variance([1e308, -1e308])");
    ASSERT_EVAL_FAILURE("Statistics.standard_deviation([Math.infinity, 1.0])");
}

LUMA_TEST(statistics_welford_variance_matches_population_variance) {
    ASSERT_EVAL_NUM("Statistics.variance([1.0, 2.0, 3.0, 4.0])", 1.25);
    ASSERT_EVAL_NUM("Statistics.variance([1e9 + 4.0, 1e9 + 7.0, 1e9 + 13.0, 1e9 + 16.0])", 22.5);
}

// Regression: an incremental mean rounded at every step, so the mean of small
// integers differed from sum / n (mean([0, 1, 6, 1]) was 1.9999999999999998).
LUMA_TEST(statistics_mean_matches_sum_divided_by_count) {
    ASSERT_EVAL_NUM("Statistics.mean([0.0, 1.0, 6.0, 1.0])", 2.0);
    ASSERT_EVAL_NUM("Statistics.mean([0, 1, 6, 1])", 2.0);
    ASSERT_EVAL_NUM("Statistics.mean([1.0, 2.0, 4.0])", 7.0 / 3.0);
}

// Regression: Welford's update rounded at every step, so the variance of
// small integers differed from the two-pass result (0.18750000000000003).
LUMA_TEST(statistics_variance_is_exact_for_small_integers) {
    ASSERT_EVAL_NUM("Statistics.variance([0.0, 0.0, 1.0, 0.0])", 0.1875);
    ASSERT_EVAL_NUM("Statistics.variance([0.0, 1.0, 6.0, 1.0])", 5.5);
}

// The overflow fallback scales by the largest magnitude, so a standard
// deviation whose variance is not representable still succeeds.
LUMA_TEST(statistics_standard_deviation_near_max_uses_scaled_fallback) {
    ASSERT_EVAL_NUM("Statistics.standard_deviation([1e308, -1e308])", 1e308);
    ASSERT_EVAL_NUM("Statistics.variance([1e154, -1e154])", 1e308);
    ASSERT_EVAL_FAILURE("Statistics.variance([1e308, -1e308])");
    ASSERT_EVAL_NUM("Statistics.mean([1e308, 1e308, 1e308])", 1e308);
}

int main() {
    LUMA_RUN_ALL();
}
