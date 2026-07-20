#include "runtime/compiler/chunk.hpp"

#include <format>
#include <sstream>

#include "common/byte_utils.hpp"
#include "runtime/compiler/opcode.hpp"

namespace luma {

namespace {

// Read helpers — delegate to the shared byte_utils.hpp functions
// to eliminate duplication with optimizer_internal.hpp.
[[nodiscard]] std::uint16_t read_u16_at(const std::vector<std::uint8_t>& code, std::size_t offset) {
    return read_u16_be(&code[offset]);
}

[[nodiscard]] std::uint32_t read_u32_at(const std::vector<std::uint8_t>& code, std::size_t offset) {
    return read_u32_be(&code[offset]);
}

} // namespace

// Disassemble a single instruction at the given offset.
// Returns the number of variable-size trailing bytes beyond the fixed prefix.
std::size_t Chunk::disassemble_instruction(std::size_t offset, std::ostringstream& out) const {
    const auto op = static_cast<Op>(code[offset]);
    const auto base_size = opcode_base_size(op);
    std::size_t variable_size = 0;

    const auto* info = find_opcode_info(op);
    if (info == nullptr) {
        out << std::format("UNKNOWN({})\n", static_cast<int>(op));
        return 0;
    }

    // Operand decoding is selected from the opcode table's layout column;
    // per-opcode value display (constant/name resolution, jump targets) is
    // still specialised within the relevant layout cases.
    switch (info->operand_layout) {
        case OperandLayout::Simple:
            out << opcode_name(op) << "\n";
            break;

        case OperandLayout::U8: {
            auto operand = code[offset + 1];
            out << std::format("{:<20s} {}\n", opcode_name(op), operand);
            break;
        }

        // RecordWith: u8 override_count + override_count * u16 field_names
        case OperandLayout::RecordWith: {
            auto override_count = code[offset + 1];
            out << std::format("{:<20s} overrides={}\n", opcode_name(op), override_count);
            variable_size = static_cast<std::size_t>(override_count) * 2;
            break;
        }

        case OperandLayout::U16: {
            auto operand = read_u16_at(code, offset + 1);

            out << std::format("{:<20s} {}", opcode_name(op), operand);

            // Show constant value or name if applicable.
            if (op == Op::Constant && operand < constants.size()) {
                out << " (" << constants[operand].to_string() << ")";
            } else if ((op == Op::GetGlobal || op == Op::SetGlobal || op == Op::GetField ||
                        op == Op::SetField || op == Op::GetFieldOpt || op == Op::IsType ||
                        op == Op::Downcast || op == Op::TrustedDowncast) &&
                       operand < names.size()) {
                out << " (" << names[operand] << ")";
            }

            out << "\n";
            break;
        }

        // Jump instructions — u32 branch offset shown as an absolute target.
        case OperandLayout::U32Jump: {
            const auto operand = read_u32_at(code, offset + 1);
            const auto target = static_cast<std::ptrdiff_t>(offset + base_size) +
                                (op == Op::Loop ? -static_cast<std::ptrdiff_t>(operand)
                                                : static_cast<std::ptrdiff_t>(operand));
            out << std::format("{:<20s} +{} (→{})\n", opcode_name(op), operand, target);
            break;
        }

        // MakeClosure: u16 func_index + u8 upvalue_count
        case OperandLayout::MakeClosure: {
            auto func_idx = read_u16_at(code, offset + 1);
            auto upvalue_count = code[offset + 3];
            out << std::format("{:<20s} func={} upvalues={}\n", opcode_name(op), func_idx,
                               upvalue_count);
            break;
        }

        // MakeRecord: u16 type_name + u8 field_count + field_count * u16 field_names
        case OperandLayout::MakeRecord: {
            auto type_name_idx = read_u16_at(code, offset + 1);
            auto field_count = code[offset + 3];

            out << std::format("{:<20s} type={}", opcode_name(op), type_name_idx);

            if (type_name_idx < names.size()) {
                out << " (" << names[type_name_idx] << ")";
            }

            out << std::format(" fields={}\n", field_count);
            variable_size = static_cast<std::size_t>(field_count) * 2;
            break;
        }

        // ConstantLong: u32 constant-pool index.
        case OperandLayout::U32Long: {
            const auto operand = read_u32_at(code, offset + 1);
            out << std::format("{:<20s} {}\n", opcode_name(op), operand);
            break;
        }

        // CallNamed: two u8 operands (positional + named counts).
        case OperandLayout::TwoU8: {
            auto pos_count = code[offset + 1];
            auto named_count = code[offset + 2];
            out << std::format("{:<20s} pos={} named={}\n", opcode_name(op), pos_count,
                               named_count);
            break;
        }
    }

    return variable_size;
}

std::string Chunk::disassemble(const std::string& name) const {
    std::ostringstream out;

    out << "=== " << name << " ===\n";

    std::size_t offset = 0;

    while (offset < code.size()) {
        out << std::format("{:04d} ", offset);

        const auto op = static_cast<Op>(code[offset]);
        const auto base_size = opcode_base_size(op);
        const auto variable_size = disassemble_instruction(offset, out);

        offset += base_size + variable_size;
    }

    return out.str();
}

} // namespace luma
