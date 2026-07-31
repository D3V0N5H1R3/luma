#include "runtime/stdlib/math/bits_module.hpp"

#include <cstdint>
#include <format>
#include <span>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/native_function_validation.hpp"

namespace luma {

namespace {

// Maximum valid shift distance, matching the former `<<`/`>>` operators and the
// VM's k_max_shift_amount (a 64-bit integer has bits 0..63).
constexpr std::int64_t k_max_shift = 63;

// Validates a shift distance is in [0, 63], throwing a catchable RuntimeError
// otherwise — identical behaviour to the removed shift operators.
std::int64_t require_shift_amount(std::int64_t amount, std::string_view name,
                                  const SourceLocation& loc) {
    if (amount < 0 || amount > k_max_shift) {
        throw RuntimeError{std::format("{}: shift amount {} is out of range", name, amount), loc,
                           std::format("use a shift amount between 0 and {}", k_max_shift)};
    }

    return amount;
}

} // namespace

void register_bits_ns(const EnvPtr& env) {
    ModuleBuilder{"Bits", env} // Bits.and(a, b) → integer — bitwise AND.
        .func("and", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{expect_integer(args[0], "Bits.and", loc) &
                         expect_integer(args[1], "Bits.and", loc)};
        })
        // Bits.or(a, b) → integer — bitwise OR.
        .func("or", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{expect_integer(args[0], "Bits.or", loc) |
                         expect_integer(args[1], "Bits.or", loc)};
        })
        // Bits.xor(a, b) → integer — bitwise XOR.
        .func("xor", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{expect_integer(args[0], "Bits.xor", loc) ^
                         expect_integer(args[1], "Bits.xor", loc)};
        })
        // Bits.not(a) → integer — bitwise NOT (two's-complement complement).
        .func("not", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{~expect_integer(args[0], "Bits.not", loc)};
        })
        // Bits.shift_left(value, amount) → integer — logical left shift.
        .func("shift_left", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto value = expect_integer(args[0], "Bits.shift_left", loc);
            const auto amount = require_shift_amount(
                expect_integer(args[1], "Bits.shift_left", loc), "Bits.shift_left", loc);

            // Shift through the unsigned representation to keep the result well
            // defined (signed left-shift overflow is undefined behaviour); the
            // resulting bit pattern is identical to `value << amount`.
            return Value{static_cast<std::int64_t>(static_cast<std::uint64_t>(value)
                                                   << static_cast<std::uint64_t>(amount))};
        })
        // Bits.shift_right(value, amount) → integer — arithmetic right shift.
        .func("shift_right", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto value = expect_integer(args[0], "Bits.shift_right", loc);
            const auto amount = require_shift_amount(
                expect_integer(args[1], "Bits.shift_right", loc), "Bits.shift_right", loc);

            return Value{value >> amount};
        });
}

} // namespace luma
