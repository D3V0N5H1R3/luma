#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <new>
#include <string>
#include <utility>

#include "analysis/errors/error.hpp"

#if !(defined(__clang__) || defined(__GNUC__))
#include <atomic>
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
inline constexpr std::size_t max_input_size = 65536;
inline constexpr std::size_t max_vm_input_size = 16384;

// Convert raw fuzzer bytes to a std::string.
[[nodiscard]] inline std::string to_string(const std::uint8_t* data, std::size_t size) {
    return std::string(reinterpret_cast<const char*>(data), size);
}

// Force the compiler to treat `value` as observed, preventing it from
// optimising away the work that produced it.  Use this instead of a plain
// `(void)value;` cast, which does not guarantee evaluation.
#if defined(__clang__) || defined(__GNUC__)
template <typename T> inline void do_not_optimize(const T& value) {
    __asm__ __volatile__("" : : "m"(value) : "memory");
}
#else
template <typename T> inline void do_not_optimize(const T& value) {
    const volatile char& sink = reinterpret_cast<const volatile char&>(value);
    (void)sink;
    std::atomic_signal_fence(std::memory_order_acq_rel);
}
#endif

// Abort execution at the current point.  Reserved for genuinely unexpected
// conditions so that LibFuzzer records a crashing artifact.
[[noreturn]] inline void trap() {
#if defined(_MSC_VER)
    __debugbreak();
#else
    __builtin_trap();
#endif
    std::abort();
}

// Run a fuzz body with the shared exception-handling policy.
//
// Exception contract (see analysis/errors/error.hpp):
//   * luma::RuntimeError — the sole runtime exception type raised by the VM
//     and standard library (division by zero, out-of-range indexing,
//     exceeded resource limits, …).  Expected for malformed input.
//   * std::bad_alloc — pathological-but-reachable input can request very
//     large allocations.  Running out of memory is not a logic bug.
//   * other std::exception — tolerated, because a few front-end stages throw
//     on pathological input that is nonetheless reachable: NameResolver raises
//     std::runtime_error when a scope exceeds the local-variable slot limit,
//     and the include path's SourceManager raises std::runtime_error on a
//     file-load failure.  (The lexer and type checker guard their own
//     std::stoll / std::stoi conversions, so those never escape.)  Trapping
//     on the generic type would therefore produce false positives.
//   * anything else — a foreign, non-standard exception escaped, which
//     indicates memory corruption or a genuine bug; trap so the fuzzer
//     reports it.
template <typename F> [[nodiscard]] int run(F&& body) {
    try {
        std::forward<F>(body)();
    } catch (const luma::RuntimeError&) {
        // Expected — language-level runtime failure.
    } catch (const std::bad_alloc&) {
        // Expected — allocation pressure from a pathological input.
    } catch (const std::exception&) {
        // Tolerated — see the contract above for the known throw sites.
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
// (e.g. to seed a FuzzedDataProvider or memcpy a scalar) capture them from the
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
