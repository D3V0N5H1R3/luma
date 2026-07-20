// Compiler unit tests — opcode emission patterns.

#include <cstddef>
#include <string>
#include <string_view>

#include "compiler_test_helpers.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "shared_eval.hpp"
#include "test_framework.hpp"

using namespace luma;
using luma::test::has_opcode;

// ─── Helper ───

static CompileResult compile(const std::string& source, bool repl_mode = true) {
    const auto program = luma::test::lex_and_parse(source);

    Compiler compiler;

    return compiler.compile(program, repl_mode);
}

// ─── Tests ───

static void test_compile_integer_literal() {
    const auto result = compile("42");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Constant));
}

static void test_compile_boolean_true() {
    const auto result = compile("true");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::True));
}

static void test_compile_boolean_false() {
    const auto result = compile("false");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::False));
}

static void test_compile_none_literal() {
    const auto result = compile("none");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::None));
}

static void test_compile_addition() {
    const auto result = compile("1 + 2");

    ASSERT_TRUE(result.success);
    // Should be constant-folded — no Add opcode.
    ASSERT_FALSE(has_opcode(result.top_level.chunk(), Op::Add));
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Constant));
}

static void test_compile_constant_folding_integer() {
    const auto result = compile("3 * 4 + 2");

    ASSERT_TRUE(result.success);
    // 3 * 4 should be folded to 12, then 12 + 2 folded to 14.
    // Note: depends on parse tree shape — at minimum one fold occurs.
    ASSERT_TRUE(result.top_level.chunk().constants.size() >= 1);
}

static void test_compile_string_interpolation() {
    const auto result = compile("integer x = 42\n\"value ${x}\"");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Interpolate));
}

static void test_compile_array_literal() {
    const auto result = compile("[1, 2, 3]");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::MakeArray));
}

static void test_compile_function_declaration() {
    const auto result = compile("function integer add(integer a, integer b) { return a + b }");

    ASSERT_TRUE(result.success);
    // Should produce at least one compiled function.
    ASSERT_FALSE(result.functions.empty());
    ASSERT_EQ(result.functions[0].arity, 2);
}

static void test_compile_if_statement() {
    const auto result = compile("if true { 1 }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::JumpIfFalse));
}

static void test_compile_while_loop() {
    const auto result = compile("mutable integer x = 0\nwhile x < 10 { x += 1 }");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Loop));
}

static void test_compile_increment_local_fused() {
    const auto result = compile("function void f() { mutable integer x = 0\nx++ }");

    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.functions.empty());
    ASSERT_TRUE(has_opcode(result.functions[0].chunk(), Op::IncrementLocal));
}

static void test_compile_decrement_local_fused() {
    const auto result = compile("function void f() { mutable integer x = 10\nx-- }");

    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.functions.empty());
    ASSERT_TRUE(has_opcode(result.functions[0].chunk(), Op::DecrementLocal));
}

static void test_compile_set_local_pop_fused() {
    const auto result = compile("function void f() { mutable integer x = 0\nx = 5 }");

    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.functions.empty());
    ASSERT_TRUE(has_opcode(result.functions[0].chunk(), Op::SetLocalPop));
}

static void test_compile_empty_program() {
    const auto result = compile("");

    ASSERT_TRUE(result.success);
}

static void test_compile_comparison() {
    const auto result = compile("mutable integer a = 1\na < 2");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Less));
}

static void test_compile_logical_and_short_circuit() {
    const auto result = compile("mutable boolean a = true\na && false");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::JumpIfFalse));
}

static void test_compile_return_statement() {
    const auto result = compile("function integer f() { return 42 }");

    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.functions.empty());
    ASSERT_TRUE(has_opcode(result.functions[0].chunk(), Op::Return));
}

// ─── Additional opcode emission tests ───

static void test_compile_dictionary_literal() {
    const auto result = compile("{\"a\": 1, \"b\": 2}");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::MakeDict));
}

static void test_compile_tuple_literal() {
    const auto result = compile("(1, \"hello\", true)");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::MakeTuple));
}

static void test_compile_range() {
    const auto result = compile("mutable integer a = 0\nmutable integer b = 10\na..b");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::MakeRange));
}

