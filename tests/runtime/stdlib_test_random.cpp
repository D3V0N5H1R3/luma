// Standard library tests: Random.

#include <cstdint>
#include <limits>
#include <vector>

#include "runtime/stdlib/system/random_bounded.hpp"
#include "stdlib_test_helpers.hpp"

namespace {

constexpr std::int64_t i64_min = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t i64_max = std::numeric_limits<std::int64_t>::max();
constexpr std::uint64_t u64_max = std::numeric_limits<std::uint64_t>::max();

// Whether the cryptographically secure generator is available in this build.
// secure_* requires the Mbed TLS backend (LUMA_FEATURE_TLS=ON, the default);
// when it is disabled the functions return a clean "requires TLS" failure
// instead of a value, so the secure tests adapt to either configuration.
[[nodiscard]] bool secure_rng_available() {
    const auto v = eval("Random.secure_boolean()");

    return luma::test::is_success_result(v);
}

} // namespace

// ── Pure bounded-uniform logic (random_bounded.hpp) ──────────────────
//
// These exercise the UB-free integer-range core directly, with a deterministic
// generator, so the rejection-sampling maths is verified independently of any
// RNG or TLS backend — including the extreme [INT64_MIN, INT64_MAX] span that
// previously triggered signed-overflow and division-by-zero.

static void test_bounded_range_pure() {
    ASSERT_EQ(luma::bounded_range(0, 0), 0ULL);
    ASSERT_EQ(luma::bounded_range(5, 5), 0ULL);
    ASSERT_EQ(luma::bounded_range(-5, 5), 10ULL);
    ASSERT_EQ(luma::bounded_range(1, 10), 9ULL);
    // The full int64 span must equal UINT64_MAX with no signed overflow.
    ASSERT_EQ(luma::bounded_range(i64_min, i64_max), u64_max);
    ASSERT_EQ(luma::bounded_range(0, i64_max), static_cast<std::uint64_t>(i64_max));
    ASSERT_EQ(luma::bounded_range(i64_min, 0), static_cast<std::uint64_t>(i64_max) + 1ULL);
}

static void test_bounded_reject_threshold_pure() {
    // Full range: span is 2^64, nothing is rejected.
    ASSERT_EQ(luma::bounded_reject_threshold(u64_max), 0ULL);
    // span = 1 (range 0): exact, no rejection.
    ASSERT_EQ(luma::bounded_reject_threshold(0), 0ULL);
    // Power-of-two spans divide 2^64 exactly: no rejection.
    ASSERT_EQ(luma::bounded_reject_threshold(1), 0ULL);   // span 2
    ASSERT_EQ(luma::bounded_reject_threshold(255), 0ULL); // span 256
    // span = 3: 2^64 mod 3 = 1.
    ASSERT_EQ(luma::bounded_reject_threshold(2), 1ULL);
    // span = 10: 2^64 mod 10 = 6.
    ASSERT_EQ(luma::bounded_reject_threshold(9), 6ULL);
}

static void test_bounded_map_pure() {
    // Full range maps the raw value through unchanged.
    ASSERT_EQ(luma::bounded_map(i64_min, u64_max, 0ULL), i64_min);
    ASSERT_EQ(luma::bounded_map(i64_min, u64_max, u64_max), i64_max);
    ASSERT_EQ(luma::bounded_map(i64_min, u64_max, 1ULL), i64_min + 1);
    // Bounded spans reduce modulo (range + 1).
    ASSERT_EQ(luma::bounded_map(0, 9, 0ULL), 0);
    ASSERT_EQ(luma::bounded_map(0, 9, 10ULL), 0);
    ASSERT_EQ(luma::bounded_map(0, 9, 13ULL), 3);
    // Negative lo is handled via unsigned arithmetic.
    ASSERT_EQ(luma::bounded_map(-5, 10, 0ULL), -5);
    ASSERT_EQ(luma::bounded_map(-5, 10, 7ULL), 2);
    ASSERT_EQ(luma::bounded_map(-5, 10, 10ULL), 5);
    ASSERT_EQ(luma::bounded_map(-5, 10, 11ULL), -5);
}

static void test_bounded_uniform_invalid_range() {
    std::int64_t out{-999};
    bool called{false};

    const bool ok = luma::bounded_uniform(
        10, 1,
        [&called](std::uint64_t& r) {
            called = true;
            r = 0;

            return true;
        },
        out);

    ASSERT_FALSE(ok);
    ASSERT_FALSE(called); // generator must not be consulted for lo > hi
}

