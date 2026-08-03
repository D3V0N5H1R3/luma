// ─────────────────────────────────────────────────────────────────────────────
// Virtual Machine (VM) Module
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Execute Luma bytecode.
//
// Key Types:
//   - VM: The main virtual machine class.
//   - VMStack: Stack and call frame storage (vm_stack.hpp).
//   - VMGlobalCache: Inline cache for global variable lookups (vm_global_cache.hpp).
//   - CallFrame: Represents an active function call on the stack (vm_stack.hpp).
//   - Value: Represents a runtime value (integer, string, record, etc.).
//
// Dependencies:
//   - runtime/compiler: For bytecode and compiled function structures.
//   - runtime/interpreter: For the `Value` type and environment.
//   - runtime/concurrency: For task management.
//
// ─── Method naming conventions ───────────────────────────────────────────────
//   op_<opcode>()      — Dispatch table entries: one thin method per opcode,
//                         called via the function-pointer dispatch table
//                         (k_dispatch_table) or the computed-goto labels.
//   handle_<op>()      — Implementation helpers: contain the non-trivial
//                         logic for opcodes that are too complex for an
//                         inline op_*() body.  Called by op_*() methods and
//                         by the switch-based dispatch path in vm.cpp.
//   validate_<what>()  — Pre-condition checks that throw RuntimeError on
//                         failure.  Return void (or the validated value).
//   check_<what>()     — Returns bool, caller handles the result.
//   is_<what>()        — Pure query predicates, no side effects.
//   notify_<what>()    — Side-effect actions (e.g. debugger notifications)
//                         that neither throw nor return a status.
//
// Handler suffix conventions:
//   _get / _set        — Index or field access (e.g. handle_index_get).
//   _opt               — Optional/nullable variant (e.g. handle_index_get_opt).
//   _next / _next_pair — Iteration: single value vs key-value pair.
//
// The two-tier op_*/handle_* split keeps the dispatch table entries
// minimal (often a single delegation call) while allowing complex opcode
// logic to live in the vm_dispatch_*.cpp files that own each category.
//
// ─── Composition ─────────────────────────────────────────────────────────────
// VM owns three composed components:
//
//   stack_         (VMStack)              — pre-allocated value stack, stack-pointer
//                                          triple (base/top/limit), call frames, and
//                                          stack high-water mark.
//   exceptions_    (VMExceptionManager)   — LIFO stack of ExceptionHandler records
//                                          for try/catch block tracking.
//   global_cache_  (VMGlobalCache)        — name → Binding* cache for GetGlobal /
//                                          SetGlobal; populated lazily on first access.
//
// ─── Dispatch file division ──────────────────────────────────────────────────
// The VM execution loop (vm.cpp) calls out to dispatch helpers split across
// several translation units to keep file sizes manageable.  Each file owns a
// coherent slice of the opcode set:
//
//   vm.cpp                       — main run() loop, stack & frame management,
//                                  function call dispatch, helper utilities
//   vm_dispatch_table.cpp        — function pointer dispatch table and all
//                                  op_*() handler methods (one per opcode)
//   vm_dispatch_arithmetic.cpp   — numeric binary/unary ops, comparisons,
//                                  integer division, modulo, string concatenation
//   vm_dispatch_collections.cpp  — array, dict, record, tuple creation and
//                                  indexing (get/set), field access, ranges
//   vm_dispatch_control_flow.cpp — constants, globals, upvalues, closures,
//                                  jumps/loops, tail-calls, pipe operators,
//                                  try/catch, null-coalesce
//   vm_dispatch_types.cpp        — type matching, result/optional wrapping,
//                                  downcast, string interpolation
//   vm_dispatch_concurrency.cpp  — task spawn/await, task_scope begin/end,
//                                  for-iterator opcodes, print, assert
//   vm_helpers.cpp               — shared helpers used across dispatch files
//   vm_introspection.cpp         — read-only VM state queries used by the
//                                  debugger (VMIntrospector)
//
// Dispatch strategy:
//   The dispatch path uses a function pointer table: a constinit
//   std::array<VM::DispatchFn, k_dispatch_table_size> indexed by the raw
//   opcode byte.
//   Each entry is a void (VM::*)() member function pointer.  The table is
//   built via a constexpr helper so it is fully initialised at compile time.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_VM_VM_HPP
#define LUMA_VM_VM_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/string_hash.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/concurrency/task_scope.hpp"
#include "runtime/concurrency/thread_pool.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/native_function_fwd.hpp"
#include "runtime/vm/vm_constants.hpp"
#include "runtime/vm/vm_debug_interface.hpp"
#include "runtime/vm/vm_debug_types.hpp"
#include "runtime/vm/vm_dispatch_helpers.hpp"
#include "runtime/vm/vm_error_messages.hpp"
#include "runtime/vm/vm_exception_manager.hpp"
#include "runtime/vm/vm_global_cache.hpp"
#include "runtime/vm/vm_stack.hpp"
#include "runtime/vm/vm_stack_api.hpp"
#include "runtime/vm/vm_task_manager.hpp"
#include "runtime/vm/vm_types.hpp"

