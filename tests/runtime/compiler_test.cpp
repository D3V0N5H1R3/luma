// Compiler unit tests — opcode emission patterns.

#include <cstddef>
#include <string>
#include <string_view>

#include "compiler_test_helpers.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/verifier.hpp"
#include "runtime/interpreter/value.hpp"
#include "shared_eval.hpp"
#include "test_framework.hpp"

using namespace luma;
using luma::test::eval;
using luma::test::eval_throws;
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

static void test_compile_contains_range_operator() {
    const auto result = compile("5 in 1..=100");

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Contains));
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::MakeRangeInc));
}

static void test_compile_match_range_pattern() {
    // A range arm reuses the `in` bounds test: MakeRange(Inc) + Contains.
    const auto inc = compile("integer x = 5\n"
                             "match x {\n"
                             "    case 0..=9 { }\n"
                             "    else { }\n"
                             "}");

    ASSERT_TRUE(inc.success);
    ASSERT_TRUE(has_opcode(inc.top_level.chunk(), Op::MakeRangeInc));
    ASSERT_TRUE(has_opcode(inc.top_level.chunk(), Op::Contains));

    const auto half_open = compile("integer x = 5\n"
                                   "match x {\n"
                                   "    case 0..9 { }\n"
                                   "    else { }\n"
                                   "}");

    ASSERT_TRUE(half_open.success);
    ASSERT_TRUE(has_opcode(half_open.top_level.chunk(), Op::MakeRange));
    ASSERT_TRUE(has_opcode(half_open.top_level.chunk(), Op::Contains));
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
// at compile time.  The const folder must bail out and leave the expression for
// the VM (which raises the overflow at runtime).

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

// ─── Const folder integer overflow (R01) ───
// Signed +, - and * whose exact result does not fit in an int64 are NOT folded:
// the VM raises "integer overflow in <operation>" for them instead of widening
// to `number`, so the folder leaves the operator in place for the runtime to
// raise.  (With the type checker enabled such a constant expression is already a
// compile error; unchecked compilation — REPL, debugger — relies on this.)

// Returns true if any constant in the chunk is a floating-point (number) value.
static bool has_number_constant(const Chunk& chunk) {
    for (const auto& constant : chunk.constants) {
        if (constant.is_number()) {
            return true;
        }
    }

    return false;
}

// Returns the message of the error evaluating `source` raises ("" if none).
static std::string eval_error_message(const std::string& source) {
    try {
        (void)eval(source);
    } catch (const std::exception& e) {
        return e.what();
    }

    return {};
}

static void test_const_fold_add_overflow_is_left_for_runtime() {
    const auto result = compile("9223372036854775807 + 1");
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Add));
    ASSERT_FALSE(has_number_constant(result.top_level.chunk()));

    ASSERT_TRUE(
        eval_error_message("9223372036854775807 + 1").find("integer overflow in addition") !=
        std::string::npos);
}

static void test_const_fold_mul_overflow_is_left_for_runtime() {
    const auto result = compile("9223372036854775807 * 2");
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(has_opcode(result.top_level.chunk(), Op::Multiply));
    ASSERT_FALSE(has_number_constant(result.top_level.chunk()));

    ASSERT_TRUE(
        eval_error_message("9223372036854775807 * 2").find("integer overflow in multiplication") !=
        std::string::npos);
}

static void test_const_fold_sub_overflow_is_left_for_runtime() {
    const auto result = compile("-9223372036854775807 - 2");
    ASSERT_TRUE(result.success);
    ASSERT_FALSE(has_number_constant(result.top_level.chunk()));

    ASSERT_TRUE(
        eval_error_message("-9223372036854775807 - 2").find("integer overflow in subtraction") !=
        std::string::npos);
}

// Non-overflowing integer arithmetic still folds to an integer constant.
static void test_const_fold_in_range_integer_arithmetic_still_folds() {
    const auto result = compile("9223372036854775806 + 1");
    ASSERT_TRUE(result.success);
    ASSERT_FALSE(has_opcode(result.top_level.chunk(), Op::Add));
    ASSERT_FALSE(has_number_constant(result.top_level.chunk()));
}

// Integer/number equality folds with the VM's exact rule (Value::equals): a
// number equals an integer only when it is exactly that whole value.  Routing
// the comparison through double made `2^53 + 1 == 2^53.0` fold to true.
static void test_const_fold_integer_number_equality_is_exact() {
    const auto above = compile("9007199254740993 == 9007199254740992.0");
    ASSERT_TRUE(above.success);
    ASSERT_TRUE(has_opcode(above.top_level.chunk(), Op::False));
    ASSERT_FALSE(has_opcode(above.top_level.chunk(), Op::True));

    const auto above_ne = compile("9007199254740992.0 != 9007199254740993");
    ASSERT_TRUE(above_ne.success);
    ASSERT_TRUE(has_opcode(above_ne.top_level.chunk(), Op::True));
    ASSERT_FALSE(has_opcode(above_ne.top_level.chunk(), Op::False));

    const auto whole = compile("9007199254740992 == 9007199254740992.0");
    ASSERT_TRUE(whole.success);
    ASSERT_TRUE(has_opcode(whole.top_level.chunk(), Op::True));

    const auto fraction = compile("3 == 3.5");
    ASSERT_TRUE(fraction.success);
    ASSERT_TRUE(has_opcode(fraction.top_level.chunk(), Op::False));

    const auto same = compile("3 != 3.0");
    ASSERT_TRUE(same.success);
    ASSERT_TRUE(has_opcode(same.top_level.chunk(), Op::False));

    // The folded result agrees with the unfolded runtime comparison.
    const auto runtime = eval("integer i = 9007199254740993\n"
                              "number n = 9007199254740992.0\n"
                              "i == n\n");
    ASSERT_TRUE(runtime.is_bool());
    ASSERT_FALSE(runtime.as_bool());
}