static void test_compile_contains_operator() {
    const auto result = compile("3 in [1, 2, 3]");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Contains));
}

static void test_compile_try_catch() {
    const auto result = compile("function void f() {\n"
                                "    try { integer x = 1 } catch(e) { integer y = 2 }\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.functions.empty());
    ASSERT_TRUE(has_opcode(result.functions[0].chunk(), Op::TryCatch));
    ASSERT_TRUE(has_opcode(result.functions[0].chunk(), Op::TryEnd));
}

// Regression: a non-local exit (return/break/continue) inside a finally whose
// try is ALSO non-locally exited previously re-entered emit_try_unwind over the
// same still-active handler set, recursing without bound → a compile-time
// native stack overflow (0xC00000FD).  emit_try_unwind now detaches the
// handlers it is unwinding before emitting their finally bodies, so a nested
// exit only unwinds OUTER handlers.  Compiling must succeed and, at runtime,
// the finally's return must win (yields 2, not 1).
static void test_compile_return_in_finally_does_not_recurse() {
    const auto result = compile("function integer choose() {\n"
                                "    try {\n"
                                "        return 1\n"
                                "    } finally {\n"
                                "        return 2\n"
                                "    }\n"
                                "}");

    ASSERT_TRUE(result.success);

    const auto value = luma::test::eval("function integer choose() {\n"
                                        "    try {\n"
                                        "        return 1\n"
                                        "    } finally {\n"
                                        "        return 2\n"
                                        "    }\n"
                                        "}\n"
                                        "choose()\n");

    ASSERT_EQ(value.as_integer(), 2);
}

static void test_compile_for_in_loop() {
    const auto result = compile("function void f() {\n"
                                "    for x in [1, 2, 3] { print(x) }\n"
                                "}");

    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.functions.empty());
    ASSERT_TRUE(has_opcode(result.functions[0].chunk(), Op::ForIterInit));
    ASSERT_TRUE(has_opcode(result.functions[0].chunk(), Op::ForIterStep));
}

static void test_compile_closure() {
    const auto result = compile("function void f() {\n"
                                "    integer x = 10\n"
                                "    function(integer) -> integer add = (integer y) -> x + y\n"
                                "}");

    ASSERT_TRUE(result.success);
    // At least one function should be compiled as a closure.
    bool found_closure = false;
    for (const auto& fn : result.functions) {
        if (has_opcode(fn.chunk(), Op::MakeClosure)) {
            found_closure = true;
            break;
        }
    }
    ASSERT_TRUE(found_closure);
}

static void test_compile_block_body_closure() {
    // A block-bodied lambda capturing an outer local must still emit MakeClosure.
    const auto result = compile("function void f() {\n"
                                "    integer base = 10\n"
                                "    function(integer) -> integer g = (integer x) -> {\n"
                                "        integer y = x * x\n"
                                "        return y + base\n"
                                "    }\n"
                                "}");

    ASSERT_TRUE(result.success);
    bool found_closure = false;
    for (const auto& fn : result.functions) {
        if (has_opcode(fn.chunk(), Op::MakeClosure)) {
            found_closure = true;
            break;
        }
    }
    ASSERT_TRUE(found_closure);
}

static void test_compile_nested_closure() {
    // Each lambda nesting level is compiled into its own function, so a nested
    // lambda yields strictly more compiled functions than a single-level one.
    const auto single = compile("function void f() {\n"
                                "    function(integer) -> integer g = (integer x) -> x + 1\n"
                                "}");
    const auto nested = compile("function void f() {\n"
                                "    function(integer) -> function(integer) -> integer make =\n"
                                "        (integer n) -> (integer x) -> x + n\n"
                                "}");

    ASSERT_TRUE(single.success);
    ASSERT_TRUE(nested.success);
    ASSERT_TRUE(nested.functions.size() > single.functions.size());
}

// Returns true when any emitted diagnostic message contains `needle`.
static bool has_diagnostic(const CompileResult& result, std::string_view needle) {
    for (const auto& diag : result.diagnostics) {
        if (diag.message.find(needle) != std::string::npos) {
            return true;
        }
    }

    return false;
}