namespace luma {

class Environment;
using EnvPtr = std::shared_ptr<Environment>;
struct Binding;
class RuntimeError;

// Stack-based virtual machine for executing Luma bytecode.
//
// TODO(sandbox): Accept RuntimeConstraints in the VM constructor
// to enable per-execution resource limits. Currently uses global
// ResourceLimits directly. See RuntimeConstraints in resource_limits.hpp.
//
// ═══════════════════════════════════════════════════════════
// VM error handling conventions
// ═══════════════════════════════════════════════════════════
//
// Exceptions (throw RuntimeError):
//   Use for VM-internal errors that represent bugs or unrecoverable
//   conditions.  The catch handler is typically at the top-level
//   run loop or the user's try/catch in Luma source code.
//   ● Stack underflow / overflow
//   ● Invalid bytecode (unknown opcode, truncated operand)
//   ● Invalid upvalue or constant pool index
//   ● Type mismatches the type checker should have prevented
//   ● Function call arity mismatches
//   ● Resource limit violations (max stack depth, max call frames)
//
// Result<T> (returned by stdlib to user code):
//   Stdlib functions that interact with the outside world or whose
//   failure is expected return result<T>.  The VM does not use
//   Result internally — it is a user-facing mechanism.
//   ● I/O errors  (file not found, network timeout)
//   ● Parse errors (JSON, number conversion)
//   ● Lookup misses (index out of bounds, key not found)
//   ● Domain errors (division by zero, singular matrix)
//
// See also: runtime/stdlib/common/native_function.hpp for the stdlib
// error handling conventions (throw vs result vs optional).
// ═══════════════════════════════════════════════════════════
//
// ═══════════════════════════════════════════════════════════
// Coupling design and future VMStackAPI refactor
// ═══════════════════════════════════════════════════════════
//
// Current design: VM dispatch handlers (vm_dispatch_*.cpp) are
// implemented as private VM member functions.  This gives them
// unrestricted access to all private members (stack_, task_manager_,
// exceptions_, global_cache_, etc.) through the implicit `this`.
//
// The coupling this creates:
//   ● Dispatch handlers cannot be tested in isolation — instantiating
//     a VM is the only way to exercise a single opcode handler.
//   ● Handlers depend implicitly on the layout of VMStack, VMTaskManager,
//     and VMDebugInterface rather than on a stable abstraction.
//
// Most-accessed private members across dispatch files (descending frequency):
//   1. stack_                       — value stack, frame list, stack pointers (~61 accesses)
//   2. VMTaskManager::current_scope — thread-local active TaskScope pointer   (~8 accesses)
//   3. exceptions_                  — exception handler LIFO stack            (~3 accesses)
//   4. global_cache_   — inline cache for GetGlobal/SetGlobal    (~1 access)
//
// Future direction — VMStackAPI interface:
//   Extract a thin, mockable interface (e.g., VMStackAPI) that exposes
//   only the operations dispatch handlers need:
//     push(Value) / pop() / peek(n)
//     current_frame() const → const CallFrame&
//     current_task_scope() const → TaskScope*
//     runtime_error(message) [[noreturn]]
//     read_byte() / read_u16() / read_u32()
//   Handler methods would then receive a VMStackAPI& parameter
//   (or be free functions taking VMStackAPI&), enabling lightweight
//   unit tests with a mock/stub implementation of VMStackAPI.
//
// TODO(refactor): Introduce VMStackAPI in core/runtime/vm/vm_stack_api.hpp
// and migrate dispatch handlers to use it instead of accessing
// VM private members directly.  The public const accessors below
// (current_task_scope(), frames(), stack()) are the seed of this API.
// ═══════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════
// TODO(refactor/V1): VM god-class decomposition
// ═══════════════════════════════════════════════════════════
//
// Current monolithic structure — VM conflates several distinct concerns:
//   • Execution engine   — run loop, program counter, bytecode dispatch
//   • Frame management   — call stack, CallFrame push/pop, return values
//   • Call dispatch      — function/closure/native resolution and argument binding
//   • Debug hooks        — step notifications, pause requests, data breakpoints
//   • Task scope         — task_scope { } lifecycle, spawn, join
//   • Exception handling — try/catch handler LIFO stack
//
// Proposed decomposition:
//   VMCore             (vm_core.hpp)
//     Stack, frame management, and program counter.  Owns stack_, the
//     run loop entry, and all push/pop/peek/read_byte primitives.
//
//   VMCallDispatch     (vm_call_dispatch.hpp)
//     Function call resolution and argument binding: call_value(),
//     call_function(), call_closure(), call_native(), handle_tail_call().
//
//   VMDebugInterface   (vm_debug_interface.hpp)  ── DONE
//     Owns the debugger-integration state (the DAP callback set, last
//     reported line/file, and the cross-thread pause flag) and the
//     thread-safe set_*/copy_* callback accessors.  The frame/stack-touching
//     notification logic (check_debug_hooks(), notify_local_data_breakpoint(),
//     notify_global_data_breakpoint()) stays on VM because it needs the call
//     frames and value stack to report state to the debugger.
//
//   VMTaskManager      (vm_task_manager.hpp)  ── DONE
//     Owns all structured-concurrency STATE: the task_scope { } LIFO stack,
//     the thread pool (pool() lazy-init), the task-id counter, and the
//     thread-local current-scope pointer.  The task opcode handlers
//     (handle_spawn(), handle_await(), handle_task_scope_begin/end(),
//     unwind_task_scopes_to()) stay on VM because they push/pop the value
//     stack, raise runtime_error, and drive the exception-safe join/cancel
//     sequencing; they delegate their state to this component.
//
//   VMExceptionHandler (vm_exception_handler.hpp)
//     try/catch frame management: handle_try_catch(), handle_exception().
//     VMExceptionManager is already a composed type — this would elevate it
//     to a first-class component with its own interface.
//
// Progress:
//   • VMStackAPI (vm_stack_api.hpp) now EXISTS — VM derives from it and the
//     dispatch handlers reach stack/frame state through its push()/pop()/
//     peek()/current_frame()/read_* seam (devirtualised because VM is final).
//   • VMDebugInterface (vm_debug_interface.hpp) extracted — owns the DAP
//     callback set + thread-safe accessors (see the DONE note above).
//   • VMTaskManager (vm_task_manager.hpp) extracted — owns the task-scope /
//     pool / task-id / current-scope state (see the DONE note above).
//
// Why the rest is not done yet:
//   The opcode dispatch table is a std::array<void (VM::*)(), N> of
//   pointers-to-VM-member (see k_dispatch_table).  Every op_*() handler must
//   therefore remain a VM member to be addressable by the table, so the
//   remaining components (VMCore, VMCallDispatch, VMExceptionHandler) can only
//   own STATE and helper logic that the thin op_*() members delegate to — they
//   cannot themselves hold the handlers.  VMCallDispatch in particular is the
//   execution-engine core: call_value/function/closure/native, handle_tail_call,
//   and init_call_fn are coupled to stack_.frames, base_depth_,
//   compiled_functions_, and the program counter well beyond the VMStackAPI
//   seam, so a separate component would buy little at the highest hot-path risk.
//   Each such extraction touches the hot dispatch path, so they are staged and
//   verified independently rather than landed as one commit.
// ═══════════════════════════════════════════════════════════

class VM final : public VMStackAPI {
    friend class TaskScope;

public:
    explicit VM(EnvPtr global_env);
    // The compiled_fns pointer must remain valid for the lifetime of the VM instance.
    VM(EnvPtr global_env, ThreadPool& pool, const std::vector<CompiledFunction>* compiled_fns);

