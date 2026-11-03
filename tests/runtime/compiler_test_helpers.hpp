// Shared helpers for compiler and optimizer unit tests.
// See stdlib_test_helpers.hpp header comment for why these helpers are not unified.
//
// Provides compilation shortcuts and opcode-inspection utilities. The opcode
// helpers decode the chunk instruction by instruction (instruction_size()), so
// an operand byte that happens to equal an opcode's value is never mistaken for
// that opcode.

#ifndef LUMA_COMPILER_TEST_HELPERS_HPP
#define LUMA_COMPILER_TEST_HELPERS_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/byte_utils.hpp"
#include "lex_parse_util.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/opcode_metadata.hpp"
#include "test_framework.hpp"

namespace luma::test {

// ─── Compilation ───

// Compile `source` the way the REPL and eval() do (top-level expression
// statements keep their value). Throws if `source` has a lexer or parser error.
[[nodiscard]] inline CompileResult compile(const std::string& source) {
    const auto program = lex_and_parse_valid(source);

    Compiler compiler;

    return compiler.compile(program, /*repl_mode=*/true);
}

// Compile `source` as a script file (not REPL mode). Throws if `source` has a
// lexer or parser error.
[[nodiscard]] inline CompileResult compile_script(const std::string& source) {
    const auto program = lex_and_parse_valid(source);

    Compiler compiler;

    return compiler.compile(program, /*repl_mode=*/false);
}

// ─── Opcode inspection ───

// One decoded instruction: its opcode and the byte offset where it starts.
struct DecodedInstruction {
    Op op;
    std::size_t offset;
};

// Decode every instruction in a chunk, stepping over operands.
[[nodiscard]] inline std::vector<DecodedInstruction> decode_instructions(const Chunk& chunk) {
    std::vector<DecodedInstruction> instructions;

    for (std::size_t offset = 0; offset < chunk.code.size();
         offset += instruction_size(chunk.code, offset)) {
        instructions.push_back({static_cast<Op>(chunk.code[offset]), offset});
    }

    return instructions;
}

// Check whether a chunk contains a specific opcode.
[[nodiscard]] inline bool has_opcode(const Chunk& chunk, Op op) {
    return std::ranges::any_of(decode_instructions(chunk),
                               [op](const DecodedInstruction& insn) { return insn.op == op; });
}

// Count how many times a specific opcode appears in a chunk.
[[nodiscard]] inline std::size_t count_opcode(const Chunk& chunk, Op op) {
    return static_cast<std::size_t>(
        std::ranges::count_if(decode_instructions(chunk),
                              [op](const DecodedInstruction& insn) { return insn.op == op; }));
}

// Byte offsets of every instruction with the given opcode.
[[nodiscard]] inline std::vector<std::size_t> opcode_offsets(const Chunk& chunk, Op op) {
    std::vector<std::size_t> offsets;

    for (const auto& insn : decode_instructions(chunk)) {
        if (insn.op == op) {
            offsets.push_back(insn.offset);
        }
    }

    return offsets;
}

// Check whether the top-level chunk or any compiled function contains `op`.
[[nodiscard]] inline bool any_function_has_opcode(const CompileResult& result, Op op) {
    return has_opcode(result.top_level.chunk(), op) ||
           std::ranges::any_of(result.functions,
                               [op](const auto& fn) { return has_opcode(fn.chunk(), op); });
}

// Read the big-endian u16 operand that starts at `offset`.
[[nodiscard]] inline std::uint16_t read_u16_operand(const Chunk& chunk, std::size_t offset) {
    if (offset + 2 > chunk.code.size()) {
        throw_assertion_error("read_u16_operand: operand at offset " + std::to_string(offset) +
                              " runs past the end of the chunk");
    }

    return read_u16_be(&chunk.code[offset]);
}

} // namespace luma::test

#endif // LUMA_COMPILER_TEST_HELPERS_HPP
