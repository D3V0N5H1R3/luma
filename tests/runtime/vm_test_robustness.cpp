// VM unit tests: crafted-bytecode robustness and debugger introspection.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/errors/error.hpp"
#include "common/resource_limits.hpp"
#include "runtime/compiler/compiled_function.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_introspection.hpp"
#include "runtime/vm/vm_types.hpp"
#include "stdlib_test_helpers.hpp"

using luma::Args;
using luma::ArrayValue;
using luma::CompiledFunction;
using luma::define_native;
using luma::InternalError;
using luma::NullValue;
using luma::NumericWidener;
using luma::Op;
using luma::RuntimeError;
using luma::SourceLocation;
using luma::TupleValue;
using luma::TypeMatcher;
using luma::Value;
using luma::VM;
using luma::VMIntrospector;
using luma::test::compile_for_eval;
using luma::test::lex_and_parse;
using luma::test::make_std_env;

// ─── Crafted-bytecode robustness (untrusted .lumc trust boundary) ───
//
// A corrupt or malicious .lumc file can hand the VM bytecode that the static
// verifier does not fully constrain. The VM must turn such input into a
// catchable error rather than reading out of bounds.

namespace {

// Build a self-contained, arity-0 top-level function from raw bytecode bytes.
CompiledFunction make_raw_function(std::initializer_list<std::uint8_t> code) {
    CompiledFunction func;
    func.name = "<crafted>";
    func.mutable_chunk().code = std::vector<std::uint8_t>(code);
    return func;
}

// Run a crafted top-level function through a fresh VM.
void run_crafted(const CompiledFunction& func) {
    const auto env = make_std_env();
    VM vm{env};
    (void)vm.execute_function(func);
}

// Run a crafted top-level function, with its child functions, through a fresh VM.
void run_crafted(const std::vector<CompiledFunction>& functions, const CompiledFunction& top) {
    const auto env = make_std_env();
    VM vm{env};
    vm.execute(functions, top);
}

// Run `action`, which must throw a RuntimeError — the VM's own, catchable error
// type — and return its message.  Any other exception (such as the
// std::bad_variant_access that the VM's type guards prevent) or no exception
// fails the test, so a test using this pins the hardening rather than merely
// "something threw".
template <typename Action> [[nodiscard]] std::string runtime_error_message(Action&& action) {
    try {
        std::forward<Action>(action)();
    } catch (const RuntimeError& error) {
        return error.what();
    } catch (const std::exception& error) {
        luma::test::throw_assertion_error(std::string{"expected a RuntimeError, got: "} +
                                          error.what());
    }

    luma::test::throw_assertion_error("expected a RuntimeError, but nothing was thrown");
}

// A crafted .lumc can hand MakeClosure a child function whose upvalue descriptor
// carries an out-of-range index.  The verifier only bounds the GetUpvalue /
// SetUpvalue *operands* against a function's upvalue_count — it never inspects
// the descriptor array — and the deserializer accepts any u16 index, so the VM
// must range-check the descriptor when forwarding it rather than reading out of
// bounds.  Builds a top-level MakeClosure over a single child function carrying
// one bad descriptor and returns the RuntimeError message execution raised.
std::string crafted_closure_upvalue_error(bool is_local) {
    // Slot 60000 is far beyond any real frame or upvalue array but still inside
    // the value stack's backing buffer, so an unchecked access reads live-but-
    // wrong memory instead of segfaulting.  The child is never executed —
    // MakeClosure fails while forwarding the descriptor.
    CompiledFunction child;
    child.name = "<child>";
    child.upvalue_count = 1;
    child.upvalues.push_back(
        {.index = static_cast<std::uint16_t>(60000), .is_local = is_local, .is_mutable = false});
    child.mutable_chunk().code = {static_cast<std::uint8_t>(Op::Return)};

    std::vector<CompiledFunction> functions;
    functions.push_back(std::move(child));

    // Top-level: MakeClosure over function 0 (big-endian u16 index 0x0000, u8
    // upvalue_count 1), then return.
    const auto top = make_raw_function({
        static_cast<std::uint8_t>(Op::MakeClosure),
        0x00U,
        0x00U,
        0x01U,
        static_cast<std::uint8_t>(Op::Return),
    });

    return runtime_error_message([&] { run_crafted(functions, top); });
}

} // namespace

