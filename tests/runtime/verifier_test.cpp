// Bytecode verifier unit tests.

#include <cstdint>
#include <string>
#include <vector>

#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/verifier.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Helpers ───

static CompiledFunction make_func(std::initializer_list<std::uint8_t> bytes, int arity = 0) {
    CompiledFunction func;
    func.mutable_chunk().code = std::vector<std::uint8_t>(bytes);
    func.arity = arity;
    return func;
}

static bool has_error_containing(const std::vector<BytecodeVerifier::VerifyError>& errors,
                                 const std::string& substr) {
    for (const auto& err : errors) {
        if (err.message.find(substr) != std::string::npos) {
            return true;
        }
    }

    return false;
}

// ─── Valid bytecode passes verification ───

static void test_valid_simple_bytecode() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_TRUE(errors.empty());
}

static void test_valid_constant_bytecode() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::Constant),
        0x00,
        0x00,
        static_cast<std::uint8_t>(Op::Return),
    });

    // Add one constant to the pool.
    func.mutable_chunk().constants.push_back(Value{static_cast<std::int64_t>(42)});

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_TRUE(errors.empty());
}

// ─── Empty chunk ───

static void test_empty_chunk_error() {
    CompiledFunction func;

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "empty"));
}

// ─── Invalid opcode ───

static void test_invalid_opcode_byte() {
    auto func = make_func({0xFF}); // No valid opcode has value 0xFF.

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "invalid opcode"));
}

// ─── Truncated instruction ───

static void test_truncated_constant_instruction() {
    // Constant needs 3 bytes but we only provide 2.
    auto func = make_func({
        static_cast<std::uint8_t>(Op::Constant),
        0x00,
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "truncated"));
}

// ─── Constant index out of bounds ───

static void test_constant_index_out_of_bounds() {
    // Reference constant at index 5, but pool is empty.
    auto func = make_func({
        static_cast<std::uint8_t>(Op::Constant),
        0x00,
        0x05,
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "constant index"));
}

// ─── Jump target out of bounds ───

static void test_jump_target_out_of_bounds() {
    // Jump with offset that goes past the end of the code.
    auto func = make_func({
        static_cast<std::uint8_t>(Op::Jump),
        0x00,
        0x00,
        0x00,
        0xFF, // offset=255, target=5+255=260 > code_size=6
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "jump"));
}

// ─── Valid jump target ───

static void test_valid_jump_target() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::Jump),
        0x00,
        0x00,
        0x00,
        0x01,                                  // jump +1 → offset 6
        static_cast<std::uint8_t>(Op::True),   // offset 5 (skipped)
        static_cast<std::uint8_t>(Op::Return), // offset 6 (target)
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    // Filter out only jump-related errors (stack depth may report issues
    // because Return at end doesn't push a return value).
    bool jump_error = has_error_containing(errors, "jump");
    ASSERT_FALSE(jump_error);
}

// ─── Loop underflow ───

static void test_loop_underflow() {
    // Loop with offset larger than the current position.
    auto func = make_func({
        static_cast<std::uint8_t>(Op::Loop),
        0x00,
        0x00,
        0x00,
        0xFF, // offset=255 but we're at offset 0+5=5
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "loop"));
}

// ─── Name table index out of bounds ───

static void test_name_index_out_of_bounds() {
    // GetGlobal references name index 5 but name table is empty.
    auto func = make_func({
        static_cast<std::uint8_t>(Op::GetGlobal),
        0x00,
        0x05,
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "name index"));
}

// ─── Valid name table reference ───

static void test_valid_name_reference() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::GetGlobal),
        0x00,
        0x00,
        static_cast<std::uint8_t>(Op::Return),
    });

    (void)func.mutable_chunk().names.add("my_var");

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    bool name_error = has_error_containing(errors, "name index");
    ASSERT_FALSE(name_error);
}

// ─── Stack underflow ───

static void test_stack_underflow() {
    // Pop with nothing on the stack.
    auto func = make_func({
        static_cast<std::uint8_t>(Op::Pop),
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "stack underflow"));
}

// ─── Stack depth with arity ───