static void test_bounded_uniform_singleton() {
    std::int64_t out{-999};
    bool called{false};

    const bool ok = luma::bounded_uniform(
        42, 42,
        [&called](std::uint64_t& r) {
            called = true;
            r = 0;

            return true;
        },
        out);

    ASSERT_TRUE(ok);
    ASSERT_FALSE(called); // lo == hi needs no draw
    ASSERT_EQ(out, 42);
}

static void test_bounded_uniform_generator_failure() {
    std::int64_t out{0};

    const bool ok = luma::bounded_uniform(0, 100, [](std::uint64_t&) { return false; }, out);

    ASSERT_FALSE(ok);
}

static void test_bounded_uniform_rejection() {
    // range = 2 (span 3) → threshold 1, so a raw of 0 is rejected and the next
    // draw is taken.
    std::vector<std::uint64_t> seq{0, 7};
    std::size_t idx{0};
    std::int64_t out{0};

    const bool ok = luma::bounded_uniform(
        0, 2,
        [&seq, &idx](std::uint64_t& r) {
            r = seq[idx++];

            return true;
        },
        out);

    ASSERT_TRUE(ok);
    ASSERT_EQ(idx, 2U); // exactly one rejection occurred
    ASSERT_EQ(out, 1);  // 7 % 3
}

static void test_bounded_uniform_full_range() {
    std::int64_t out{0};

    const bool lo_ok = luma::bounded_uniform(
        i64_min, i64_max,
        [](std::uint64_t& r) {
            r = 0;
            return true;
        },
        out);
    ASSERT_TRUE(lo_ok);
    ASSERT_EQ(out, i64_min);

    const bool hi_ok = luma::bounded_uniform(
        i64_min, i64_max,
        [](std::uint64_t& r) {
            r = u64_max;
            return true;
        },
        out);
    ASSERT_TRUE(hi_ok);
    ASSERT_EQ(out, i64_max);
}

static void test_bounded_uniform_in_range_property() {
    const std::int64_t ranges[][2] = {{-5, 5},      {0, 1},      {-100, -100}, {i64_min, i64_max},
                                      {i64_min, 0}, {0, i64_max}};

    for (const auto& range : ranges) {
        for (const std::uint64_t raw :
             {0ULL, 1ULL, 7ULL, 12345ULL, u64_max / 2U, u64_max - 1U, u64_max}) {
            std::int64_t out{0};
            std::size_t calls{0};

            // Return the probe value first; if it is rejected, fall back to
            // UINT64_MAX (always accepted) so the loop is guaranteed to halt.
            const bool ok = luma::bounded_uniform(
                range[0], range[1],
                [raw, &calls](std::uint64_t& r) {
                    r = (calls++ == 0) ? raw : u64_max;

                    return true;
                },
                out);

            ASSERT_TRUE(ok);
            ASSERT_GE(out, range[0]);
            ASSERT_LE(out, range[1]);
        }
    }
}

// ── choice ───────────────────────────────────────────────────────────

static void test_random_choice() {
    const auto v = eval("Random.choice([\"a\", \"b\", \"c\"])");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_string());

    ASSERT_EVAL_FAILURE("Random.choice([])");
}

// ── generate_boolean ──────────────────────────────────────────────────

static void test_random_generate_boolean() {
    const auto v = eval("Random.generate_boolean()");

    ASSERT_TRUE(v.is_bool());
}

// ── generate_number ───────────────────────────────────────────────────

static void test_random_generate_number() {
    const auto v = eval("Random.generate_number()");

    ASSERT_TRUE(v.is_number());
    ASSERT_GE(v.as_number(), 0.0);
    ASSERT_LT(v.as_number(), 1.0);
}

// ── generate_integer ──────────────────────────────────────────────────

