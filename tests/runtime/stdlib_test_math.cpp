// Standard library tests: Math module.

#include "stdlib_test_helpers.hpp"

LUMA_TEST(math_absolute) {
    ASSERT_EVAL_INT("Math.absolute(-5)", 5);

    ASSERT_EVAL_INT("Math.absolute(5)", 5);
}

LUMA_TEST(math_approximately_equal) {
    // Default epsilon (1e-9).
    ASSERT_EQ(eval("Math.approximately_equal(1.0, 1.0)").as_bool(), true);
    ASSERT_EQ(eval("Math.approximately_equal(1.0, 1.0000000001)").as_bool(), true);
    ASSERT_EQ(eval("Math.approximately_equal(1.0, 1.001)").as_bool(), false);
    ASSERT_EQ(eval("Math.approximately_equal(0.1 + 0.2, 0.3)").as_bool(), true);

    // Custom epsilon.
    ASSERT_EQ(eval("Math.approximately_equal(1.0, 1.05, 0.1)").as_bool(), true);
    ASSERT_EQ(eval("Math.approximately_equal(1.0, 1.2, 0.1)").as_bool(), false);
    ASSERT_EQ(eval("Math.approximately_equal(0.0, 0.0, 0.0)").as_bool(), true);
}

LUMA_TEST(math_arc_cosine) {
    const auto ok_val = eval("Math.arc_cosine(0.0)");

    ASSERT_RESULT_SUCCESS(ok_val);

    // value ≈ π/2
    const auto n = ok_val.as_result()->owned_inner->as_number();

    ASSERT_TRUE(n > 1.57 && n < 1.58);

    ASSERT_EVAL_FAILURE("Math.arc_cosine(2.0)");
}

LUMA_TEST(math_arc_sine) {
    const auto ok_val = eval("Math.arc_sine(1.0)");

    ASSERT_RESULT_SUCCESS(ok_val);

    // value ≈ π/2
    const auto n = ok_val.as_result()->owned_inner->as_number();

    ASSERT_TRUE(n > 1.57 && n < 1.58);

    ASSERT_EVAL_FAILURE("Math.arc_sine(-2.0)");
}

LUMA_TEST(math_clamp) {
    // Happy path: returns result<number>.
    ASSERT_EVAL_NUM("Math.clamp(15.0, 0.0, 10.0)", 10.0);

    ASSERT_EVAL_NUM("Math.clamp(-5.0, 0.0, 10.0)", 0.0);

    ASSERT_EVAL_NUM("Math.clamp(5.0, 0.0, 10.0)", 5.0);

    // lo > hi → fail.
    ASSERT_EVAL_FAILURE("Math.clamp(5.0, 10.0, 0.0)");
}

LUMA_TEST(math_constants) {
    const auto pi = eval("Math.pi");

    ASSERT_TRUE(pi.is_number());

    const auto d = pi.as_number();

    ASSERT_TRUE(d > 3.14 && d < 3.15);
}

LUMA_TEST(math_correlation) {
    const auto ok = eval("Math.correlation([1.0, 2.0, 3.0], [2.0, 4.0, 6.0])");

    ASSERT_RESULT_SUCCESS(ok);
    ASSERT_TRUE(ok.as_result()->owned_inner->as_number() > 0.99);

    ASSERT_EVAL_FAILURE("Math.correlation([1.0, 2.0], [1.0, 2.0, 3.0])");

    ASSERT_EVAL_FAILURE("Math.correlation([1.0], [1.0])");
}

LUMA_TEST(math_factorial) {
    ASSERT_EVAL_INT("Math.factorial(5)", 120);
}

LUMA_TEST(math_floor_ceil_round) {
    ASSERT_EVAL_INT("Math.floor(3.7)", 3);

    ASSERT_EVAL_INT("Math.ceil(3.2)", 4);

    ASSERT_EVAL_INT("Math.round(3.5)", 4);
}

LUMA_TEST(math_gcd) {
    ASSERT_EVAL_INT("Math.greatest_common_divisor(12, 8)", 4);
}

LUMA_TEST(math_is_infinite) {
    ASSERT_EQ(eval("Math.is_infinite(Math.infinity)").as_bool(), true);
    ASSERT_EQ(eval("Math.is_infinite(-Math.infinity)").as_bool(), true);
    ASSERT_EQ(eval("Math.is_infinite(3.14)").as_bool(), false);
    ASSERT_EQ(eval("Math.is_infinite(0.0)").as_bool(), false);
}