LUMA_TEST(vm_invalid_opcode_is_rejected) {
    // 0xFF is well beyond the highest defined opcode. The dispatch table must
    // span the full uint8_t range so this maps to a catchable "invalid opcode"
    // error rather than an out-of-bounds dispatch-table read.
    const auto message = runtime_error_message([] { run_crafted(make_raw_function({0xFFU})); });
    ASSERT_NE(message.find("unknown or reserved opcode"), std::string::npos);
}

LUMA_TEST(vm_verified_out_of_bounds_local_is_rejected) {
    // A crafted function can set its verified bit yet write to a local slot far
    // beyond its frame. Because a frame's slot offset is a runtime quantity the
    // verifier cannot bound, the VM must range-check local slots even on the
    // verified fast path. read_u16 is big-endian, so 0xEA 0x60 == slot 60000.
    auto func = make_raw_function({
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::SetLocal),
        0xEAU,
        0x60U,
        static_cast<std::uint8_t>(Op::Return),
    });
    func.set_is_verified(true);
    const auto message = runtime_error_message([&] { run_crafted(func); });
    ASSERT_NE(message.find("local variable index out of bounds"), std::string::npos);
}

LUMA_TEST(vm_crafted_closure_local_upvalue_index_is_rejected) {
    // is_local descriptor: forward_upvalue computes slot_offset + desc.index and
    // reads the value stack.  An out-of-range index must be caught, not read.
    const auto message = crafted_closure_upvalue_error(/*is_local=*/true);
    ASSERT_NE(message.find("invalid upvalue index 60000"), std::string::npos);
}

LUMA_TEST(vm_crafted_closure_forwarded_upvalue_index_is_rejected) {
    // Forwarded (is_local == false) descriptor: forward_upvalue indexes the
    // parent closure's upvalues array (size 0 for a top-level frame).  An
    // out-of-range index must be caught, not copied out of bounds (a Value copy
    // through a garbage pointer corrupts a refcount).
    const auto message = crafted_closure_upvalue_error(/*is_local=*/false);
    ASSERT_NE(message.find("invalid upvalue index 60000"), std::string::npos);
}

// A crafted .lumc can jump onto ForIterStep with an iterator-state tuple whose
// index element is not an integer. dispatch_iter_step guards the tuple's size
// but must also guard the index element's type, or as_integer() throws
// std::bad_variant_access instead of a clean RuntimeError.
LUMA_TEST(vm_crafted_iterator_state_non_integer_index_is_rejected) {
    // A well-formed state is [iterable, integer index]; craft [array, string].
    auto state = std::make_shared<TupleValue>();
    state->elements.push_back(Value{std::make_shared<ArrayValue>()}); // iterable slot
    state->elements.push_back(Value{"not-an-integer"});               // index slot (wrong type)

    CompiledFunction func;
    func.name = "<crafted>";
    const auto slot = func.mutable_chunk().constants.add(Value{std::move(state)});
    func.mutable_chunk().code = {
        static_cast<std::uint8_t>(Op::Constant), static_cast<std::uint8_t>((slot >> 8) & 0xFF),
        static_cast<std::uint8_t>(slot & 0xFF),  static_cast<std::uint8_t>(Op::ForIterStep),
        static_cast<std::uint8_t>(Op::Return),
    };

    const auto message = runtime_error_message([&] { run_crafted(func); });
    ASSERT_NE(message.find("invalid iterator state"), std::string::npos);
}