    ~VM() {
        // Null out the thread-local scope pointer to prevent dangling access
        // if the VM is destroyed with task scopes still active.
        VMTaskManager::current_scope = nullptr;
    }

    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;
    VM(VM&& other) noexcept;
    VM& operator=(VM&& other) noexcept;

    // Execute a compiled program.
    void execute(const std::vector<CompiledFunction>& functions, const CompiledFunction& top_level);

    // Execute a single compiled function (for REPL use).
    [[nodiscard]] Value execute_function(const CompiledFunction& func);

    // Execute a compiled function with its associated function list.
    [[nodiscard]] Value execute_function(const CompiledFunction& func,
                                         const std::vector<CompiledFunction>& functions);

    [[nodiscard]] EnvPtr global_env() const {
        return global_env_;
    }

    // ─── Debug hooks ───
    //
    // Type aliases and DebugCallbacks are defined in vm_debug_types.hpp.
    // The aliases below re-export them as nested names for backward
    // compatibility (VM::DebugHook, VM::PauseCallback, etc.).

    using DebugHook = luma::DebugHook;
    using PauseCallback = luma::PauseCallback;
    using ExceptionHook = luma::ExceptionHook;
    using DataBreakpointHook = luma::DataBreakpointHook;
    using TaskSpawnHook = luma::TaskSpawnHook;
    using TaskExitHook = luma::TaskExitHook;
    using DebugCallbacks = luma::DebugCallbacks;

    // Set all debug callbacks at once.
    void set_debug_callbacks(DebugCallbacks callbacks);

    // Individual setters (backward-compatible).
    void set_debug_hook(DebugHook hook);
    void set_pause_callback(PauseCallback callback);
    void set_exception_hook(ExceptionHook hook);
    void set_data_breakpoint_hook(DataBreakpointHook hook);
    void set_task_spawn_hook(TaskSpawnHook hook);
    void set_task_exit_hook(TaskExitHook hook);

    // Request that the VM checks for a pause at the next line change.
    // Thread-safe: can be called from the protocol thread while the VM runs.
    void request_pause_check() {
        debug_.pause_requested.store(true, std::memory_order_release);
    }

    // Expose state for inspection while paused.
    //
    // ═══ Debug / Introspection API ═══
    // Naming conventions:
    //   current_*() — returns current state (frame, instruction) without copying
    //   get_*()     — returns a value, potentially copying for safety
    //   try_*()     — returns optional<T> for lookups that may fail
    [[nodiscard]] const std::vector<CallFrame>& frames() const {
        return stack_.frames;
    }

    // The innermost active call frame (top of the frame stack).
    // Part of the VMStackAPI seam — see the "Coupling design" comment above.
    [[nodiscard]] const CallFrame& current_frame() const override {
        return stack_.frames.back();
    }

    [[nodiscard]] std::span<const Value> stack() const {
        return stack_.span();
    }

    // Mutable stack access — only for debugger setVariable while paused.
    [[nodiscard]] std::span<Value> stack_mut() {
        return stack_.span_mut();
    }

    // Replace the stack contents — used by the time-travel debugger to restore snapshots.
    void restore_stack(std::vector<Value> s);

    // Query the currently active task scope for this thread.
    // Returns nullptr when no task_scope { } block is executing on this thread.
    //
    // This is a thread-local state query: the returned pointer varies per thread
    // and is only valid for the duration of the current task_scope { } block.
    //
    // Note: this is a seed accessor for the future VMStackAPI interface — see
    // the "Coupling design" comment above the class declaration.
    [[nodiscard]] TaskScope* current_task_scope() const noexcept override {
        return VMTaskManager::current_scope;
    }

    // Run @test functions.  Returns true when every test passes.
    [[nodiscard]] bool execute_tests(const std::vector<CompiledFunction>& functions,
                                     const CompiledFunction& top_level);

private:
    // ─── Execution loop ───
    [[nodiscard]] Value run();

    // Wrap a compiled function in a FunctionValue, push it as the active
    // callee, enter it via call_closure(), and run the dispatch loop until
    // that frame returns.  Shared by execute(), execute_function(), and
    // execute_tests() so the "build FunctionValue → push → call → run"
    // sequence lives in exactly one place.
    [[nodiscard]] Value run_top_level(const CompiledFunction& fn);

    // ─── Operand readers ───
    [[nodiscard]] std::uint8_t read_byte() override;
    [[nodiscard]] std::uint16_t read_u16() override;
    [[nodiscard]] std::uint32_t read_u32() override;
    [[nodiscard]] std::string_view read_name();
    [[nodiscard]] std::string_view checked_name(std::uint16_t index) const;
    [[nodiscard]] SmallVector<std::uint16_t, VMConstants::k_small_vector_capacity>
    read_name_indices(std::size_t count);
    [[nodiscard]] Value run_to_return();
    [[nodiscard]] Value read_constant();

    /// Verify that at least `count` bytes remain in `frame`'s bytecode stream
    /// starting from its instruction pointer.  Throws a RuntimeError if the
    /// bytecode is truncated.  Designed for use before multi-byte operand reads.
    inline void ensure_bytecode_available(const CallFrame& frame, std::size_t count) const {
        const auto* end = frame.function->chunk().code.data() + frame.function->chunk().code.size();
        if (frame.ip + count > end) [[unlikely]] {
            runtime_error(vm_errors::bytecode_truncated);
        }
    }