// ─── Code-generation regressions ───

static const CompiledFunction* find_function(const CompileResult& result, std::string_view name) {
    for (const auto& fn : result.functions) {
        if (fn.name == name) {
            return &fn;
        }
    }

    return nullptr;
}

// Regression: the implicit `None; Return` was skipped whenever the last emitted
// opcode was a Return, even though the then-branch's Jump was later patched to
// land on the end of the code. A zero-parameter function (no scope-exit Pops)
// then ran off the end of its bytecode ("bytecode truncated").
static void test_compile_branch_ending_in_return_keeps_implicit_return() {
    const auto result = compile("function boolean ready() { return true }\n"
                                "function void go() {\n"
                                "    if ready() {\n"
                                "        print(\"work\")\n"
                                "    } else {\n"
                                "        return\n"
                                "    }\n"
                                "}\n",
                                /*repl_mode=*/false);

    ASSERT_TRUE(result.success);

    const auto* go = find_function(result, "go");
    ASSERT_TRUE(go != nullptr);

    const auto& code = go->chunk().code;
    ASSERT_TRUE(code.size() >= 2);
    ASSERT_EQ(static_cast<Op>(code[code.size() - 2]), Op::None);
    ASSERT_EQ(static_cast<Op>(code.back()), Op::Return);

    BytecodeVerifier verifier;
    ASSERT_TRUE(verifier.verify(*go).empty());

    // Runs to completion instead of falling past the last instruction.
    ASSERT_FALSE(eval_throws("function boolean ready() { return true }\n"
                             "function void go() {\n"
                             "    if ready() { 1 } else { return }\n"
                             "}\n"
                             "go()\n"));
}

// A block-bodied lambda that already ends in `return` gets no unreachable
// `None; Return` tail (it used to defeat the -O2 inliner's terminator check).
static void test_compile_block_lambda_ending_in_return_has_no_dead_tail() {
    const auto result = compile("function() -> integer f = () -> { return 1 }");

    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.functions.empty());
    ASSERT_EQ(luma::test::count_opcode(result.functions[0].chunk(), Op::Return), 1U);
}

// Regression: `return <expr>` inside try/finally compiled the finally body while
// the return value sat untracked on the operand stack, so a local declared in
// the finally body was assigned the return value's slot and read it back.
static void test_return_through_finally_keeps_finally_locals_apart() {
    const auto value = eval("mutable string seen = \"\"\n"
                            "function integer f() {\n"
                            "    try {\n"
                            "        return 42\n"
                            "    } finally {\n"
                            "        string message = \"done\"\n"
                            "        seen = message\n"
                            "    }\n"
                            "}\n"
                            "integer r = f()\n"
                            "\"${seen}:${r}\"\n");

    ASSERT_TRUE(value.is_string());
    ASSERT_EQ(value.as_string(), std::string{"done:42"});
}

// Same invariant for the failure path of the `?` operator.
static void test_question_mark_through_finally_keeps_finally_locals_apart() {
    const auto value = eval("mutable string seen = \"\"\n"
                            "function result<integer> parse() { return failure(\"bad\") }\n"
                            "function result<integer> f() {\n"
                            "    try {\n"
                            "        integer v = parse()?\n"
                            "        return success(v)\n"
                            "    } finally {\n"
                            "        string message = \"cleanup\"\n"
                            "        seen = message\n"
                            "    }\n"
                            "}\n"
                            "result<integer> r = f()\n"
                            "\"${seen}:${r}\"\n");

    ASSERT_TRUE(value.is_string());
    ASSERT_TRUE(value.as_string().starts_with("cleanup:"));
}

// Regression: break/continue popped the loop body's locals *before* compiling
// the enclosing finally bodies, which still addressed those locals by slot.
static void test_break_through_finally_reads_loop_body_locals() {
    const auto value = eval("mutable string seen = \"\"\n"
                            "mutable integer i = 0\n"
                            "while i < 3 {\n"
                            "    string label = \"x${i}\"\n"
                            "    try {\n"
                            "        if i == 1 { break }\n"
                            "    } finally {\n"
                            "        string suffix = \"!\"\n"
                            "        seen = \"${seen}${label}${suffix}\"\n"
                            "    }\n"
                            "    i += 1\n"
                            "}\n"
                            "seen\n");

    ASSERT_TRUE(value.is_string());
    ASSERT_EQ(value.as_string(), std::string{"x0!x1!"});
}

