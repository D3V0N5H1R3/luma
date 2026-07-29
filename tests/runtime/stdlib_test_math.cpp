// Standard library tests: Math module.

#include <limits>

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

LUMA_TEST(math_combinations) {
    ASSERT_EVAL_INT("Math.combinations(5, 2)", 10);

    ASSERT_EVAL_INT("Math.combinations(5, 0)", 1);

    ASSERT_EVAL_INT("Math.combinations(5, 5)", 1);

    // Symmetry: C(n, k) == C(n, n - k).
    ASSERT_EVAL_INT("Math.combinations(10, 3)", 120);

    ASSERT_EVAL_INT("Math.combinations(10, 7)", 120);

    // A large but representable central binomial coefficient (does not overflow
    // even though the equivalent factorials would).
    ASSERT_EVAL_INT("Math.combinations(62, 31)", 465428353255261088);
}

LUMA_TEST(math_permutations) {
    ASSERT_EVAL_INT("Math.permutations(5, 2)", 20);

    ASSERT_EVAL_INT("Math.permutations(5, 0)", 1);

    ASSERT_EVAL_INT("Math.permutations(5, 5)", 120);

    ASSERT_EVAL_INT("Math.permutations(10, 3)", 720);
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

LUMA_TEST(math_is_finite) {
    ASSERT_EQ(eval("Math.is_finite(3.14)").as_bool(), true);
    ASSERT_EQ(eval("Math.is_finite(0.0)").as_bool(), true);
    ASSERT_EQ(eval("Math.is_finite(Math.infinity)").as_bool(), false);
    ASSERT_EQ(eval("Math.is_finite(-Math.infinity)").as_bool(), false);
    ASSERT_EQ(eval("Math.is_finite(Math.infinity - Math.infinity)").as_bool(), false);
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

LUMA_TEST(math_is_even_odd) {
    ASSERT_EQ(eval("Math.is_even(4)").as_bool(), true);
    ASSERT_EQ(eval("Math.is_even(3)").as_bool(), false);
    ASSERT_EQ(eval("Math.is_even(0)").as_bool(), true);
    // Negative-safe: -4 is even, -3 is odd.
    ASSERT_EQ(eval("Math.is_even(-4)").as_bool(), true);
    ASSERT_EQ(eval("Math.is_odd(-3)").as_bool(), true);
    ASSERT_EQ(eval("Math.is_odd(4)").as_bool(), false);
    ASSERT_EQ(eval("Math.is_odd(3)").as_bool(), true);
}

LUMA_TEST(math_integer_bounds) {
    ASSERT_EQ(eval("Math.max_integer").as_integer(), 9223372036854775807LL);
    ASSERT_EQ(eval("Math.min_integer").as_integer(), -9223372036854775807LL - 1);
}

LUMA_TEST(math_number_bounds) {
    // Largest finite double and smallest positive normal double.
    ASSERT_EQ(eval("Math.max_number").as_number(), std::numeric_limits<double>::max());
    ASSERT_EQ(eval("Math.min_number").as_number(), std::numeric_limits<double>::min());
    ASSERT_TRUE(eval("Math.min_number").as_number() > 0.0);
    ASSERT_EQ(eval("Math.is_finite(Math.max_number)").as_bool(), true);
}

LUMA_TEST(math_named_constants) {
    ASSERT_NEAR(eval("Math.sqrt2").as_number(), 1.4142135623730951, 1e-12);
    ASSERT_NEAR(eval("Math.golden_ratio").as_number(), 1.618033988749895, 1e-12);
    ASSERT_NEAR(eval("Math.ln2").as_number(), 0.6931471805599453, 1e-12);
    ASSERT_NEAR(eval("Math.ln10").as_number(), 2.302585092994046, 1e-12);
}

LUMA_TEST(math_epsilon) {
    // Machine epsilon is positive and smaller than any everyday tolerance.
    ASSERT_TRUE(eval("Math.epsilon").as_number() > 0.0);
    ASSERT_TRUE(eval("Math.epsilon").as_number() < 1e-15);
    // 1.0 + epsilon is distinguishable from 1.0.
    ASSERT_EQ(eval("1.0 + Math.epsilon > 1.0").as_bool(), true);
}

LUMA_TEST(math_nan) {
    ASSERT_EQ(eval("Math.is_not_a_number(Math.nan)").as_bool(), true);
    ASSERT_EQ(eval("Math.is_infinite(Math.nan)").as_bool(), false);
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

LUMA_TEST(math_complex_polar_bridges) {
    const auto p = eval("Math.complex_to_polar(Math.complex(0.0, 5.0))");
    ASSERT_EQ(p.as_record()->type_name, std::string{"Polar"});
    ASSERT_NEAR(p.as_record()->find_field("radius")->as_number(), 5.0, 1e-9);
    ASSERT_NEAR(p.as_record()->find_field("angle")->as_number(), 3.14159265358979 / 2.0, 1e-9);

    const auto c = eval("Math.complex_from_polar(Math.complex_to_polar(Math.complex(3.0, 4.0)))");
    ASSERT_EQ(c.as_record()->type_name, std::string{"Complex"});
    ASSERT_NEAR(c.as_record()->find_field("real")->as_number(), 3.0, 1e-9);
    ASSERT_NEAR(c.as_record()->find_field("imaginary")->as_number(), 4.0, 1e-9);
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

LUMA_TEST(math_combinations_invalid) {
    ASSERT_EVAL_FAILURE("Math.combinations(-1, 2)");

    ASSERT_EVAL_FAILURE("Math.combinations(5, -1)");

    ASSERT_EVAL_FAILURE("Math.combinations(3, 5)");
}

LUMA_TEST(math_combinations_overflow) {
    ASSERT_EVAL_FAILURE("Math.combinations(100, 50)");
}

LUMA_TEST(math_permutations_invalid) {
    ASSERT_EVAL_FAILURE("Math.permutations(-1, 2)");

    ASSERT_EVAL_FAILURE("Math.permutations(5, -1)");

    ASSERT_EVAL_FAILURE("Math.permutations(3, 5)");
}

LUMA_TEST(math_permutations_overflow) {
    ASSERT_EVAL_FAILURE("Math.permutations(30, 20)");
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

LUMA_TEST(math_round_to) {
    const auto a = eval("Math.round_to(3.14159, 2)");
    ASSERT_RESULT_SUCCESS(a);
    ASSERT_NEAR(a.as_result()->owned_inner->as_number(), 3.14, 1e-9);

    const auto b = eval("Math.round_to(2.5, 0)");
    ASSERT_RESULT_SUCCESS(b);
    ASSERT_NEAR(b.as_result()->owned_inner->as_number(), 3.0, 1e-9);

    const auto c = eval("Math.round_to(-1.2345, 2)");
    ASSERT_RESULT_SUCCESS(c);
    ASSERT_NEAR(c.as_result()->owned_inner->as_number(), -1.23, 1e-9);

    // Negative places and places above the cap fail.
    ASSERT_EVAL_FAILURE("Math.round_to(1.5, -1)");
    ASSERT_EVAL_FAILURE("Math.round_to(1.5, 16)");

    // Non-finite input fails.
    ASSERT_EVAL_FAILURE("Math.round_to(Math.infinity, 2)");

    // A finite input whose scaled value overflows to ±inf must fail rather
    // than wrap a non-finite value in a success result.
    ASSERT_EVAL_FAILURE("Math.round_to(1.0e300, 15)");
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

LUMA_TEST(math_to_polar_and_from_polar) {
    // (3, 4) -> radius 5, angle atan2(4, 3).
    const auto p = eval("Math.to_polar(Math.vector2(3.0, 4.0))");
    ASSERT_EQ(p.as_record()->type_name, std::string{"Polar"});
    ASSERT_NEAR(p.as_record()->find_field("radius")->as_number(), 5.0, 1e-9);
    ASSERT_NEAR(p.as_record()->find_field("angle")->as_number(), 0.9272952180016122, 1e-9);

    // Round-trip: from_polar(to_polar(v)) == v.
    const auto v = eval("Math.from_polar(Math.to_polar(Math.vector2(-1.0, 2.5)))");
    ASSERT_EQ(v.as_record()->type_name, std::string{"Vector2"});
    ASSERT_NEAR(v.as_record()->find_field("x")->as_number(), -1.0, 1e-9);
    ASSERT_NEAR(v.as_record()->find_field("y")->as_number(), 2.5, 1e-9);

    // The origin is a total conversion too: radius 0, angle 0.
    const auto origin = eval("Math.to_polar(Math.vector2(0.0, 0.0))");
    ASSERT_NEAR(origin.as_record()->find_field("radius")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(origin.as_record()->find_field("angle")->as_number(), 0.0, 1e-9);
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

// --- Math.Histogram (N01) ---

LUMA_TEST(math_histogram_basic) {
    // Ten values in [0, 10) across five equal-width bins of width 2.
    const auto v = eval("Math.histogram([0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0], 5)");
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

LUMA_TEST(math_histogram_equal_values) {
    // A zero-width range widens so every equal value falls in one bin.
    const auto v = eval("Math.histogram([5.0, 5.0, 5.0], 3)");
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

LUMA_TEST(math_histogram_single_bin) {
    const auto v = eval("Math.histogram([1.0, 2.0, 3.0, 4.0], 1)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    const auto& edges = *rec->find_field("bin_edges")->as_array()->elements;
    const auto& counts = *rec->find_field("counts")->as_array()->elements;
    ASSERT_EQ(edges.size(), 2U);
    ASSERT_EQ(counts.size(), 1U);
    ASSERT_EQ(counts.front().as_integer(), static_cast<std::int64_t>(4));
}

LUMA_TEST(math_histogram_empty_fails) {
    ASSERT_EVAL_FAILURE("Math.histogram([], 5)");
}

LUMA_TEST(math_histogram_zero_bins_fails) {
    ASSERT_EVAL_FAILURE("Math.histogram([1.0, 2.0, 3.0], 0)");
}

LUMA_TEST(math_histogram_skips_non_finite) {
    // Non-finite samples are excluded from the range and the tally rather than
    // poisoning bin_width / bin positions.
    const auto v = eval("Math.histogram([1.0, 2.0, 3.0, Math.infinity, Math.infinity * -1.0], 2)");
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

LUMA_TEST(math_histogram_all_non_finite_fails) {
    ASSERT_EVAL_FAILURE("Math.histogram([Math.infinity, Math.infinity * -1.0], 3)");
}

LUMA_TEST(math_histogram_excessive_bins_fails) {
    // A bin count past the array-size contract fails instead of allocating.
    ASSERT_EVAL_FAILURE("Math.histogram([1.0, 2.0, 3.0], 2000000000)");
}

// --- Math.Interval (T02) ---

LUMA_TEST(math_interval_construct_and_contains) {
    const auto v = eval("Math.interval(1.0, 5.0)");
    ASSERT_RESULT_SUCCESS(v);

    const auto& rec = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(rec->type_name, std::string{"Interval"});
    ASSERT_NEAR(rec->find_field("min")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(rec->find_field("max")->as_number(), 5.0, 1e-9);

    ASSERT_EQ(eval("Math.interval_contains(Result.unwrap(Math.interval(1.0, 5.0)), 3.0)").as_bool(),
              true);
    // Closed interval: endpoints are inside.
    ASSERT_EQ(eval("Math.interval_contains(Result.unwrap(Math.interval(1.0, 5.0)), 1.0)").as_bool(),
              true);
    ASSERT_EQ(eval("Math.interval_contains(Result.unwrap(Math.interval(1.0, 5.0)), 6.0)").as_bool(),
              false);
}

LUMA_TEST(math_interval_invalid_fails) {
    ASSERT_EVAL_FAILURE("Math.interval(5.0, 1.0)");
}

LUMA_TEST(math_interval_clamp_length_overlap) {
    ASSERT_NEAR(
        eval("Math.interval_clamp(Result.unwrap(Math.interval(0.0, 10.0)), 15.0)").as_number(),
        10.0, 1e-9);
    ASSERT_NEAR(
        eval("Math.interval_clamp(Result.unwrap(Math.interval(0.0, 10.0)), -3.0)").as_number(), 0.0,
        1e-9);
    ASSERT_NEAR(eval("Math.interval_length(Result.unwrap(Math.interval(2.0, 7.5)))").as_number(),
                5.5, 1e-9);

    ASSERT_EQ(eval("Math.intervals_overlap(Result.unwrap(Math.interval(0.0, 5.0)), "
                   "Result.unwrap(Math.interval(4.0, 9.0)))")
                  .as_bool(),
              true);
    ASSERT_EQ(eval("Math.intervals_overlap(Result.unwrap(Math.interval(0.0, 5.0)), "
                   "Result.unwrap(Math.interval(6.0, 9.0)))")
                  .as_bool(),
              false);
}

// --- Math.Rect (N03) ---

LUMA_TEST(math_rect_construct_and_fields) {
    const auto v = eval("Math.rect(1.0, 2.0, 30.0, 40.0)");
    ASSERT_TRUE(v.is_record());

    const auto& rec = v.as_record();
    ASSERT_EQ(rec->type_name, std::string{"Rect"});
    ASSERT_NEAR(rec->find_field("x")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(rec->find_field("y")->as_number(), 2.0, 1e-9);
    ASSERT_NEAR(rec->find_field("width")->as_number(), 30.0, 1e-9);
    ASSERT_NEAR(rec->find_field("height")->as_number(), 40.0, 1e-9);
}

LUMA_TEST(math_rect_negative_dimensions_clamped) {
    // A degenerate (inside-out) rectangle clamps its extent to zero.
    const auto v = eval("Math.rect(5.0, 5.0, -3.0, -4.0)");
    const auto& rec = v.as_record();
    ASSERT_NEAR(rec->find_field("width")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(rec->find_field("height")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(eval("Math.rect_area(Math.rect(5.0, 5.0, -3.0, -4.0))").as_number(), 0.0, 1e-9);
}

LUMA_TEST(math_rect_contains_half_open) {
    // Half-open: the min edges are inside, the max edges are outside.
    ASSERT_EQ(eval("Math.rect_contains(Math.rect(0.0, 0.0, 10.0, 10.0), 5.0, 5.0)").as_bool(),
              true);
    ASSERT_EQ(eval("Math.rect_contains(Math.rect(0.0, 0.0, 10.0, 10.0), 0.0, 0.0)").as_bool(),
              true);
    ASSERT_EQ(eval("Math.rect_contains(Math.rect(0.0, 0.0, 10.0, 10.0), 10.0, 5.0)").as_bool(),
              false);
    ASSERT_EQ(eval("Math.rect_contains(Math.rect(0.0, 0.0, 10.0, 10.0), 5.0, 10.0)").as_bool(),
              false);
}

LUMA_TEST(math_rect_intersects) {
    ASSERT_EQ(eval("Math.rect_intersects(Math.rect(0.0, 0.0, 10.0, 10.0), "
                   "Math.rect(5.0, 5.0, 10.0, 10.0))")
                  .as_bool(),
              true);
    // Touching edges do not count (half-open).
    ASSERT_EQ(eval("Math.rect_intersects(Math.rect(0.0, 0.0, 10.0, 10.0), "
                   "Math.rect(10.0, 0.0, 5.0, 5.0))")
                  .as_bool(),
              false);
}

LUMA_TEST(math_rect_intersection_some_and_none) {
    ASSERT_EQ(eval("Optional.is_some(Math.rect_intersection(Math.rect(0.0, 0.0, 10.0, 10.0), "
                   "Math.rect(5.0, 5.0, 10.0, 10.0)))")
                  .as_bool(),
              true);

    const auto inter = eval("Optional.unwrap(Math.rect_intersection("
                            "Math.rect(0.0, 0.0, 10.0, 10.0), Math.rect(5.0, 5.0, 10.0, 10.0)))");
    const auto& rec = inter.as_record();
    ASSERT_NEAR(rec->find_field("x")->as_number(), 5.0, 1e-9);
    ASSERT_NEAR(rec->find_field("y")->as_number(), 5.0, 1e-9);
    ASSERT_NEAR(rec->find_field("width")->as_number(), 5.0, 1e-9);
    ASSERT_NEAR(rec->find_field("height")->as_number(), 5.0, 1e-9);

    // Disjoint rectangles → none.
    ASSERT_EQ(eval("Optional.is_none(Math.rect_intersection(Math.rect(0.0, 0.0, 5.0, 5.0), "
                   "Math.rect(10.0, 10.0, 5.0, 5.0)))")
                  .as_bool(),
              true);
}

LUMA_TEST(math_rect_union) {
    const auto v = eval("Math.rect_union(Math.rect(0.0, 0.0, 5.0, 5.0), "
                        "Math.rect(10.0, 10.0, 5.0, 5.0))");
    const auto& rec = v.as_record();
    ASSERT_NEAR(rec->find_field("x")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(rec->find_field("y")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(rec->find_field("width")->as_number(), 15.0, 1e-9);
    ASSERT_NEAR(rec->find_field("height")->as_number(), 15.0, 1e-9);
}

LUMA_TEST(math_rect_center_and_area) {
    const auto c = eval("Math.rect_center(Math.rect(0.0, 0.0, 10.0, 20.0))");
    ASSERT_TRUE(c.is_record());
    ASSERT_EQ(c.as_record()->type_name, std::string{"Vector2"});
    ASSERT_NEAR(c.as_record()->find_field("x")->as_number(), 5.0, 1e-9);
    ASSERT_NEAR(c.as_record()->find_field("y")->as_number(), 10.0, 1e-9);

    ASSERT_NEAR(eval("Math.rect_area(Math.rect(0.0, 0.0, 10.0, 20.0))").as_number(), 200.0, 1e-9);
}

// --- Math.Circle (N05) ---

LUMA_TEST(math_circle_construct_and_fields) {
    const auto v = eval("Math.circle(Math.vector2(3.0, 4.0), 5.0)");
    ASSERT_TRUE(v.is_record());

    const auto& rec = v.as_record();
    ASSERT_EQ(rec->type_name, std::string{"Circle"});
    ASSERT_NEAR(rec->find_field("radius")->as_number(), 5.0, 1e-9);

    const auto* center = rec->find_field("center");
    ASSERT_TRUE(center->is_record());
    ASSERT_EQ(center->as_record()->type_name, std::string{"Vector2"});
    ASSERT_NEAR(center->as_record()->find_field("x")->as_number(), 3.0, 1e-9);
    ASSERT_NEAR(center->as_record()->find_field("y")->as_number(), 4.0, 1e-9);
}

LUMA_TEST(math_circle_negative_radius_clamped) {
    // A negative radius clamps to zero (a degenerate circle is a point).
    const auto v = eval("Math.circle(Math.vector2(0.0, 0.0), -3.0)");
    ASSERT_NEAR(v.as_record()->find_field("radius")->as_number(), 0.0, 1e-9);
}

LUMA_TEST(math_circle_contains_closed_disk) {
    // Closed disk: a point exactly on the boundary (distance 5 == radius) counts.
    ASSERT_EQ(eval("Math.circle_contains(Math.circle(Math.vector2(0.0, 0.0), 5.0), "
                   "Math.vector2(3.0, 4.0))")
                  .as_bool(),
              true);
    ASSERT_EQ(eval("Math.circle_contains(Math.circle(Math.vector2(0.0, 0.0), 5.0), "
                   "Math.vector2(0.0, 0.0))")
                  .as_bool(),
              true);
    // Just outside the boundary → not contained.
    ASSERT_EQ(eval("Math.circle_contains(Math.circle(Math.vector2(0.0, 0.0), 5.0), "
                   "Math.vector2(3.0, 4.1))")
                  .as_bool(),
              false);
}

LUMA_TEST(math_circle_intersects) {
    // Overlapping: centres 6 apart, radii 5 + 5 = 10 → overlap.
    ASSERT_EQ(eval("Math.circle_intersects(Math.circle(Math.vector2(0.0, 0.0), 5.0), "
                   "Math.circle(Math.vector2(6.0, 0.0), 5.0))")
                  .as_bool(),
              true);
    // Exactly touching: centres 10 apart, radii 5 + 5 = 10 → inclusive touch.
    ASSERT_EQ(eval("Math.circle_intersects(Math.circle(Math.vector2(0.0, 0.0), 5.0), "
                   "Math.circle(Math.vector2(10.0, 0.0), 5.0))")
                  .as_bool(),
              true);
    // Disjoint: centres 12 apart, radii 5 + 5 = 10 → no overlap.
    ASSERT_EQ(eval("Math.circle_intersects(Math.circle(Math.vector2(0.0, 0.0), 5.0), "
                   "Math.circle(Math.vector2(12.0, 0.0), 5.0))")
                  .as_bool(),
              false);
}

LUMA_TEST(math_circle_rect_intersects) {
    // Circle centre inside the rect → overlap.
    ASSERT_EQ(eval("Math.circle_rect_intersects(Math.circle(Math.vector2(5.0, 5.0), 2.0), "
                   "Math.rect(0.0, 0.0, 10.0, 10.0))")
                  .as_bool(),
              true);
    // Circle near an edge, within radius of the closest point → overlap.
    ASSERT_EQ(eval("Math.circle_rect_intersects(Math.circle(Math.vector2(11.0, 5.0), 2.0), "
                   "Math.rect(0.0, 0.0, 10.0, 10.0))")
                  .as_bool(),
              true);
    // Far corner: closest point (10,10) is distance ~2.83 from centre (12,12) > radius 2.
    ASSERT_EQ(eval("Math.circle_rect_intersects(Math.circle(Math.vector2(12.0, 12.0), 2.0), "
                   "Math.rect(0.0, 0.0, 10.0, 10.0))")
                  .as_bool(),
              false);
}

LUMA_TEST(math_circle_rejects_non_record) {
    ASSERT_TRUE(luma::test::eval_throws("Math.circle_contains(42, Math.vector2(0.0, 0.0))"));
    ASSERT_TRUE(luma::test::eval_throws(
        "Math.circle_rect_intersects(Math.circle(Math.vector2(0.0, 0.0), 1.0), 42)"));
}

// --- Math.Matrix2 / Math.Matrix3 (T07) ---
LUMA_TEST(math_matrix2_identity_and_transform) {
    const auto id = eval("Math.mat2_identity()");
    ASSERT_TRUE(id.is_record());
    ASSERT_EQ(id.as_record()->type_name, std::string{"Matrix2"});
    ASSERT_NEAR(id.as_record()->find_field("m00")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(id.as_record()->find_field("m11")->as_number(), 1.0, 1e-9);

    // Identity leaves a vector unchanged.
    const auto v = eval("Math.mat2_transform(Math.mat2_identity(), Math.vector2(3.0, 4.0))");
    ASSERT_EQ(v.as_record()->type_name, std::string{"Vector2"});
    ASSERT_NEAR(v.as_record()->find_field("x")->as_number(), 3.0, 1e-9);
    ASSERT_NEAR(v.as_record()->find_field("y")->as_number(), 4.0, 1e-9);

    // 90° rotation matrix maps (1, 0) -> (0, 1).
    const auto rot =
        eval("Math.mat2_transform(Math.matrix2(0.0, -1.0, 1.0, 0.0), Math.vector2(1.0, 0.0))");
    ASSERT_NEAR(rot.as_record()->find_field("x")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(rot.as_record()->find_field("y")->as_number(), 1.0, 1e-9);
}

LUMA_TEST(math_matrix2_multiply_and_determinant) {
    // A * identity == A.
    const auto m =
        eval("Math.mat2_multiply(Math.matrix2(1.0, 2.0, 3.0, 4.0), Math.mat2_identity())");
    ASSERT_NEAR(m.as_record()->find_field("m00")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(m.as_record()->find_field("m11")->as_number(), 4.0, 1e-9);

    ASSERT_NEAR(eval("Math.mat2_determinant(Math.matrix2(1.0, 2.0, 3.0, 4.0))").as_number(), -2.0,
                1e-9);
}

LUMA_TEST(math_matrix3_identity_multiply_transform) {
    const auto id = eval("Math.mat3_identity()");
    ASSERT_EQ(id.as_record()->type_name, std::string{"Matrix3"});
    ASSERT_NEAR(id.as_record()->find_field("m22")->as_number(), 1.0, 1e-9);

    ASSERT_NEAR(eval("Math.mat3_determinant(Math.mat3_identity())").as_number(), 1.0, 1e-9);

    const auto v = eval("Math.mat3_transform(Math.mat3_identity(), Math.vector3(1.0, 2.0, 3.0))");
    ASSERT_EQ(v.as_record()->type_name, std::string{"Vector3"});
    ASSERT_NEAR(v.as_record()->find_field("z")->as_number(), 3.0, 1e-9);

    const auto prod =
        eval("Math.mat3_multiply(Math.mat3_identity(), Math.matrix3(1.0, 0.0, 0.0, 0.0, 2.0, 0.0, "
             "0.0, 0.0, 3.0))");
    ASSERT_NEAR(prod.as_record()->find_field("m11")->as_number(), 2.0, 1e-9);
}

// --- Math.Vector4 / Math.Matrix4 (N02) ---

LUMA_TEST(math_vector4_and_arithmetic) {
    const auto v = eval("Math.vector4(1.0, 2.0, 3.0, 4.0)");
    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Vector4"});
    ASSERT_NEAR(v.as_record()->find_field("x")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(v.as_record()->find_field("w")->as_number(), 4.0, 1e-9);

    // 3-4-... length: (1,2,2,4) has length sqrt(1+4+4+16) = 5.
    ASSERT_NEAR(eval("Math.vec4_length(Math.vector4(1.0, 2.0, 2.0, 4.0))").as_number(), 5.0, 1e-9);

    ASSERT_NEAR(eval("Math.vec4_dot(Math.vector4(1.0, 2.0, 3.0, 4.0), "
                     "Math.vector4(1.0, 1.0, 1.0, 1.0))")
                    .as_number(),
                10.0, 1e-9);

    const auto sum = eval("Math.vec4_add(Math.vector4(1.0, 2.0, 3.0, 4.0), "
                          "Math.vector4(4.0, 3.0, 2.0, 1.0))");
    ASSERT_NEAR(sum.as_record()->find_field("x")->as_number(), 5.0, 1e-9);
    ASSERT_NEAR(sum.as_record()->find_field("w")->as_number(), 5.0, 1e-9);

    const auto diff = eval("Math.vec4_sub(Math.vector4(4.0, 3.0, 2.0, 1.0), "
                           "Math.vector4(1.0, 1.0, 1.0, 1.0))");
    ASSERT_NEAR(diff.as_record()->find_field("x")->as_number(), 3.0, 1e-9);

    const auto scaled = eval("Math.vec4_scale(Math.vector4(1.0, 2.0, 3.0, 4.0), 2.0)");
    ASSERT_NEAR(scaled.as_record()->find_field("z")->as_number(), 6.0, 1e-9);

    const auto norm = eval("Math.vec4_normalize(Math.vector4(0.0, 0.0, 0.0, 5.0))");
    ASSERT_NEAR(norm.as_record()->find_field("w")->as_number(), 1.0, 1e-9);

    // The zero vector normalises to itself rather than dividing by zero.
    const auto zero = eval("Math.vec4_normalize(Math.vector4(0.0, 0.0, 0.0, 0.0))");
    ASSERT_NEAR(zero.as_record()->find_field("x")->as_number(), 0.0, 1e-9);
}

LUMA_TEST(math_matrix4_identity_multiply_transform) {
    const auto id = eval("Math.mat4_identity()");
    ASSERT_TRUE(id.is_record());
    ASSERT_EQ(id.as_record()->type_name, std::string{"Matrix4"});
    ASSERT_NEAR(id.as_record()->find_field("m00")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(id.as_record()->find_field("m33")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(id.as_record()->find_field("m01")->as_number(), 0.0, 1e-9);

    ASSERT_NEAR(eval("Math.mat4_determinant(Math.mat4_identity())").as_number(), 1.0, 1e-9);

    // Identity leaves a 4D vector unchanged.
    const auto v = eval("Math.mat4_transform(Math.mat4_identity(), "
                        "Math.vector4(1.0, 2.0, 3.0, 4.0))");
    ASSERT_EQ(v.as_record()->type_name, std::string{"Vector4"});
    ASSERT_NEAR(v.as_record()->find_field("z")->as_number(), 3.0, 1e-9);
    ASSERT_NEAR(v.as_record()->find_field("w")->as_number(), 4.0, 1e-9);

    // A * identity == A.
    const auto prod = eval("Math.mat4_multiply(Math.matrix4("
                           "2.0, 0.0, 0.0, 0.0, 0.0, 3.0, 0.0, 0.0, "
                           "0.0, 0.0, 4.0, 0.0, 0.0, 0.0, 0.0, 1.0), Math.mat4_identity())");
    ASSERT_NEAR(prod.as_record()->find_field("m11")->as_number(), 3.0, 1e-9);
    ASSERT_NEAR(prod.as_record()->find_field("m22")->as_number(), 4.0, 1e-9);

    // Determinant of a diagonal scale matrix is the product of the diagonal.
    ASSERT_NEAR(eval("Math.mat4_determinant(Math.matrix4("
                     "2.0, 0.0, 0.0, 0.0, 0.0, 3.0, 0.0, 0.0, "
                     "0.0, 0.0, 4.0, 0.0, 0.0, 0.0, 0.0, 5.0))")
                    .as_number(),
                120.0, 1e-9);
}

LUMA_TEST(math_matrix4_transform_point_translation) {
    // A translation matrix moves a point by (10, 20, 30).
    const auto p = eval("Math.mat4_transform_point(Math.matrix4("
                        "1.0, 0.0, 0.0, 10.0, 0.0, 1.0, 0.0, 20.0, "
                        "0.0, 0.0, 1.0, 30.0, 0.0, 0.0, 0.0, 1.0), Math.vector3(1.0, 2.0, 3.0))");
    ASSERT_EQ(p.as_record()->type_name, std::string{"Vector3"});
    ASSERT_NEAR(p.as_record()->find_field("x")->as_number(), 11.0, 1e-9);
    ASSERT_NEAR(p.as_record()->find_field("y")->as_number(), 22.0, 1e-9);
    ASSERT_NEAR(p.as_record()->find_field("z")->as_number(), 33.0, 1e-9);
}

LUMA_TEST(math_matrix4_perspective_and_look_at) {
    // Perspective is a valid Matrix4 with the -1 in the w row that enables the
    // homogeneous divide.
    const auto proj = eval("Math.mat4_perspective(Math.pi / 2.0, 1.0, 1.0, 100.0)");
    ASSERT_EQ(proj.as_record()->type_name, std::string{"Matrix4"});
    // fov 90°, aspect 1 => m00 = m11 = 1 / tan(45°) = 1.
    ASSERT_NEAR(proj.as_record()->find_field("m00")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(proj.as_record()->find_field("m11")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(proj.as_record()->find_field("m32")->as_number(), -1.0, 1e-9);

    // Looking down the -Z axis from the origin: the eye maps to the view-space
    // origin, so transforming the eye position yields (0, 0, 0).
    const auto view = eval("Math.mat4_look_at(Math.vector3(0.0, 0.0, 5.0), "
                           "Math.vector3(0.0, 0.0, 0.0), Math.vector3(0.0, 1.0, 0.0))");
    ASSERT_EQ(view.as_record()->type_name, std::string{"Matrix4"});
    const auto at_eye =
        eval("Math.mat4_transform_point(Math.mat4_look_at(Math.vector3(0.0, 0.0, 5.0), "
             "Math.vector3(0.0, 0.0, 0.0), Math.vector3(0.0, 1.0, 0.0)), "
             "Math.vector3(0.0, 0.0, 5.0))");
    ASSERT_NEAR(at_eye.as_record()->find_field("x")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(at_eye.as_record()->find_field("y")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(at_eye.as_record()->find_field("z")->as_number(), 0.0, 1e-9);
}

// --- Math.Angle (N07) ---

LUMA_TEST(math_angle_to_radians_and_degrees) {
    // 90 degrees is pi/2 radians.
    ASSERT_NEAR(eval("Math.to_radians(Math.Angle.Degrees(90.0))").as_number(), 1.5707963267948966,
                1e-9);
    // Radians pass through unchanged.
    ASSERT_NEAR(eval("Math.to_radians(Math.Angle.Radians(1.5))").as_number(), 1.5, 1e-9);
    // pi radians is 180 degrees.
    ASSERT_NEAR(eval("Math.to_degrees(Math.Angle.Radians(Math.pi))").as_number(), 180.0, 1e-9);
    // Degrees pass through unchanged.
    ASSERT_NEAR(eval("Math.to_degrees(Math.Angle.Degrees(45.0))").as_number(), 45.0, 1e-9);
}

LUMA_TEST(math_sin_of_angle) {
    ASSERT_NEAR(eval("Math.sin_of(Math.Angle.Degrees(90.0))").as_number(), 1.0, 1e-9);
    ASSERT_NEAR(eval("Math.sin_of(Math.Angle.Degrees(0.0))").as_number(), 0.0, 1e-9);
    ASSERT_NEAR(eval("Math.sin_of(Math.Angle.Radians(Math.pi / 2.0))").as_number(), 1.0, 1e-9);
}

// --- Math.Quaternion (N06) ---

LUMA_TEST(math_quaternion_constructor) {
    const auto q = eval("Math.quaternion(1.0, 2.0, 3.0, 4.0)");
    ASSERT_TRUE(q.is_record());
    ASSERT_EQ(q.as_record()->type_name, std::string{"Quaternion"});
    ASSERT_NEAR(q.as_record()->find_field("w")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("x")->as_number(), 2.0, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("y")->as_number(), 3.0, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("z")->as_number(), 4.0, 1e-9);
}

LUMA_TEST(math_quat_from_axis_angle_is_unit) {
    // A 90-degree rotation about Z: w = cos(45°), z = sin(45°).
    const auto q = eval("Math.quat_from_axis_angle(Math.vector3(0.0, 0.0, 1.0), Math.pi / 2.0)");
    ASSERT_EQ(q.as_record()->type_name, std::string{"Quaternion"});
    ASSERT_NEAR(q.as_record()->find_field("w")->as_number(), 0.7071067811865476, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("x")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("y")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("z")->as_number(), 0.7071067811865476, 1e-9);
}

LUMA_TEST(math_quat_from_axis_angle_normalizes_axis) {
    // A non-unit axis (0,0,2) yields the same rotation as the unit axis.
    const auto q = eval("Math.quat_from_axis_angle(Math.vector3(0.0, 0.0, 2.0), Math.pi / 2.0)");
    ASSERT_NEAR(q.as_record()->find_field("z")->as_number(), 0.7071067811865476, 1e-9);

    // A zero axis yields the identity rotation.
    const auto id = eval("Math.quat_from_axis_angle(Math.vector3(0.0, 0.0, 0.0), 1.0)");
    ASSERT_NEAR(id.as_record()->find_field("w")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(id.as_record()->find_field("x")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(id.as_record()->find_field("z")->as_number(), 0.0, 1e-9);
}

LUMA_TEST(math_quat_multiply_is_hamilton_product) {
    // i * j = k in quaternion algebra: (0,1,0,0) * (0,0,1,0) = (0,0,0,1).
    const auto q = eval("Math.quat_multiply(Math.quaternion(0.0, 1.0, 0.0, 0.0), "
                        "Math.quaternion(0.0, 0.0, 1.0, 0.0))");
    ASSERT_NEAR(q.as_record()->find_field("w")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("x")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("y")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("z")->as_number(), 1.0, 1e-9);
}

LUMA_TEST(math_quat_normalize_unit_length) {
    const auto q = eval("Math.quat_normalize(Math.quaternion(1.0, 1.0, 1.0, 1.0))");
    // Each component of a normalised (1,1,1,1) is 0.5.
    ASSERT_NEAR(q.as_record()->find_field("w")->as_number(), 0.5, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("x")->as_number(), 0.5, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("y")->as_number(), 0.5, 1e-9);
    ASSERT_NEAR(q.as_record()->find_field("z")->as_number(), 0.5, 1e-9);

    // A zero quaternion has no direction; it is returned unchanged.
    const auto zero = eval("Math.quat_normalize(Math.quaternion(0.0, 0.0, 0.0, 0.0))");
    ASSERT_NEAR(zero.as_record()->find_field("w")->as_number(), 0.0, 1e-9);
}

LUMA_TEST(math_quat_rotate_vector) {
    // Rotating the unit-x vector 90 degrees about Z yields the unit-y vector.
    const auto v = eval("Math.quat_rotate_vector("
                        "Math.quat_from_axis_angle(Math.vector3(0.0, 0.0, 1.0), Math.pi / 2.0), "
                        "Math.vector3(1.0, 0.0, 0.0))");
    ASSERT_EQ(v.as_record()->type_name, std::string{"Vector3"});
    ASSERT_NEAR(v.as_record()->find_field("x")->as_number(), 0.0, 1e-9);
    ASSERT_NEAR(v.as_record()->find_field("y")->as_number(), 1.0, 1e-9);
    ASSERT_NEAR(v.as_record()->find_field("z")->as_number(), 0.0, 1e-9);

    // A point on the rotation axis is unchanged by the rotation.
    const auto axis = eval("Math.quat_rotate_vector("
                           "Math.quat_from_axis_angle(Math.vector3(0.0, 0.0, 1.0), Math.pi / 2.0), "
                           "Math.vector3(0.0, 0.0, 5.0))");
    ASSERT_NEAR(axis.as_record()->find_field("z")->as_number(), 5.0, 1e-9);
}

LUMA_TEST(math_quat_rejects_non_record) {
    ASSERT_THROWS(eval("Math.quat_multiply(42, Math.quaternion(1.0, 0.0, 0.0, 0.0))"));
    ASSERT_THROWS(eval("Math.quat_normalize(\"not a quaternion\")"));
    ASSERT_THROWS(eval("Math.quat_rotate_vector(Math.quaternion(1.0, 0.0, 0.0, 0.0), 42)"));
}

int main() {
    LUMA_RUN_ALL();
}
