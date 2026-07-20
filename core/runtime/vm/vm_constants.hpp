// ─────────────────────────────────────────────────────────────────────────────
// VM Constants
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Centralise named numeric constants used throughout the VM
// module, replacing scattered magic literals with self-documenting names.
//
// All constants are static constexpr members of the VMConstants struct so
// they can be used in template parameters, array sizes, and other
// compile-time contexts.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_RUNTIME_VM_VM_CONSTANTS_HPP
#define LUMA_RUNTIME_VM_VM_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>

namespace luma {

struct VMConstants {
    // ─── Byte shift amounts for big-endian instruction decoding ──────────
    // Used to unpack multi-byte operands from the bytecode stream:
    //   value = (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3.
    static constexpr int k_byte_shift_8{8};
    static constexpr int k_byte_shift_16{16};
    static constexpr int k_byte_shift_24{24};

    // ─── Shift operation limits ─────────────────────────────────────────
    // Maximum valid shift amount for ShiftLeft / ShiftRight opcodes.
    // Luma integers are 64-bit, so valid shifts are 0..63.
    static constexpr std::int64_t k_max_shift_amount{63};

    // ─── Stack monitoring ───────────────────────────────────────────────
    // Fraction of stack capacity at which a usage warning is emitted.
    static constexpr double k_stack_warning_threshold{0.9};

    // ─── SmallVector inline capacity ────────────────────────────────────
    // Default number of elements stored inline (on the stack) before
    // SmallVector falls back to heap allocation.  Tuned for common
    // small-arity function calls and short name/field lists.
    static constexpr std::size_t k_small_vector_capacity{8};

    // ─── Tuple type parsing ─────────────────────────────────────────────
    // Reserve hint for the element-type vector when parsing tuple type
    // patterns like "(integer,string,boolean)".
    static constexpr std::size_t k_tuple_type_reserve{8};

    // ─── Concurrency ────────────────────────────────────────────────────
    // Initial value for the monotonically increasing task ID counter.
    // Starts at 2 because task ID 1 is reserved for the main thread.
    static constexpr int k_initial_task_id{2};

    // ─── Iterator state tuple indices ───────────────────────────────────
    // Named indices into the (iterable, index) iterator state tuple
    // constructed by handle_for_iter_init().  See the Iterator protocol
    // comment in vm_dispatch_concurrency.cpp.
    static constexpr std::size_t k_iter_state_iterable{0};
    static constexpr std::size_t k_iter_state_index{1};
};

} // namespace luma

#endif // LUMA_RUNTIME_VM_VM_CONSTANTS_HPP
