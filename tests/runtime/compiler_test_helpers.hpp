// Shared helpers for compiler unit tests.
// See stdlib_test_helpers.hpp header comment for why these helpers are not unified.
//
// Provides opcode-inspection utilities so that compiler tests (and
// future optimisation tests) can reuse the same boilerplate.

#ifndef LUMA_COMPILER_TEST_HELPERS_HPP
#define LUMA_COMPILER_TEST_HELPERS_HPP

#include <cstddef>
#include <cstdint>

#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/opcode.hpp"

namespace luma::test {

// Check whether a chunk contains a specific opcode.
[[nodiscard]] inline bool has_opcode(const Chunk& chunk, Op op) {
    for (std::size_t i{0}; i < chunk.code.size(); ++i) {
        if (static_cast<Op>(chunk.code[i]) == op) {
            return true;
        }
    }

    return false;
}

// Count how many times a specific opcode appears in a chunk.
[[nodiscard]] inline std::size_t count_opcode(const Chunk& chunk, Op op) {
    std::size_t count = 0;

    for (std::size_t i = 0; i < chunk.code.size(); ++i) {
        if (static_cast<Op>(chunk.code[i]) == op) {
            ++count;
        }
    }

    return count;
}

} // namespace luma::test

#endif // LUMA_COMPILER_TEST_HELPERS_HPP
