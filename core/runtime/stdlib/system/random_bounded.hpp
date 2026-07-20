#ifndef LUMA_STDLIB_RANDOM_BOUNDED_HPP
#define LUMA_STDLIB_RANDOM_BOUNDED_HPP

#include <cstdint>

namespace luma {

// Pure, UB-free helpers for drawing a uniformly distributed integer in a closed
// range [lo, hi] from a source of uniform 64-bit values, using rejection
// sampling to eliminate modulo bias. All arithmetic is performed in unsigned
// 64-bit space so that extreme ranges — including the full
// [INT64_MIN, INT64_MAX] span — are handled without signed overflow or
// division by zero.

// Width of the closed interval [lo, hi] expressed as an unsigned count of steps
// (i.e. hi - lo). Computed via modular unsigned subtraction so it stays well
// defined even when the mathematical difference exceeds INT64_MAX. Assumes
// lo <= hi.
[[nodiscard]] constexpr std::uint64_t bounded_range(std::int64_t lo, std::int64_t hi) {
    return static_cast<std::uint64_t>(hi) - static_cast<std::uint64_t>(lo);
}

// Rejection threshold for an unbiased draw: a raw 64-bit value is accepted when
// it is >= the returned threshold. The rejected low band has size
// (2^64 mod span), where span = range + 1, leaving an exact multiple of span
// acceptable values so that (raw % span) is unbiased. For the full 64-bit range
// (range == UINT64_MAX) the span is 2^64 and no value is rejected.
[[nodiscard]] constexpr std::uint64_t bounded_reject_threshold(std::uint64_t range) {
    const std::uint64_t span = range + 1U; // wraps to 0 for the full range
    if (span == 0U) {
        return 0U;
    }

    return (0U - span) % span; // == 2^64 mod span
}

// Map an accepted raw value into [lo, hi]. For the full range the raw value is
// used directly; otherwise it is reduced modulo span. The final addition is
// performed in unsigned space and reinterpreted, which yields the correct
// signed result across the entire int64 domain.
[[nodiscard]] constexpr std::int64_t bounded_map(std::int64_t lo, std::uint64_t range,
                                                 std::uint64_t raw) {
    const std::uint64_t span = range + 1U;
    const std::uint64_t reduced = (span == 0U) ? raw : (raw % span);

    return static_cast<std::int64_t>(static_cast<std::uint64_t>(lo) + reduced);
}

// Draw a uniformly distributed integer in the closed range [lo, hi].
//
// `next` is invoked as `bool next(std::uint64_t&)`; it must fill its argument
// with a uniformly distributed 64-bit value and return true on success, or
// return false to signal an unrecoverable generator failure (which is then
// propagated as a false return here). On success the drawn value is written to
// `out` and true is returned. Returns false without calling `next` when
// lo > hi.
template <typename NextU64>
[[nodiscard]] bool bounded_uniform(std::int64_t lo, std::int64_t hi, NextU64 next,
                                   std::int64_t& out) {
    if (lo > hi) {
        return false;
    }

    if (lo == hi) {
        out = lo;

        return true;
    }

    const std::uint64_t range = bounded_range(lo, hi);
    const std::uint64_t threshold = bounded_reject_threshold(range);

    std::uint64_t raw{};

    do {
        if (!next(raw)) {
            return false;
        }
    } while (raw < threshold);

    out = bounded_map(lo, range, raw);

    return true;
}

} // namespace luma

#endif // LUMA_STDLIB_RANDOM_BOUNDED_HPP