// A crafted .lumc can leave a non-string in a CallNamed argument-name slot. The
// compiled-callee path reads it via as_string_mut(); without a type guard that
// throws std::bad_variant_access rather than a clean RuntimeError.
LUMA_TEST(vm_crafted_named_call_non_string_name_is_rejected) {
    CompiledFunction callee;
    callee.name = "<callee>";
    callee.mutable_chunk().code = {static_cast<std::uint8_t>(Op::Return)};

    std::vector<CompiledFunction> functions;
    functions.push_back(std::move(callee));

    // Top-level: close over function 0, push a non-string name operand (Op::True)
    // and a value operand (Op::True), then CallNamed with 0 positional / 1 named.
    const auto top = make_raw_function({
        static_cast<std::uint8_t>(Op::MakeClosure),
        0x00U,
        0x00U,                               // function index 0 (big-endian u16)
        0x00U,                               // upvalue_count
        static_cast<std::uint8_t>(Op::True), // name operand — not a string
        static_cast<std::uint8_t>(Op::True), // value operand
        static_cast<std::uint8_t>(Op::CallNamed),
        0x00U, // pos_count
        0x01U, // named_count
        static_cast<std::uint8_t>(Op::Return),
    });

    // Pre-hardening this raised std::bad_variant_access, which fails here.
    const auto message = runtime_error_message([&] { run_crafted(functions, top); });
    ASSERT_NE(message.find("argument name is not a string"), std::string::npos);
}

// A crafted .lumc (or a REPL call, which skips the type checker) can invoke a
// compiled function with fewer positional arguments than its required_arity.
// Before the fix, VM::call_function only padded up to `arity` and never
// checked `arg_count` against `required_arity`, so a missing required
// parameter silently ran the function body with that slot set to `none`
// instead of raising a clear arity error.
LUMA_TEST(vm_call_with_too_few_required_args_is_rejected) {
    // Child function requires 2 parameters (arity == required_arity == 2) and
    // simply returns.
    CompiledFunction callee;
    callee.name = "<callee>";
    callee.arity = 2;
    callee.required_arity = 2;
    callee.param_names = {"a", "b"};
    callee.build_param_name_index();
    callee.mutable_chunk().code = {static_cast<std::uint8_t>(Op::Return)};

    std::vector<CompiledFunction> functions;
    functions.push_back(std::move(callee));

    // Top-level: MakeClosure over function 0, push a single positional
    // argument (Op::True), then Op::Call with arg_count 1 — one short of the
    // callee's required_arity of 2.
    const auto top = make_raw_function({
        static_cast<std::uint8_t>(Op::MakeClosure),
        0x00U,
        0x00U, // function index 0 (big-endian u16)
        0x00U, // upvalue_count
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::Call),
        0x01U, // arg_count
        static_cast<std::uint8_t>(Op::Return),
    });

    // Pre-fix, no error was raised at all — the function ran silently.
    const auto message = runtime_error_message([&] { run_crafted(functions, top); });
    ASSERT_NE(message.find("too few arguments: function requires at least 2"), std::string::npos);
}