static void test_compile_too_many_functions_fails_cleanly() {
    // Each lambda literal compiles to a distinct function addressed by a 16-bit
    // MakeClosure operand. Exceeding the 16-bit index space must be reported as a
    // clean compile error rather than silently wrapping the function index and
    // miscompiling every closure past the limit.
    std::string source;
    source.reserve((CompilerLimits::k_max_functions + 2) * 8);
    for (std::size_t i = 0; i <= CompilerLimits::k_max_functions; ++i) {
        source += "() -> 0\n";
    }

    const auto result = compile(source);

    ASSERT_FALSE(result.success);
    ASSERT_TRUE(has_diagnostic(result, "too many functions"));
}

static void test_compile_lambda_too_many_upvalues_fails_cleanly() {
    // A lambda capturing more than k_max_upvalues enclosing locals must be
    // reported as a clean compile error. The upvalue-count check runs before the
    // MakeClosure is emitted, so the error path leaves no truncated instruction.
    const std::size_t count = CompilerLimits::k_max_upvalues + 1;

    std::string source = "function void outer() {\n";
    for (std::size_t i = 0; i < count; ++i) {
        source += "    integer v" + std::to_string(i) + " = 0\n";
    }
    source += "    function() -> integer g = () -> {\n";
    for (std::size_t i = 0; i < count; ++i) {
        source += "        v" + std::to_string(i) + "\n";
    }
    source += "        return 0\n    }\n}\n";

    const auto result = compile(source);

    ASSERT_FALSE(result.success);
    ASSERT_TRUE(has_diagnostic(result, "too many upvalues"));
}

static void test_compile_negate() {
    const auto result = compile("function integer f(integer x) { return -x }");

    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.functions.empty());
    ASSERT_TRUE(has_opcode(result.functions[0].chunk(), Op::Negate));
}

static void test_compile_not() {
    const auto result = compile("mutable boolean x = true\n!x");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Not));
}

static void test_compile_bitwise_and() {
    const auto result = compile("mutable integer a = 5\nmutable integer b = 3\na & b");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::BitwiseAnd));
}

static void test_compile_concatenate() {
    const auto result = compile("mutable string a = \"hello\"\na + \" world\"");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Concatenate) ||
                has_opcode(result.top_level.chunk(), Op::Add));
}

static void test_compile_index_get() {
    const auto result = compile("mutable array<integer> a = [1, 2, 3]\na[0]");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::IndexGet));
}

static void test_compile_logical_or_short_circuit() {
    const auto result = compile("mutable boolean a = false\na || true");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::JumpIfTrue));
}

// ─── Const folder overflow guard tests ───
// These verify that expressions involving INT64_MIN with -1 don't trigger UB
// at compile time.  The const folder should either fold safely (to a double) or
// bail out and leave the expression for the VM.

static void test_const_fold_int64_min_div_neg1() {
    // INT64_MIN / -1 overflows int64_t.  The const folder must not crash.
    const auto result = compile("-9223372036854775808 // -1");
    ASSERT_TRUE(result.success);
}

static void test_const_fold_int64_min_mul_neg1() {
    // INT64_MIN * -1 overflows int64_t.  The const folder must not crash.
    const auto result = compile("-9223372036854775808 * -1");
    ASSERT_TRUE(result.success);
}

static void test_const_fold_int64_min_mod_neg1() {
    // INT64_MIN % -1 is UB in C++.  The const folder must bail out.
    const auto result = compile("-9223372036854775808 % -1");
    ASSERT_TRUE(result.success);
}

static void test_const_fold_division_by_zero() {
    // Division by zero should not crash the const folder.
    const auto result = compile("42 // 0");
    ASSERT_TRUE(result.success);
}

// Two integer literals differing by 1 but both > 2^53.  A double-based compare
// collapses them to the same value (wrongly equal); the const folder must
// compare them as exact int64 so the folded constant agrees with the VM's
// runtime comparison.  Pre-fix `==` folded to Op::True; post-fix it is Op::False.
static void test_const_fold_large_integer_comparison_is_exact() {
    const auto equal = compile("9007199254740993 == 9007199254740992");
    ASSERT_TRUE(equal.success);
    ASSERT_TRUE(has_opcode(equal.top_level.chunk(), Op::False));
    ASSERT_FALSE(has_opcode(equal.top_level.chunk(), Op::True));

    const auto not_equal = compile("9007199254740993 != 9007199254740992");
    ASSERT_TRUE(not_equal.success);
    ASSERT_TRUE(has_opcode(not_equal.top_level.chunk(), Op::True));
    ASSERT_FALSE(has_opcode(not_equal.top_level.chunk(), Op::False));

    const auto greater = compile("9007199254740993 > 9007199254740992");
    ASSERT_TRUE(greater.success);
    ASSERT_TRUE(has_opcode(greater.top_level.chunk(), Op::True));
    ASSERT_FALSE(has_opcode(greater.top_level.chunk(), Op::False));
}