    /// Convenience overload that checks the current (top) frame.  Used by the
    /// run() loop's fetch guard, which must stay active for all bytecode: the
    /// verifier permits reachable jumps to one-past-the-end and does not require
    /// a trailing terminator, so this guard is what turns those into a catchable
    /// runtime error rather than an out-of-bounds read.
    inline void ensure_bytecode_available(std::size_t count) const {
        ensure_bytecode_available(stack_.frames.back(), count);
    }

    // ─── Stack manipulation ───
    void push(Value value) override;
    [[nodiscard]] Value pop() override;
    [[nodiscard]] Value& peek(std::size_t distance = 0) override;
    [[nodiscard]] const Value& peek(std::size_t distance = 0) const;

    // Pop the right operand and return a reference to the left operand
    // that remains on the stack.  Used by binary operators to avoid
    // an extra pop+push round-trip: the result is written directly
    // into a_ref.
    //
    //     auto [a, b] = pop_binary_ref();
    //     a = numeric_binary_op(a, b, Op::Add);
    [[nodiscard]] BinaryOperands pop_binary_ref() {
        auto b = pop();
        // After popping the right operand, the left operand must still be on
        // the stack.  Verified bytecode guarantees this, but unverified or
        // crafted bytecode could apply a binary operator with only one operand
        // present — forming a reference to the slot below the stack base, an
        // out-of-bounds read/write.  This guard mirrors pop()'s underflow check.
        if (stack_.top == stack_.base) [[unlikely]] {
            runtime_error(vm_errors::stack_underflow);
        }
        return {*(stack_.top - 1), std::move(b)};
    }

    /// Pop `count` values from the stack into a SmallVector in source order
    /// (i.e. the first-pushed value ends up at index 0).
    ///
    /// Replaces the recurring pattern:
    ///     SmallVector<Value> v(count);
    ///     for (int i = count - 1; i >= 0; --i) { v[i] = pop(); }
    [[nodiscard]] SmallVector<Value, VMConstants::k_small_vector_capacity>
    pop_sequence(std::size_t count) {
        SmallVector<Value, VMConstants::k_small_vector_capacity> result(count);
        for (std::size_t i = count; i > 0; --i) {
            result[i - 1] = pop();
        }
        return result;
    }

    // Return a pointer past the last byte of the current frame's code.
    // Replaces the repeated `cf.function->chunk().code.data() + code.size()`.
    [[nodiscard]] const std::uint8_t* current_code_end() const {
        return stack_.frames.back().code_end;
    }

    // Return a pointer to the first byte of the current frame's code.
    [[nodiscard]] const std::uint8_t* current_code_start() const {
        return stack_.frames.back().function->chunk().code.data();
    }

    // Check if there is enough stack space for `needed` additional values.
    // Throws a runtime error if pushing `needed` more values would exceed
    // the stack capacity (VMStack::k_max).
    // Distinct from validate_stack_depth(): this checks *headroom* (can we push?),
    // while validate_stack_depth() checks *content* (do we have enough to pop?).
    void validate_stack_space(std::size_t needed);

    // Verify the stack holds at least `required` values.
    // Throws a runtime error with `context` in the message on underflow.
    // Distinct from validate_stack_space(): this checks *content* (can we pop?),
    // while validate_stack_space() checks *headroom* (can we push?).
    void validate_stack_depth(std::size_t required, std::string_view context);

    // ─── Function calls ───
    void call_value(const Value& callee, int arg_count);
    void call_function(const FunctionValue& func, int arg_count);
    [[nodiscard]] bool call_closure(FunctionValue* func, std::uint8_t arg_count);
    void call_native(const NativeFunctionValue& function, int arg_count);

    // ─── Dispatch helpers (shared infrastructure) ───
    [[nodiscard]] SourceLocation current_location() const;
    // ─── Run-loop exception dispatch ───
    // The single-argument overloads are the entry points called from the
    // run() dispatch loop's catch blocks.  They extract the error payload and
    // delegate to the two-argument core.  The std::optional<Value> payload is
    // materialised *inside* these helpers (a normal stack frame), never inside
    // the catch block: a non-trivial temporary destroyed in a catch funclet
    // that also rethrows triggers a clang-cl MSVC-EH codegen bug that traps
    // (int3) during funclet cleanup.  Keeping the temporary out of the funclet
    // avoids it.  Each returns true if a Luma handler caught the exception and
    // false if the caller must re-raise it (with a bare `throw;` in its catch).
    [[nodiscard]] bool handle_exception(const RuntimeError& e);
    [[nodiscard]] bool handle_exception(const std::runtime_error& e);
    [[nodiscard]] bool handle_exception(const std::string& message,
                                        const std::optional<Value>& error_value);

    // Check debug hooks and handle pause requests.  Returns true if the
    // debugger requested termination (the caller should return Value{}).
    // Extracted from the run loop to reduce duplication between the
    // function-pointer and switch dispatch paths.
    bool check_debug_hooks();

    // ─── Validation helpers (vm_helpers.cpp) ────────────────────────────────
    // Shared type-checking and bounds-checking utilities used across
    // dispatch files to reduce duplicated validation patterns.

    // Notify the debugger of a local variable write.
    // Triggers a pause if the debugger registered a breakpoint for `slot`.
    void notify_local_data_breakpoint(const CallFrame& cf, std::uint16_t slot);

    // Notify the debugger of a global variable write by name.
    // Triggers a pause if a data breakpoint is set for `name`.
    void notify_global_data_breakpoint(std::string_view name);

    // Shared implementation for data-breakpoint notification.
    // NameProvider is a callable returning std::optional<std::string>;
    // if it returns std::nullopt the notification is silently skipped.
    template <typename NameProvider> void notify_data_breakpoint_impl(NameProvider name_provider);