static void test_random_generate_integer() {
    const auto v = eval("Random.generate_integer(1, 10)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_integer());
    ASSERT_TRUE(v.as_result()->owned_inner->as_integer() >= 1 &&
                v.as_result()->owned_inner->as_integer() <= 10);
}

static void test_random_generate_integer_singleton() {
    ASSERT_EVAL_INT("Random.generate_integer(7, 7)", 7);
}

static void test_random_generate_integer_extreme_range() {
    // A near-full span must not overflow or divide by zero; the result stays in
    // bounds. (The literal for INT64_MIN cannot be written directly, so use
    // INT64_MIN + 1 as the low bound.)
    const auto v = eval("Random.generate_integer(-9223372036854775807, 9223372036854775807)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_integer());
}

static void test_random_generate_integer_invalid_range() {
    ASSERT_EVAL_FAILURE("Random.generate_integer(10, 1)");
}

// ── generate_string ───────────────────────────────────────────────────

static void test_random_generate_string() {
    const auto v = eval("Random.generate_string(12)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_string());

    const auto& s = v.as_result()->owned_inner->as_string();
    ASSERT_EQ(s.size(), 12U);

    for (const char c : s) {
        const bool alnum =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        ASSERT_TRUE(alnum);
    }
}

static void test_random_generate_string_empty() {
    const auto v = eval("Random.generate_string(0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_string());
    ASSERT_EQ(v.as_result()->owned_inner->as_string().size(), 0U);
}

static void test_random_generate_string_negative() {
    ASSERT_EVAL_FAILURE("Random.generate_string(-1)");
}

static void test_random_generate_string_too_large() {
    // A length beyond max_string_size must fail rather than attempt a multi-
    // gigabyte allocation.
    ASSERT_EVAL_FAILURE("Random.generate_string(10000000000)");
}

// ── sample ─────────────────────────────────────────────────────────────

static void test_random_sample() {
    const auto v = eval("Random.sample([1, 2, 3, 4, 5], 3)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_array());
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 3U);
}

static void test_random_sample_too_large() {
    // A count greater than the source size fails per the documented contract
    // (Random.sample returns result<array<T>> and "fail if k > length").
    ASSERT_EVAL_FAILURE("Random.sample([1, 2, 3], 10)");
}

static void test_random_sample_zero() {
    const auto v = eval("Random.sample([1, 2, 3], 0)");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_array()->elements->size(), 0U);
}

static void test_random_sample_negative() {
    ASSERT_EVAL_FAILURE("Random.sample([1, 2, 3], -1)");
}

// ── shuffle ────────────────────────────────────────────────────────────

static void test_random_shuffle() {
    const auto v = eval("Random.shuffle([1, 2, 3])");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
}

// ── uuid ───────────────────────────────────────────────────────────────

static void test_random_uuid() {
    const auto v = eval("Random.generate_uuid()");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string().size(), 36U);
    ASSERT_EQ(v.as_string()[8], '-');
    ASSERT_EQ(v.as_string()[13], '-');
    ASSERT_EQ(v.as_string()[18], '-');
    ASSERT_EQ(v.as_string()[23], '-');
    // Version nibble must be '4'.
    ASSERT_EQ(v.as_string()[14], '4');
}

static void test_random_uuid_unique() {
    const auto a = eval("Random.generate_uuid()");
    const auto b = eval("Random.generate_uuid()");

    ASSERT_TRUE(a.as_string() != b.as_string());
}

// ── secure variants (adapt to TLS availability) ───────────────────────

static void test_random_secure_boolean() {
    const auto v = eval("Random.secure_boolean()");

    ASSERT_TRUE(v.is_result());

    if (secure_rng_available()) {
        ASSERT_RESULT_SUCCESS(v);
        ASSERT_TRUE(v.as_result()->owned_inner->is_bool());
    } else {
        ASSERT_RESULT_FAILURE(v);
    }
}

static void test_random_secure_integer() {
    const auto v = eval("Random.secure_integer(1, 6)");

    ASSERT_TRUE(v.is_result());

    if (secure_rng_available()) {
        ASSERT_RESULT_SUCCESS(v);
        ASSERT_TRUE(v.as_result()->owned_inner->is_integer());

        const auto n = v.as_result()->owned_inner->as_integer();
        ASSERT_TRUE(n >= 1 && n <= 6);
    } else {
        ASSERT_RESULT_FAILURE(v);
    }
}

static void test_random_secure_integer_singleton() {
    const auto v = eval("Random.secure_integer(99, 99)");

    ASSERT_TRUE(v.is_result());

    if (secure_rng_available()) {
        ASSERT_RESULT_SUCCESS(v);
        ASSERT_EQ(v.as_result()->owned_inner->as_integer(), 99);
    } else {
        ASSERT_RESULT_FAILURE(v);
    }
}

static void test_random_secure_integer_extreme_range() {
    // The previously crashing full span: must return a valid result (TLS on) or
    // a clean failure (TLS off) — never a division-by-zero crash.
    const auto v = eval("Random.secure_integer(-9223372036854775807, 9223372036854775807)");

    ASSERT_TRUE(v.is_result());

    if (secure_rng_available()) {
        ASSERT_RESULT_SUCCESS(v);
        ASSERT_TRUE(v.as_result()->owned_inner->is_integer());
    } else {
        ASSERT_RESULT_FAILURE(v);
    }
}

static void test_random_secure_integer_invalid_range() {
    // Validation runs before the TLS backend, so this fails in every build.
    ASSERT_EVAL_FAILURE("Random.secure_integer(6, 1)");
}

static void test_random_secure_number() {
    const auto v = eval("Random.secure_number()");

    ASSERT_TRUE(v.is_result());

    if (secure_rng_available()) {
        ASSERT_RESULT_SUCCESS(v);
        ASSERT_TRUE(v.as_result()->owned_inner->is_number());

        const auto n = v.as_result()->owned_inner->as_number();
        ASSERT_GE(n, 0.0);
        ASSERT_LT(n, 1.0);
    } else {
        ASSERT_RESULT_FAILURE(v);
    }
}

static void test_random_secure_string() {
    const auto v = eval("Random.secure_string(24)");

    ASSERT_TRUE(v.is_result());

    if (secure_rng_available()) {
        ASSERT_RESULT_SUCCESS(v);
        ASSERT_TRUE(v.as_result()->owned_inner->is_string());

        const auto& s = v.as_result()->owned_inner->as_string();
        ASSERT_EQ(s.size(), 24U);

        for (const char c : s) {
            const bool alnum =
                (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            ASSERT_TRUE(alnum);
        }
    } else {
        ASSERT_RESULT_FAILURE(v);
    }
}

static void test_random_secure_string_empty() {
    const auto v = eval("Random.secure_string(0)");

    ASSERT_TRUE(v.is_result());

    if (secure_rng_available()) {
        ASSERT_RESULT_SUCCESS(v);
        ASSERT_EQ(v.as_result()->owned_inner->as_string().size(), 0U);
    } else {
        ASSERT_RESULT_FAILURE(v);
    }
}

static void test_random_secure_string_negative() {
    // Validation runs before the TLS backend, so this fails in every build.
    ASSERT_EVAL_FAILURE("Random.secure_string(-1)");
}

static void test_random_secure_uuid() {
    const auto v = eval("Random.secure_uuid()");

    ASSERT_TRUE(v.is_result());

    if (secure_rng_available()) {
        ASSERT_RESULT_SUCCESS(v);
        ASSERT_TRUE(v.as_result()->owned_inner->is_string());

        const auto& s = v.as_result()->owned_inner->as_string();
        ASSERT_EQ(s.size(), 36U);
        ASSERT_EQ(s[14], '4');
    } else {
        ASSERT_RESULT_FAILURE(v);
    }
}

// ── registration ───────────────────────────────────────────────────────

static void test_random_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Random.generate_number"));
    ASSERT_TRUE(env->has("Random.generate_integer"));
    ASSERT_TRUE(env->has("Random.generate_boolean"));
    ASSERT_TRUE(env->has("Random.generate_string"));
    ASSERT_TRUE(env->has("Random.choice"));
    ASSERT_TRUE(env->has("Random.shuffle"));
    ASSERT_TRUE(env->has("Random.sample"));
    ASSERT_TRUE(env->has("Random.generate_uuid"));
    ASSERT_TRUE(env->has("Random.secure_boolean"));
    ASSERT_TRUE(env->has("Random.secure_integer"));
    ASSERT_TRUE(env->has("Random.secure_number"));
    ASSERT_TRUE(env->has("Random.secure_string"));
    ASSERT_TRUE(env->has("Random.secure_uuid"));
}

int main() {
    RUN(test_bounded_range_pure);
    RUN(test_bounded_reject_threshold_pure);
    RUN(test_bounded_map_pure);
    RUN(test_bounded_uniform_invalid_range);
    RUN(test_bounded_uniform_singleton);
    RUN(test_bounded_uniform_generator_failure);
    RUN(test_bounded_uniform_rejection);
    RUN(test_bounded_uniform_full_range);
    RUN(test_bounded_uniform_in_range_property);

    RUN(test_random_choice);
    RUN(test_random_generate_boolean);
    RUN(test_random_generate_number);
    RUN(test_random_generate_integer);
    RUN(test_random_generate_integer_singleton);
    RUN(test_random_generate_integer_extreme_range);
    RUN(test_random_generate_integer_invalid_range);
    RUN(test_random_generate_string);
    RUN(test_random_generate_string_empty);
    RUN(test_random_generate_string_negative);
    RUN(test_random_generate_string_too_large);
    RUN(test_random_sample);
    RUN(test_random_sample_too_large);
    RUN(test_random_sample_zero);
    RUN(test_random_sample_negative);
    RUN(test_random_shuffle);
    RUN(test_random_uuid);
    RUN(test_random_uuid_unique);

    RUN(test_random_secure_boolean);
    RUN(test_random_secure_integer);
    RUN(test_random_secure_integer_singleton);
    RUN(test_random_secure_integer_extreme_range);
    RUN(test_random_secure_integer_invalid_range);
    RUN(test_random_secure_number);
    RUN(test_random_secure_string);
    RUN(test_random_secure_string_empty);
    RUN(test_random_secure_string_negative);
    RUN(test_random_secure_uuid);

    RUN(test_random_module);
    return SUMMARY();
}