// Regression: `?` inside @main returned silently from main, bypassing any
// enclosing try/catch and exiting 0. It must raise a RuntimeError instead.
static void test_question_mark_in_main_raises_catchable_error() {
    const auto value = eval("function result<integer> parse() { return failure(\"bad input\") }\n"
                            "@main\n"
                            "function string main() {\n"
                            "    try {\n"
                            "        integer v = parse()?\n"
                            "        return \"unreached ${v}\"\n"
                            "    } catch (err) {\n"
                            "        return \"caught: ${err}\"\n"
                            "    }\n"
                            "}\n"
                            "main()\n");

    ASSERT_TRUE(value.is_string());
    ASSERT_TRUE(value.as_string().starts_with("caught: "));
    ASSERT_TRUE(value.as_string().find("bad input") != std::string::npos);

    // Uncaught, the failure terminates the program.
    ASSERT_TRUE(eval_throws("function result<integer> parse() { return failure(\"bad\") }\n"
                            "@main\n"
                            "function void main() {\n"
                            "    integer v = parse()?\n"
                            "    print(v)\n"
                            "}\n"
                            "main()\n"));

    // A success value is still unwrapped.
    const auto ok = eval("function result<integer> parse() { return success(7) }\n"
                         "@main\n"
                         "function integer main() {\n"
                         "    return parse()? + 1\n"
                         "}\n"
                         "main()\n");
    ASSERT_TRUE(ok.is_integer());
    ASSERT_EQ(ok.as_integer(), 8);
}

// Regression: an interpolation made only of empty string literals pushed no
// value at all.
static void test_interpolation_of_only_empty_literals_pushes_empty_string() {
    const auto value = eval("string s = \"${\"\"}\"\n\"[${s}]\"\n");

    ASSERT_TRUE(value.is_string());
    ASSERT_EQ(value.as_string(), std::string{"[]"});
}

// A full constant pool must be reported as a located compile error instead of
// letting std::overflow_error escape Compiler::compile.
static void test_compile_too_many_constants_fails_cleanly() {
    std::string source;
    source.reserve((CompilerLimits::k_max_constants + 2) * 8);
    for (std::size_t i = 0; i <= CompilerLimits::k_max_constants; ++i) {
        source += std::to_string(i + 1000);
        source += '\n';
    }

    const auto result = compile(source);

    ASSERT_FALSE(result.success);
    ASSERT_TRUE(has_diagnostic(result, "too many distinct constants"));

    // Reported once, not once per overflowing constant.
    std::size_t reports = 0;
    for (const auto& diag : result.diagnostics) {
        if (diag.message.find("too many distinct constants") != std::string::npos) {
            ++reports;
        }
    }
    ASSERT_EQ(reports, 1U);
}

// Same for a full name table.
static void test_compile_too_many_names_fails_cleanly() {
    std::string source;
    source.reserve((CompilerLimits::k_max_names + 2) * 8);
    for (std::size_t i = 0; i <= CompilerLimits::k_max_names; ++i) {
        source += "g" + std::to_string(i) + '\n';
    }

    const auto result = compile(source);

    ASSERT_FALSE(result.success);
    ASSERT_TRUE(has_diagnostic(result, "too many distinct global and field names"));
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
    RUN(test_compile_contains_range_operator);
    RUN(test_compile_match_range_pattern);
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
    RUN(test_compile_concatenate);
    RUN(test_compile_index_get);
    RUN(test_compile_logical_or_short_circuit);

    // Const folder overflow guards.
    RUN(test_const_fold_int64_min_div_neg1);
    RUN(test_const_fold_int64_min_mul_neg1);
    RUN(test_const_fold_int64_min_mod_neg1);
    RUN(test_const_fold_division_by_zero);
    RUN(test_const_fold_large_integer_comparison_is_exact);
    RUN(test_const_fold_add_overflow_is_left_for_runtime);
    RUN(test_const_fold_mul_overflow_is_left_for_runtime);
    RUN(test_const_fold_sub_overflow_is_left_for_runtime);
    RUN(test_const_fold_in_range_integer_arithmetic_still_folds);
    RUN(test_const_fold_integer_number_equality_is_exact);
    RUN(test_compile_branch_ending_in_return_keeps_implicit_return);
    RUN(test_compile_block_lambda_ending_in_return_has_no_dead_tail);
    RUN(test_return_through_finally_keeps_finally_locals_apart);
    RUN(test_question_mark_through_finally_keeps_finally_locals_apart);
    RUN(test_break_through_finally_reads_loop_body_locals);
    RUN(test_question_mark_in_main_raises_catchable_error);
    RUN(test_interpolation_of_only_empty_literals_pushes_empty_string);
    RUN(test_compile_too_many_constants_fails_cleanly);
    RUN(test_compile_too_many_names_fails_cleanly);

    return SUMMARY();
}