// The named-call path (Op::CallNamed) builds its argument vector padded to
// the full arity and never validated that the required parameter prefix was
// actually bound, so `f(b: 1)` on a function requiring both `a` and `b`
// silently ran with `a` set to `none`.
LUMA_TEST(vm_named_call_missing_required_argument_is_rejected) {
    CompiledFunction callee;
    callee.name = "<callee>";
    callee.arity = 2;
    callee.required_arity = 2;
    callee.param_names = {"a", "b"};
    callee.build_param_name_index();
    callee.mutable_chunk().code = {static_cast<std::uint8_t>(Op::Return)};

    std::vector<CompiledFunction> functions;
    functions.push_back(std::move(callee));

    CompiledFunction top_fn;
    top_fn.name = "<crafted>";

    const auto name_slot = top_fn.mutable_chunk().constants.add(Value{std::string{"b"}});

    top_fn.mutable_chunk().code = {
        static_cast<std::uint8_t>(Op::MakeClosure),
        0x00U,
        0x00U, // function index 0 (big-endian u16)
        0x00U, // upvalue_count
        static_cast<std::uint8_t>(Op::Constant),
        static_cast<std::uint8_t>((name_slot >> 8) & 0xFF),
        static_cast<std::uint8_t>(name_slot & 0xFF), // name operand "b"
        static_cast<std::uint8_t>(Op::True),         // value operand
        static_cast<std::uint8_t>(Op::CallNamed),
        0x00U, // pos_count
        0x01U, // named_count — only "b" is supplied, "a" is missing
        static_cast<std::uint8_t>(Op::Return),
    };

    // Pre-fix, no error was raised at all — "a" silently ran as `none`.
    const auto message = runtime_error_message([&] { run_crafted(functions, top_fn); });
    ASSERT_NE(message.find("missing required argument 'a'"), std::string::npos);
}

// ─── Debugger introspection ───

// Regression test for the VMIntrospector dual-storage bug.  A mutable captured
// variable lives in the closure's heap cell (upvalue_cells), not the inline
// upvalues array.  Before the fix, reading such an upvalue while paused showed
// a stale/none value, and "set variable" silently no-opped because it wrote the
// unused inline slot the running program never reads.  Pause inside a closure
// that captures a mutable local, assert the read-back reflects the live cell,
// then set it and confirm the edit round-trips through continued execution.
LUMA_TEST(vm_introspect_mutable_upvalue_round_trip) {
    // `inner()` is called in a non-tail position (its result is bound to a
    // local before returning) so make_and_run's frame — which defines the
    // captured name — stays live on the stack for upvalue-name resolution.
    // A direct `return inner()` would be tail-call optimised, discarding that
    // frame and leaving the name unresolvable (a tail-call limitation, not the
    // dual-storage bug under test).
    const std::string source = "function integer make_and_run() {\n"
                               "    mutable integer captured = 100\n"
                               "    function() -> integer inner = () -> {\n"
                               "        integer probe = 0\n"
                               "        return captured + probe\n"
                               "    }\n"
                               "    integer produced = inner()\n"
                               "    return produced\n"
                               "}\n"
                               "make_and_run()\n";

    const auto program = lex_and_parse(source);
    const auto compiled = compile_for_eval(program);
    const auto env = make_std_env();

    VM vm{env};

    bool captured_seen = false;
    bool read_back_ok = false;
    bool set_ok = false;

    // The hook fires on every line change while a pause is armed.  Returning
    // false keeps the pause armed without actually halting, so it re-fires as
    // execution steps into the closure frame (the only frame with upvalues).
    vm.set_debug_hook([&](int /*file_id*/, int /*line*/, std::size_t depth) -> bool {
        if (captured_seen || depth == 0) {
            return false;
        }

        const VMIntrospector intro(vm);
        const std::size_t top = depth - 1;

        if (!intro.has_upvalues(top)) {
            return false;
        }

        for (const auto& uv : intro.upvalues(top)) {
            if (uv.name == "captured") {
                captured_seen = true;
                read_back_ok =
                    uv.is_mutable && uv.value.is_integer() && uv.value.as_integer() == 100;
                set_ok = VMIntrospector::set_upvalue(vm, top, "captured", Value{999});
            }
        }

        return false;
    });

    vm.request_pause_check();

    const auto result = vm.execute_function(compiled.top_level, compiled.functions);

    ASSERT_TRUE(captured_seen); // The closure frame was observed while paused.
    ASSERT_TRUE(read_back_ok);  // Read reflected the live cell (100), not none.
    ASSERT_TRUE(set_ok);        // set_upvalue reported success.
    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 999); // The edit round-tripped through the cell.
}

