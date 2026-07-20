#ifndef LUMA_COMPILER_OPCODE_HPP
#define LUMA_COMPILER_OPCODE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace luma {

// Bytecode opcodes for the Luma VM.
enum class Op : std::uint8_t {
    // ─── Stack manipulation ───
    Constant,     // Push constant from pool: Constant <u16 index>
    ConstantLong, // Push constant (32-bit index): ConstantLong <u32 index>
    Pop,          // Pop top of stack.
    Dup,          // Duplicate top of stack.
    Dup2,         // Duplicate top two stack values.
    Swap,         // Swap top two stack values.

    // ─── Local variables (slot-indexed) ───
    GetLocal,   // Push local: GetLocal <u16 slot>
    SetLocal,   // Set local: SetLocal <u16 slot>
    GetUpvalue, // Push upvalue: GetUpvalue <u16 index>
    SetUpvalue, // Set upvalue: SetUpvalue <u16 index>
    GetGlobal,  // Push global by name: GetGlobal <u16 name_index>
    SetGlobal,  // Set global by name: SetGlobal <u16 name_index>

    // ─── Literals ───
    None,  // Push none.
    True,  // Push true.
    False, // Push false.
    Zero,  // Push integer 0.
    One,   // Push integer 1.

    // ─── Arithmetic ───
    Add,       // a + b
    Subtract,  // a - b
    Multiply,  // a * b
    Divide,    // a / b
    IntDivide, // a // b
    Modulo,    // a % b
    Negate,    // -a
    Increment, // a + 1
    Decrement, // a - 1

    // ─── Comparison ───
    Equal,        // a == b
    NotEqual,     // a != b
    Less,         // a < b
    LessEqual,    // a <= b
    Greater,      // a > b
    GreaterEqual, // a >= b

    // ─── Logical ───
    Not, // !a
    And, // a && b (short-circuit)
    Or,  // a || b (short-circuit)

    // ─── Bitwise ───
    BitwiseAnd, // a & b
    BitwiseOr,  // a | b
    BitwiseXor, // a ^ b
    BitwiseNot, // ~a
    ShiftLeft,  // a << b
    ShiftRight, // a >> b

    // ─── Strings ───
    Concatenate, // string + string
    Interpolate, // Build interpolated string: Interpolate <u8 part_count>

    // ─── Collections ───
    MakeArray,    // Create array: MakeArray <u16 element_count>
    MakeDict,     // Create dict: MakeDict <u16 entry_count>
    MakeTuple,    // Create tuple: MakeTuple <u8 element_count>
    MakeRange,    // Create range (exclusive): [start, end)
    MakeRangeInc, // Create range (inclusive): [start, end]
    IndexGet,     // a[b]
    IndexSet,     // a[b] = c
    IndexGetOpt,  // a?[b] (optional index)

    // ─── Records ───
    MakeRecord,  // Create record: MakeRecord <u16 type_name> <u8 field_count>
    GetField,    // a.field: GetField <u16 field_name>
    SetField,    // a.field = b: SetField <u16 field_name>
    GetFieldOpt, // a?.field: GetFieldOpt <u16 field_name>
    RecordWith,  // record with { ... }: RecordWith <u8 override_count>

    // ─── Choice types ───
    MakeChoice,            // Create unit choice variant from stack (type_name, variant_name)
    MakeChoiceConstructor, // Create data-variant constructor: MakeChoiceConstructor <u8 field_count>

    // ─── Result / Optional ───
    MakeSuccess,   // success(v)
    MakeFailure,   // failure(v)
    MakeSome,      // some(v)
    Unwrap,        // Unwrap result/optional (for error pipe)
    ResultInner,   // Extract inner value from result (success or failure), for match bindings.
    IsSuccess,     // Check if result is success.
    IsSome,        // Check if optional is some.
    EnsureSuccess, // Wrap TOS in success() if it is not already a result.

    // ─── Downcast / Is ───
    Downcast,        // downcast<T>(v): Downcast <u16 type_name>
    TrustedDowncast, // trusted_downcast<T>(v): TrustedDowncast <u16 type_name>
    IsType,          // is<T>(v): IsType <u16 type_name>

    // ─── Control flow ───
    Jump,         // Unconditional jump: Jump <u32 offset>
    JumpIfFalse,  // Jump if top is falsy: JumpIfFalse <u32 offset>
    JumpIfTrue,   // Jump if top is truthy: JumpIfTrue <u32 offset>
    Loop,         // Jump backward: Loop <u32 offset>
    NullCoalesce, // a ?? b: NullCoalesce <u32 offset>