static void test_stack_depth_with_arity() {
    // A function with arity 2 starts with 2 items on the stack.
    // Pop twice should be fine.
    auto func = make_func(
        {
            static_cast<std::uint8_t>(Op::Pop),
            static_cast<std::uint8_t>(Op::Pop),
            static_cast<std::uint8_t>(Op::Return),
        },
        2);

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    // No stack underflow expected.
    bool underflow = has_error_containing(errors, "stack underflow");
    ASSERT_FALSE(underflow);
}

// ═══════════════════════════════════════════════════════════
// Additional robustness tests
// ═══════════════════════════════════════════════════════════

// ─── Valid multi-instruction sequence ───

static void test_valid_multi_instruction_sequence() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::False),
        static_cast<std::uint8_t>(Op::And),
        static_cast<std::uint8_t>(Op::Not),
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_TRUE(errors.empty());
}

// ─── Valid conditional jump ───

static void test_conditional_jump_valid() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::JumpIfFalse),
        0x00,
        0x00,
        0x00,
        0x01,                                  // jump +1 → offset 7
        static_cast<std::uint8_t>(Op::True),   // offset 6
        static_cast<std::uint8_t>(Op::Return), // offset 7 (target)
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    bool jump_error = has_error_containing(errors, "jump");
    ASSERT_FALSE(jump_error);
}

// ─── Valid TryCatch ───

static void test_try_catch_valid() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::TryCatch),
        0x00,
        0x00,
        0x00,
        0x02,                                  // catch at offset 5+2=7
        static_cast<std::uint8_t>(Op::True),   // try body (offset 5)
        static_cast<std::uint8_t>(Op::TryEnd), // end try (offset 6)
        static_cast<std::uint8_t>(Op::Return), // catch/after (offset 7)
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    bool jump_error = has_error_containing(errors, "jump");
    ASSERT_FALSE(jump_error);
}

// ─── Valid multiple constants ───

static void test_multiple_constants_valid() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::Constant),
        0x00,
        0x00,
        static_cast<std::uint8_t>(Op::Constant),
        0x00,
        0x01,
        static_cast<std::uint8_t>(Op::Add),
        static_cast<std::uint8_t>(Op::Return),
    });

    func.mutable_chunk().constants.push_back(Value{static_cast<std::int64_t>(10)});
    func.mutable_chunk().constants.push_back(Value{static_cast<std::int64_t>(20)});

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    bool const_error = has_error_containing(errors, "constant");
    ASSERT_FALSE(const_error);
}

// ─── ConstantLong out of bounds ───

static void test_constant_long_out_of_bounds() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::ConstantLong),
        0x00,
        0x00,
        0x00,
        0x05,
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "constant"));
}

// ─── Multiple name ops valid ───

static void test_multiple_name_ops_valid() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::GetGlobal),
        0x00,
        0x00,
        static_cast<std::uint8_t>(Op::GetField),
        0x00,
        0x01,
        static_cast<std::uint8_t>(Op::Return),
    });

    (void)func.mutable_chunk().names.add("my_module");
    (void)func.mutable_chunk().names.add("field_name");

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    bool name_error = has_error_containing(errors, "name index");
    ASSERT_FALSE(name_error);
}

// ─── Stack push/pop balanced ───

static void test_stack_push_pop_balanced() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::False),
        static_cast<std::uint8_t>(Op::Pop),
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    bool underflow = has_error_containing(errors, "stack underflow");
    ASSERT_FALSE(underflow);
}

// ─── Stack depth with arithmetic ───

