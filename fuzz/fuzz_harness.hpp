#ifndef LUMA_FUZZ_FUZZ_HARNESS_HPP
#define LUMA_FUZZ_FUZZ_HARNESS_HPP

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "analysis/errors/error.hpp"

#if !defined(__clang__) && !defined(__GNUC__)
#error "The fuzz harness requires Clang or GCC (it uses GNU inline asm and __builtin_trap)."
#endif

// Shared low-level utilities for Luma fuzz targets.
//
// This header is deliberately free of Luma pipeline types so that every
// target — including the analysis-only ones that link against luma_analysis —
// can include it cheaply.  Pipeline-stage helpers live in fuzz_frontend.hpp
// and fuzz_pipeline.hpp.

namespace luma::fuzz {

// ─── Input size caps ────────────────────────────────────────────────────────
// Upper bounds that keep individual fuzz iterations fast.  Oversized inputs
// are rejected before any work happens.
inline constexpr std::size_t k_max_input_size = 65536;
inline constexpr std::size_t k_max_vm_input_size = 16384;

// Convert raw fuzzer bytes to a std::string.
[[nodiscard]] inline std::string to_string(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> bytes{data, size};
    return std::string(bytes.begin(), bytes.end());
}

// Reinterpret the first eight input bytes as a double — any bit pattern,
// including NaNs, infinities and subnormals — so numeric guards are driven with
// hostile values.  Inputs shorter than a double yield 0.0.
[[nodiscard]] inline double leading_double(const std::uint8_t* data, std::size_t size) {
    std::array<std::uint8_t, sizeof(double)> raw{};
    if (size < raw.size()) {
        return 0.0;
    }
    std::ranges::copy(std::span<const std::uint8_t>{data, raw.size()}, raw.begin());
    return std::bit_cast<double>(raw);
}

// Force the compiler to treat `value` as observed, preventing it from
// optimising away the work that produced it.  Use this instead of a plain
// `(void)value;` cast, which does not guarantee evaluation.
template <typename T> inline void do_not_optimize(const T& value) {
    __asm__ __volatile__("" : : "m"(value) : "memory");
}

// Abort execution at the current point.  Reserved for genuinely unexpected
// conditions so that LibFuzzer records a crashing artifact.
[[noreturn]] inline void trap() {
    __builtin_trap();
}

// Run a fuzz body with the shared exception-handling policy.
//
// Exception contract (see analysis/errors/error.hpp):
//   * luma::InternalError — a built-in failed with an interpreter fault (an
//     escaped std::logic_error, std::bad_variant_access, …) that the VM wrapped
//     so Luma programs can catch it.  That is a bug, so trap.
//   * std::runtime_error — every expected failure derives from it: the VM and
//     stdlib's luma::RuntimeError, the shared parsers' ParseError /
//     JsonParseError, the protocol transport errors, std::filesystem_error,
//     and the SourceManager's file-load failure on the include path.
//   * std::bad_alloc — pathological-but-reachable input can request very
//     large allocations.  Running out of memory is not a logic bug.
//   * anything else — std::logic_error (out_of_range, invalid_argument,
//     length_error, …), std::bad_variant_access, std::bad_optional_access, or a
//     foreign non-standard exception signals a genuine bug; trap so the fuzzer
//     reports it.
template <typename F> [[nodiscard]] int run(F&& body) {
    try {
        std::forward<F>(body)();
    } catch (const luma::InternalError&) {
        trap();
    } catch (const std::runtime_error&) {
        // Expected — malformed input rejected with a recoverable error.
    } catch (const std::bad_alloc&) {
        // Expected — allocation pressure from a pathological input.
    } catch (...) {
        trap();
    }
    return 0;
}

// Run a text-driven fuzz target end to end: reject oversized input, build the
// input string, and invoke `body` with it under the shared run() policy.
//
// This folds the identical size-cap + to_string() prologue shared by every
// string-based LLVMFuzzerTestOneInput into one place, so the oversized-input
// policy (and any future uniform pre/post instrumentation) lives in a single
// helper rather than being copied to each entry point.  `body` is any callable
// taking a `const std::string&`; targets that also need the raw `data` / `size`
// (e.g. to seed a FuzzedDataProvider or read a scalar) capture them from the
// enclosing entry point.
template <typename Body>
[[nodiscard]] inline int run_text(const std::uint8_t* data, std::size_t size, std::size_t max_size,
                                  Body&& body) {
    if (size > max_size) {
        return 0;
    }
    const auto input = to_string(data, size);
    return run([&] { body(input); });
}

} // namespace luma::fuzz

#endif // LUMA_FUZZ_FUZZ_HARNESS_HPP
