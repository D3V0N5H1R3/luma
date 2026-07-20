#pragma once

// Structured Luma-source generator.
//
// Generates syntactically plausible Luma programs from a fuzzer-provided
// stream of bytes, reaching deeper pipeline code paths than raw byte fuzzing.
//
// The generator is templated on a Provider type rather than depending on
// LibFuzzer's FuzzedDataProvider directly.  This decouples it from the fuzzing
// runtime so it can be unit-tested (or reused for corpus pre-seeding) with a
// mock provider.  Provider must offer the subset of the FuzzedDataProvider
// interface used below:
//   * T   ConsumeIntegralInRange<T>(T min, T max)
//   * double ConsumeFloatingPointInRange<double>(double min, double max)
//   * bool ConsumeBool()
//   * std::string ConsumeRandomLengthString(std::size_t max_length)
//   * std::size_t remaining_bytes()
//
// The implementation is split by concern across four headers, re-included here so
// existing includers of this umbrella header are unaffected:
//   * fuzz_gen_vocab.hpp   — token vocabulary, pick, and the simple generate_*
//   * fuzz_gen_grammar.hpp — the recursive, deliberately-ill-typed expression and
//                            statement grammar
//   * fuzz_gen_stdlib.hpp  — the curated, type-correct stdlib feature emitters
//   * fuzz_gen_program.hpp — declaration builders and fixed-prelude assembly

#include "fuzz_gen_grammar.hpp"
#include "fuzz_gen_program.hpp"
#include "fuzz_gen_stdlib.hpp"
#include "fuzz_gen_vocab.hpp"