    // ─── Functions ───
    Call,        // Call function: Call <u8 arg_count>
    CallNamed,   // Call with named args: CallNamed <u8 pos_count> <u8 named_count>
    TailCall,    // Tail call: TailCall <u8 arg_count> (reuses current frame)
    Return,      // Return from function.
    MakeClosure, // Create closure: MakeClosure <u16 func_index> <u8 upvalue_count>

    // ─── Pipe operators ───
    Pipe,      // a |> f: call f(a)
    ErrorPipe, // a !> f: unwrap success, pipe into f; short-circuit failure

    // ─── Exception handling ───
    TryCatch, // Push exception handler: TryCatch <u32 catch_offset>
    TryEnd,   // Pop exception handler (try body completed normally).
    Rethrow,  // Re-throw TOS as RuntimeError (used by try/finally).

    // ─── Match ───
    MatchStart, // Begin match on top-of-stack value.
    MatchArm,   // Test match arm pattern.
    MatchEnd,   // End match block.

    // ─── Containment ───
    Contains, // a in b (element in array, key in dict, substring in string)

    // ─── Concurrency ───
    Spawn,          // spawn fn(args): Spawn
    Await,          // await task: Await
    TaskScopeBegin, // Begin task_scope block.
    TaskScopeEnd,   // End task_scope block — join all tasks.

    // ─── Iteration ───
    ForIterInit, // Initialize iterator from iterable on top of stack. Replaces it with iterator state.
    ForIterStep,   // Advance iterator: push next element + true, or push false if exhausted.
    ForIterStepKV, // Advance iterator: push value, key, true; or push false if exhausted.

    // ─── Misc ───
    Print,  // Built-in print: Print <u8 arg_count>
    Assert, // Built-in assert: Assert <u8 arg_count>
    TypeOf, // Built-in type_of.

    // ─── Fused ───
    IncrementLocal, // GetLocal + Increment + SetLocal + Pop: IncrementLocal <u16 slot>
    DecrementLocal, // GetLocal + Decrement + SetLocal + Pop: DecrementLocal <u16 slot>
    SetLocalPop,    // SetLocal + Pop: SetLocalPop <u16 slot>
    GetLocalReturn, // GetLocal + Return: GetLocalReturn <u16 slot>

    // ─── Conversions ───
    IntToNumber, // Convert integer on top of stack to number (double). No-op if already number.
    Clone,       // Deep-copy top of stack (value semantics for mutable bindings).

    // ─── Module ───
    EndModule // End of bytecode stream.
};

// Operand category for verifier bounds checking.  Each u16-operand opcode
// indexes into one of these categories; the verifier runs a single
// parameterised bounds check per category instead of one function each.
enum class OperandCategory : std::uint8_t {
    None,     // No u16 operand to validate.
    Constant, // Indexes into the constant pool.
    Name,     // Indexes into the name table.
    Upvalue,  // Indexes into the upvalue table.
    Local,    // Indexes a local variable slot.
};

// Operand byte layout, letting the disassembler select operand decoding
// from a single table lookup instead of a per-opcode switch.
enum class OperandLayout : std::uint8_t {
    Simple,      // No operands.
    U8,          // Single u8 operand.
    U16,         // Single u16 operand.
    U32Jump,     // u32 branch offset (shown as a jump target).
    U32Long,     // u32 operand (ConstantLong).
    TwoU8,       // Two u8 operands (CallNamed).
    MakeClosure, // u16 func index + u8 upvalue count.
    MakeRecord,  // u16 type name + u8 field count + field_count * u16.
    RecordWith,  // u8 override count + override_count * u16.
};

// Bitmask of the semantic groups an opcode belongs to.  Classification
// helpers (is_arithmetic, is_control_flow, ...) test a single bit instead of
// maintaining parallel switch statements.
namespace op_flag {
inline constexpr std::uint16_t k_arithmetic = 1u << 0;   // Binary/unary arithmetic.
inline constexpr std::uint16_t k_comparison = 1u << 1;   // Comparison operators.
inline constexpr std::uint16_t k_stack_op = 1u << 2;     // Pure stack manipulation.
inline constexpr std::uint16_t k_load_store = 1u << 3;   // Variable load/store.
inline constexpr std::uint16_t k_control_flow = 1u << 4; // Alters PC or call stack.
inline constexpr std::uint16_t k_terminator = 1u << 5;   // Ends a basic block.
inline constexpr std::uint16_t k_forward_jump = 1u << 6; // Forward u32 branch.
inline constexpr std::uint16_t k_jump = 1u << 7;         // Any jump (forward or Loop).
} // namespace op_flag