LUMA_TEST(math_is_not_a_number) {
    // Math.infinity - Math.infinity produces NaN.
    ASSERT_EQ(eval("Math.is_not_a_number(Math.infinity - Math.infinity)").as_bool(), true);
    ASSERT_EQ(eval("Math.is_not_a_number(3.14)").as_bool(), false);
    ASSERT_EQ(eval("Math.is_not_a_number(0.0)").as_bool(), false);
    ASSERT_EQ(eval("Math.is_not_a_number(Math.infinity)").as_bool(), false);
}

LUMA_TEST(math_is_prime) {
    ASSERT_EQ(eval("Math.is_prime(7)").as_bool(), true);
    ASSERT_EQ(eval("Math.is_prime(4)").as_bool(), false);

    // Large prime (2^31-1) exercises the overflow-safe `i <= n / i` trial
    // division near where the old `i * i` form would begin to overflow.
    ASSERT_EQ(eval("Math.is_prime(2147483647)").as_bool(), true);
}

LUMA_TEST(math_lerp) {
    // Happy path: returns result<number>.
    ASSERT_EVAL_NUM("Math.lerp(0.0, 10.0, 0.5)", 5.0);

    ASSERT_EVAL_NUM("Math.lerp(0.0, 10.0, 0.0)", 0.0);

    ASSERT_EVAL_NUM("Math.lerp(0.0, 10.0, 1.0)", 10.0);

    // t outside [0, 1] → fail.
    ASSERT_EVAL_FAILURE("Math.lerp(0.0, 10.0, 1.5)");

    ASSERT_EVAL_FAILURE("Math.lerp(0.0, 10.0, -0.1)");
}

LUMA_TEST(math_mean) {
    const auto v = eval("Math.mean([2.0, 4.0, 6.0])");

    ASSERT_RESULT_SUCCESS(v);

    const auto d = v.as_result()->owned_inner->as_number();

    ASSERT_TRUE(d > 3.99 && d < 4.01);
}

LUMA_TEST(math_mean_empty) {
    ASSERT_EVAL_FAILURE("Math.mean([])");
}

LUMA_TEST(math_median) {
    const auto v = eval("Math.median([1.0, 3.0, 2.0])");

    ASSERT_RESULT_SUCCESS(v);

    const auto d = v.as_result()->owned_inner->as_number();

    ASSERT_TRUE(d > 1.99 && d < 2.01);
}

LUMA_TEST(math_median_empty) {
    ASSERT_EVAL_FAILURE("Math.median([])");
}

LUMA_TEST(math_mode) {
    ASSERT_EVAL_NUM("Math.mode([1.0, 2.0, 2.0, 3.0])", 2.0);
}

LUMA_TEST(math_mode_empty) {
    ASSERT_EVAL_FAILURE("Math.mode([])");
}

LUMA_TEST(math_module) {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Math.absolute"));
    ASSERT_TRUE(env->has("Math.floor"));
    ASSERT_TRUE(env->has("Math.ceil"));
}

LUMA_TEST(math_percentile) {
    ASSERT_EVAL_NUM("Math.percentile([1.0, 2.0, 3.0, 4.0, 5.0], 50.0)", 3.0);
}

LUMA_TEST(math_percentile_empty) {
    ASSERT_EVAL_FAILURE("Math.percentile([], 50.0)");
}

LUMA_TEST(math_percentile_out_of_range) {
    ASSERT_EVAL_FAILURE("Math.percentile([1.0, 2.0, 3.0], 101.0)");
}

LUMA_TEST(math_percentile_not_a_number) {
    // Regression: a NaN percentile passed the [0,100] range check (every NaN
    // comparison is false), then a NaN-derived rank was cast to size_t (UB) and
    // used to subscript the values.  Non-finite p is now rejected cleanly.
    ASSERT_EVAL_FAILURE("Math.percentile([1.0, 2.0, 3.0], Math.infinity - Math.infinity)");
}

LUMA_TEST(math_power) {
    const auto v = eval("Math.power(2.0, 10.0)");

    ASSERT_RESULT_SUCCESS(v);

    const auto d = v.as_result()->owned_inner->as_number();

    ASSERT_TRUE(d > 1023.9 && d < 1024.1);
}

LUMA_TEST(math_power_fail) {
    // Negative base with fractional exponent → NaN → fail.
    ASSERT_EVAL_FAILURE("Math.power(-1.0, 0.5)");
}

LUMA_TEST(math_remap) {
    const auto v = eval("Math.remap(5.0, 0.0, 10.0, 0.0, 100.0)");
    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 50.0, 1e-10);

    ASSERT_EVAL_FAILURE("Math.remap(5.0, 3.0, 3.0, 0.0, 1.0)");
}