    // ─── Arithmetic dispatch (vm_dispatch_arithmetic.cpp) ───
    [[nodiscard]] Value numeric_binary_op(const Value& a, const Value& b, Op op) const;
    [[nodiscard]] Value compare_values(const Value& a, const Value& b, Op op) const;
    void handle_divide();
    void handle_int_divide();
    void handle_modulo();
    void validate_integer_operands(const Value& a, const Value& b, std::string_view op_name) const;
    void validate_integer_operand(const Value& v, std::string_view op_name) const;
    void validate_shift_amount(std::int64_t shift) const;

    // Shared scaffolding for integer-only binary operators.  Pops the right
    // operand, validates both operands are integers, then writes op(left, right)
    // back into the left stack slot.  op_bitwise_* delegate here.
    template <typename IntOp> void apply_integer_binary_op(std::string_view op_name, IntOp op) {
        auto [a, b] = pop_binary_ref();
        validate_integer_operands(a, b, op_name);
        a = Value{op(a.as_integer(), b.as_integer())};
    }

    // As apply_integer_binary_op, but additionally validates the shift amount.
    // op_shift_left / op_shift_right delegate here.
    template <typename IntOp> void apply_shift_op(std::string_view op_name, IntOp op) {
        auto [a, b] = pop_binary_ref();
        validate_integer_operands(a, b, op_name);
        validate_shift_amount(b.as_integer());
        a = Value{op(a.as_integer(), b.as_integer())};
    }

    void validate_nonzero_divisor(const Value& divisor, std::string_view op_name) const;
    void handle_concatenate();

    // ─── Collection dispatch (vm_dispatch_collections.cpp) ───
    void handle_index_get();
    void handle_index_set();
    void handle_make_record(const std::uint8_t* code_end);
    void handle_record_with(const std::uint8_t* code_end);
    void handle_contains();
    void handle_make_dict();
    void handle_get_field();
    void handle_set_field();
    void handle_get_field_opt();
    void handle_index_get_opt();
    void make_range(bool inclusive);
    void handle_make_array();
    void handle_make_tuple();
    void handle_make_choice();
    void handle_make_choice_constructor();

    // ─── Control flow dispatch (vm_dispatch_control_flow.cpp) ───
    void validate_upvalue_index(const CallFrame& cf, std::size_t index) const;
    void handle_constant_long(const std::uint8_t* code_end);
    void handle_get_upvalue();
    void handle_set_upvalue();
    void handle_get_global();
    void handle_set_global();
    void handle_loop(const std::uint8_t* code_start);
    void handle_null_coalesce(const std::uint8_t* code_end);
    void jump_if(bool jump_on_truthy);
    void handle_tail_call(const std::uint8_t*& code_start, const std::uint8_t*& code_end);
    void handle_call_named();
    void handle_make_closure();
    void forward_upvalue(FunctionValue& target, std::size_t index,
                         const CompiledFunction::Upvalue& desc, const CallFrame& cf) const;
    void handle_pipe();
    void handle_error_pipe();
    void handle_try_catch(const std::uint8_t* code_end);

    // ─── Type dispatch (vm_dispatch_types.cpp) ───
    [[nodiscard]] bool matches_type(const Value& val, std::string_view type_name) const;
    void handle_make_failure();
    void handle_unwrap();
    void handle_result_inner();
    void handle_is_success();
    void handle_downcast();
    void handle_trusted_downcast();

    // ─── Concurrency dispatch (vm_dispatch_concurrency.cpp) ───
    void handle_spawn();
    void handle_await();
    void handle_task_scope_begin();
    void handle_task_scope_end();

    /// Cancel, join, and pop every active task_scope whose index is at or above
    /// `target_depth`.  Called during exception dispatch so scopes an exception
    /// unwinds past do not leak or orphan their child tasks.
    void unwind_task_scopes_to(std::size_t target_depth);

    /// Retire the innermost (back) task scope with the concurrency-teardown
    /// ordering shared by normal-end cleanup and exceptional unwinding: request
    /// cooperative cancellation of the scope's children, wait for them to observe
    /// it (suppressing secondary failures), restore the parent scope as current,
    /// then pop it.  `scope` must be `task_scopes.back()`; it dangles once this
    /// returns.  Defining the sequence once keeps the ordering identical on both
    /// paths.
    void retire_task_scope(TaskScope* scope);

    /// Copies the parent VM's debug hooks into a child VM for spawned tasks.
    static void propagate_debug_hooks(const DebugCallbacks& parent_cbs, VM& child);

    /// Dispatches the callable (native or compiled) inside a spawned task VM.
    static Value execute_spawned_callable(VM& task_vm, Value& callee, std::vector<Value>& args,
                                          SourceLocation loc);

    /// Creates the callable that runs in the spawned task's thread.
    static auto create_spawn_callable(std::shared_ptr<std::promise<Value>> promise, Value callable,
                                      std::vector<Value> args, SourceLocation loc,
                                      const std::vector<CompiledFunction>* compiled_fns, EnvPtr env,
                                      std::shared_ptr<CancellationToken> cancel_token,
                                      ThreadPool& pool_ref, DebugCallbacks debug_cbs, int task_id)
        -> std::function<void()>;

    /// Configures the child task VM: propagates debug hooks and fires the spawn hook.
    static void prepare_spawn_environment(VM& task_vm, const DebugCallbacks& debug_cbs,
                                          int task_id);

    /// Executes callee in task_vm, fulfils promise, and restores the TLS cancel token.
    static void build_spawn_future(VM& task_vm, std::shared_ptr<std::promise<Value>>& promise,
                                   Value& callee, std::vector<Value>& args, SourceLocation loc,
                                   const std::shared_ptr<CancellationToken>& cancel_token);

    void handle_for_iter_init();
    void handle_for_iter_step();
    void handle_for_iter_step_pair();

    // Shared dispatch logic for handle_for_iter_step / handle_for_iter_step_pair.
    // Pops the iterator state tuple, validates it, extracts the iterable and
    // index, then dispatches to the appropriate per-type handler.
    using IterStepFn = void (VM::*)(TupleValue&, std::int64_t);
    void dispatch_iter_step(IterStepFn range_fn, IterStepFn array_fn, IterStepFn dict_fn,
                            IterStepFn string_fn);

