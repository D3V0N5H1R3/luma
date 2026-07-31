// Standard library tests: Statistics module.

#include <cstdint>
#include <string>

#include "stdlib_test_helpers.hpp"

LUMA_TEST(statistics_correlation) {
    const auto ok = eval("Statistics.correlation([1.0, 2.0, 3.0], [2.0, 4.0, 6.0])");

    ASSERT_RESULT_SUCCESS(ok);
    ASSERT_TRUE(ok.as_result()->owned_inner->as_number() > 0.99);

    ASSERT_EVAL_FAILURE("Statistics.correlation([1.0, 2.0], [1.0, 2.0, 3.0])");

    ASSERT_EVAL_FAILURE("Statistics.correlation([1.0], [1.0])");
}

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

LUMA_TEST(statistics_percentile) {
    ASSERT_EVAL_NUM("Statistics.percentile([1.0, 2.0, 3.0, 4.0, 5.0], 50.0)", 3.0);
}

LUMA_TEST(statistics_percentile_empty) {
    ASSERT_EVAL_FAILURE("Statistics.percentile([], 50.0)");
}

LUMA_TEST(statistics_percentile_out_of_range) {
    ASSERT_EVAL_FAILURE("Statistics.percentile([1.0, 2.0, 3.0], 101.0)");
}