// A crafted .lumc can carry a type-pattern string (referenced by IsType /
// Downcast / TrustedDowncast) nested far deeper than any source-level type,
// which the parser caps at load time.  TypeMatcher::matches recurses once per
// optional<...> layer on the same value, so without a depth cap such a string
// overflows the native stack.  The matcher must instead bottom out at a safe
// "no match" — reaching the assertion at all proves the recursion is bounded.
LUMA_TEST(vm_typematcher_deeply_nested_pattern_is_bounded) {
    constexpr int layers = 100000;

    std::string pattern;
    pattern.reserve(static_cast<std::size_t>(layers) * 9 + 16);

    for (int i = 0; i < layers; ++i) {
        pattern += "optional<";
    }

    pattern += "integer";

    for (int i = 0; i < layers; ++i) {
        pattern += ">";
    }

    // An integer value is not `none`, so every layer takes the recursing branch
    // until the depth cap stops it and returns false.
    ASSERT_FALSE(TypeMatcher::matches(Value{static_cast<std::int64_t>(0)}, pattern));
}

// NumericWidener converts integers wherever a pattern says `number`, copying
// only the containers it changes and leaving other components untouched.
LUMA_TEST(vm_numeric_widener_converts_nested_integers) {
    const auto array = std::make_shared<ArrayValue>();
    array->elements->push_back(Value{static_cast<std::int64_t>(1)});
    array->elements->push_back(Value{2.5});
    const Value original{array};

    const auto widened = NumericWidener::widen(original, "array<number>");

    ASSERT_TRUE(widened.is_array());
    ASSERT_TRUE((*widened.as_array()->elements)[0].is_number());
    ASSERT_TRUE((*widened.as_array()->elements)[1].is_number());
    // The source array is not mutated in place: other holders keep its value.
    ASSERT_TRUE((*original.as_array()->elements)[0].is_integer());

    const auto tuple = std::make_shared<TupleValue>();
    tuple->elements = {Value{static_cast<std::int64_t>(3)}, Value{static_cast<std::int64_t>(4)}};
    const auto widened_tuple = NumericWidener::widen(Value{tuple}, "(number,_)");

    ASSERT_TRUE(widened_tuple.as_tuple()->elements[0].is_number());
    ASSERT_TRUE(widened_tuple.as_tuple()->elements[1].is_integer());
}

LUMA_TEST(vm_numeric_widener_shares_unchanged_values) {
    const auto array = std::make_shared<ArrayValue>();
    array->elements->push_back(Value{1.5});
    const Value original{array};

    const auto widened = NumericWidener::widen(original, "array<number>");

    // Nothing needed converting, so the storage is shared rather than copied.
    ASSERT_EQ(widened.as_array().get(), original.as_array().get());
    ASSERT_TRUE(NumericWidener::widen(Value{NullValue{}}, "optional<number>").is_null());
    ASSERT_TRUE(NumericWidener::widen(Value{std::string{"7"}}, "number").is_string());
}

// Regression (review #16): Op::TailCall padded missing parameters with `none`
// without checking required_arity, unlike the Op::Call path above.
LUMA_TEST(vm_tail_call_with_too_few_required_args_is_rejected) {
    CompiledFunction callee;
    callee.name = "<callee>";
    callee.arity = 2;
    callee.required_arity = 2;
    callee.param_names = {"a", "b"};
    callee.build_param_name_index();
    callee.mutable_chunk().code = {static_cast<std::uint8_t>(Op::None),
                                   static_cast<std::uint8_t>(Op::Return)};

    std::vector<CompiledFunction> functions;
    functions.push_back(std::move(callee));

    // Top-level: close over function 0, push one argument, tail-call it.
    const auto top = make_raw_function({
        static_cast<std::uint8_t>(Op::MakeClosure),
        0x00U,
        0x00U, // function index 0 (big-endian u16)
        0x00U, // upvalue_count
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::TailCall),
        0x01U, // arg_count — one short of required_arity
        static_cast<std::uint8_t>(Op::Return),
    });

    // Pre-fix, no error was raised at all — the callee ran with b = none.
    const auto message = runtime_error_message([&] { run_crafted(functions, top); });
    ASSERT_NE(message.find("too few arguments"), std::string::npos);
}