    // Shared bookkeeping for all iter_step_* / iter_step_pair_* helpers.
    // `body` attempts to push the next element(s) and returns the new index,
    // or std::nullopt when the sequence is exhausted.  On success the state
    // tuple is updated and true is pushed; on exhaustion false is pushed.
    // Defined in vm_dispatch_concurrency.cpp and instantiated there only.
    template <typename BodyFn> void iter_step_generic(TupleValue& state, BodyFn&& body);

    // Type-specific single-value iteration helpers (vm_dispatch_concurrency.cpp).
    void iter_step_range(TupleValue& state, std::int64_t idx);
    void iter_step_array(TupleValue& state, std::int64_t idx);
    void iter_step_string(TupleValue& state, std::int64_t idx);
    void iter_step_dict(TupleValue& state, std::int64_t idx);

    // Type-specific key-value pair iteration helpers (vm_dispatch_concurrency.cpp).
    void iter_step_pair_range(TupleValue& state, std::int64_t idx);
    void iter_step_pair_array(TupleValue& state, std::int64_t idx);
    void iter_step_pair_string(TupleValue& state, std::int64_t idx);
    void iter_step_pair_dict(TupleValue& state, std::int64_t idx);

    void handle_print();
    void handle_assert();

    // ─── Function pointer dispatch table ───────────────────────────────────
    // Each opcode maps to a void (VM::*)() handler.  The table is defined in
    // vm_dispatch_table.cpp and verified by a static_assert.  It is sized to
    // cover the full 0-255 opcode-byte range so an invalid opcode in a corrupt
    // or crafted .lumc indexes an op_invalid slot instead of reading out of
    // bounds; undefined bytes map to op_invalid.
    using DispatchFn = void (VM::*)();
    // Ideally constexpr, but MSVC does not treat pointer-to-member
    // initialisers as constant expressions (even in C++20).
    static const std::array<DispatchFn, k_dispatch_table_size> k_dispatch_table;
    static constexpr std::array<DispatchFn, k_dispatch_table_size> build_dispatch_table() noexcept;

    // Return-value signaling for the table dispatch loop.
    // op_return / op_end_module set this and the loop exits immediately.
    std::optional<Value> dispatch_return_value_;

    // ─── Op handlers (vm_dispatch_table.cpp) ────────────────────────────────
    void op_invalid();                 // Fires a runtime_error for unmapped opcode slots.
    void op_constant();                // Op::Constant
    void op_constant_long();           // Op::ConstantLong
    void op_pop();                     // Op::Pop
    void op_dup();                     // Op::Dup
    void op_dup2();                    // Op::Dup2
    void op_swap();                    // Op::Swap
    void op_get_local();               // Op::GetLocal
    void op_set_local();               // Op::SetLocal
    void op_get_upvalue();             // Op::GetUpvalue
    void op_set_upvalue();             // Op::SetUpvalue
    void op_get_global();              // Op::GetGlobal
    void op_set_global();              // Op::SetGlobal
    void op_none();                    // Op::None
    void op_true();                    // Op::True
    void op_false();                   // Op::False
    void op_zero();                    // Op::Zero
    void op_one();                     // Op::One
    void op_add();                     // Op::Add
    void op_subtract();                // Op::Subtract
    void op_multiply();                // Op::Multiply
    void op_divide();                  // Op::Divide
    void op_int_divide();              // Op::IntDivide
    void op_modulo();                  // Op::Modulo
    void op_negate();                  // Op::Negate
    void op_increment();               // Op::Increment
    void op_decrement();               // Op::Decrement
    void op_equal();                   // Op::Equal
    void op_not_equal();               // Op::NotEqual
    void op_less();                    // Op::Less
    void op_less_equal();              // Op::LessEqual
    void op_greater();                 // Op::Greater
    void op_greater_equal();           // Op::GreaterEqual
    void op_not();                     // Op::Not
    void op_and();                     // Op::And
    void op_or();                      // Op::Or
    void op_bitwise_and();             // Op::BitwiseAnd
    void op_bitwise_or();              // Op::BitwiseOr
    void op_bitwise_xor();             // Op::BitwiseXor
    void op_bitwise_not();             // Op::BitwiseNot
    void op_shift_left();              // Op::ShiftLeft
    void op_shift_right();             // Op::ShiftRight
    void op_concatenate();             // Op::Concatenate
    void op_interpolate();             // Op::Interpolate
    void op_make_array();              // Op::MakeArray
    void op_make_dict();               // Op::MakeDict
    void op_make_tuple();              // Op::MakeTuple
    void op_make_range();              // Op::MakeRange
    void op_make_range_inc();          // Op::MakeRangeInc
    void op_index_get();               // Op::IndexGet
    void op_index_set();               // Op::IndexSet
    void op_index_get_opt();           // Op::IndexGetOpt
    void op_make_record();             // Op::MakeRecord
    void op_get_field();               // Op::GetField
    void op_set_field();               // Op::SetField
    void op_get_field_opt();           // Op::GetFieldOpt
    void op_record_with();             // Op::RecordWith
    void op_make_choice();             // Op::MakeChoice
    void op_make_choice_constructor(); // Op::MakeChoiceConstructor
    void op_make_success();            // Op::MakeSuccess
    void op_make_failure();            // Op::MakeFailure
    void op_make_some();               // Op::MakeSome
    void op_unwrap();                  // Op::Unwrap
    void op_result_inner();            // Op::ResultInner
    void op_is_success();              // Op::IsSuccess
    void op_is_some();                 // Op::IsSome
    void op_ensure_success();          // Op::EnsureSuccess
    void op_downcast();                // Op::Downcast
    void op_trusted_downcast();        // Op::TrustedDowncast
    void op_is_type();                 // Op::IsType
    void op_jump();                    // Op::Jump
    void op_jump_if_false();           // Op::JumpIfFalse
    void op_jump_if_true();            // Op::JumpIfTrue
    void op_loop();                    // Op::Loop
    void op_null_coalesce();           // Op::NullCoalesce
    void op_call();                    // Op::Call
    void op_call_named();              // Op::CallNamed
    void op_tail_call();               // Op::TailCall
    void op_return();                  // Op::Return
    void op_make_closure();            // Op::MakeClosure
    void op_pipe();                    // Op::Pipe
    void op_error_pipe();              // Op::ErrorPipe
    void op_try_catch();               // Op::TryCatch
    void op_try_end();                 // Op::TryEnd
    void op_rethrow();                 // Op::Rethrow
    void op_match_start();             // Op::MatchStart
    void op_match_arm();               // Op::MatchArm
    void op_match_end();               // Op::MatchEnd
    void op_contains();                // Op::Contains
    void op_spawn();                   // Op::Spawn
    void op_await();                   // Op::Await
    void op_task_scope_begin();        // Op::TaskScopeBegin
    void op_task_scope_end();          // Op::TaskScopeEnd
    void op_for_iter_init();           // Op::ForIterInit
    void op_for_iter_step();           // Op::ForIterStep
    void op_for_iter_step_kv();        // Op::ForIterStepKV
    void op_print();                   // Op::Print
    void op_assert();                  // Op::Assert
    void op_type_of();                 // Op::TypeOf
    void op_increment_local();         // Op::IncrementLocal
    void op_decrement_local();         // Op::DecrementLocal
    void op_set_local_pop();           // Op::SetLocalPop
    void op_get_local_return();        // Op::GetLocalReturn
    void op_int_to_number();           // Op::IntToNumber
    void op_clone();                   // Op::Clone
    void op_end_module();              // Op::EndModule