LUMA_TEST(statistics_percentile_not_a_number) {
    // Regression: a NaN percentile passed the [0,100] range check (every NaN
    // comparison is false), then a NaN-derived rank was cast to size_t (UB) and
    // used to subscript the values.  Non-finite p is now rejected cleanly.
    ASSERT_EVAL_FAILURE("Statistics.percentile([1.0, 2.0, 3.0], Math.infinity - Math.infinity)");
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

LUMA_TEST(statistics_summarize) {
    const auto v = eval("Statistics.summarize([2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, std::string{"Summary"});
    ASSERT_EQ(rec->find_field("count")->as_integer(), static_cast<std::int64_t>(8));
    ASSERT_NEAR(rec->find_field("minimum")->as_number(), 2.0, 1e-9);
    ASSERT_NEAR(rec->find_field("maximum")->as_number(), 9.0, 1e-9);
    ASSERT_NEAR(rec->find_field("mean")->as_number(), 5.0, 1e-9);
    ASSERT_NEAR(rec->find_field("median")->as_number(), 4.5, 1e-9);
    ASSERT_TRUE(rec->find_field("standard_deviation")->as_number() > 1.9);
    ASSERT_TRUE(rec->find_field("standard_deviation")->as_number() < 2.1);
}

LUMA_TEST(statistics_summarize_single) {
    const auto v = eval("Statistics.summarize([42.0])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->find_field("count")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_NEAR(rec->find_field("minimum")->as_number(), 42.0, 1e-9);
    ASSERT_NEAR(rec->find_field("maximum")->as_number(), 42.0, 1e-9);
    ASSERT_NEAR(rec->find_field("mean")->as_number(), 42.0, 1e-9);
    ASSERT_NEAR(rec->find_field("median")->as_number(), 42.0, 1e-9);
    ASSERT_NEAR(rec->find_field("standard_deviation")->as_number(), 0.0, 1e-9);
}

LUMA_TEST(statistics_summarize_empty) {
    ASSERT_EVAL_FAILURE("Statistics.summarize([])");
}

LUMA_TEST(statistics_linear_fit) {
    // Perfect line y = 2x.
    const auto v = eval("Statistics.linear_fit([1.0, 2.0, 3.0, 4.0], [2.0, 4.0, 6.0, 8.0])");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, std::string{"LineFit"});
    ASSERT_NEAR(rec->find_field("slope")->as_number(), 2.0, 1e-9);
    ASSERT_NEAR(rec->find_field("intercept")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(rec->find_field("r_squared")->as_number(), 1.0, 1e-9);
}

LUMA_TEST(statistics_linear_fit_failures) {
    // Length mismatch, too few points, and zero x-variance all fail.
    ASSERT_EVAL_FAILURE("Statistics.linear_fit([1.0, 2.0], [1.0])");
    ASSERT_EVAL_FAILURE("Statistics.linear_fit([1.0], [1.0])");
    ASSERT_EVAL_FAILURE("Statistics.linear_fit([5.0, 5.0, 5.0], [1.0, 2.0, 3.0])");
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

// --- Statistics.FiveNumberSummary (N06) ---

LUMA_TEST(statistics_five_number_summary) {
    const auto v =
        eval("Statistics.five_number_summary([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0])");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, std::string{"FiveNumberSummary"});
    ASSERT_NEAR(rec->find_field("minimum")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(rec->find_field("median")->as_number(), 5.0, 1e-9);
    ASSERT_NEAR(rec->find_field("maximum")->as_number(), 9.0, 1e-9);
    ASSERT_NEAR(rec->find_field("q1")->as_number(), 3.0, 1e-9);
    ASSERT_NEAR(rec->find_field("q3")->as_number(), 7.0, 1e-9);
}

LUMA_TEST(statistics_five_number_summary_empty_fails) {
    ASSERT_EVAL_FAILURE("Statistics.five_number_summary([])");
}

// --- Statistics.Histogram (N01) ---

LUMA_TEST(statistics_histogram_basic) {
    // Ten values in [0, 10) across five equal-width bins of width 2.
    const auto v =
        eval("Statistics.histogram([0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0], 5)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, std::string{"Histogram"});
    ASSERT_NEAR(rec->find_field("bin_width")->as_number(), 1.8, 1e-9);

    const auto& edges = *rec->find_field("bin_edges")->as_array()->elements;
    ASSERT_EQ(edges.size(), 6U);
    ASSERT_NEAR(edges.front().as_number(), 0.0, 1e-9);
    ASSERT_NEAR(edges.back().as_number(), 9.0, 1e-9);

    const auto& counts = *rec->find_field("counts")->as_array()->elements;
    ASSERT_EQ(counts.size(), 5U);

    // Every sample counted exactly once, including the maximum (closed last bin).
    std::int64_t total{0};
    for (const auto& c : counts) {
        total += c.as_integer();
    }
    ASSERT_EQ(total, static_cast<std::int64_t>(10));
    // The maximum (9.0) lands in the final bin.
    ASSERT_TRUE(counts.back().as_integer() >= static_cast<std::int64_t>(1));
}

LUMA_TEST(statistics_histogram_equal_values) {
    // A zero-width range widens so every equal value falls in one bin.
    const auto v = eval("Statistics.histogram([5.0, 5.0, 5.0], 3)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    const auto& counts = *rec->find_field("counts")->as_array()->elements;
    ASSERT_EQ(counts.size(), 3U);

    std::int64_t total{0};
    for (const auto& c : counts) {
        total += c.as_integer();
    }
    ASSERT_EQ(total, static_cast<std::int64_t>(3));
    ASSERT_TRUE(rec->find_field("bin_width")->as_number() > 0.0);
}

LUMA_TEST(statistics_histogram_single_bin) {
    const auto v = eval("Statistics.histogram([1.0, 2.0, 3.0, 4.0], 1)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    const auto& edges = *rec->find_field("bin_edges")->as_array()->elements;
    const auto& counts = *rec->find_field("counts")->as_array()->elements;
    ASSERT_EQ(edges.size(), 2U);
    ASSERT_EQ(counts.size(), 1U);
    ASSERT_EQ(counts.front().as_integer(), static_cast<std::int64_t>(4));
}

LUMA_TEST(statistics_histogram_empty_fails) {
    ASSERT_EVAL_FAILURE("Statistics.histogram([], 5)");
}

LUMA_TEST(statistics_histogram_zero_bins_fails) {
    ASSERT_EVAL_FAILURE("Statistics.histogram([1.0, 2.0, 3.0], 0)");
}

LUMA_TEST(statistics_histogram_skips_non_finite) {
    // Non-finite samples are excluded from the range and the tally rather than
    // poisoning bin_width / bin positions.
    const auto v =
        eval("Statistics.histogram([1.0, 2.0, 3.0, Math.infinity, Math.infinity * -1.0], 2)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    const auto& edges = *rec->find_field("bin_edges")->as_array()->elements;
    const auto& counts = *rec->find_field("counts")->as_array()->elements;

    // Range spans only the finite samples [1, 3].
    ASSERT_NEAR(edges.front().as_number(), 1.0, 1e-9);
    ASSERT_NEAR(edges.back().as_number(), 3.0, 1e-9);

    std::int64_t total{0};
    for (const auto& c : counts) {
        total += c.as_integer();
    }
    ASSERT_EQ(total, static_cast<std::int64_t>(3));
}

LUMA_TEST(statistics_histogram_all_non_finite_fails) {
    ASSERT_EVAL_FAILURE("Statistics.histogram([Math.infinity, Math.infinity * -1.0], 3)");
}

LUMA_TEST(statistics_histogram_excessive_bins_fails) {
    // A bin count past the array-size contract fails instead of allocating.
    ASSERT_EVAL_FAILURE("Statistics.histogram([1.0, 2.0, 3.0], 2000000000)");
}

int main() {
    LUMA_RUN_ALL();
}