// Regression (review #11): a function that executes Op::TryCatch and returns
// without Op::TryEnd left a stale handler behind.  A later error in the caller
// then "caught" it by installing the dead function's catch_ip into the
// caller's frame — executing the callee's bytecode in the wrong frame (and,
// with a hostile offset, reading outside the caller's chunk).  The handler
// must die with its frame, so the caller's error propagates.
LUMA_TEST(vm_stale_exception_handler_does_not_survive_return) {
    CompiledFunction callee;
    callee.name = "<leaky-try>";
    // TryCatch with offset 0 (catch_ip = the None below), then return without
    // the matching TryEnd.
    callee.mutable_chunk().code = {
        static_cast<std::uint8_t>(Op::TryCatch),
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        static_cast<std::uint8_t>(Op::None),
        static_cast<std::uint8_t>(Op::Return),
    };

    std::vector<CompiledFunction> functions;
    functions.push_back(std::move(callee));

    // Top-level: call the leaky function, discard its result, then hit an
    // invalid opcode (a runtime error with no live handler).
    const auto top = make_raw_function({
        static_cast<std::uint8_t>(Op::MakeClosure),
        0x00U,
        0x00U,
        0x00U,
        static_cast<std::uint8_t>(Op::Call),
        0x00U,
        static_cast<std::uint8_t>(Op::Pop),
        0xFFU,
        static_cast<std::uint8_t>(Op::None),
        static_cast<std::uint8_t>(Op::Return),
    });

    // The invalid-opcode error itself must surface — not a secondary fault from
    // running the dead frame's bytecode.  Pre-fix: the stale handler swallowed
    // the error and the top-level frame resumed inside <leaky-try>'s bytecode.
    const auto message = runtime_error_message([&] { run_crafted(functions, top); });
    ASSERT_NE(message.find("unknown or reserved opcode"), std::string::npos);
}

// ─── Built-in fault classification ───
//
// call_native turns a non-runtime_error exception escaping a built-in into a
// catchable error.  An interpreter fault must surface as InternalError (so the
// fuzz harness can flag it as a defect), while allocation pressure stays an
// ordinary RuntimeError.

namespace {

// How the VM surfaced an exception escaping a built-in.
enum class SurfacedAs {
    nothing,
    internal_error,
    runtime_error,
    other
};

// Call a native that throws `fault` and classify the exception the VM raised.
template <typename Fault> SurfacedAs raise_from_native(Fault fault) {
    const auto compiled = compile_for_eval(lex_and_parse("explode()"));
    const auto env = make_std_env();
    define_native(env, "explode", [fault](Args, SourceLocation) -> Value { throw fault; });

    VM vm{env};
    try {
        (void)vm.execute_function(compiled.top_level, compiled.functions);
    } catch (const InternalError& error) {
        // The original fault's message must survive the conversion.
        return std::string{error.what()}.find("broken invariant") != std::string::npos
                   ? SurfacedAs::internal_error
                   : SurfacedAs::other;
    } catch (const RuntimeError&) {
        return SurfacedAs::runtime_error;
    } catch (const std::exception&) {
        return SurfacedAs::other;
    }
    return SurfacedAs::nothing;
}

} // namespace

LUMA_TEST(vm_native_logic_error_becomes_internal_error) {
    ASSERT_EQ(raise_from_native(std::logic_error{"broken invariant"}), SurfacedAs::internal_error);
}

LUMA_TEST(vm_native_bad_alloc_stays_plain_runtime_error) {
    ASSERT_EQ(raise_from_native(std::bad_alloc{}), SurfacedAs::runtime_error);
}

