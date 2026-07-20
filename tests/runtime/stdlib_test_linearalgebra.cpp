// Standard library tests: LinearAlgebra module.

#include <cstddef>

#include "common/resource_limits.hpp"
#include "stdlib_test_helpers.hpp"

LUMA_TEST(linear_algebra_identity_overflow_guarded) {
    // A size whose square overflows int64 must be rejected with a clean limit
    // error rather than bypassing the guard and attempting an absurd
    // allocation. (Regression: the guard computed n*n, which overflowed for
    // very large n and silently passed.)
    bool threw{false};

    try {
        (void)eval("LinearAlgebra.identity(4000000000)");
    } catch (const std::exception& e) {
        threw = true;
        ASSERT_TRUE(std::string{e.what()}.find("maximum matrix element count") !=
                    std::string::npos);
    }

    ASSERT_TRUE(threw);
}

LUMA_TEST(linear_algebra_multiply_overflow_guarded) {
    // Both operands are within the array-size limit, but the product's element
    // count (rows(a) × cols(b)) exceeds it.  multiply must reject the operation
    // via the shared size guard before allocating the oversized result.
    // (Regression: multiply had no result-size guard, unlike the constructors.)
    const LimitGuard guard{ResourceLimits::max_array_size, static_cast<std::size_t>(8)};

    bool threw{false};

    try {
        (void)eval("LinearAlgebra.multiply(LinearAlgebra.zero_matrix(3, 1), "
                   "LinearAlgebra.zero_matrix(1, 3))");
    } catch (const std::exception& e) {
        threw = true;
        ASSERT_TRUE(std::string{e.what()}.find("maximum matrix element count") !=
                    std::string::npos);
    }

    ASSERT_TRUE(threw);
}

LUMA_TEST(linearalgebra_add) {
    const auto v = eval("LinearAlgebra.add([1.0, 2.0], [3.0, 4.0])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(arr.size(), 2U);
}

LUMA_TEST(linearalgebra_cross) {
    const auto result = eval(R"(
        LinearAlgebra.cross([1.0, 0.0, 0.0], [0.0, 1.0, 0.0]) |> Result.unwrap()
    )");

    ASSERT_TRUE(result.is_array());

    auto& elems = *result.as_array()->elements;

    ASSERT_NEAR(elems[2].to_numeric(), 1.0, 1e-10);
}

LUMA_TEST(linearalgebra_cross_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.cross([1.0, 2.0], [3.0, 4.0])");
}

LUMA_TEST(linearalgebra_determinant) {
    const auto v = eval("LinearAlgebra.determinant([[1.0, 2.0], [3.0, 4.0]])");

    ASSERT_RESULT_SUCCESS(v);

    const auto n = v.as_result()->owned_inner->as_number();

    ASSERT_TRUE(n > -2.1 && n < -1.9);
}

LUMA_TEST(linearalgebra_dot) {
    const auto result = eval(R"(
        LinearAlgebra.dot([1.0, 2.0, 3.0], [4.0, 5.0, 6.0]) |> Result.unwrap()
    )");

    ASSERT_NEAR(result.to_numeric(), 32.0, 1e-10);
}

LUMA_TEST(linearalgebra_dot_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.dot([1.0, 2.0], [3.0])");
}

LUMA_TEST(linearalgebra_identity) {
    auto result =
        eval("LinearAlgebra.identity(2) |> LinearAlgebra.determinant() |> Result.unwrap()");

    ASSERT_NEAR(result.to_numeric(), 1.0, 1e-10);
}

LUMA_TEST(linearalgebra_inverse) {
    const auto v = eval("LinearAlgebra.inverse([[1.0, 0.0], [0.0, 1.0]])");

    ASSERT_RESULT_SUCCESS(v);
}

LUMA_TEST(linearalgebra_module) {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("LinearAlgebra.add"));
    ASSERT_TRUE(env->has("LinearAlgebra.dot"));
    ASSERT_TRUE(env->has("LinearAlgebra.cross"));
    ASSERT_TRUE(env->has("LinearAlgebra.norm"));
    ASSERT_TRUE(env->has("LinearAlgebra.identity"));
    ASSERT_TRUE(env->has("LinearAlgebra.multiply"));
    ASSERT_TRUE(env->has("LinearAlgebra.determinant"));
    ASSERT_TRUE(env->has("LinearAlgebra.inverse"));
    ASSERT_TRUE(env->has("LinearAlgebra.solve"));
}