// Metadata for a single opcode.  base_size is the fixed instruction size in
// bytes (opcode + fixed operands); for variable-size instructions
// (MakeRecord, RecordWith) it is the fixed prefix size.  has_fixed_stack_effect
// records whether stack_effect is a compile-time-constant net stack delta.
struct OpcodeInfo {
    Op code;
    std::string_view name;
    int base_size;
    OperandCategory operand_category;
    OperandLayout operand_layout;
    std::uint16_t flags;
    bool has_fixed_stack_effect;
    std::int8_t stack_effect;
};

// Number of defined opcodes (derived from the last enum value).
// EndModule must always be the last enumerator in Op.
inline constexpr std::size_t opcode_enum_count = static_cast<std::size_t>(Op::EndModule) + 1;

// Reserved dispatch-table slots for forward compatibility — new opcodes can
// be added without immediately rebuilding every translation unit that sizes
// a table from opcode_count.
inline constexpr std::size_t k_opcode_reserved_slots = 3;

// Total dispatch / lookup table size (defined opcodes + reserved slots).
inline constexpr std::size_t opcode_count = opcode_enum_count + k_opcode_reserved_slots;

// The VM dispatch table is indexed directly by the raw opcode byte read from a
// function's bytecode.  That byte is attacker-controllable in a corrupt or
// crafted .lumc file, so the table covers the full 0-255 byte range: any
// invalid opcode then indexes a valid (op_invalid) slot instead of reading past
// the array end.  Defined opcodes occupy [0, opcode_count); the rest stay
// op_invalid.
inline constexpr std::size_t k_dispatch_table_size = 256;
static_assert(opcode_count <= k_dispatch_table_size,
              "opcode_count must fit within the byte-indexed dispatch table");

