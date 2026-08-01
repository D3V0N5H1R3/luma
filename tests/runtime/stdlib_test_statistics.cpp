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

int main() {
    LUMA_RUN_ALL();
}