    // ─── Template numeric helpers ───
    // Consolidate type-dispatch for unary numeric ops (Negate, Increment, Decrement).
    // IntOp: (std::int64_t) → Value   (handles overflow internally)
    // NumOp: (double) → Value
    template <typename IntOp, typename NumOp>
    void handle_unary_numeric(Value& val, IntOp int_op, NumOp num_op, std::string_view op_name);

    // Consolidate type-dispatch for binary numeric ops.
    // IntOp: (std::int64_t, std::int64_t) → Value   (handles overflow internally)
    // NumOp: (double, double) → Value
    template <typename IntOp, typename NumOp>
    void handle_binary_numeric(Value& a_ref, const Value& b, IntOp int_op, NumOp num_op,
                               std::string_view op_name);

    // Signal the dispatch loop to return a value.  Shared by op_return()
    // and op_get_local_return() to avoid duplicating the frame-pop +
    // stack-restore + return-signaling sequence.
    void complete_return(Value result, std::size_t callee_slot_offset);

    // ─── Frame slot validation ───
    [[nodiscard]] Value& get_local_slot(const CallFrame& cf, std::uint16_t slot);

    // ─── Index helpers (per-type, extracted from handle_index_get) ───
    void push_element_at_index(std::span<const Value> elements, const Value& index_val,
                               std::string_view context);
    void handle_array_index_get(const Value& container, const Value& index_val);
    void handle_dict_index_get(const Value& container, const Value& index_val);
    void handle_string_index_get(const Value& container, const Value& index_val);

    // ─── String dispatch (vm_dispatch_types.cpp) ───
    void handle_interpolate();

    [[nodiscard]] ThreadPool& pool();
    [[nodiscard]] std::int64_t resolve_index(std::int64_t idx, std::size_t size,
                                             std::string_view context) const;

    // ─── Upvalues ───
    // Upvalues stored directly on FunctionValue.

    // ─── Inline cache for global variable lookups ───
    // Caches Binding* pointers from the global Environment by variable
    // name.  std::unordered_map guarantees pointer stability for existing
    // elements across insertions, so cached pointers remain valid as
    // long as the binding is never erased (globals are never erased).
    [[nodiscard]] Binding* lookup_global_cache(std::string_view name);

    // Returns a reference to the resolved-binding slot for the u16 name handle
    // `idx` in the current frame's function, lazily building (and caching on the
    // frame) the per-function binding vector on first use.  The slot is nullptr
    // until resolved via lookup_global_cache; callers populate it on a miss.
    // Precondition: `idx` has already been bounds-checked (e.g. by checked_name)
    // against the current chunk's name table.
    [[nodiscard]] Binding*& global_slot(std::uint16_t idx);

    // Install a new compiled-functions store as the active one, dropping the
    // index-keyed global cache.  The cache keys are CompiledFunction* addresses
    // into the previous store; a replacement store (e.g. a fresh REPL line whose
    // functions vector reuses freed addresses) could otherwise alias stale
    // entries.  Only ever called at top-level entry, where no live frame
    // references the cache.
    void install_compiled_functions(const std::vector<CompiledFunction>* functions);

    // ─── Error handling ───
    [[noreturn]] void runtime_error(std::string_view message,
                                    std::string_view hint = {}) const override;

    // ─── State ───

    // ─── Composed stack/frame component ───────────────────────────────────
    // Holds the pre-allocated value stack, stack-pointer triple, call frames,
    // and stack high-water mark.  All VM member-function implementations
    // access stack data via stack_.base, stack_.top, stack_.frames, etc.
    VMStack stack_;

    EnvPtr global_env_;

    // Non-owning — the Program/caller that owns the compiled functions must
    // outlive the VM execution.  Verified by execute() and execute_function(),
    // which accept the vector by const-reference from the caller's stack.
    const std::vector<CompiledFunction>* compiled_functions_{nullptr};
    std::int64_t loop_iterations_{0};
    std::size_t base_depth_{0};

    // Callable wrapper for spawned tasks.
    NativeCallable vm_native_callable_;
    void init_call_fn();
    void move_from(VM&& other) noexcept;

    // Transfers all movable state fields from `other` to `*this`, excluding stack_.
    // Both the move constructor (init list handles stack_) and move_from() (explicit
    // assignment handles stack_) call this after establishing the appropriate locks:
    //   - move constructor: std::unique_lock on other.debug_.callbacks_mutex
    //   - move_from():      std::scoped_lock on both debug_.callbacks_mutexes
    // Precondition: other.debug_.callbacks_mutex is locked by the caller.
    void transfer_state(VM&& other) noexcept;

