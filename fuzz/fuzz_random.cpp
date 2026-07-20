#include <cstddef>
#include <cstdint>
#include <limits>

#include <fuzzer/FuzzedDataProvider.h>

#include "fuzz_harness.hpp"
#include "runtime/stdlib/system/random_bounded.hpp"

// LibFuzzer entry point for the Random module's bounded-integer core
// (core/runtime/stdlib/system/random_bounded.hpp).
//
// bounded_uniform is the trust-boundary logic behind Random.generate_integer and
// Random.secure_integer: it draws a uniformly distributed integer in a closed
// range [lo, hi] — both supplied by an untrusted Luma program — from a stream of
// 64-bit values, using rejection sampling to remove modulo bias. The arithmetic
// is the dangerous part: an earlier version computed the span as
// static_cast<uint64_t>(hi - lo), which signed-overflows for spans wider than
// INT64_MAX, and took `% (range + 1)`, which divides by zero for the full
// [INT64_MIN, INT64_MAX] span. This target drives the rewritten unsigned core
// across the entire int64 x uint64 input domain so any such defect resurfaces as
// a crash.
//
// The fuzzer chooses lo and hi, then feeds attacker-controlled draws into the
// rejection loop; once the input is exhausted it supplies UINT64_MAX (always
// accepted) so the loop is guaranteed to terminate.
//
// Oracles on top of the never-crash contract:
//   1. Empty range: when lo > hi the draw must be refused (returns false).
//   2. In range: when lo <= hi the draw must succeed and land in [lo, hi].
//   3. Threshold bound: for any span narrower than 2^64 the rejected band is no
//      wider than the span itself (threshold <= range); the full span rejects
//      nothing (threshold 0).
//   4. Total mapping: bounded_map keeps *every* raw value inside [lo, hi], not
//      only the accepted ones, because the modular reduction can never exceed
//      the span.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > luma::fuzz::max_input_size) {
        return 0;
    }

    return luma::fuzz::run([&] {
        FuzzedDataProvider fdp{data, size};

        const auto lo = fdp.ConsumeIntegral<std::int64_t>();
        const auto hi = fdp.ConsumeIntegral<std::int64_t>();

        // Drive the rejection loop with attacker-controlled draws; once the
        // input is spent, hand back UINT64_MAX, which is never rejected, so the
        // loop always halts.
        auto next = [&fdp](std::uint64_t& out) {
            out = (fdp.remaining_bytes() == 0) ? std::numeric_limits<std::uint64_t>::max()
                                               : fdp.ConsumeIntegral<std::uint64_t>();

            return true;
        };

        std::int64_t result{0};
        const bool ok = luma::bounded_uniform(lo, hi, next, result);

        // ── Oracle 1: an empty range must be refused. ──
        if (lo > hi) {
            if (ok) {
                luma::fuzz::trap(); // lo > hi must not yield a value.
            }

            return;
        }

        // ── Oracle 2: a valid range with a terminating generator succeeds and
        // stays inside the closed interval. ──
        if (!ok) {
            luma::fuzz::trap(); // a non-empty range must produce a draw.
        }

        if (result < lo || result > hi) {
            luma::fuzz::trap(); // the draw escaped its closed range.
        }

        const std::uint64_t range = luma::bounded_range(lo, hi);
        const std::uint64_t threshold = luma::bounded_reject_threshold(range);

        // ── Oracle 3: the rejected band never exceeds the span. ──
        if (range != std::numeric_limits<std::uint64_t>::max() && threshold > range) {
            luma::fuzz::trap(); // threshold must be <= range (= span - 1).
        }

        // ── Oracle 4: bounded_map keeps any raw value inside the range. ──
        const auto probe = fdp.ConsumeIntegral<std::uint64_t>();
        const std::int64_t mapped = luma::bounded_map(lo, range, probe);
        if (mapped < lo || mapped > hi) {
            luma::fuzz::trap(); // modular mapping left the range.
        }
    });
}
