#ifndef LUMA_COMPILER_OPTIMIZER_INTERNAL_HPP
#define LUMA_COMPILER_OPTIMIZER_INTERNAL_HPP

// Internal header shared across optimizer pass implementation files.
// Not part of the public API — only included by optimizer_*.cpp files.

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

#include "common/byte_utils.hpp"
#include "common/overflow.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/opcode_metadata.hpp"
#include "runtime/compiler/optimizer.hpp"

namespace luma {

// Sentinel byte value used by the optimizer to mark dead/nop bytes during
// optimisation passes.  The compaction pass later removes these bytes and
// fixes up jump offsets.
constexpr std::uint8_t k_dead_byte = 0xFF;

// Returns true if the byte at the given position is a dead-byte sentinel.
[[nodiscard]] inline bool is_dead_byte(std::uint8_t byte) noexcept {
    return byte == k_dead_byte;
}

// ─── Shared bytecode manipulation helpers ───
// Operand read/write helpers live in the optimizer_util namespace below.

// Replace a range of bytes with the dead-byte sentinel.
inline void nop_out(std::vector<std::uint8_t>& code, std::size_t start, std::size_t length) {
    if (start >= code.size()) {
        return;
    }
    std::ranges::fill(std::span{code}.subspan(start, std::min(length, code.size() - start)),
                      k_dead_byte);
}

// ─── Operand I/O helpers ───
// Centralised read/write helpers used by all optimizer passes.
// Eliminates repeated inline shifts and pointer casts.

namespace optimizer_util {

/// Read a big-endian u16 operand at code[offset].
[[nodiscard]] inline std::uint16_t read_u16(const std::vector<std::uint8_t>& code,
                                            std::size_t offset) {
    return read_u16_be(&code[offset]);
}

/// Read a big-endian u32 operand at code[offset].
[[nodiscard]] inline std::uint32_t read_u32(const std::vector<std::uint8_t>& code,
                                            std::size_t offset) {
    return read_u32_be(&code[offset]);
}

/// Write a big-endian u16 operand to code[offset].
inline void write_u16(std::vector<std::uint8_t>& code, std::size_t offset, std::uint16_t value) {
    patch_u16_be(&code[offset], value);
}

/// Write a big-endian u32 operand to code[offset].
inline void write_u32(std::vector<std::uint8_t>& code, std::size_t offset, std::uint32_t value) {
    patch_u32_be(&code[offset], value);
}

/// Return true if code[offset .. offset+size) is within bounds.
[[nodiscard]] inline bool in_bounds(const std::vector<std::uint8_t>& code, std::size_t offset,
                                    std::size_t size) {
    return luma::in_bounds(code, offset, size);
}

} // namespace optimizer_util

// ─── BytecodeWalker ───
// Encapsulates the common `for (i = 0; i < code.size(); i += instruction_size(code, i))`
// pattern, with optional dead-byte skipping for optimizer passes.

class BytecodeWalker {
public:
    explicit BytecodeWalker(const std::vector<std::uint8_t>& code) : code_(code) {}

    // Walk all instructions, calling fn(offset, op) for each.
    // Advances by instruction_size automatically.
    template <typename Fn> void walk(Fn&& fn) const {
        for (std::size_t i = 0; i < code_.size(); i += instruction_size(code_, i)) {
            fn(i, static_cast<Op>(code_[i]));
        }
    }

    // Walk live instructions only, skipping dead-byte sentinels.
    template <typename Fn> void walk_live(Fn&& fn) const {
        for (std::size_t i = 0; i < code_.size();) {
            if (is_dead_byte(code_[i])) {
                ++i;
                continue;
            }
            const auto op = static_cast<Op>(code_[i]);
            fn(i, op);
            i += instruction_size(code_, i);
        }
    }

private:
    const std::vector<std::uint8_t>& code_;
};

// ─── Jump-target collection ───

// Collect the set of byte offsets that are the destination of any jump in the
// chunk (forward jumps and backward Loop).  Dead-byte sentinels (from earlier
// optimisation passes within the same optimize() run) are skipped one byte at
// a time so operand bytes are never misread as instruction boundaries.
//
// Two-instruction peepholes must not fuse/remove an instruction that a jump can
// land on: the fused form assumes the first instruction always executes
// immediately before the second.  Short-circuit operators (&&, ||, ??) emit an
// exit jump that lands exactly on the instruction following the right operand,
// so fusing e.g. `Less; Not` across that boundary would drop the Not on the
// short-circuit path.
[[nodiscard]] inline std::unordered_set<std::size_t>
collect_jump_targets(const std::vector<std::uint8_t>& code) {
    std::unordered_set<std::size_t> targets;
    for (std::size_t i = 0; i < code.size();) {
        if (is_dead_byte(code[i])) {
            ++i;
            continue;
        }

        const auto op = static_cast<Op>(code[i]);

        if (is_forward_jump(op)) {
            if (i + InstructionLayout::k_u32_operand_size < code.size()) {
                const auto offset = static_cast<std::size_t>(optimizer_util::read_u32(code, i + 1));
                targets.insert(i + InstructionLayout::k_jump_instruction_size + offset);
            }
        } else if (op == Op::Loop) {
            if (i + InstructionLayout::k_u32_operand_size < code.size()) {
                const auto offset = static_cast<std::size_t>(optimizer_util::read_u32(code, i + 1));
                if (offset <= i + InstructionLayout::k_jump_instruction_size) {
                    targets.insert(i + InstructionLayout::k_jump_instruction_size - offset);
                }
            }
        }

        i += instruction_size(code, i);
    }
    return targets;
}

} // namespace luma

#endif // LUMA_COMPILER_OPTIMIZER_INTERNAL_HPP