// ─── Const folder overflow promotion characterization (R01) ───
// Signed + and * that overflow int64 promote to a double constant at compile
// time (matching the VM), so the folded chunk carries a number constant and no
// arithmetic opcode.  This pins the "promote, don't decline" half of the
// overflow contract shared with common/overflow.hpp; the // and % INT64_MIN / -1
// cases above pin the "decline" half.

// Returns true if any constant in the chunk is a floating-point (number) value.
static bool has_number_constant(const Chunk& chunk) {
    for (const auto& constant : chunk.constants) {
        if (constant.is_number()) {
            return true;
        }
    }

    return false;
}

static void test_const_fold_add_overflow_promotes_to_double() {
    // INT64_MAX + 1 overflows int64 and folds to a double constant.
    const auto result = compile("9223372036854775807 + 1");
    ASSERT_TRUE(result.success);
    ASSERT_FALSE(has_opcode(result.top_level.chunk(), Op::Add));
    ASSERT_TRUE(has_number_constant(result.top_level.chunk()));
}

static void test_const_fold_mul_overflow_promotes_to_double() {
    // INT64_MAX * 2 overflows int64 and folds to a double constant.
    const auto result = compile("9223372036854775807 * 2");
    ASSERT_TRUE(result.success);
    ASSERT_FALSE(has_opcode(result.top_level.chunk(), Op::Multiply));
    ASSERT_TRUE(has_number_constant(result.top_level.chunk()));
}

// ─── Main ───

int main() {
    RUN(test_compile_integer_literal);
    RUN(test_compile_boolean_true);
    RUN(test_compile_boolean_false);
    RUN(test_compile_none_literal);
    RUN(test_compile_addition);
    RUN(test_compile_constant_folding_integer);
    RUN(test_compile_string_interpolation);
    RUN(test_compile_array_literal);
    RUN(test_compile_function_declaration);
    RUN(test_compile_if_statement);
    RUN(test_compile_while_loop);
    RUN(test_compile_increment_local_fused);
    RUN(test_compile_decrement_local_fused);
    RUN(test_compile_set_local_pop_fused);
    RUN(test_compile_empty_program);
    RUN(test_compile_comparison);
    RUN(test_compile_logical_and_short_circuit);
    RUN(test_compile_return_statement);

    // Additional opcode emission tests.
    RUN(test_compile_dictionary_literal);
    RUN(test_compile_tuple_literal);
    RUN(test_compile_range);
    RUN(test_compile_contains_operator);
    RUN(test_compile_try_catch);
    RUN(test_compile_return_in_finally_does_not_recurse);
    RUN(test_compile_for_in_loop);
    RUN(test_compile_closure);
    RUN(test_compile_block_body_closure);
    RUN(test_compile_nested_closure);
    RUN(test_compile_too_many_functions_fails_cleanly);
    RUN(test_compile_lambda_too_many_upvalues_fails_cleanly);
    RUN(test_compile_negate);
    RUN(test_compile_not);
    RUN(test_compile_bitwise_and);
    RUN(test_compile_concatenate);
    RUN(test_compile_index_get);
    RUN(test_compile_logical_or_short_circuit);

    // Const folder overflow guards.
    RUN(test_const_fold_int64_min_div_neg1);
    RUN(test_const_fold_int64_min_mul_neg1);
    RUN(test_const_fold_int64_min_mod_neg1);
    RUN(test_const_fold_division_by_zero);
    RUN(test_const_fold_large_integer_comparison_is_exact);
    RUN(test_const_fold_add_overflow_promotes_to_double);
    RUN(test_const_fold_mul_overflow_promotes_to_double);

    return SUMMARY();
}