// ─── Call arity (too many arguments) ───
//
// call_function rejected too few arguments but not too many: the extra
// arguments landed in the callee's first local slots, so a zero-parameter
// lambda's first local read the caller's argument.  Every bytecode call path
// funnels through call_function (Call, pipes, spawn), and tail calls mirror
// the check.  Built-ins calling a callback instead drop the arguments it does
// not declare (VM::init_call_fn), because they pass a fixed argument list
// (Dictionary.map passes key and value) to callbacks that may ignore the
// trailing ones.  eval() skips the type checker, so these reach the VM.

LUMA_TEST(vm_call_with_too_many_arguments_is_rejected) {
    ASSERT_EVAL_THROWS("function integer f() {\n"
                       "    integer base = 40\n"
                       "    return base + 2\n"
                       "}\n"
                       "f(\"surprise\")",
                       "too many arguments: function expects 0 but got 1");
}

LUMA_TEST(vm_lambda_call_with_too_many_arguments_is_rejected) {
    ASSERT_EVAL_THROWS("function() -> integer g = () -> 1\n"
                       "g(1, 2)",
                       "too many arguments: function expects 0 but got 2");
}

LUMA_TEST(vm_pipe_call_with_too_many_arguments_is_rejected) {
    ASSERT_EVAL_THROWS("function integer twice(integer x) { return x * 2 }\n"
                       "3 |> twice(4)",
                       "too many arguments: function expects 1 but got 2");
}

LUMA_TEST(vm_native_callback_drops_undeclared_arguments) {
    // Array.map invokes the closure with one argument; a zero-parameter
    // closure ignores it, and it must not land in the closure's first local.
    const auto result = luma::test::eval("Array.map([1, 2], () -> {\n"
                                         "    integer base = 40\n"
                                         "    return base + 2\n"
                                         "})");

    ASSERT_TRUE(result.is_result());
    ASSERT_TRUE(result.as_result()->is_success);
    const auto& mapped = *result.as_result()->owned_inner->as_array()->elements;
    ASSERT_EQ(mapped.size(), 2U);
    ASSERT_EQ(mapped[0].as_integer(), 42);
    ASSERT_EQ(mapped[1].as_integer(), 42);
}

LUMA_TEST(vm_native_callback_with_fewer_parameters_runs) {
    // Dictionary.reduce passes (accumulator, key, value) and Array.reduce
    // (accumulator, element); callbacks that ignore the trailing arguments ran
    // before the arity check and must keep running.
    const auto by_key = luma::test::eval(
        "Dictionary.reduce({\"a\": 1, \"b\": 2}, 0, (integer acc, string _k) -> acc + 1)");

    ASSERT_TRUE(by_key.is_result());
    ASSERT_TRUE(by_key.as_result()->is_success);
    ASSERT_EQ(by_key.as_result()->owned_inner->as_integer(), 2);

    const auto counted = luma::test::eval("Array.reduce([1, 2, 3], 0, (integer acc) -> acc + 1)");

    ASSERT_TRUE(counted.is_result());
    ASSERT_TRUE(counted.as_result()->is_success);
    ASSERT_EQ(counted.as_result()->owned_inner->as_integer(), 3);
}

LUMA_TEST(vm_tail_call_with_too_many_arguments_is_rejected) {
    ASSERT_EVAL_THROWS("function integer zero() { return 0 }\n"
                       "function integer f(integer n) { return zero(n) }\n"
                       "f(1)",
                       "too many arguments: function expects 0 but got 1");
}