LUMA_TEST(linearalgebra_multiply) {
    const auto v = eval("LinearAlgebra.multiply([[1.0, 0.0], [0.0, 1.0]], [[2.0], [3.0]])");

    ASSERT_RESULT_SUCCESS(v);
}

LUMA_TEST(linearalgebra_norm) {
    const auto result = eval(R"(
        LinearAlgebra.norm([3.0, 4.0])
    )");

    ASSERT_NEAR(result.to_numeric(), 5.0, 1e-10);
}

LUMA_TEST(linearalgebra_solve) {
    auto result =
        eval("LinearAlgebra.solve([[2.0, 1.0], [1.0, 3.0]], [5.0, 10.0]) |> Result.unwrap()");

    ASSERT_TRUE(result.is_array());

    auto& elems = *result.as_array()->elements;

    ASSERT_NEAR(elems[0].to_numeric(), 1.0, 1e-8);
    ASSERT_NEAR(elems[1].to_numeric(), 3.0, 1e-8);
}

LUMA_TEST(linearalgebra_subtract) {
    const auto v = eval("LinearAlgebra.subtract([5.0, 3.0], [1.0, 1.0])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_EQ(arr.size(), 2U);
    ASSERT_NEAR(arr[0].to_numeric(), 4.0, 1e-9);
    ASSERT_NEAR(arr[1].to_numeric(), 2.0, 1e-9);
}

LUMA_TEST(linearalgebra_scale) {
    const auto v = eval("LinearAlgebra.scale([1.0, 2.0, 3.0], 2.0)");

    ASSERT_TRUE(v.is_array());

    const auto& arr = *v.as_array()->elements;

    ASSERT_NEAR(arr[0].to_numeric(), 2.0, 1e-9);
    ASSERT_NEAR(arr[2].to_numeric(), 6.0, 1e-9);
}

LUMA_TEST(linearalgebra_negate) {
    const auto v = eval("LinearAlgebra.negate([1.0, -2.0, 3.0])");

    ASSERT_TRUE(v.is_array());

    const auto& arr = *v.as_array()->elements;

    ASSERT_NEAR(arr[0].to_numeric(), -1.0, 1e-9);
    ASSERT_NEAR(arr[1].to_numeric(), 2.0, 1e-9);
    ASSERT_NEAR(arr[2].to_numeric(), -3.0, 1e-9);
}

LUMA_TEST(linearalgebra_normalize) {
    const auto v = eval("LinearAlgebra.normalize([3.0, 4.0])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_NEAR(arr[0].to_numeric(), 0.6, 1e-9);
    ASSERT_NEAR(arr[1].to_numeric(), 0.8, 1e-9);
}

LUMA_TEST(linearalgebra_distance) {
    const auto v = eval("LinearAlgebra.distance([0.0, 0.0], [3.0, 4.0])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->to_numeric(), 5.0, 1e-9);
}

LUMA_TEST(linearalgebra_dimension) {
    const auto v = eval("LinearAlgebra.dimension([1.0, 2.0, 3.0, 4.0])");

    ASSERT_EQ(v.as_integer(), 4);
}

LUMA_TEST(linearalgebra_angle) {
    const auto v = eval("LinearAlgebra.angle([1.0, 0.0], [0.0, 1.0])");

    ASSERT_RESULT_SUCCESS(v);

    // Angle between orthogonal axes is pi/2.
    ASSERT_NEAR(v.as_result()->owned_inner->to_numeric(), 1.5707963267948966, 1e-9);
}

LUMA_TEST(linearalgebra_unit_vector) {
    const auto v = eval("LinearAlgebra.unit_vector(3, 1)");

    ASSERT_TRUE(v.is_array());

    const auto& arr = *v.as_array()->elements;

    ASSERT_EQ(arr.size(), 3U);
    ASSERT_NEAR(arr[0].to_numeric(), 0.0, 1e-9);
    ASSERT_NEAR(arr[1].to_numeric(), 1.0, 1e-9);
    ASSERT_NEAR(arr[2].to_numeric(), 0.0, 1e-9);
}

LUMA_TEST(linearalgebra_zero_vector) {
    const auto v = eval("LinearAlgebra.zero_vector(4)");

    ASSERT_TRUE(v.is_array());

    const auto& arr = *v.as_array()->elements;

    ASSERT_EQ(arr.size(), 4U);
    ASSERT_NEAR(arr[0].to_numeric(), 0.0, 1e-9);
    ASSERT_NEAR(arr[3].to_numeric(), 0.0, 1e-9);
}

LUMA_TEST(linearalgebra_zero_matrix) {
    const auto v = eval("LinearAlgebra.zero_matrix(2, 3)");

    ASSERT_TRUE(v.is_array());

    const auto& rows = *v.as_array()->elements;

    ASSERT_EQ(rows.size(), 2U);
    ASSERT_EQ(rows[0].as_array()->elements->size(), 3U);
    ASSERT_NEAR((*rows[0].as_array()->elements)[0].to_numeric(), 0.0, 1e-9);
}

LUMA_TEST(linearalgebra_diagonal) {
    const auto v = eval("LinearAlgebra.diagonal([1.0, 2.0, 3.0])");

    ASSERT_TRUE(v.is_array());

    const auto& rows = *v.as_array()->elements;

    ASSERT_EQ(rows.size(), 3U);
    ASSERT_NEAR((*rows[0].as_array()->elements)[0].to_numeric(), 1.0, 1e-9);
    ASSERT_NEAR((*rows[1].as_array()->elements)[1].to_numeric(), 2.0, 1e-9);
    ASSERT_NEAR((*rows[2].as_array()->elements)[2].to_numeric(), 3.0, 1e-9);
    ASSERT_NEAR((*rows[0].as_array()->elements)[1].to_numeric(), 0.0, 1e-9);
}

LUMA_TEST(linearalgebra_rows) {
    const auto v = eval("LinearAlgebra.rows([[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]])");

    ASSERT_EQ(v.as_integer(), 3);
}

LUMA_TEST(linearalgebra_columns) {
    const auto v = eval("LinearAlgebra.columns([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]])");

    ASSERT_EQ(v.as_integer(), 3);
}

LUMA_TEST(linearalgebra_shape) {
    const auto v = eval("LinearAlgebra.shape([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]])");

    const auto& elems = v.as_tuple()->elements;

    ASSERT_EQ(elems.size(), 2U);
    ASSERT_EQ(elems[0].as_integer(), 2);
    ASSERT_EQ(elems[1].as_integer(), 3);
}

LUMA_TEST(linearalgebra_is_square) {
    ASSERT_TRUE(eval("LinearAlgebra.is_square([[1.0, 2.0], [3.0, 4.0]])").as_bool());
    ASSERT_FALSE(eval("LinearAlgebra.is_square([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]])").as_bool());
}

LUMA_TEST(linearalgebra_transpose) {
    const auto v = eval("LinearAlgebra.transpose([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]])");

    ASSERT_TRUE(v.is_array());

    const auto& rows = *v.as_array()->elements;

    ASSERT_EQ(rows.size(), 3U);
    ASSERT_EQ(rows[0].as_array()->elements->size(), 2U);
    ASSERT_NEAR((*rows[0].as_array()->elements)[1].to_numeric(), 4.0, 1e-9);
    ASSERT_NEAR((*rows[2].as_array()->elements)[0].to_numeric(), 3.0, 1e-9);
}

LUMA_TEST(linearalgebra_multiply_vector) {
    const auto v = eval("LinearAlgebra.multiply_vector([[1.0, 0.0], [0.0, 2.0]], [3.0, 4.0])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& arr = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_NEAR(arr[0].to_numeric(), 3.0, 1e-9);
    ASSERT_NEAR(arr[1].to_numeric(), 8.0, 1e-9);
}

LUMA_TEST(linearalgebra_scale_matrix) {
    const auto v = eval("LinearAlgebra.scale_matrix([[1.0, 2.0], [3.0, 4.0]], 2.0)");

    ASSERT_TRUE(v.is_array());

    const auto& rows = *v.as_array()->elements;

    ASSERT_NEAR((*rows[0].as_array()->elements)[0].to_numeric(), 2.0, 1e-9);
    ASSERT_NEAR((*rows[1].as_array()->elements)[1].to_numeric(), 8.0, 1e-9);
}

LUMA_TEST(linearalgebra_add_matrix) {
    const auto v =
        eval("LinearAlgebra.add_matrix([[1.0, 2.0], [3.0, 4.0]], [[10.0, 20.0], [30.0, 40.0]])");

    ASSERT_RESULT_SUCCESS(v);

    const auto& rows = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_NEAR((*rows[0].as_array()->elements)[0].to_numeric(), 11.0, 1e-9);
    ASSERT_NEAR((*rows[1].as_array()->elements)[1].to_numeric(), 44.0, 1e-9);
}

LUMA_TEST(linearalgebra_trace) {
    const auto v = eval("LinearAlgebra.trace([[1.0, 2.0], [3.0, 4.0]])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->to_numeric(), 5.0, 1e-9);
}

LUMA_TEST(linearalgebra_is_orthogonal) {
    ASSERT_TRUE(eval("LinearAlgebra.is_orthogonal([1.0, 0.0], [0.0, 1.0])").as_bool());
    ASSERT_FALSE(eval("LinearAlgebra.is_orthogonal([1.0, 0.0], [1.0, 1.0])").as_bool());
}

LUMA_TEST(linearalgebra_is_symmetric) {
    ASSERT_TRUE(eval("LinearAlgebra.is_symmetric([[1.0, 2.0], [2.0, 1.0]])").as_bool());
    ASSERT_FALSE(eval("LinearAlgebra.is_symmetric([[1.0, 2.0], [3.0, 1.0]])").as_bool());
}

LUMA_TEST(linearalgebra_determinant_3x3) {
    const auto v =
        eval("LinearAlgebra.determinant([[6.0, 1.0, 1.0], [4.0, -2.0, 5.0], [2.0, 8.0, 7.0]])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->to_numeric(), -306.0, 1e-6);
}

LUMA_TEST(linearalgebra_inverse_values) {
    const auto v = eval("LinearAlgebra.inverse([[4.0, 7.0], [2.0, 6.0]])");

    ASSERT_RESULT_SUCCESS(v);

    // inverse([[4,7],[2,6]]) = [[0.6, -0.7], [-0.2, 0.4]]
    const auto& rows = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_NEAR((*rows[0].as_array()->elements)[0].to_numeric(), 0.6, 1e-9);
    ASSERT_NEAR((*rows[0].as_array()->elements)[1].to_numeric(), -0.7, 1e-9);
    ASSERT_NEAR((*rows[1].as_array()->elements)[0].to_numeric(), -0.2, 1e-9);
    ASSERT_NEAR((*rows[1].as_array()->elements)[1].to_numeric(), 0.4, 1e-9);
}

LUMA_TEST(linearalgebra_multiply_values) {
    const auto v =
        eval("LinearAlgebra.multiply([[1.0, 2.0], [3.0, 4.0]], [[5.0, 6.0], [7.0, 8.0]])");

    ASSERT_RESULT_SUCCESS(v);

    // [[1,2],[3,4]] * [[5,6],[7,8]] = [[19,22],[43,50]]
    const auto& rows = *v.as_result()->owned_inner->as_array()->elements;

    ASSERT_NEAR((*rows[0].as_array()->elements)[0].to_numeric(), 19.0, 1e-9);
    ASSERT_NEAR((*rows[0].as_array()->elements)[1].to_numeric(), 22.0, 1e-9);
    ASSERT_NEAR((*rows[1].as_array()->elements)[0].to_numeric(), 43.0, 1e-9);
    ASSERT_NEAR((*rows[1].as_array()->elements)[1].to_numeric(), 50.0, 1e-9);
}

LUMA_TEST(linearalgebra_add_mismatch_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.add([1.0, 2.0], [1.0, 2.0, 3.0])");
}

LUMA_TEST(linearalgebra_subtract_mismatch_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.subtract([1.0, 2.0], [1.0])");
}

LUMA_TEST(linearalgebra_distance_mismatch_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.distance([1.0, 2.0], [1.0])");
}

LUMA_TEST(linearalgebra_angle_mismatch_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.angle([1.0, 2.0], [1.0])");
}

LUMA_TEST(linearalgebra_angle_zero_vector_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.angle([0.0, 0.0], [1.0, 1.0])");
}

LUMA_TEST(linearalgebra_normalize_zero_vector_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.normalize([0.0, 0.0, 0.0])");
}

LUMA_TEST(linearalgebra_multiply_mismatch_fail) {
    // Inner dimensions disagree: (1x2) * (1x2) is invalid.
    ASSERT_EVAL_FAILURE("LinearAlgebra.multiply([[1.0, 2.0]], [[1.0, 2.0]])");
}

LUMA_TEST(linearalgebra_multiply_vector_mismatch_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.multiply_vector([[1.0, 2.0], [3.0, 4.0]], [1.0])");
}

LUMA_TEST(linearalgebra_add_matrix_mismatch_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.add_matrix([[1.0, 2.0]], [[1.0, 2.0], [3.0, 4.0]])");
}

LUMA_TEST(linearalgebra_determinant_non_square_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.determinant([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]])");
}

LUMA_TEST(linearalgebra_trace_non_square_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.trace([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]])");
}

LUMA_TEST(linearalgebra_inverse_non_square_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.inverse([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]])");
}

LUMA_TEST(linearalgebra_inverse_singular_fail) {
    // Second row is a multiple of the first: no inverse exists.
    ASSERT_EVAL_FAILURE("LinearAlgebra.inverse([[1.0, 2.0], [2.0, 4.0]])");
}

LUMA_TEST(linearalgebra_solve_non_square_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.solve([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]], [1.0, 2.0])");
}

LUMA_TEST(linearalgebra_solve_mismatch_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.solve([[2.0, 1.0], [1.0, 3.0]], [5.0])");
}

LUMA_TEST(linearalgebra_solve_singular_fail) {
    ASSERT_EVAL_FAILURE("LinearAlgebra.solve([[1.0, 1.0], [1.0, 1.0]], [1.0, 2.0])");
}

LUMA_TEST(linearalgebra_zero_vector_non_positive_throws) {
    ASSERT_THROWS(eval("LinearAlgebra.zero_vector(0)"));
    ASSERT_THROWS(eval("LinearAlgebra.zero_vector(-3)"));
}

LUMA_TEST(linearalgebra_unit_vector_out_of_range_throws) {
    ASSERT_THROWS(eval("LinearAlgebra.unit_vector(3, 5)"));
    ASSERT_THROWS(eval("LinearAlgebra.unit_vector(3, -1)"));
}

LUMA_TEST(linearalgebra_identity_non_positive_throws) {
    ASSERT_THROWS(eval("LinearAlgebra.identity(0)"));
}

LUMA_TEST(linearalgebra_zero_matrix_non_positive_throws) {
    ASSERT_THROWS(eval("LinearAlgebra.zero_matrix(0, 3)"));
    ASSERT_THROWS(eval("LinearAlgebra.zero_matrix(2, -1)"));
}

LUMA_TEST(linearalgebra_ragged_matrix_throws) {
    // to_mat rejects matrices whose rows differ in length.
    ASSERT_THROWS(eval("LinearAlgebra.transpose([[1.0, 2.0], [3.0]])"));
}

LUMA_TEST(linearalgebra_non_numeric_throws) {
    // to_vec rejects array elements that are not numbers.
    ASSERT_THROWS(eval("LinearAlgebra.norm([\"a\", \"b\"])"));
    ASSERT_THROWS(eval("LinearAlgebra.dot([1.0, 2.0], [\"x\", \"y\"])"));
}

int main() {
    LUMA_RUN_ALL();
}
