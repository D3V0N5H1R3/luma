// Unit tests for core/common/index_validator.hpp.

#include <cstdint>
#include <limits>
#include <stdexcept>

#include "common/index_validator.hpp"
#include "test_framework.hpp"

using namespace luma;

// ═══════════════════════════════════════════════════════════
// is_index_out_of_bounds / validate_index
// ═══════════════════════════════════════════════════════════

static void test_is_index_out_of_bounds_negative() {
    ASSERT_TRUE(is_index_out_of_bounds(-1, 5));
}

static void test_is_index_out_of_bounds_too_large() {
    ASSERT_TRUE(is_index_out_of_bounds(5, 5));
}

static void test_is_index_out_of_bounds_in_range() {
    ASSERT_FALSE(is_index_out_of_bounds(0, 5));
    ASSERT_FALSE(is_index_out_of_bounds(4, 5));
}

static void test_validate_index_throws_out_of_range() {
    ASSERT_THROWS_AS(validate_index(-1, 5), std::out_of_range);
    ASSERT_THROWS_AS(validate_index(5, 5), std::out_of_range);
}

// ═══════════════════════════════════════════════════════════
// normalize_index
// ═══════════════════════════════════════════════════════════

static void test_normalize_index_positive_unchanged() {
    ASSERT_EQ(normalize_index(2, 5), std::int64_t{2});
}

static void test_normalize_index_negative_maps_from_end() {
    ASSERT_EQ(normalize_index(-1, 5), std::int64_t{4});
    ASSERT_EQ(normalize_index(-5, 5), std::int64_t{0});
}

// ═══════════════════════════════════════════════════════════
// compute_slice_range
// ═══════════════════════════════════════════════════════════

static void test_compute_slice_range_basic_exclusive() {
    const auto range = compute_slice_range(1, 3, 5, RangeEnd::Exclusive);
    ASSERT_EQ(range.start, std::int64_t{1});
    ASSERT_EQ(range.end, std::int64_t{3});
}

static void test_compute_slice_range_basic_inclusive() {
    const auto range = compute_slice_range(1, 3, 5, RangeEnd::Inclusive);
    ASSERT_EQ(range.start, std::int64_t{1});
    ASSERT_EQ(range.end, std::int64_t{4});
}

static void test_compute_slice_range_negative_indices() {
    // Python-style: -1 maps to size-1.
    const auto range = compute_slice_range(-3, -1, 5, RangeEnd::Inclusive);
    ASSERT_EQ(range.start, std::int64_t{2});
    ASSERT_EQ(range.end, std::int64_t{5});
}

static void test_compute_slice_range_clamps_end_to_size() {
    const auto range = compute_slice_range(0, 100, 3, RangeEnd::Exclusive);
    ASSERT_EQ(range.start, std::int64_t{0});
    ASSERT_EQ(range.end, std::int64_t{3});
}

static void test_compute_slice_range_inclusive_through_last_element() {
    // x[a..=-1] — "through the last element, inclusive".
    const auto range = compute_slice_range(0, -1, 5, RangeEnd::Inclusive);
    ASSERT_EQ(range.start, std::int64_t{0});
    ASSERT_EQ(range.end, std::int64_t{5});
}

// Regression test for B02: both raw bounds deeply negative (beyond -size)
// must clamp `end` to 0, not leave it negative. The function's contract
// (documented as "[0, size]") was previously violated: end stayed negative
// because only the upper bound (`end = std::min(end, size)`) was clamped.
static void test_compute_slice_range_deeply_negative_end_clamped_to_zero() {
    const auto range = compute_slice_range(-100, -90, 3, RangeEnd::Exclusive);
    ASSERT_EQ(range.start, std::int64_t{0});
    ASSERT_EQ(range.end, std::int64_t{0});
    ASSERT_GE(range.end, std::int64_t{0});
}

static void test_compute_slice_range_deeply_negative_start_and_end() {
    const auto range = compute_slice_range(-1000, -999, 10, RangeEnd::Inclusive);
    ASSERT_EQ(range.start, std::int64_t{0});
    ASSERT_EQ(range.end, std::int64_t{0});
}

// ─── main ───

int main() {
    // is_index_out_of_bounds / validate_index
    RUN(test_is_index_out_of_bounds_negative);
    RUN(test_is_index_out_of_bounds_too_large);
    RUN(test_is_index_out_of_bounds_in_range);
    RUN(test_validate_index_throws_out_of_range);

    // normalize_index
    RUN(test_normalize_index_positive_unchanged);
    RUN(test_normalize_index_negative_maps_from_end);

    // compute_slice_range
    RUN(test_compute_slice_range_basic_exclusive);
    RUN(test_compute_slice_range_basic_inclusive);
    RUN(test_compute_slice_range_negative_indices);
    RUN(test_compute_slice_range_clamps_end_to_size);
    RUN(test_compute_slice_range_inclusive_through_last_element);
    RUN(test_compute_slice_range_deeply_negative_end_clamped_to_zero);
    RUN(test_compute_slice_range_deeply_negative_start_and_end);

    return SUMMARY();
}
