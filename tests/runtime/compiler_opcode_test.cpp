// Compiler opcode emission tests — advanced opcodes.
// Complements compiler_test.cpp by covering opcodes related to
// records, choice types, matching, pipes, concurrency, and more.

#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include "compiler_test_helpers.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/opcode_metadata.hpp"
#include "shared_eval.hpp"
#include "test_framework.hpp"

using namespace luma;
using luma::test::has_opcode;

// ─── Helpers ───

static CompileResult compile(const std::string& source, bool repl_mode = true) {
    const auto program = luma::test::lex_and_parse(source);

    Compiler compiler;

    return compiler.compile(program, repl_mode);
}

// Search for an opcode in any compiled function, including the top-level chunk.
static bool any_func_has_opcode(const CompileResult& result, Op op) {
    if (has_opcode(result.top_level.chunk(), op)) {
        return true;
    }

    for (const auto& fn : result.functions) {
        if (has_opcode(fn.chunk(), op)) {
            return true;
        }
    }

    return false;
}

// ═══════════════════════════════════════════════════════════
// Records
// ═══════════════════════════════════════════════════════════

static void test_compile_make_record() {
    const auto result = compile("record Point { integer x, integer y }\n"
                                "function void f() {\n"
                                "    Point p = Point { x = 1, y = 2 }\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::MakeRecord));
}

static void test_compile_get_field() {
    const auto result = compile("record Point { integer x, integer y }\n"
                                "function integer f() {\n"
                                "    Point p = Point { x = 1, y = 2 }\n"
                                "    return p.x\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::GetField));
}

static void test_compile_set_field() {
    const auto result = compile("record Point { integer x, integer y }\n"
                                "function void f() {\n"
                                "    mutable Point p = Point { x = 1, y = 2 }\n"
                                "    p.x = 10\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::SetField));
}

static void test_compile_record_with() {
    const auto result = compile("record Point { integer x, integer y }\n"
                                "function Point f() {\n"
                                "    Point p = Point { x = 1, y = 2 }\n"
                                "    return p with { x = 10 }\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::RecordWith));
}

// ═══════════════════════════════════════════════════════════
// Choice types
// ═══════════════════════════════════════════════════════════

static void test_compile_make_choice() {
    const auto result = compile("choice Shape {\n"
                                "    Circle(number radius),\n"
                                "    Rectangle(number width, number height)\n"
                                "}\n"
                                "function void f() {\n"
                                "    Shape s = Shape.Circle(5.0)\n"
                                "}");

    ASSERT_TRUE(result.success);
    // Choice construction compiles as a regular call to the variant constructor.
    ASSERT_TRUE(any_func_has_opcode(result, Op::Call));
}

static void test_compile_make_choice_constructor() {
    // Data variants emit MakeChoiceConstructor (not a NativeFunctionValue constant)
    // so the constructor can be reconstructed from serialisable string operands.
    const auto result = compile("choice Shape {\n"
                                "    Circle(number radius),\n"
                                "    Rectangle(number width, number height)\n"
                                "}\n"
                                "function void f() {\n"
                                "    Shape s = Shape.Circle(5.0)\n"
                                "}");

    ASSERT_TRUE(result.success);
    // The top-level chunk registers each data variant constructor via MakeChoiceConstructor.
    ASSERT_TRUE(any_func_has_opcode(result, Op::MakeChoiceConstructor));
}

static void test_disassemble_make_choice_constructor() {
    // Regression: the disassembler must render MakeChoiceConstructor by name,
    // not as UNKNOWN(<n>). It is a u8-operand (field_count) opcode emitted for
    // data variant constructors.
    const auto result = compile("choice Shape {\n"
                                "    Circle(number radius),\n"
                                "    Rectangle(number width, number height)\n"
                                "}\n"
                                "function void f() {\n"
                                "    Shape s = Shape.Circle(5.0)\n"
                                "}");

    ASSERT_TRUE(result.success);

    std::string listing = result.top_level.chunk().disassemble("top_level");

    for (const auto& fn : result.functions) {
        listing += fn.chunk().disassemble(fn.name);
    }

    ASSERT_TRUE(listing.find("MakeChoiceConstructor") != std::string::npos);
    ASSERT_TRUE(listing.find("UNKNOWN") == std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// Match
// ═══════════════════════════════════════════════════════════

static void test_compile_match_start_end() {
    const auto result = compile("function string f(integer x) {\n"
                                "    return match x {\n"
                                "        case 1 { \"one\" }\n"
                                "        else { \"other\" }\n"
                                "    }\n"
                                "}");

    ASSERT_TRUE(result.success);
    // Match compiles to equality checks and conditional jumps.
    ASSERT_TRUE(any_func_has_opcode(result, Op::Equal));
    ASSERT_TRUE(any_func_has_opcode(result, Op::JumpIfFalse));
}

// ═══════════════════════════════════════════════════════════
// Pipe operators
// ═══════════════════════════════════════════════════════════

static void test_compile_pipe() {
    const auto result = compile("function integer double(integer x) { return x * 2 }\n"
                                "function integer f() { return 5 |> double() }");

    ASSERT_TRUE(result.success);
    // Pipe compiles the left value and right callee, then emits a Call.
    ASSERT_TRUE(any_func_has_opcode(result, Op::Call));
}

static void test_compile_error_pipe() {
    const auto result =
        compile("function result<integer> parse(string s) { return success(42) }\n"
                "function result<integer> double_result(integer x) { return success(x * 2) }\n"
                "function result<integer> f() { return parse(\"42\") !> double_result() }");

    ASSERT_TRUE(result.success);
    // Error pipe compiles as Dup + IsSuccess + JumpIfFalse + Unwrap + Call.
    ASSERT_TRUE(any_func_has_opcode(result, Op::IsSuccess));
    ASSERT_TRUE(any_func_has_opcode(result, Op::Unwrap));
}

// ═══════════════════════════════════════════════════════════
// Result / Optional
// ═══════════════════════════════════════════════════════════

static void test_compile_make_success() {
    const auto result = compile("function result<integer> f() { return success(42) }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::MakeSuccess));
}

static void test_compile_make_failure() {
    const auto result = compile("function result<integer> f() { return failure(\"err\") }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::MakeFailure));
}

static void test_compile_make_some() {
    const auto result = compile("function optional<integer> f() { return some(42) }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::MakeSome));
}

// ═══════════════════════════════════════════════════════════
// Null coalescing
// ═══════════════════════════════════════════════════════════

static void test_compile_null_coalesce() {
    const auto result = compile("function integer f() {\n"
                                "    optional<integer> x = some(5)\n"
                                "    return x ?? 0\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::NullCoalesce));
}

// ═══════════════════════════════════════════════════════════
// Stack manipulation
// ═══════════════════════════════════════════════════════════

static void test_compile_pop() {
    // Statements whose values are discarded emit Pop.
    const auto result = compile("function void f() {\n"
                                "    1 + 2\n"
                                "    3 + 4\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::Pop));
}

// ═══════════════════════════════════════════════════════════
// Downcast / IsType / TypeOf
// ═══════════════════════════════════════════════════════════

static void test_compile_typeof() {
    // type_of is a native function, compiled as GetGlobal + Call.
    const auto result = compile("type_of(42)");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::GetGlobal));
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Call));
}

// ═══════════════════════════════════════════════════════════
// For iteration with key-value
// ═══════════════════════════════════════════════════════════

static void test_compile_for_iter_kv() {
    const auto result = compile("function void f() {\n"
                                "    dictionary<integer> d = {\"a\": 1, \"b\": 2}\n"
                                "    for k, v in d { print(k) }\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::ForIterInit));
    ASSERT_TRUE(any_func_has_opcode(result, Op::ForIterStepKV));
}

// ═══════════════════════════════════════════════════════════
// Range inclusive
// ═══════════════════════════════════════════════════════════

static void test_compile_range_inclusive() {
    const auto result = compile("mutable integer a = 1\nmutable integer b = 10\na..=b");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::MakeRangeInc));
}

// (Bitwise operators were removed from the language surface — R06. Bit
// manipulation now lives in the Bits stdlib module, exercised by
// tests/features/stdlib/bits_functions.luma.)

// ═══════════════════════════════════════════════════════════
// Comparison operators (complete set)
// ═══════════════════════════════════════════════════════════

static void test_compile_less_equal() {
    const auto result = compile("mutable integer a = 1\na <= 2");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::LessEqual));
}

static void test_compile_greater() {
    const auto result = compile("mutable integer a = 5\na > 2");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Greater));
}

static void test_compile_greater_equal() {
    const auto result = compile("mutable integer a = 5\na >= 2");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::GreaterEqual));
}

