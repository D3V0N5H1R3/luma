// Shared helpers for the optimizer_test_* unit tests.

#ifndef LUMA_OPTIMIZER_TEST_HELPERS_HPP
#define LUMA_OPTIMIZER_TEST_HELPERS_HPP

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "runtime/compiler/chunk.hpp"

namespace luma::test {

[[nodiscard]] inline Chunk make_chunk(std::initializer_list<std::uint8_t> bytes) {
    Chunk chunk;
    chunk.code = std::vector<std::uint8_t>(bytes);
    return chunk;
}

} // namespace luma::test

#endif // LUMA_OPTIMIZER_TEST_HELPERS_HPP