    // Computes the stack base slot for a new call frame.
    //
    // When a function is called, the stack looks like:
    //   [... | callee | arg0 | arg1 | ... | argN]  <- top
    // The frame's slot 0 (the callee itself) sits at index:
    //   stack_size() - arg_count - 1
    // The -1 accounts for the callee occupying the slot before the arguments.
    [[nodiscard]] std::size_t call_frame_base_slot(int arg_count) const noexcept {
        return stack_size() - static_cast<std::size_t>(arg_count) - 1;
    }

    [[nodiscard]] std::size_t stack_size() const noexcept {
        return stack_.size();
    }

    // ─── Composed exception handler stack ────────────────────────────────
    // Manages the LIFO stack of ExceptionHandler records for try/catch
    // blocks.  Handlers are pushed by Op::TryCatch, popped by Op::TryEnd
    // (normal exit) or by handle_exception() (exception dispatch).
    //
    // Coupling note: the actual exception dispatch logic lives in
    // VM::handle_exception() (vm_helpers.cpp) because it must unwind
    // the call frame stack, restore the value-stack pointer, set the
    // instruction pointer to the catch block, and push the error value.
    // The manager owns only the handler records themselves.
    VMExceptionManager exceptions_;

    // ─── Debug context ───────────────────────────────────────────────────
    // Owns all debugger-integration state (the DAP callback set, last
    // reported line/file, and the cross-thread pause flag) and mediates
    // thread-safe access to the callbacks.  See vm_debug_interface.hpp for
    // the full contract.
    //
    // Coupling note: the frame/stack-touching notification logic
    // (check_debug_hooks(), notify_*_data_breakpoint()) stays on VM because
    // every hook callback needs direct access to the VM's call frames, value
    // stack, and instruction pointer to report state to the debugger.
    VMDebugInterface debug_;

    // ─── Task manager component ──────────────────────────────────────────
    // Owns all structured-concurrency state: the task_scope { } LIFO stack,
    // the thread pool, the task-id counter, and the thread-local current-scope
    // pointer.  See vm_task_manager.hpp for the full contract.
    //
    // Coupling note: the task opcode handlers (handle_spawn / handle_await /
    // handle_task_scope_begin / handle_task_scope_end / unwind_task_scopes_to)
    // stay on VM because they push/pop the value stack, raise runtime_error,
    // and drive the exception-safe join/cancel sequencing.  This component owns
    // only the state and the thread-pool lazy-init.
    //
    // A future TaskExecutor interface could abstract spawn behaviour so that
    // tests can substitute a synchronous or deterministic executor without a
    // real thread pool.  The natural boundary is:
    //   - pool()                       → executor.enqueue(callable)
    //   - VMTaskManager::current_scope → executor.active_scope()
    //   - next_task_id                 → executor.next_task_id()
    VMTaskManager task_manager_;

    // ─── Composed global variable cache ───────────────────────────────────
    // Maps global variable name → Binding* in the global Environment.
    // Populated lazily on first GetGlobal/SetGlobal access; never
    // invalidated during normal execution because globals are never
    // erased from the Environment.
    //
    // Invariant: every Binding* in global_cache_ points into global_env_'s
    // internal std::unordered_map, which guarantees pointer stability on
    // insertion.  If globals could ever be erased or the Environment
    // rehashed destructively, the cache would hold dangling pointers.
    // Currently this is safe: define_or_assign() only inserts or updates
    // existing entries in-place.
    VMGlobalCache global_cache_;

    // ─── Index-keyed global inline cache ──────────────────────────────────
    // For each function (chunk), a vector of resolved Binding* indexed by the
    // u16 name handle emitted in the bytecode.  Because NameTable deduplicates,
    // each name has one stable index per chunk, so after the first resolution a
    // GetGlobal/SetGlobal becomes a direct vector index instead of the string
    // hash + probe in global_cache_.  Keyed by CompiledFunction* (stable and
    // per-VM); unordered_map keeps element addresses stable across insertion, so
    // the pointers cached in CallFrame::global_bindings never dangle.  Mirrors
    // global_cache_'s validity contract and is moved alongside it in
    // transfer_state().
    std::unordered_map<const CompiledFunction*, std::vector<Binding*>> global_index_cache_;
};

} // namespace luma

// ─── Template definitions ───────────────────────────────────────────────────
// Must be visible to all translation units that instantiate these templates.

namespace luma {

template <typename IntOp, typename NumOp>
void VM::handle_unary_numeric(Value& val, IntOp int_op, NumOp num_op, std::string_view op_name) {
    if (val.is_integer()) [[likely]] {
        val = int_op(val.as_integer());
    } else if (val.is_number()) {
        val = num_op(val.as_number());
    } else [[unlikely]] {
        runtime_error(vm_errors::requires_integer_or_number(op_name, val.display_type_name()));
    }
}

template <typename IntOp, typename NumOp>
void VM::handle_binary_numeric(Value& a_ref, const Value& b, IntOp int_op, NumOp num_op,
                               std::string_view op_name) {
    if (a_ref.is_integer() && b.is_integer()) [[likely]] {
        a_ref = int_op(a_ref.as_integer(), b.as_integer());
    } else if ((a_ref.is_integer() || a_ref.is_number()) && (b.is_integer() || b.is_number())) {
        const auto l =
            a_ref.is_integer() ? static_cast<double>(a_ref.as_integer()) : a_ref.as_number();
        const auto r = b.is_integer() ? static_cast<double>(b.as_integer()) : b.as_number();
        a_ref = num_op(l, r);
    } else [[unlikely]] {
        runtime_error(vm_errors::cannot_operate_on(op_name, a_ref.display_type_name(),
                                                   b.display_type_name()));
    }
}

} // namespace luma

#endif // LUMA_VM_VM_HPP
