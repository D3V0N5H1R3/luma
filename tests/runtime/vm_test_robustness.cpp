// VM unit tests: crafted-bytecode robustness and debugger introspection.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/errors/error.hpp"
#include "runtime/compiler/compiled_function.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_introspection.hpp"
#include "runtime/vm/vm_types.hpp"
#include "stdlib_test_helpers.hpp"

// ─── Crafted-bytecode robustness (untrusted .lumc trust boundary) ───
//
// A corrupt or malicious .lumc file can hand the VM bytecode that the static
// verifier does not fully constrain. The VM must turn such input into a
// catchable error rather than reading out of bounds.

// Build a self-contained, arity-0 top-level function from raw bytecode bytes.
static CompiledFunction make_raw_function(std::initializer_list<std::uint8_t> code) {
    CompiledFunction func;
    func.name = "<crafted>";
    func.mutable_chunk().code = std::vector<std::uint8_t>(code);
    return func;
}

// Run a crafted function through a fresh VM; report whether it raised a
// catchable error instead of crashing or silently misbehaving.
static bool crafted_function_throws(const CompiledFunction& func) {
    const auto env = luma::test::make_std_env();
    VM vm{env};
    try {
        (void)vm.execute_function(func);
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

// Like crafted_function_throws, but only reports success for a RuntimeError —
// the VM's own error type. A std::bad_variant_access (the failure mode these
// type guards remove) is treated as a miss, so the test actually pins the fix.
static bool crafted_function_raises_runtime_error(const CompiledFunction& func) {
    const auto env = luma::test::make_std_env();
    VM vm{env};
    try {
        (void)vm.execute_function(func);
    } catch (const RuntimeError&) {
        return true;
    } catch (const std::exception&) {
        return false;
    }
    return false;
}

LUMA_TEST(vm_invalid_opcode_is_rejected) {
    // 0xFF is well beyond the highest defined opcode. The dispatch table must
    // span the full uint8_t range so this maps to a catchable "invalid opcode"
    // error rather than an out-of-bounds dispatch-table read.
    ASSERT_TRUE(crafted_function_throws(make_raw_function({0xFFU})));
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
    ASSERT_TRUE(crafted_function_throws(func));
}

// A crafted .lumc can hand MakeClosure a child function whose upvalue descriptor
// carries an out-of-range index.  The verifier only bounds the GetUpvalue /
// SetUpvalue *operands* against a function's upvalue_count — it never inspects
// the descriptor array — and the deserializer accepts any u16 index, so the VM
// must range-check the descriptor when forwarding it rather than reading out of
// bounds.  Builds a top-level MakeClosure over a single child function carrying
// one bad descriptor and reports whether execution raised a catchable error.
static bool crafted_closure_upvalue_throws(bool is_local) {
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
    auto top = make_raw_function({
        static_cast<std::uint8_t>(Op::MakeClosure),
        0x00U,
        0x00U,
        0x01U,
        static_cast<std::uint8_t>(Op::Return),
    });

    const auto env = luma::test::make_std_env();
    VM vm{env};
    try {
        vm.execute(functions, top);
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

LUMA_TEST(vm_crafted_closure_local_upvalue_index_is_rejected) {
    // is_local descriptor: forward_upvalue computes slot_offset + desc.index and
    // reads the value stack.  An out-of-range index must be caught, not read.
    ASSERT_TRUE(crafted_closure_upvalue_throws(true));
}

LUMA_TEST(vm_crafted_closure_forwarded_upvalue_index_is_rejected) {
    // Forwarded (is_local == false) descriptor: forward_upvalue indexes the
    // parent closure's upvalues array (size 0 for a top-level frame).  An
    // out-of-range index must be caught, not copied out of bounds (a Value copy
    // through a garbage pointer corrupts a refcount).
    ASSERT_TRUE(crafted_closure_upvalue_throws(false));
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

    ASSERT_TRUE(crafted_function_raises_runtime_error(func));
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
    auto top = make_raw_function({
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

    const auto env = luma::test::make_std_env();
    VM vm{env};
    bool raised_runtime_error = false;
    try {
        vm.execute(functions, top);
    } catch (const RuntimeError&) {
        raised_runtime_error = true;
    } catch (const std::exception&) {
        // e.g. std::bad_variant_access — the pre-hardening failure mode.
    }
    ASSERT_TRUE(raised_runtime_error);
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
    auto top = make_raw_function({
        static_cast<std::uint8_t>(Op::MakeClosure),
        0x00U,
        0x00U, // function index 0 (big-endian u16)
        0x00U, // upvalue_count
        static_cast<std::uint8_t>(Op::True),
        static_cast<std::uint8_t>(Op::Call),
        0x01U, // arg_count
        static_cast<std::uint8_t>(Op::Return),
    });

    const auto env = luma::test::make_std_env();
    VM vm{env};
    bool raised_runtime_error = false;
    try {
        vm.execute(functions, top);
    } catch (const RuntimeError&) {
        raised_runtime_error = true;
    } catch (const std::exception&) {
        // Pre-fix, no error was raised at all — the function ran silently.
    }
    ASSERT_TRUE(raised_runtime_error);
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

    const auto env = luma::test::make_std_env();
    VM vm{env};
    bool raised_runtime_error = false;
    try {
        vm.execute(functions, top_fn);
    } catch (const RuntimeError&) {
        raised_runtime_error = true;
    } catch (const std::exception&) {
        // Pre-fix, no error was raised at all — "a" silently ran as `none`.
    }
    ASSERT_TRUE(raised_runtime_error);
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

    const auto program = luma::test::lex_and_parse(source);
    const auto compiled = luma::test::compile_for_eval(program);
    const auto env = luma::test::make_std_env();

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

// ─── Main ───

int main() {
    LUMA_RUN_ALL();
}