// clang-format off
inline constexpr std::array<OpcodeInfo, opcode_count> opcode_table{{
    // Rows are in Op enum order; see the Op enum above for section grouping.
    {Op::Constant, "Constant", 3, OperandCategory::Constant, OperandLayout::U16, op_flag::k_stack_op, true, 1},
    {Op::ConstantLong, "ConstantLong", 5, OperandCategory::Constant, OperandLayout::U32Long, op_flag::k_stack_op, true, 1},
    {Op::Pop, "Pop", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_stack_op, true, -1},
    {Op::Dup, "Dup", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_stack_op, true, 1},
    {Op::Dup2, "Dup2", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_stack_op, true, 2},
    {Op::Swap, "Swap", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_stack_op, false, 0},
    {Op::GetLocal, "GetLocal", 3, OperandCategory::Local, OperandLayout::U16, op_flag::k_load_store, true, 1},
    {Op::SetLocal, "SetLocal", 3, OperandCategory::Local, OperandLayout::U16, op_flag::k_load_store, true, 0},
    {Op::GetUpvalue, "GetUpvalue", 3, OperandCategory::Upvalue, OperandLayout::U16, op_flag::k_load_store, true, 1},
    {Op::SetUpvalue, "SetUpvalue", 3, OperandCategory::Upvalue, OperandLayout::U16, op_flag::k_load_store, true, 0},
    {Op::GetGlobal, "GetGlobal", 3, OperandCategory::Name, OperandLayout::U16, op_flag::k_load_store, true, 1},
    {Op::SetGlobal, "SetGlobal", 3, OperandCategory::Name, OperandLayout::U16, op_flag::k_load_store, true, 0},
    {Op::None, "None", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_stack_op, true, 1},
    {Op::True, "True", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_stack_op, true, 1},
    {Op::False, "False", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_stack_op, true, 1},
    {Op::Zero, "Zero", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_stack_op, true, 1},
    {Op::One, "One", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_stack_op, true, 1},
    {Op::Add, "Add", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_arithmetic, true, -1},
    {Op::Subtract, "Subtract", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_arithmetic, true, -1},
    {Op::Multiply, "Multiply", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_arithmetic, true, -1},
    {Op::Divide, "Divide", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_arithmetic, true, -1},
    {Op::IntDivide, "IntDivide", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_arithmetic, true, -1},
    {Op::Modulo, "Modulo", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_arithmetic, true, -1},
    {Op::Negate, "Negate", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_arithmetic, false, 0},
    {Op::Increment, "Increment", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_arithmetic, false, 0},
    {Op::Decrement, "Decrement", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_arithmetic, false, 0},
    {Op::Equal, "Equal", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_comparison, true, -1},
    {Op::NotEqual, "NotEqual", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_comparison, true, -1},
    {Op::Less, "Less", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_comparison, true, -1},
    {Op::LessEqual, "LessEqual", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_comparison, true, -1},
    {Op::Greater, "Greater", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_comparison, true, -1},
    {Op::GreaterEqual, "GreaterEqual", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_comparison, true, -1},
    {Op::Not, "Not", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::And, "And", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::Or, "Or", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::BitwiseAnd, "BitwiseAnd", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -1},
    {Op::BitwiseOr, "BitwiseOr", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -1},
    {Op::BitwiseXor, "BitwiseXor", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -1},
    {Op::BitwiseNot, "BitwiseNot", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::ShiftLeft, "ShiftLeft", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -1},
    {Op::ShiftRight, "ShiftRight", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -1},
    {Op::Concatenate, "Concatenate", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -1},
    {Op::Interpolate, "Interpolate", 2, OperandCategory::None, OperandLayout::U8, 0, false, 0},
    {Op::MakeArray, "MakeArray", 3, OperandCategory::None, OperandLayout::U16, 0, false, 0},
    {Op::MakeDict, "MakeDict", 3, OperandCategory::None, OperandLayout::U16, 0, false, 0},
    {Op::MakeTuple, "MakeTuple", 3, OperandCategory::None, OperandLayout::U16, 0, false, 0},
    {Op::MakeRange, "MakeRange", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -1},
    {Op::MakeRangeInc, "MakeRangeInc", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -1},
    {Op::IndexGet, "IndexGet", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -1},
    {Op::IndexSet, "IndexSet", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -2},
    {Op::IndexGetOpt, "IndexGetOpt", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::MakeRecord, "MakeRecord", 4, OperandCategory::None, OperandLayout::MakeRecord, 0, false, 0},
    {Op::GetField, "GetField", 3, OperandCategory::Name, OperandLayout::U16, 0, false, 0},
    {Op::SetField, "SetField", 3, OperandCategory::Name, OperandLayout::U16, 0, false, 0},
    {Op::GetFieldOpt, "GetFieldOpt", 3, OperandCategory::Name, OperandLayout::U16, 0, false, 0},
    {Op::RecordWith, "RecordWith", 2, OperandCategory::None, OperandLayout::RecordWith, 0, false, 0},
    {Op::MakeChoice, "MakeChoice", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -1},
    {Op::MakeChoiceConstructor, "MakeChoiceConstructor", 2, OperandCategory::None, OperandLayout::U8, 0, true, -1},
    {Op::MakeSuccess, "MakeSuccess", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::MakeFailure, "MakeFailure", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::MakeSome, "MakeSome", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::Unwrap, "Unwrap", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::ResultInner, "ResultInner", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::IsSuccess, "IsSuccess", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::IsSome, "IsSome", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::EnsureSuccess, "EnsureSuccess", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::Downcast, "Downcast", 3, OperandCategory::Name, OperandLayout::U16, 0, false, 0},
    {Op::TrustedDowncast, "TrustedDowncast", 3, OperandCategory::Name, OperandLayout::U16, 0, false, 0},
    {Op::IsType, "IsType", 3, OperandCategory::Name, OperandLayout::U16, 0, false, 0},
    {Op::Jump, "Jump", 5, OperandCategory::None, OperandLayout::U32Jump, op_flag::k_control_flow | op_flag::k_terminator | op_flag::k_forward_jump | op_flag::k_jump, false, 0},
    {Op::JumpIfFalse, "JumpIfFalse", 5, OperandCategory::None, OperandLayout::U32Jump, op_flag::k_control_flow | op_flag::k_forward_jump | op_flag::k_jump, false, 0},
    {Op::JumpIfTrue, "JumpIfTrue", 5, OperandCategory::None, OperandLayout::U32Jump, op_flag::k_control_flow | op_flag::k_forward_jump | op_flag::k_jump, false, 0},
    {Op::Loop, "Loop", 5, OperandCategory::None, OperandLayout::U32Jump, op_flag::k_control_flow | op_flag::k_jump, false, 0},
    {Op::NullCoalesce, "NullCoalesce", 5, OperandCategory::None, OperandLayout::U32Jump, op_flag::k_control_flow | op_flag::k_forward_jump | op_flag::k_jump, false, 0},
    {Op::Call, "Call", 2, OperandCategory::None, OperandLayout::U8, op_flag::k_control_flow, false, 0},
    {Op::CallNamed, "CallNamed", 3, OperandCategory::None, OperandLayout::TwoU8, op_flag::k_control_flow, false, 0},
    {Op::TailCall, "TailCall", 2, OperandCategory::None, OperandLayout::U8, op_flag::k_control_flow, false, 0},
    {Op::Return, "Return", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_control_flow | op_flag::k_terminator, true, 0},
    {Op::MakeClosure, "MakeClosure", 4, OperandCategory::None, OperandLayout::MakeClosure, 0, false, 0},
    {Op::Pipe, "Pipe", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_control_flow, false, 0},
    {Op::ErrorPipe, "ErrorPipe", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_control_flow, false, 0},
    {Op::TryCatch, "TryCatch", 5, OperandCategory::None, OperandLayout::U32Jump, op_flag::k_control_flow | op_flag::k_forward_jump | op_flag::k_jump, false, 0},
    {Op::TryEnd, "TryEnd", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_control_flow, false, 0},
    {Op::Rethrow, "Rethrow", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_control_flow, false, 0},
    {Op::MatchStart, "MatchStart", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_control_flow, false, 0},
    {Op::MatchArm, "MatchArm", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_control_flow, false, 0},
    {Op::MatchEnd, "MatchEnd", 1, OperandCategory::None, OperandLayout::Simple, op_flag::k_control_flow, false, 0},
    {Op::Contains, "Contains", 1, OperandCategory::None, OperandLayout::Simple, 0, true, -1},
    {Op::Spawn, "Spawn", 2, OperandCategory::None, OperandLayout::U8, 0, false, 0},
    {Op::Await, "Await", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::TaskScopeBegin, "TaskScopeBegin", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::TaskScopeEnd, "TaskScopeEnd", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::ForIterInit, "ForIterInit", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::ForIterStep, "ForIterStep", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::ForIterStepKV, "ForIterStepKV", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::Print, "Print", 2, OperandCategory::None, OperandLayout::U8, 0, false, 0},
    {Op::Assert, "Assert", 2, OperandCategory::None, OperandLayout::U8, 0, false, 0},
    {Op::TypeOf, "TypeOf", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::IncrementLocal, "IncrementLocal", 3, OperandCategory::Local, OperandLayout::U16, 0, false, 0},
    {Op::DecrementLocal, "DecrementLocal", 3, OperandCategory::Local, OperandLayout::U16, 0, false, 0},
    {Op::SetLocalPop, "SetLocalPop", 3, OperandCategory::Local, OperandLayout::U16, 0, true, -1},
    {Op::GetLocalReturn, "GetLocalReturn", 3, OperandCategory::Local, OperandLayout::U16, op_flag::k_terminator, true, 0},
    {Op::IntToNumber, "IntToNumber", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::Clone, "Clone", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
    {Op::EndModule, "EndModule", 1, OperandCategory::None, OperandLayout::Simple, 0, false, 0},
}};
// clang-format on

// Verify the opcode table has an entry at the EndModule position with the
// correct enum value.  This catches forgotten or misordered table entries.
static_assert(opcode_table[static_cast<std::size_t>(Op::EndModule)].code == Op::EndModule,
              "opcode_table entry at EndModule position does not match Op::EndModule — "
              "table is incomplete or misordered");

// Verify that every defined opcode in the table is at the position matching
// its enum value.  This guarantees the fast direct-index lookup in
// find_opcode_info() always works without falling back to linear search.
// clang-format off
static_assert(opcode_table[static_cast<std::size_t>(Op::Constant)].code       == Op::Constant,       "opcode_table: Constant position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Pop)].code            == Op::Pop,            "opcode_table: Pop position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::GetLocal)].code       == Op::GetLocal,       "opcode_table: GetLocal position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::SetLocal)].code       == Op::SetLocal,       "opcode_table: SetLocal position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::None)].code           == Op::None,           "opcode_table: None position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Add)].code            == Op::Add,            "opcode_table: Add position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Equal)].code          == Op::Equal,          "opcode_table: Equal position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Not)].code            == Op::Not,            "opcode_table: Not position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::BitwiseAnd)].code     == Op::BitwiseAnd,     "opcode_table: BitwiseAnd position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Concatenate)].code    == Op::Concatenate,    "opcode_table: Concatenate position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::MakeArray)].code      == Op::MakeArray,      "opcode_table: MakeArray position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::MakeRecord)].code     == Op::MakeRecord,     "opcode_table: MakeRecord position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::MakeChoice)].code     == Op::MakeChoice,     "opcode_table: MakeChoice position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::MakeChoiceConstructor)].code == Op::MakeChoiceConstructor, "opcode_table: MakeChoiceConstructor position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::MakeSuccess)].code    == Op::MakeSuccess,    "opcode_table: MakeSuccess position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Downcast)].code       == Op::Downcast,       "opcode_table: Downcast position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Jump)].code           == Op::Jump,           "opcode_table: Jump position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Call)].code           == Op::Call,            "opcode_table: Call position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Return)].code         == Op::Return,         "opcode_table: Return position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Pipe)].code           == Op::Pipe,           "opcode_table: Pipe position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::TryCatch)].code       == Op::TryCatch,       "opcode_table: TryCatch position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Contains)].code       == Op::Contains,       "opcode_table: Contains position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Spawn)].code          == Op::Spawn,          "opcode_table: Spawn position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::ForIterInit)].code    == Op::ForIterInit,    "opcode_table: ForIterInit position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Print)].code          == Op::Print,          "opcode_table: Print position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::IncrementLocal)].code == Op::IncrementLocal, "opcode_table: IncrementLocal position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::IntToNumber)].code    == Op::IntToNumber,    "opcode_table: IntToNumber position mismatch");
static_assert(opcode_table[static_cast<std::size_t>(Op::Clone)].code          == Op::Clone,          "opcode_table: Clone position mismatch");
// clang-format on