static void test_compile_not_equal() {
    const auto result = compile("mutable integer a = 5\na != 2");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::NotEqual));
}

static void test_compile_equal() {
    const auto result = compile("mutable integer a = 5\na == 5");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Equal));
}

// ═══════════════════════════════════════════════════════════
// Arithmetic operators
// ═══════════════════════════════════════════════════════════

static void test_compile_subtract() {
    const auto result = compile("function integer f(integer a, integer b) { return a - b }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::Subtract));
}

static void test_compile_multiply() {
    const auto result = compile("function integer f(integer a, integer b) { return a * b }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::Multiply));
}

static void test_compile_divide() {
    const auto result = compile("function number f(number a, number b) { return a / b }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::Divide));
}

static void test_compile_int_divide() {
    const auto result = compile("function integer f(integer a, integer b) { return a // b }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::IntDivide));
}

static void test_compile_modulo() {
    const auto result = compile("function integer f(integer a, integer b) { return a % b }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::Modulo));
}

// ═══════════════════════════════════════════════════════════
// Print and Assert
// ═══════════════════════════════════════════════════════════

static void test_compile_print() {
    // print is a native function, compiled as GetGlobal + Call.
    const auto result = compile("function void f() { print(42) }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::GetGlobal));
    ASSERT_TRUE(any_func_has_opcode(result, Op::Call));
}

static void test_compile_assert() {
    // assert is a native function, compiled as GetGlobal + Call.
    const auto result = compile("function void f() { assert(true) }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::GetGlobal));
    ASSERT_TRUE(any_func_has_opcode(result, Op::Call));
}

// ═══════════════════════════════════════════════════════════
// Index set
// ═══════════════════════════════════════════════════════════

static void test_compile_index_set() {
    const auto result = compile("function void f() {\n"
                                "    mutable array<integer> a = [1, 2, 3]\n"
                                "    a[0] = 10\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::IndexSet));
}

// ═══════════════════════════════════════════════════════════
// Upvalue access
// ═══════════════════════════════════════════════════════════

static void test_compile_get_upvalue() {
    const auto result = compile("function void f() {\n"
                                "    integer x = 10\n"
                                "    function(integer) -> integer add = (integer y) -> x + y\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::GetUpvalue));
}

// ═══════════════════════════════════════════════════════════
// EndModule
// ═══════════════════════════════════════════════════════════

static void test_compile_end_module() {
    // In non-REPL mode, the top-level chunk ends with a terminator.
    const auto result = compile("42", /*repl_mode=*/false);

    ASSERT_TRUE(result.success);
    // The chunk must contain at least one instruction.
    ASSERT_TRUE(!result.top_level.chunk().code.empty());
    // The last opcode should be a terminator (Return or EndModule).
    auto last_op = static_cast<Op>(result.top_level.chunk().code.back());
    ASSERT_TRUE(last_op == Op::Return || last_op == Op::EndModule);
}

// ═══════════════════════════════════════════════════════════
// Dup (duplicate top of stack)
// ═══════════════════════════════════════════════════════════

static void test_compile_dup_via_set_local() {
    // SetLocal peeks — compiler may emit Dup in some patterns.
    const auto result = compile("function integer f() {\n"
                                "    mutable integer x = 0\n"
                                "    x = 5\n"
                                "    return x\n"
                                "}");

    ASSERT_TRUE(result.success);
    // After peephole: SetLocal+GetLocal → SetLocal+Dup (optimizer).
    // The compiled bytecode should have the SetLocal at minimum.
    ASSERT_TRUE(any_func_has_opcode(result, Op::SetLocal) ||
                any_func_has_opcode(result, Op::SetLocalPop));
}

// ═══════════════════════════════════════════════════════════
// Named arguments (CallNamed)
// ═══════════════════════════════════════════════════════════

static void test_compile_call_named() {
    const auto result = compile("function string greet(string name, integer age) { return name }\n"
                                "function string f() { return greet(name: \"Alice\", age: 30) }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::CallNamed));
}

static void test_compile_call_named_mixed() {
    const auto result =
        compile("function string greet(string name, integer age, boolean active) { return name }\n"
                "function string f() { return greet(\"Alice\", active: true, age: 30) }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::CallNamed));
}

// ═══════════════════════════════════════════════════════════
// Downcast / IsType / TrustedDowncast
// ═══════════════════════════════════════════════════════════

static void test_compile_downcast() {
    const auto result =
        compile("function result<string> f(integer x) { return downcast<string>(x) }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::Downcast));
}

static void test_compile_trusted_downcast() {
    const auto result =
        compile("function string f(integer x) { return trusted_downcast<string>(x) }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::TrustedDowncast));
}

static void test_compile_is_type() {
    const auto result = compile("function boolean f(integer x) { return is<integer>(x) }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::IsType));
}

// ═══════════════════════════════════════════════════════════
// TailCall
// ═══════════════════════════════════════════════════════════

static void test_compile_tail_call() {
    const auto result = compile("function integer loop(integer n, integer acc) {\n"
                                "    if n <= 0 { return acc }\n"
                                "    return loop(n - 1, acc + n)\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::TailCall));
}

// ═══════════════════════════════════════════════════════════
// Clone (mutable value semantics)
// ═══════════════════════════════════════════════════════════

static void test_compile_clone_mutable() {
    const auto result = compile("function void f() {\n"
                                "    array<integer> a = [1, 2, 3]\n"
                                "    mutable array<integer> b = a\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::Clone));
}

// ═══════════════════════════════════════════════════════════
// IntToNumber (integer → number widening)
// ═══════════════════════════════════════════════════════════

static void test_compile_int_to_number() {
    const auto result = compile("number x = 42");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::IntToNumber));
}

// ═══════════════════════════════════════════════════════════
// Optional chaining (GetFieldOpt, IndexGetOpt)
// ═══════════════════════════════════════════════════════════

static void test_compile_get_field_opt() {
    const auto result = compile("record Point { integer x, integer y }\n"
                                "function void f() {\n"
                                "    optional<Point> p = some(Point { x = 1, y = 2 })\n"
                                "    p?.x\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::GetFieldOpt));
}

static void test_compile_index_get_opt() {
    const auto result = compile("function void f() {\n"
                                "    optional<array<integer>> a = some([1, 2, 3])\n"
                                "    a?[0]\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::IndexGetOpt));
}

// ═══════════════════════════════════════════════════════════
// Rethrow — re-raised by try/finally after the cleanup body runs
// ═══════════════════════════════════════════════════════════

static void test_compile_try_finally_emits_rethrow() {
    // A try/finally with no catch must re-raise any error that escaped the try
    // body after the finally body runs, so the compiler emits a Rethrow opcode.
    const auto result = compile("function void f() {\n"
                                "    try {\n"
                                "        integer x = 1\n"
                                "    } finally {\n"
                                "        integer y = 2\n"
                                "    }\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::TryCatch));
    ASSERT_TRUE(any_func_has_opcode(result, Op::Rethrow));
}

static void test_compile_try_catch_finally_emits_rethrow() {
    // try/catch/finally is lowered as nested layers (try { try A catch B } finally C).
    // The outer finally layer re-raises any error that escapes the catch body,
    // so Rethrow is emitted even though a catch clause is present.
    const auto result = compile("function void f() {\n"
                                "    try {\n"
                                "        integer x = 1\n"
                                "    } catch(e) {\n"
                                "        integer y = 2\n"
                                "    } finally {\n"
                                "        integer z = 3\n"
                                "    }\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::TryCatch));
    ASSERT_TRUE(any_func_has_opcode(result, Op::Rethrow));
}

static void test_compile_try_catch_has_no_rethrow() {
    // A try/catch with no finally fully recovers the error in the catch block,
    // so there is nothing to re-raise — no Rethrow opcode should be emitted.
    const auto result = compile("function void f() {\n"
                                "    try {\n"
                                "        integer x = 1\n"
                                "    } catch(e) {\n"
                                "        integer y = 2\n"
                                "    }\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::TryCatch));
    ASSERT_FALSE(any_func_has_opcode(result, Op::Rethrow));
}

// ═══════════════════════════════════════════════════════════
// Concurrency (Spawn / Await / TaskScope)
// ═══════════════════════════════════════════════════════════

static void test_compile_spawn() {
    const auto result = compile("function integer work(integer x) { return x * 2 }\n"
                                "function void f() {\n"
                                "    task<integer> t = spawn work(21)\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::Spawn));
}

static void test_compile_await() {
    const auto result = compile("function integer work(integer x) { return x * 2 }\n"
                                "function integer f() {\n"
                                "    task<integer> t = spawn work(21)\n"
                                "    return await t\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::Await));
}

static void test_compile_task_scope() {
    const auto result = compile("function integer work(integer x) { return x * 2 }\n"
                                "function void f() {\n"
                                "    task_scope {\n"
                                "        task<integer> t = spawn work(21)\n"
                                "    }\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(any_func_has_opcode(result, Op::TaskScopeBegin));
    ASSERT_TRUE(any_func_has_opcode(result, Op::TaskScopeEnd));
}

// ─── main ───

// --- R02: opcode metadata single-source-of-truth characterization ---
//
// Baseline snapshot of every opcode's derived metadata, captured from the
// original hand-written switch statements before they were collapsed onto the
// constexpr opcode table.  If a table row drifts from these values the checks
// below fail -- at compile time via static_assert and at run time in the report.
namespace {

struct OpcodeExpectation {
    Op op;
    OperandCategory category;
    OperandLayout layout;
    bool arithmetic;
    bool comparison;
    bool stack_op;
    bool load_store;
    bool control_flow;
    bool terminator;
    bool forward_jump;
    bool jump;
    bool has_fixed_effect;
    int fixed_effect;
};

// clang-format off
constexpr std::array<OpcodeExpectation, 99> k_opcode_expectations{{
    {Op::Constant, OperandCategory::Constant, OperandLayout::U16, false,false,true,false,false,false,false,false, true, 1},
    {Op::ConstantLong, OperandCategory::Constant, OperandLayout::U32Long, false,false,true,false,false,false,false,false, true, 1},
    {Op::Pop, OperandCategory::None, OperandLayout::Simple, false,false,true,false,false,false,false,false, true, -1},
    {Op::Dup, OperandCategory::None, OperandLayout::Simple, false,false,true,false,false,false,false,false, true, 1},
    {Op::Dup2, OperandCategory::None, OperandLayout::Simple, false,false,true,false,false,false,false,false, true, 2},
    {Op::Swap, OperandCategory::None, OperandLayout::Simple, false,false,true,false,false,false,false,false, false, 0},
    {Op::GetLocal, OperandCategory::Local, OperandLayout::U16, false,false,false,true,false,false,false,false, true, 1},
    {Op::SetLocal, OperandCategory::Local, OperandLayout::U16, false,false,false,true,false,false,false,false, true, 0},
    {Op::GetUpvalue, OperandCategory::Upvalue, OperandLayout::U16, false,false,false,true,false,false,false,false, true, 1},
    {Op::SetUpvalue, OperandCategory::Upvalue, OperandLayout::U16, false,false,false,true,false,false,false,false, true, 0},
    {Op::GetGlobal, OperandCategory::Name, OperandLayout::U16, false,false,false,true,false,false,false,false, true, 1},
    {Op::SetGlobal, OperandCategory::Name, OperandLayout::U16, false,false,false,true,false,false,false,false, true, 0},
    {Op::None, OperandCategory::None, OperandLayout::Simple, false,false,true,false,false,false,false,false, true, 1},
    {Op::True, OperandCategory::None, OperandLayout::Simple, false,false,true,false,false,false,false,false, true, 1},
    {Op::False, OperandCategory::None, OperandLayout::Simple, false,false,true,false,false,false,false,false, true, 1},
    {Op::Zero, OperandCategory::None, OperandLayout::Simple, false,false,true,false,false,false,false,false, true, 1},
    {Op::One, OperandCategory::None, OperandLayout::Simple, false,false,true,false,false,false,false,false, true, 1},
    {Op::Add, OperandCategory::None, OperandLayout::Simple, true,false,false,false,false,false,false,false, true, -1},
    {Op::Subtract, OperandCategory::None, OperandLayout::Simple, true,false,false,false,false,false,false,false, true, -1},
    {Op::Multiply, OperandCategory::None, OperandLayout::Simple, true,false,false,false,false,false,false,false, true, -1},
    {Op::Divide, OperandCategory::None, OperandLayout::Simple, true,false,false,false,false,false,false,false, true, -1},
    {Op::IntDivide, OperandCategory::None, OperandLayout::Simple, true,false,false,false,false,false,false,false, true, -1},
    {Op::Modulo, OperandCategory::None, OperandLayout::Simple, true,false,false,false,false,false,false,false, true, -1},
    {Op::Negate, OperandCategory::None, OperandLayout::Simple, true,false,false,false,false,false,false,false, false, 0},
    {Op::Increment, OperandCategory::None, OperandLayout::Simple, true,false,false,false,false,false,false,false, false, 0},
    {Op::Decrement, OperandCategory::None, OperandLayout::Simple, true,false,false,false,false,false,false,false, false, 0},
    {Op::Equal, OperandCategory::None, OperandLayout::Simple, false,true,false,false,false,false,false,false, true, -1},
    {Op::NotEqual, OperandCategory::None, OperandLayout::Simple, false,true,false,false,false,false,false,false, true, -1},
    {Op::Less, OperandCategory::None, OperandLayout::Simple, false,true,false,false,false,false,false,false, true, -1},
    {Op::LessEqual, OperandCategory::None, OperandLayout::Simple, false,true,false,false,false,false,false,false, true, -1},
    {Op::Greater, OperandCategory::None, OperandLayout::Simple, false,true,false,false,false,false,false,false, true, -1},
    {Op::GreaterEqual, OperandCategory::None, OperandLayout::Simple, false,true,false,false,false,false,false,false, true, -1},
    {Op::Not, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::And, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::Or, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::Concatenate, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, true, -1},
    {Op::Interpolate, OperandCategory::None, OperandLayout::U8, false,false,false,false,false,false,false,false, false, 0},
    {Op::MakeArray, OperandCategory::None, OperandLayout::U16, false,false,false,false,false,false,false,false, false, 0},
    {Op::MakeDict, OperandCategory::None, OperandLayout::U16, false,false,false,false,false,false,false,false, false, 0},
    {Op::MakeTuple, OperandCategory::None, OperandLayout::U16, false,false,false,false,false,false,false,false, false, 0},
    {Op::MakeRange, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, true, -1},
    {Op::MakeRangeInc, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, true, -1},
    {Op::IndexGet, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, true, -1},
    {Op::IndexSet, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, true, -2},
    {Op::IndexGetOpt, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::MakeRecord, OperandCategory::None, OperandLayout::MakeRecord, false,false,false,false,false,false,false,false, false, 0},
    {Op::GetField, OperandCategory::Name, OperandLayout::U16, false,false,false,false,false,false,false,false, false, 0},
    {Op::SetField, OperandCategory::Name, OperandLayout::U16, false,false,false,false,false,false,false,false, false, 0},
    {Op::GetFieldOpt, OperandCategory::Name, OperandLayout::U16, false,false,false,false,false,false,false,false, false, 0},
    {Op::RecordWith, OperandCategory::None, OperandLayout::RecordWith, false,false,false,false,false,false,false,false, false, 0},
    {Op::MakeChoice, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, true, -1},
    {Op::MakeChoiceConstructor, OperandCategory::None, OperandLayout::U8, false,false,false,false,false,false,false,false, true, -1},
    {Op::MakeSuccess, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::MakeFailure, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::MakeSome, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::Unwrap, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::ResultInner, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::IsSuccess, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::IsSome, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::EnsureSuccess, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::Downcast, OperandCategory::Name, OperandLayout::U16, false,false,false,false,false,false,false,false, false, 0},
    {Op::TrustedDowncast, OperandCategory::Name, OperandLayout::U16, false,false,false,false,false,false,false,false, false, 0},
    {Op::IsType, OperandCategory::Name, OperandLayout::U16, false,false,false,false,false,false,false,false, false, 0},
    {Op::Jump, OperandCategory::None, OperandLayout::U32Jump, false,false,false,false,true,true,true,true, false, 0},
    {Op::JumpIfFalse, OperandCategory::None, OperandLayout::U32Jump, false,false,false,false,true,false,true,true, false, 0},
    {Op::JumpIfTrue, OperandCategory::None, OperandLayout::U32Jump, false,false,false,false,true,false,true,true, false, 0},
    {Op::Loop, OperandCategory::None, OperandLayout::U32Jump, false,false,false,false,true,false,false,true, false, 0},
    {Op::NullCoalesce, OperandCategory::None, OperandLayout::U32Jump, false,false,false,false,true,false,true,true, false, 0},
    {Op::Call, OperandCategory::None, OperandLayout::U8, false,false,false,false,true,false,false,false, false, 0},
    {Op::CallNamed, OperandCategory::None, OperandLayout::TwoU8, false,false,false,false,true,false,false,false, false, 0},
    {Op::TailCall, OperandCategory::None, OperandLayout::U8, false,false,false,false,true,false,false,false, false, 0},
    {Op::Return, OperandCategory::None, OperandLayout::Simple, false,false,false,false,true,true,false,false, true, 0},
    {Op::MakeClosure, OperandCategory::None, OperandLayout::MakeClosure, false,false,false,false,false,false,false,false, false, 0},
    {Op::Pipe, OperandCategory::None, OperandLayout::Simple, false,false,false,false,true,false,false,false, false, 0},
    {Op::ErrorPipe, OperandCategory::None, OperandLayout::Simple, false,false,false,false,true,false,false,false, false, 0},
    {Op::TryCatch, OperandCategory::None, OperandLayout::U32Jump, false,false,false,false,true,false,true,true, false, 0},
    {Op::TryEnd, OperandCategory::None, OperandLayout::Simple, false,false,false,false,true,false,false,false, false, 0},
    {Op::Rethrow, OperandCategory::None, OperandLayout::Simple, false,false,false,false,true,false,false,false, false, 0},
    {Op::MatchStart, OperandCategory::None, OperandLayout::Simple, false,false,false,false,true,false,false,false, false, 0},
    {Op::MatchArm, OperandCategory::None, OperandLayout::Simple, false,false,false,false,true,false,false,false, false, 0},
    {Op::MatchEnd, OperandCategory::None, OperandLayout::Simple, false,false,false,false,true,false,false,false, false, 0},
    {Op::Contains, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, true, -1},
    {Op::Spawn, OperandCategory::None, OperandLayout::U8, false,false,false,false,false,false,false,false, false, 0},
    {Op::Await, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::TaskScopeBegin, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::TaskScopeEnd, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::ForIterInit, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::ForIterStep, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::ForIterStepKV, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::Print, OperandCategory::None, OperandLayout::U8, false,false,false,false,false,false,false,false, false, 0},
    {Op::Assert, OperandCategory::None, OperandLayout::U8, false,false,false,false,false,false,false,false, false, 0},
    {Op::TypeOf, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::IncrementLocal, OperandCategory::Local, OperandLayout::U16, false,false,false,false,false,false,false,false, false, 0},
    {Op::DecrementLocal, OperandCategory::Local, OperandLayout::U16, false,false,false,false,false,false,false,false, false, 0},
    {Op::SetLocalPop, OperandCategory::Local, OperandLayout::U16, false,false,false,false,false,false,false,false, true, -1},
    {Op::GetLocalReturn, OperandCategory::Local, OperandLayout::U16, false,false,false,false,false,true,false,false, true, 0},
    {Op::IntToNumber, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::Clone, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
    {Op::EndModule, OperandCategory::None, OperandLayout::Simple, false,false,false,false,false,false,false,false, false, 0},
}};
// clang-format on

[[nodiscard]] constexpr bool opcode_metadata_matches() {
    for (const auto& e : k_opcode_expectations) {
        if (operand_category(e.op) != e.category) {
            return false;
        }
        if (opcode_operand_layout(e.op) != e.layout) {
            return false;
        }
        if (is_arithmetic(e.op) != e.arithmetic) {
            return false;
        }
        if (is_comparison(e.op) != e.comparison) {
            return false;
        }
        if (is_stack_op(e.op) != e.stack_op) {
            return false;
        }
        if (is_load_store(e.op) != e.load_store) {
            return false;
        }
        if (is_control_flow(e.op) != e.control_flow) {
            return false;
        }
        if (is_terminator(e.op) != e.terminator) {
            return false;
        }
        if (is_forward_jump(e.op) != e.forward_jump) {
            return false;
        }
        if (is_jump(e.op) != e.jump) {
            return false;
        }
        const auto effect = fixed_stack_effect(e.op);
        if (effect.has_value() != e.has_fixed_effect) {
            return false;
        }
        if (e.has_fixed_effect && *effect != e.fixed_effect) {
            return false;
        }
    }
    return true;
}

static_assert(k_opcode_expectations.size() == static_cast<std::size_t>(Op::EndModule) + 1,
              "characterization table must cover every defined opcode");

// AddressSanitizer instruments every access to a global variable, which makes
// the address of `opcode_table` (reached here through find_opcode_info) not a
// core constant expression.  GCC then rejects this static_assert with
// "'(&opcode_table[0]) != 0' is not a constant expression".  The identical
// check runs at run time in test_opcode_metadata_characterization below, which
// executes under ASan, so nothing is lost by skipping the compile-time form
// there.  Detect ASan via GCC's __SANITIZE_ADDRESS__ and Clang's __has_feature.
#if defined(__SANITIZE_ADDRESS__)
#define LUMA_OPCODE_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define LUMA_OPCODE_TEST_ASAN 1
#endif
#endif

#if !defined(LUMA_OPCODE_TEST_ASAN)
static_assert(opcode_metadata_matches(),
              "opcode metadata drifted from the R02 characterization baseline");
#endif

#undef LUMA_OPCODE_TEST_ASAN

} // namespace

static void test_opcode_metadata_characterization() {
    // The static_assert above already proves this at compile time; assert again
    // at run time so the characterization is visible in the test report.
    ASSERT_TRUE(opcode_metadata_matches());
    ASSERT_EQ(k_opcode_expectations.size(), static_cast<std::size_t>(Op::EndModule) + 1);
}

int main() {
    // Records.
    RUN(test_compile_make_record);
    RUN(test_compile_get_field);
    RUN(test_compile_set_field);
    RUN(test_compile_record_with);

    // Choice types.
    RUN(test_compile_make_choice);
    RUN(test_compile_make_choice_constructor);
    RUN(test_disassemble_make_choice_constructor);

    // Match.
    RUN(test_compile_match_start_end);

    // Pipe operators.
    RUN(test_compile_pipe);
    RUN(test_compile_error_pipe);

    // Result / Optional.
    RUN(test_compile_make_success);
    RUN(test_compile_make_failure);
    RUN(test_compile_make_some);

    // Null coalescing.
    RUN(test_compile_null_coalesce);

    // Stack manipulation.
    RUN(test_compile_pop);

    // TypeOf.
    RUN(test_compile_typeof);

    // For iteration.
    RUN(test_compile_for_iter_kv);

    // Range inclusive.
    RUN(test_compile_range_inclusive);

    // Comparison operators.
    RUN(test_compile_less_equal);
    RUN(test_compile_greater);
    RUN(test_compile_greater_equal);
    RUN(test_compile_not_equal);
    RUN(test_compile_equal);

    // Arithmetic operators.
    RUN(test_compile_subtract);
    RUN(test_compile_multiply);
    RUN(test_compile_divide);
    RUN(test_compile_int_divide);
    RUN(test_compile_modulo);

    // Print / Assert.
    RUN(test_compile_print);
    RUN(test_compile_assert);

    // Index set.
    RUN(test_compile_index_set);

    // Upvalue access.
    RUN(test_compile_get_upvalue);

    // EndModule.
    RUN(test_compile_end_module);

    // Dup.
    RUN(test_compile_dup_via_set_local);

    // Named arguments.
    RUN(test_compile_call_named);
    RUN(test_compile_call_named_mixed);

    // Downcast / IsType.
    RUN(test_compile_downcast);
    RUN(test_compile_trusted_downcast);
    RUN(test_compile_is_type);

    // TailCall.
    RUN(test_compile_tail_call);

    // Clone.
    RUN(test_compile_clone_mutable);

    // IntToNumber.
    RUN(test_compile_int_to_number);

    // Optional chaining.
    RUN(test_compile_get_field_opt);
    RUN(test_compile_index_get_opt);

    // Rethrow.
    RUN(test_compile_try_finally_emits_rethrow);
    RUN(test_compile_try_catch_finally_emits_rethrow);
    RUN(test_compile_try_catch_has_no_rethrow);

    // Concurrency.
    RUN(test_compile_spawn);
    RUN(test_compile_await);
    RUN(test_compile_task_scope);
    // R02 opcode metadata characterization.
    RUN(test_opcode_metadata_characterization);

    return SUMMARY();
}
