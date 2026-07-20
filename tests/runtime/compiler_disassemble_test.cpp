// Disassembler coverage tests — Chunk::disassemble_instruction for every opcode.
//
// The disassembler drives the bytecode dump (luma --dump) and DAP inspection.
// A single regression test in compiler_opcode_test.cpp guards one opcode; this
// suite instead walks *every* defined opcode so no opcode can silently render as
// "UNKNOWN" and every OperandLayout branch (Simple, U8, U16, U32Jump, U32Long,
// TwoU8, MakeClosure, MakeRecord, RecordWith) is exercised. It complements the
// opcode-metadata characterization (which checks the table) by driving the code
// that consumes that table.

#include <sstream>
#include <string>

#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/opcode_metadata.hpp"
#include "runtime/interpreter/value.hpp"
#include "test_framework.hpp"

using namespace luma;

namespace {

// Build a single-instruction chunk: the opcode byte followed by base_size-1
// zero operand bytes. For variable-size opcodes (MakeRecord, RecordWith) the
// embedded count byte is therefore 0, so no trailing field bytes are needed and
// the fixed prefix fully describes the instruction. Constant/name operand index
// 0 is safe: disassemble_instruction bounds-checks the (empty) pools before
// dereferencing, so it simply omits the resolved value.
Chunk single_instruction_chunk(Op op) {
    Chunk chunk;

    chunk.code.push_back(static_cast<std::uint8_t>(op));
    for (std::size_t i = 1; i < opcode_base_size(op); ++i) {
        chunk.code.push_back(0);
    }

    return chunk;
}

} // namespace

// ─── Every opcode disassembles to its mnemonic ───

static void test_every_opcode_renders_its_mnemonic() {
    for (std::size_t raw = 0; raw < opcode_enum_count; ++raw) {
        const auto op = static_cast<Op>(raw);
        const auto chunk = single_instruction_chunk(op);

        std::ostringstream out;
        const std::size_t variable_size = chunk.disassemble_instruction(0, out);
        const std::string listing = out.str();

        // The mnemonic must appear — never the UNKNOWN(n) fallback.
        const std::string_view name = opcode_name(op);
        ASSERT_TRUE(listing.find(std::string(name)) != std::string::npos);
        ASSERT_TRUE(listing.find("UNKNOWN") == std::string::npos);

        // With every embedded count byte zeroed, no opcode has trailing
        // variable-length operands, so the fixed prefix is the whole instruction.
        ASSERT_EQ(variable_size, 0u);
    }
}

// ─── Variable-length operand accounting ───

static void test_make_record_variable_size_tracks_field_count() {
    // MakeRecord: u16 type_name + u8 field_count + field_count * u16 field_names.
    Chunk chunk;
    chunk.code.push_back(static_cast<std::uint8_t>(Op::MakeRecord));
    chunk.code.push_back(0); // type_name hi
    chunk.code.push_back(0); // type_name lo
    chunk.code.push_back(2); // field_count = 2
    for (int i = 0; i < 2 * 2; ++i) {
        chunk.code.push_back(0); // two u16 field-name indices
    }

    std::ostringstream out;
    const std::size_t variable_size = chunk.disassemble_instruction(0, out);

    ASSERT_EQ(variable_size, 4u); // 2 fields * 2 bytes each
    ASSERT_TRUE(out.str().find("MakeRecord") != std::string::npos);
    ASSERT_TRUE(out.str().find("fields=2") != std::string::npos);
}

static void test_record_with_variable_size_tracks_override_count() {
    // RecordWith: u8 override_count + override_count * u16 field_names.
    Chunk chunk;
    chunk.code.push_back(static_cast<std::uint8_t>(Op::RecordWith));
    chunk.code.push_back(3); // override_count = 3
    for (int i = 0; i < 3 * 2; ++i) {
        chunk.code.push_back(0);
    }

    std::ostringstream out;
    const std::size_t variable_size = chunk.disassemble_instruction(0, out);

    ASSERT_EQ(variable_size, 6u); // 3 overrides * 2 bytes each
    ASSERT_TRUE(out.str().find("overrides=3") != std::string::npos);
}

// ─── Operand rendering for specific layouts ───

static void test_constant_operand_value_rendered() {
    // Op::Constant resolves its u16 index against the constant pool.
    Chunk chunk;
    const std::uint16_t index = chunk.add_constant(Value{static_cast<std::int64_t>(7)});

    chunk.code.push_back(static_cast<std::uint8_t>(Op::Constant));
    chunk.code.push_back(static_cast<std::uint8_t>(index >> 8));
    chunk.code.push_back(static_cast<std::uint8_t>(index & 0xFF));

    std::ostringstream out;
    (void)chunk.disassemble_instruction(0, out);

    ASSERT_TRUE(out.str().find("Constant") != std::string::npos);
    ASSERT_TRUE(out.str().find("(7)") != std::string::npos);
}

static void test_global_operand_name_rendered() {
    // Op::GetGlobal resolves its u16 index against the name table.
    Chunk chunk;
    const std::uint16_t index = chunk.add_name("counter");

    chunk.code.push_back(static_cast<std::uint8_t>(Op::GetGlobal));
    chunk.code.push_back(static_cast<std::uint8_t>(index >> 8));
    chunk.code.push_back(static_cast<std::uint8_t>(index & 0xFF));

    std::ostringstream out;
    (void)chunk.disassemble_instruction(0, out);

    ASSERT_TRUE(out.str().find("GetGlobal") != std::string::npos);
    ASSERT_TRUE(out.str().find("(counter)") != std::string::npos);
}

static void test_jump_target_rendered() {
    // Forward jumps render an absolute target: offset + base_size + operand.
    Chunk chunk;
    chunk.code.push_back(static_cast<std::uint8_t>(Op::Jump));
    chunk.code.push_back(0);
    chunk.code.push_back(0);
    chunk.code.push_back(0);
    chunk.code.push_back(4); // +4

    std::ostringstream out;
    (void)chunk.disassemble_instruction(0, out);

    // base_size for Jump is 5, so target = 0 + 5 + 4 = 9.
    ASSERT_TRUE(out.str().find("Jump") != std::string::npos);
    ASSERT_TRUE(out.str().find("(\xe2\x86\x92"
                               "9)") != std::string::npos);
}

// ─── Whole-chunk disassembly ───

static void test_disassemble_full_chunk_has_banner() {
    Chunk chunk;
    chunk.code.push_back(static_cast<std::uint8_t>(Op::None));
    chunk.code.push_back(static_cast<std::uint8_t>(Op::Return));

    const std::string listing = chunk.disassemble("demo");

    ASSERT_TRUE(listing.find("=== demo ===") != std::string::npos);
    ASSERT_TRUE(listing.find("None") != std::string::npos);
    ASSERT_TRUE(listing.find("Return") != std::string::npos);
}

int main() {
    luma::test::print_suite_header("Compiler — Disassembler Coverage");

    RUN(test_every_opcode_renders_its_mnemonic);

    RUN(test_make_record_variable_size_tracks_field_count);
    RUN(test_record_with_variable_size_tracks_override_count);

    RUN(test_constant_operand_value_rendered);
    RUN(test_global_operand_name_rendered);
    RUN(test_jump_target_rendered);

    RUN(test_disassemble_full_chunk_has_banner);

    return SUMMARY();
}