LUMA_TEST(vm_call_with_optional_parameters_still_pads) {
    const auto result = luma::test::eval("function integer f(integer a, integer b = 5) {\n"
                                         "    return a + b\n"
                                         "}\n"
                                         "f(1)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 6);
}

// ─── String repetition limits ───
//
// The `*` operator skipped ResourceLimits::max_string_repeat and looped
// `count` times even for an empty string, so `"" * 9223372036854775807`
// spun for centuries.

LUMA_TEST(vm_string_repeat_of_empty_string_returns_immediately) {
    // At the maximum allowed count the empty string must not iterate `count`
    // times; past it the count limit applies, exactly as for String.repeat.
    const luma::test::LimitGuard guard{luma::ResourceLimits::max_string_repeat,
                                       std::int64_t{1'000'000'000}};

    const auto result = luma::test::eval("\"\" * 1000000000");

    ASSERT_TRUE(result.is_string());
    ASSERT_TRUE(result.as_string().empty());

    ASSERT_EVAL_THROWS("\"\" * 9223372036854775807", "string repeat count exceeds maximum");
}

LUMA_TEST(vm_string_repeat_enforces_max_string_repeat) {
    const luma::test::LimitGuard guard{luma::ResourceLimits::max_string_repeat,
                                       std::int64_t{1'000}};

    ASSERT_EVAL_THROWS("\"ab\" * 1001", "string repeat count exceeds maximum");
    ASSERT_EVAL_THROWS("1001 * \"ab\"", "string repeat count exceeds maximum");
}

LUMA_TEST(vm_string_repeat_builds_expected_result) {
    const auto result = luma::test::eval("\"abc\" * 5");

    ASSERT_TRUE(result.is_string());
    ASSERT_EQ(result.as_string(), std::string{"abcabcabcabcabc"});

    const auto odd = luma::test::eval("7 * \"xy\"");

    ASSERT_EQ(odd.as_string(), std::string{"xyxyxyxyxyxyxy"});
}

LUMA_TEST(vm_string_repeat_enforces_max_string_size) {
    const luma::test::LimitGuard guard{luma::ResourceLimits::max_string_size, std::size_t{1'000}};

    ASSERT_EVAL_THROWS("\"abcd\" * 251", "string repetition exceeds maximum string size");
}

// ─── Dictionary index assignment limit ───
//
// `d[key] = value` inserted new keys without the max_dictionary_size check
// that Dictionary.set performs.

LUMA_TEST(vm_dictionary_index_assignment_enforces_max_dictionary_size) {
    const luma::test::LimitGuard guard{luma::ResourceLimits::max_dictionary_size, std::size_t{2}};

    ASSERT_EVAL_THROWS("function integer f() {\n"
                       "    mutable dictionary<integer> d = {\"z\": 0}\n"
                       "    d[\"a\"] = 1\n"
                       "    d[\"b\"] = 2\n"
                       "    d[\"c\"] = 3\n"
                       "    return 0\n"
                       "}\n"
                       "f()",
                       "Dictionary index assignment: result exceeds maximum dictionary size");
}

LUMA_TEST(vm_dictionary_index_assignment_overwrite_at_limit_is_allowed) {
    const luma::test::LimitGuard guard{luma::ResourceLimits::max_dictionary_size, std::size_t{3}};

    const auto result = luma::test::eval("function integer f() {\n"
                                         "    mutable dictionary<integer> d = {\"z\": 0}\n"
                                         "    d[\"a\"] = 1\n"
                                         "    d[\"b\"] = 2\n"
                                         "    d[\"b\"] = 5\n"
                                         "    return d[\"b\"]\n"
                                         "}\n"
                                         "f()");

    ASSERT_EQ(result.as_integer(), 5);
}

LUMA_TEST(vm_collection_literals_enforce_configured_limits) {
    const luma::test::LimitGuard array_guard{luma::ResourceLimits::max_array_size, std::size_t{2}};
    const luma::test::LimitGuard dict_guard{luma::ResourceLimits::max_dictionary_size,
                                            std::size_t{1}};

    ASSERT_EVAL_THROWS("[1, 2, 3]", "array literal: result exceeds maximum array size");
    ASSERT_EVAL_THROWS("{\"a\": 1, \"b\": 2}",
                       "dictionary literal: result exceeds maximum dictionary size");
}

// ─── Main ───

int main() {
    LUMA_RUN_ALL();
}
