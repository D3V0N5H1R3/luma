#ifndef LUMA_FUZZ_FUZZ_SOURCE_GENERATOR_HPP
#define LUMA_FUZZ_FUZZ_SOURCE_GENERATOR_HPP

// Structured Luma-source generator.
//
// Generates Luma programs from a fuzzer-provided stream of bytes, reaching
// deeper pipeline code paths than raw byte fuzzing.  Each program is either
// well-typed (it reaches the compiler, optimiser, verifier and VM) or chaos
// (deliberately ill-typed, stressing parser recovery and the type checker).
//
// The generator is templated on a Provider type rather than depending on
// LibFuzzer's FuzzedDataProvider directly.  This decouples it from the fuzzing
// runtime so it can be unit-tested (tests/integration/fuzz_generator_test.cpp
// drives it with a deterministic provider) or reused for corpus pre-seeding.
// Provider must offer the subset of the FuzzedDataProvider interface used:
//   * T   ConsumeIntegralInRange<T>(T min, T max)
//   * double ConsumeFloatingPointInRange<double>(double min, double max)
//   * bool ConsumeBool()
//   * std::string ConsumeRandomLengthString(std::size_t max_length)
//   * std::size_t remaining_bytes()
//
// The implementation is split by concern across these headers, re-included here
// so includers only need this umbrella header:
//   * fuzz_gen_vocab.hpp    — token vocabulary, pick / pick_kind, literals
//   * fuzz_gen_features.hpp — type-correct language-feature emitters
//   * fuzz_gen_stdlib.hpp   — type-correct standard-library emitters
//   * fuzz_gen_grammar.hpp  — the deliberately-ill-typed chaos grammar
//   * fuzz_gen_typed.hpp    — the well-typed expression and statement grammar
//   * fuzz_gen_program.hpp  — preludes, declarations and program assembly

#include "fuzz_gen_features.hpp"
#include "fuzz_gen_grammar.hpp"
#include "fuzz_gen_program.hpp"
#include "fuzz_gen_stdlib.hpp"
#include "fuzz_gen_typed.hpp"
#include "fuzz_gen_vocab.hpp"

#endif // LUMA_FUZZ_FUZZ_SOURCE_GENERATOR_HPP