LUMA_TEST(math_smooth_step) {
    const auto v = eval("Math.smooth_step(0.0, 1.0, 0.5)");
    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 0.5, 1e-10);

    const auto edge = eval("Math.smooth_step(0.0, 1.0, 0.0)");
    ASSERT_RESULT_SUCCESS(edge);
    ASSERT_NEAR(edge.as_result()->owned_inner->as_number(), 0.0, 1e-10);

    ASSERT_EVAL_FAILURE("Math.smooth_step(5.0, 5.0, 0.5)");
}

LUMA_TEST(math_remainder) {
    ASSERT_EVAL_INT("Math.remainder(7, 3)", 1);

    ASSERT_EVAL_FAILURE("Math.remainder(7, 0)");
}

LUMA_TEST(math_sign) {
    ASSERT_EQ(eval("Math.sign(5)").as_integer(), 1);
    ASSERT_EQ(eval("Math.sign(-5)").as_integer(), -1);
    ASSERT_EQ(eval("Math.sign(0)").as_integer(), 0);
}

LUMA_TEST(math_sign_of) {
    // The exhaustive Sign choice counterpart to Math.sign's magic -1 / 0 / 1.
    const auto neg = eval("Math.sign_of(-5)");
    ASSERT_TRUE(neg.is_choice());
    ASSERT_EQ(neg.as_choice()->type_name, "Sign");
    ASSERT_EQ(neg.as_choice()->variant, "Negative");

    const auto zero = eval("Math.sign_of(0)");
    ASSERT_TRUE(zero.is_choice());
    ASSERT_EQ(zero.as_choice()->variant, "Zero");

    const auto pos = eval("Math.sign_of(7)");
    ASSERT_TRUE(pos.is_choice());
    ASSERT_EQ(pos.as_choice()->variant, "Positive");

    // Works for number as well as integer inputs.
    ASSERT_EQ(eval("Math.sign_of(-0.5)").as_choice()->variant, "Negative");
    ASSERT_EQ(eval("Math.sign_of(3.14)").as_choice()->variant, "Positive");
    ASSERT_EQ(eval("Math.sign_of(0.0)").as_choice()->variant, "Zero");
}

LUMA_TEST(math_square_root) {
    const auto v = eval("Math.square_root(16.0)");

    ASSERT_RESULT_SUCCESS(v);
}

LUMA_TEST(math_standard_deviation) {
    const auto v = eval("Math.standard_deviation([2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() > 1.9);
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() < 2.1);
}

LUMA_TEST(math_standard_deviation_empty) {
    ASSERT_EVAL_FAILURE("Math.standard_deviation([])");
}