static void test_stack_arithmetic_depth() {
    // Push two values, add them (net: +1), then return.
    auto func = make_func({
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::Add),
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    bool underflow = has_error_containing(errors, "stack underflow");
    ASSERT_FALSE(underflow);
}

// ─── Stack depth with MakeArray ───

static void test_make_array_stack_effect() {
    // Push 3 elements, make array (3 → 1).
    auto func = make_func({
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::One),
        static_cast<std::uint8_t>(Op::MakeArray),
        0x00,
        0x03, // 3 elements
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    bool underflow = has_error_containing(errors, "stack underflow");
    ASSERT_FALSE(underflow);
}

// ─── Stack depth with Call ───

static void test_call_stack_effect() {
    // Call with 0 args: pops callee, pushes result → net 0.
    auto func = make_func({
        static_cast<std::uint8_t>(Op::True), // push callee
        static_cast<std::uint8_t>(Op::Call),
        0x00, // call with 0 args
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    // Should not underflow (callee is consumed, result pushed).
    bool underflow = has_error_containing(errors, "stack underflow");
    ASSERT_FALSE(underflow);
}

// ─── Truncated jump instruction ───

static void test_truncated_jump_instruction() {
    // Jump needs 3 bytes but only 2 provided.
    auto func = make_func({
        static_cast<std::uint8_t>(Op::Jump),
        0x00,
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "truncated"));
}

// ─── Field name index out of bounds ───

static void test_field_name_out_of_bounds() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::GetField),
        0x00,
        0x05, // name index 5
        static_cast<std::uint8_t>(Op::Return),
    });

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "name index"));
}

// ─── Upvalue index out of bounds ───

static void test_upvalue_index_out_of_bounds() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::GetUpvalue),
        0x00,
        0x05, // upvalue index 5
        static_cast<std::uint8_t>(Op::Return),
    });

    func.upvalue_count = 2; // Only 2 upvalues.

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "upvalue index"));
}

// ─── Valid upvalue index ───

static void test_valid_upvalue_index() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::GetUpvalue),
        0x00,
        0x01, // upvalue index 1
        static_cast<std::uint8_t>(Op::Return),
    });

    func.upvalue_count = 3; // 3 upvalues available (0, 1, 2).

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    bool upvalue_error = has_error_containing(errors, "upvalue index");
    ASSERT_FALSE(upvalue_error);
}

// ─── SetUpvalue index out of bounds ───

static void test_set_upvalue_out_of_bounds() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::SetUpvalue),
        0x00,
        0x03, // upvalue index 3
        static_cast<std::uint8_t>(Op::Return),
    });

    func.upvalue_count = 1; // Only 1 upvalue.

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "upvalue index"));
}

// ─── Zero upvalues with upvalue access ───

static void test_upvalue_access_with_zero_upvalues() {
    auto func = make_func({
        static_cast<std::uint8_t>(Op::GetUpvalue),
        0x00,
        0x00, // upvalue index 0
        static_cast<std::uint8_t>(Op::Return),
    });

    func.upvalue_count = 0; // No upvalues at all.

    BytecodeVerifier verifier;
    auto errors = verifier.verify(func);

    ASSERT_FALSE(errors.empty());
    ASSERT_TRUE(has_error_containing(errors, "upvalue index"));
}

// ─── main ───

int main() {
    // Valid bytecode.
    RUN(test_valid_simple_bytecode);
    RUN(test_valid_constant_bytecode);

    // Error cases.
    RUN(test_empty_chunk_error);
    RUN(test_invalid_opcode_byte);
    RUN(test_truncated_constant_instruction);
    RUN(test_constant_index_out_of_bounds);
    RUN(test_jump_target_out_of_bounds);
    RUN(test_valid_jump_target);
    RUN(test_loop_underflow);
    RUN(test_name_index_out_of_bounds);
    RUN(test_valid_name_reference);
    RUN(test_stack_underflow);
    RUN(test_stack_depth_with_arity);

    // Additional robustness tests.
    RUN(test_valid_multi_instruction_sequence);
    RUN(test_conditional_jump_valid);
    RUN(test_try_catch_valid);
    RUN(test_multiple_constants_valid);
    RUN(test_constant_long_out_of_bounds);
    RUN(test_multiple_name_ops_valid);
    RUN(test_stack_push_pop_balanced);
    RUN(test_stack_arithmetic_depth);
    RUN(test_make_array_stack_effect);
    RUN(test_call_stack_effect);
    RUN(test_truncated_jump_instruction);
    RUN(test_field_name_out_of_bounds);

    // Upvalue validation.
    RUN(test_upvalue_index_out_of_bounds);
    RUN(test_valid_upvalue_index);
    RUN(test_set_upvalue_out_of_bounds);
    RUN(test_upvalue_access_with_zero_upvalues);
    return SUMMARY();
}