// Shared lookup helper used by opcode_name() and opcode_base_size().
// Performs a direct index lookup, then falls back to a linear scan if the
// table order doesn't match the enum value at that position.
[[nodiscard]] constexpr const OpcodeInfo* find_opcode_info(Op op) {
    const auto index = static_cast<std::size_t>(op);

    if (index < opcode_table.size() && opcode_table[index].code == op) {
        return &opcode_table[index];
    }

    // Fallback linear search for safety if the table order does not
    // match the enum values.
    for (const auto& entry : opcode_table) {
        if (entry.code == op) {
            return &entry;
        }
    }

    return nullptr;
}

[[nodiscard]] constexpr std::string_view opcode_name(Op op) {
    const auto* info = find_opcode_info(op);
    return info ? info->name : "Unknown";
}

// Base instruction size in bytes (opcode + fixed operands).
// For variable-size instructions (MakeRecord, RecordWith), returns the
// minimum fixed size; callers must handle the variable part.
[[nodiscard]] constexpr std::size_t opcode_base_size(Op op) {
    const auto* info = find_opcode_info(op);
    return info ? static_cast<std::size_t>(info->base_size) : 1;
}

// u16-operand category for verifier bounds checking.
[[nodiscard]] constexpr OperandCategory opcode_operand_category(Op op) {
    const auto* info = find_opcode_info(op);
    return info ? info->operand_category : OperandCategory::None;
}

// Operand byte layout for the disassembler.
[[nodiscard]] constexpr OperandLayout opcode_operand_layout(Op op) {
    const auto* info = find_opcode_info(op);
    return info ? info->operand_layout : OperandLayout::Simple;
}

// Semantic-group flag bitmask (op_flag::*).
[[nodiscard]] constexpr std::uint16_t opcode_flags(Op op) {
    const auto* info = find_opcode_info(op);
    return info ? info->flags : std::uint16_t{0};
}

// Net stack-depth delta when statically known (independent of operand
// values); std::nullopt for operand-dependent or non-tracked opcodes.
[[nodiscard]] constexpr std::optional<int> opcode_fixed_stack_effect(Op op) {
    const auto* info = find_opcode_info(op);
    if (info && info->has_fixed_stack_effect) {
        return info->stack_effect;
    }
    return std::nullopt;
}

} // namespace luma

#endif // LUMA_COMPILER_OPCODE_HPP