LUMA_TEST(math_summarize) {
    const auto v = eval("Math.summarize([2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0])");

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

LUMA_TEST(math_summarize_single) {
    const auto v = eval("Math.summarize([42.0])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->find_field("count")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_NEAR(rec->find_field("minimum")->as_number(), 42.0, 1e-9);
    ASSERT_NEAR(rec->find_field("maximum")->as_number(), 42.0, 1e-9);
    ASSERT_NEAR(rec->find_field("mean")->as_number(), 42.0, 1e-9);
    ASSERT_NEAR(rec->find_field("median")->as_number(), 42.0, 1e-9);
    ASSERT_NEAR(rec->find_field("standard_deviation")->as_number(), 0.0, 1e-9);
}

LUMA_TEST(math_summarize_empty) {
    ASSERT_EVAL_FAILURE("Math.summarize([])");
}

LUMA_TEST(math_fraction_construct_reduces) {
    // 2/4 reduces to 1/2.
    const auto v = eval("Math.fraction(2, 4)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, std::string{"Fraction"});
    ASSERT_EQ(rec->find_field("numerator")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(rec->find_field("denominator")->as_integer(), static_cast<std::int64_t>(2));
}

LUMA_TEST(math_fraction_negative_denominator_normalizes) {
    // The sign always lives in the numerator; the denominator stays positive.
    const auto v = eval("Math.fraction(1, -3)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->find_field("numerator")->as_integer(), static_cast<std::int64_t>(-1));
    ASSERT_EQ(rec->find_field("denominator")->as_integer(), static_cast<std::int64_t>(3));
}

LUMA_TEST(math_fraction_zero_is_canonical) {
    const auto v = eval("Math.fraction(0, 5)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->find_field("numerator")->as_integer(), static_cast<std::int64_t>(0));
    ASSERT_EQ(rec->find_field("denominator")->as_integer(), static_cast<std::int64_t>(1));
}

LUMA_TEST(math_fraction_zero_denominator_fails) {
    ASSERT_EVAL_FAILURE("Math.fraction(5, 0)");
}

LUMA_TEST(math_fraction_add_exact) {
    // The headline example: 1/3 + 1/6 = 1/2 exactly.
    const auto v = eval("Math.fraction_add(Result.unwrap(Math.fraction(1, 3)), "
                        "Result.unwrap(Math.fraction(1, 6)))");
    const auto& rec = v.as_record();
    ASSERT_EQ(rec->type_name, std::string{"Fraction"});
    ASSERT_EQ(rec->find_field("numerator")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(rec->find_field("denominator")->as_integer(), static_cast<std::int64_t>(2));
}

LUMA_TEST(math_fraction_subtract_exact) {
    // 1/2 - 1/3 = 1/6.
    const auto v = eval("Math.fraction_subtract(Result.unwrap(Math.fraction(1, 2)), "
                        "Result.unwrap(Math.fraction(1, 3)))");
    const auto& rec = v.as_record();
    ASSERT_EQ(rec->find_field("numerator")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(rec->find_field("denominator")->as_integer(), static_cast<std::int64_t>(6));
}

LUMA_TEST(math_fraction_multiply_reduces) {
    // 2/3 * 3/4 = 1/2.
    const auto v = eval("Math.fraction_multiply(Result.unwrap(Math.fraction(2, 3)), "
                        "Result.unwrap(Math.fraction(3, 4)))");
    const auto& rec = v.as_record();
    ASSERT_EQ(rec->find_field("numerator")->as_integer(), static_cast<std::int64_t>(1));
    ASSERT_EQ(rec->find_field("denominator")->as_integer(), static_cast<std::int64_t>(2));
}

LUMA_TEST(math_fraction_divide_exact) {
    // (1/2) / (3/4) = 2/3.
    const auto v = eval("Math.fraction_divide(Result.unwrap(Math.fraction(1, 2)), "
                        "Result.unwrap(Math.fraction(3, 4)))");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->find_field("numerator")->as_integer(), static_cast<std::int64_t>(2));
    ASSERT_EQ(rec->find_field("denominator")->as_integer(), static_cast<std::int64_t>(3));
}

LUMA_TEST(math_fraction_divide_by_zero_fails) {
    ASSERT_EVAL_FAILURE("Math.fraction_divide(Result.unwrap(Math.fraction(1, 2)), "
                        "Result.unwrap(Math.fraction(0, 5)))");
}

LUMA_TEST(math_fraction_to_number) {
    ASSERT_NEAR(eval("Math.fraction_to_number(Result.unwrap(Math.fraction(1, 4)))").as_number(),
                0.25, 1e-9);
    ASSERT_NEAR(eval("Math.fraction_to_number(Result.unwrap(Math.fraction(-2, 3)))").as_number(),
                -2.0 / 3.0, 1e-9);
}

LUMA_TEST(math_fraction_compare) {
    // 1/3 < 1/2 → Less.
    const auto less = eval("Math.fraction_compare(Result.unwrap(Math.fraction(1, 3)), "
                           "Result.unwrap(Math.fraction(1, 2)))");
    ASSERT_TRUE(less.is_choice());
    ASSERT_EQ(less.as_choice()->type_name, "Ordering");
    ASSERT_EQ(less.as_choice()->variant, "Less");

    // 1/2 == 2/4 → Equal (reduced forms are identical).
    const auto eq = eval("Math.fraction_compare(Result.unwrap(Math.fraction(1, 2)), "
                         "Result.unwrap(Math.fraction(2, 4)))");
    ASSERT_EQ(eq.as_choice()->variant, "Equal");

    // -1/2 < -1/3 → Less (more negative is smaller).
    const auto neg = eval("Math.fraction_compare(Result.unwrap(Math.fraction(-1, 2)), "
                          "Result.unwrap(Math.fraction(-1, 3)))");
    ASSERT_EQ(neg.as_choice()->variant, "Less");

    // 3/4 > 2/3 → Greater.
    const auto greater = eval("Math.fraction_compare(Result.unwrap(Math.fraction(3, 4)), "
                              "Result.unwrap(Math.fraction(2, 3)))");
    ASSERT_EQ(greater.as_choice()->variant, "Greater");
}

LUMA_TEST(math_fraction_arithmetic_int64_min_overflow) {
    // A computed numerator or denominator of exactly INT64_MIN cannot be reduced
    // (normalize's abs would be signed-overflow UB), so it must be rejected as
    // overflow rather than silently invoking UB.  INT64_MIN/2 is -4611686018427387904.
    ASSERT_TRUE(
        throws_runtime("Math.fraction_add(Result.unwrap(Math.fraction(-4611686018427387904, 1)), "
                       "Result.unwrap(Math.fraction(-4611686018427387904, 1)))"));

    ASSERT_TRUE(throws_runtime("Math.fraction_multiply(Result.unwrap(Math.fraction(2, 1)), "
                               "Result.unwrap(Math.fraction(-4611686018427387904, 1)))"));
}

LUMA_TEST(math_complex_construct) {
    const auto v = eval("Math.complex(3.0, -4.0)");
    ASSERT_TRUE(v.is_record());

    const auto& rec = v.as_record();
    ASSERT_EQ(rec->type_name, std::string{"Complex"});
    ASSERT_NEAR(rec->find_field("real")->as_number(), 3.0, 1e-9);
    ASSERT_NEAR(rec->find_field("imaginary")->as_number(), -4.0, 1e-9);
}

LUMA_TEST(math_complex_add_subtract) {
    const auto sum = eval("Math.complex_add(Math.complex(1.0, 2.0), Math.complex(3.0, -1.0))");
    ASSERT_NEAR(sum.as_record()->find_field("real")->as_number(), 4.0, 1e-9);
    ASSERT_NEAR(sum.as_record()->find_field("imaginary")->as_number(), 1.0, 1e-9);

    const auto diff =
        eval("Math.complex_subtract(Math.complex(1.0, 2.0), Math.complex(3.0, -1.0))");
    ASSERT_NEAR(diff.as_record()->find_field("real")->as_number(), -2.0, 1e-9);
    ASSERT_NEAR(diff.as_record()->find_field("imaginary")->as_number(), 3.0, 1e-9);
}

LUMA_TEST(math_complex_multiply) {
    // (1 + 2i)(3 + 4i) = -5 + 10i
    const auto v = eval("Math.complex_multiply(Math.complex(1.0, 2.0), Math.complex(3.0, 4.0))");
    ASSERT_NEAR(v.as_record()->find_field("real")->as_number(), -5.0, 1e-9);
    ASSERT_NEAR(v.as_record()->find_field("imaginary")->as_number(), 10.0, 1e-9);
}

LUMA_TEST(math_complex_divide) {
    // (1 + 2i) / (3 + 4i) = 0.44 + 0.08i
    const auto v = eval("Math.complex_divide(Math.complex(1.0, 2.0), Math.complex(3.0, 4.0))");
    ASSERT_RESULT_SUCCESS(v);
    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_NEAR(rec->find_field("real")->as_number(), 0.44, 1e-9);
    ASSERT_NEAR(rec->find_field("imaginary")->as_number(), 0.08, 1e-9);

    // Division by 0 + 0i fails.
    ASSERT_EVAL_FAILURE("Math.complex_divide(Math.complex(1.0, 1.0), Math.complex(0.0, 0.0))");
}

LUMA_TEST(math_complex_magnitude_conjugate_argument) {
    ASSERT_NEAR(eval("Math.complex_magnitude(Math.complex(3.0, 4.0))").as_number(), 5.0, 1e-9);

    const auto conj = eval("Math.complex_conjugate(Math.complex(3.0, 4.0))");
    ASSERT_NEAR(conj.as_record()->find_field("real")->as_number(), 3.0, 1e-9);
    ASSERT_NEAR(conj.as_record()->find_field("imaginary")->as_number(), -4.0, 1e-9);

    // arg(0 + 1i) = π/2
    ASSERT_NEAR(eval("Math.complex_argument(Math.complex(0.0, 1.0))").as_number(),
                3.14159265358979 / 2.0, 1e-9);
}

LUMA_TEST(math_linear_fit) {
    // Perfect line y = 2x.
    const auto v = eval("Math.linear_fit([1.0, 2.0, 3.0, 4.0], [2.0, 4.0, 6.0, 8.0])");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, std::string{"LineFit"});
    ASSERT_NEAR(rec->find_field("slope")->as_number(), 2.0, 1e-9);
    ASSERT_NEAR(rec->find_field("intercept")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(rec->find_field("r_squared")->as_number(), 1.0, 1e-9);
}

LUMA_TEST(math_linear_fit_failures) {
    // Length mismatch, too few points, and zero x-variance all fail.
    ASSERT_EVAL_FAILURE("Math.linear_fit([1.0, 2.0], [1.0])");
    ASSERT_EVAL_FAILURE("Math.linear_fit([1.0], [1.0])");
    ASSERT_EVAL_FAILURE("Math.linear_fit([5.0, 5.0, 5.0], [1.0, 2.0, 3.0])");
}

LUMA_TEST(math_sum) {
    ASSERT_EVAL_NUM("Math.sum([1.0, 2.0, 3.0])", 6.0);

    ASSERT_EVAL_INT("Math.sum([1, 2, 3])", 6);

    ASSERT_EVAL_INT("Math.sum([])", 0);

    ASSERT_EVAL_FAILURE("Math.sum([\"a\", \"b\"])");
}

LUMA_TEST(math_variance) {
    const auto v = eval("Math.variance([2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() > 3.9);
    ASSERT_TRUE(v.as_result()->owned_inner->as_number() < 4.1);
}

LUMA_TEST(math_variance_empty) {
    ASSERT_EVAL_FAILURE("Math.variance([])");
}

LUMA_TEST(math_clamp_inverted_range) {
    ASSERT_EVAL_FAILURE("Math.clamp(5.0, 10.0, 1.0)");
}

LUMA_TEST(math_lerp_out_of_range) {
    ASSERT_EVAL_FAILURE("Math.lerp(0.0, 1.0, 2.0)");
}

LUMA_TEST(math_factorial_negative) {
    ASSERT_EVAL_FAILURE("Math.factorial(-1)");
}

LUMA_TEST(math_factorial_overflow) {
    ASSERT_EVAL_FAILURE("Math.factorial(21)");
}

LUMA_TEST(math_square_root_negative) {
    ASSERT_EVAL_FAILURE("Math.square_root(-1.0)");
}

LUMA_TEST(math_log_e_non_positive) {
    ASSERT_EVAL_FAILURE("Math.log_e(0.0)");
    ASSERT_EVAL_FAILURE("Math.log_e(-1.0)");
}

LUMA_TEST(math_arc_sine_out_of_domain) {
    ASSERT_EVAL_FAILURE("Math.arc_sine(2.0)");
}

LUMA_TEST(math_remainder_division_by_zero) {
    ASSERT_EVAL_FAILURE("Math.remainder(10, 0)");
}

LUMA_TEST(math_trig) {
    const auto s = eval("Math.sine(0.0)");

    ASSERT_RESULT_SUCCESS(s);
    ASSERT_NEAR(s.as_result()->owned_inner->as_number(), 0.0, 1e-9);

    const auto c = eval("Math.cosine(0.0)");

    ASSERT_RESULT_SUCCESS(c);
    ASSERT_NEAR(c.as_result()->owned_inner->as_number(), 1.0, 1e-9);

    const auto t = eval("Math.tangent(0.0)");

    ASSERT_RESULT_SUCCESS(t);
    ASSERT_NEAR(t.as_result()->owned_inner->as_number(), 0.0, 1e-9);
}

LUMA_TEST(math_arc_tangent) {
    const auto v = eval("Math.arc_tangent(1.0)");

    ASSERT_RESULT_SUCCESS(v);

    // atan(1) = π/4 ≈ 0.785398.
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 0.785398163, 1e-6);
}

LUMA_TEST(math_degrees_radians) {
    const auto deg = eval("Math.degrees(Math.pi)");

    ASSERT_TRUE(deg.is_number());
    ASSERT_NEAR(deg.as_number(), 180.0, 1e-9);

    const auto rad = eval("Math.radians(180.0)");

    ASSERT_TRUE(rad.is_number());
    ASSERT_NEAR(rad.as_number(), 3.14159265, 1e-6);
}

LUMA_TEST(math_exponential) {
    const auto one = eval("Math.exponential(0.0)");

    ASSERT_RESULT_SUCCESS(one);
    ASSERT_NEAR(one.as_result()->owned_inner->as_number(), 1.0, 1e-9);

    const auto e = eval("Math.exponential(1.0)");

    ASSERT_RESULT_SUCCESS(e);
    ASSERT_NEAR(e.as_result()->owned_inner->as_number(), 2.718281828, 1e-6);
}

LUMA_TEST(math_least_common_multiple) {
    ASSERT_EVAL_INT("Math.least_common_multiple(4, 6)", 12);

    ASSERT_EVAL_INT("Math.least_common_multiple(0, 5)", 0);
}

LUMA_TEST(math_log_e) {
    const auto v = eval("Math.log_e(Math.e)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 1.0, 1e-9);
}

LUMA_TEST(math_log_2) {
    const auto v = eval("Math.log_2(8.0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 3.0, 1e-9);
}

LUMA_TEST(math_log_10) {
    const auto v = eval("Math.log_10(100.0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 2.0, 1e-9);
}

LUMA_TEST(math_log) {
    const auto v = eval("Math.log(2.0, 8.0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 3.0, 1e-9);
}

LUMA_TEST(math_max_min) {
    ASSERT_NEAR(eval("Math.max(3.0, 7.0)").as_number(), 7.0, 1e-9);
    ASSERT_NEAR(eval("Math.max(-1.0, -5.0)").as_number(), -1.0, 1e-9);
    ASSERT_NEAR(eval("Math.min(3.0, 7.0)").as_number(), 3.0, 1e-9);
    ASSERT_NEAR(eval("Math.min(-1.0, -5.0)").as_number(), -5.0, 1e-9);
}

LUMA_TEST(math_truncate) {
    ASSERT_EVAL_INT("Math.truncate(3.7)", 3);

    ASSERT_EVAL_INT("Math.truncate(-3.7)", -3);
}

LUMA_TEST(math_atan2) {
    const auto v = eval("Math.atan2(1.0, 1.0)");

    ASSERT_RESULT_SUCCESS(v);

    // atan2(1, 1) = π/4.
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 0.785398163, 1e-6);
}

LUMA_TEST(math_hypot) {
    ASSERT_NEAR(eval("Math.hypot(3.0, 4.0)").as_number(), 5.0, 1e-9);
    ASSERT_NEAR(eval("Math.hypot(0.0, 0.0)").as_number(), 0.0, 1e-9);
    ASSERT_NEAR(eval("Math.hypot(5.0, 12.0)").as_number(), 13.0, 1e-9);
}

LUMA_TEST(math_cube_root) {
    ASSERT_NEAR(eval("Math.cube_root(27.0)").as_number(), 3.0, 1e-9);
    ASSERT_NEAR(eval("Math.cube_root(-8.0)").as_number(), -2.0, 1e-9);
    ASSERT_NEAR(eval("Math.cube_root(0.0)").as_number(), 0.0, 1e-9);
}

LUMA_TEST(math_hyperbolic_sine) {
    const auto v = eval("Math.hyperbolic_sine(0.0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 0.0, 1e-9);
}

LUMA_TEST(math_hyperbolic_cosine) {
    const auto v = eval("Math.hyperbolic_cosine(0.0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 1.0, 1e-9);
}

LUMA_TEST(math_hyperbolic_tangent) {
    ASSERT_NEAR(eval("Math.hyperbolic_tangent(0.0)").as_number(), 0.0, 1e-9);
}

LUMA_TEST(math_sum_integers) {
    ASSERT_EVAL_INT("Math.sum([1, 2, 3, 4])", 10);
}

LUMA_TEST(math_remainder_floating_point) {
    const auto v = eval("Math.remainder(5.5, 2.0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 1.5, 1e-9);
}

LUMA_TEST(math_absolute_overflow) {
    // |INT64_MIN| is not representable as a signed 64-bit integer.
    ASSERT_EVAL_FAILURE("Math.absolute(-9223372036854775807 - 1)");
}

LUMA_TEST(math_gcd_overflow) {
    ASSERT_EVAL_FAILURE("Math.greatest_common_divisor(-9223372036854775807 - 1, 4)");
}

LUMA_TEST(math_lcm_overflow) {
    ASSERT_EVAL_FAILURE("Math.least_common_multiple(9223372036854775807, 2)");
}

LUMA_TEST(math_log_2_non_positive) {
    ASSERT_EVAL_FAILURE("Math.log_2(0.0)");
    ASSERT_EVAL_FAILURE("Math.log_2(-1.0)");
}

LUMA_TEST(math_log_10_non_positive) {
    ASSERT_EVAL_FAILURE("Math.log_10(0.0)");
    ASSERT_EVAL_FAILURE("Math.log_10(-1.0)");
}

LUMA_TEST(math_log_invalid) {
    // Base must be positive and not 1; value must be positive.
    ASSERT_EVAL_FAILURE("Math.log(1.0, 5.0)");
    ASSERT_EVAL_FAILURE("Math.log(-2.0, 8.0)");
    ASSERT_EVAL_FAILURE("Math.log(2.0, -1.0)");
}

LUMA_TEST(math_exponential_overflow) {
    ASSERT_EVAL_FAILURE("Math.exponential(1000.0)");
}

LUMA_TEST(math_hyperbolic_overflow) {
    ASSERT_EVAL_FAILURE("Math.hyperbolic_sine(1000.0)");
    ASSERT_EVAL_FAILURE("Math.hyperbolic_cosine(1000.0)");
}

LUMA_TEST(math_arc_tangent_not_a_number) {
    // atan(NaN) is NaN, which is not a valid real result.
    ASSERT_EVAL_FAILURE("Math.arc_tangent(Math.infinity - Math.infinity)");
}

LUMA_TEST(math_trig_infinite_argument) {
    // sin/cos/tan of infinity are NaN and must surface as failures.
    ASSERT_EVAL_FAILURE("Math.sine(Math.infinity)");
    ASSERT_EVAL_FAILURE("Math.cosine(Math.infinity)");
    ASSERT_EVAL_FAILURE("Math.tangent(Math.infinity)");
}

LUMA_TEST(math_rounding_out_of_integer_range) {
    // 1e30 exceeds the int64 range, so the rounding family must fail.
    ASSERT_EVAL_FAILURE("Math.floor(1e30)");
    ASSERT_EVAL_FAILURE("Math.ceil(1e30)");
    ASSERT_EVAL_FAILURE("Math.round(1e30)");
    ASSERT_EVAL_FAILURE("Math.truncate(1e30)");
}

LUMA_TEST(math_approximately_equal_negative_epsilon) {
    // A negative epsilon is a programming error and raises a runtime error.
    ASSERT_THROWS(eval("Math.approximately_equal(1.0, 1.0, -0.1)"));
}

// --- Math.Vector2 / Math.Vector3 (N05) ---

LUMA_TEST(math_vector2_and_length) {
    const auto v = eval("Math.vector2(3.0, 4.0)");
    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Vector2"});
    ASSERT_NEAR(v.as_record()->find_field("x")->as_number(), 3.0, 1e-9);
    ASSERT_NEAR(v.as_record()->find_field("y")->as_number(), 4.0, 1e-9);

    ASSERT_NEAR(eval("Math.vec2_length(Math.vector2(3.0, 4.0))").as_number(), 5.0, 1e-9);
}

LUMA_TEST(math_vec2_arithmetic) {
    ASSERT_NEAR(eval("Math.vec2_dot(Math.vector2(1.0, 2.0), Math.vector2(3.0, 4.0))").as_number(),
                11.0, 1e-9);

    const auto sum = eval("Math.vec2_add(Math.vector2(1.0, 2.0), Math.vector2(3.0, 4.0))");
    ASSERT_NEAR(sum.as_record()->find_field("x")->as_number(), 4.0, 1e-9);
    ASSERT_NEAR(sum.as_record()->find_field("y")->as_number(), 6.0, 1e-9);

    const auto scaled = eval("Math.vec2_scale(Math.vector2(1.5, 2.0), 2.0)");
    ASSERT_NEAR(scaled.as_record()->find_field("x")->as_number(), 3.0, 1e-9);

    const auto norm = eval("Math.vec2_normalize(Math.vector2(3.0, 4.0))");
    ASSERT_NEAR(norm.as_record()->find_field("x")->as_number(), 0.6, 1e-9);
    ASSERT_NEAR(norm.as_record()->find_field("y")->as_number(), 0.8, 1e-9);
}

LUMA_TEST(math_vec3_cross_and_dot) {
    const auto cross =
        eval("Math.vec3_cross(Math.vector3(1.0, 0.0, 0.0), Math.vector3(0.0, 1.0, 0.0))");
    ASSERT_EQ(cross.as_record()->type_name, std::string{"Vector3"});
    ASSERT_NEAR(cross.as_record()->find_field("z")->as_number(), 1.0, 1e-9);

    ASSERT_NEAR(
        eval("Math.vec3_dot(Math.vector3(1.0, 0.0, 0.0), Math.vector3(0.0, 1.0, 0.0))").as_number(),
        0.0, 1e-9);
    ASSERT_NEAR(eval("Math.vec3_length(Math.vector3(2.0, 3.0, 6.0))").as_number(), 7.0, 1e-9);
}

// --- Math.FiveNumberSummary (N06) ---

LUMA_TEST(math_five_number_summary) {
    const auto v = eval("Math.five_number_summary([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0])");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, std::string{"FiveNumberSummary"});
    ASSERT_NEAR(rec->find_field("minimum")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(rec->find_field("median")->as_number(), 5.0, 1e-9);
    ASSERT_NEAR(rec->find_field("maximum")->as_number(), 9.0, 1e-9);
    ASSERT_NEAR(rec->find_field("q1")->as_number(), 3.0, 1e-9);
    ASSERT_NEAR(rec->find_field("q3")->as_number(), 7.0, 1e-9);
}

LUMA_TEST(math_five_number_summary_empty_fails) {
    ASSERT_EVAL_FAILURE("Math.five_number_summary([])");
}

int main() {
    LUMA_RUN_ALL();
}
