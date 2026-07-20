#include "expression_evaluator.hpp"

#include <atomic>
#include <chrono>
#include <format>
#include <string>
#include <string_view>

#include "common/resource_limits.hpp"
#include "dap_helpers.hpp"
#include "dap_types.hpp"
#include "debugger_messages.hpp"
#include "expression_compiler.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_error_messages.hpp"
#include "runtime/vm/vm_introspection.hpp"
#include "variable_inspector.hpp"

namespace luma::dap {

ExpressionEvaluator::ExpressionEvaluator(RefAllocator alloc_ref)
    : alloc_ref_(std::move(alloc_ref)) {}

void ExpressionEvaluator::set_compiled_program(std::shared_ptr<std::vector<CompiledFunction>> fns,
                                               std::shared_ptr<CompiledFunction> top_level) {
    compiled_functions_ = std::move(fns);
    compiled_top_level_ = std::move(top_level);
    clear_cache();
}

void ExpressionEvaluator::set_evaluation_timeout(std::chrono::milliseconds timeout) {
    evaluation_timeout_ = timeout;
}

void ExpressionEvaluator::clear_cache() {
    const std::scoped_lock lock(cache_mutex_);
    expression_cache_.clear();
    cache_hits_.store(0, std::memory_order_relaxed);
    cache_misses_.store(0, std::memory_order_relaxed);
}

std::optional<Variable> ExpressionEvaluator::make_variable_from_value(const std::string& name,
                                                                      const Value& value) const {
    Variable result = make_base_variable(name, value);

    if (is_structured(value)) {
        result.variables_reference = alloc_ref_(std::make_shared<Value>(value));
    }

    return result;
}

std::optional<Variable> ExpressionEvaluator::try_local_lookup(const VMIntrospector& intro,
                                                              std::size_t target_frame,
                                                              const std::string& expression) const {
    auto local = intro.find_local(target_frame, expression);

    if (!local.has_value()) {
        return std::nullopt;
    }

    return make_variable_from_value(expression, local->value);
}

std::optional<Variable>
ExpressionEvaluator::try_upvalue_lookup(const VMIntrospector& intro, std::size_t target_frame,
                                        const std::string& expression) const {
    if (!intro.has_upvalues(target_frame)) {
        return std::nullopt;
    }

    auto upvalues = intro.upvalues(target_frame);

    for (const auto& uv : upvalues) {
        if (uv.name == expression) {
            return make_variable_from_value(expression, uv.value);
        }
    }

    return std::nullopt;
}

std::optional<Variable>
ExpressionEvaluator::try_global_lookup(VM* target_vm, const std::string& expression) const {
    auto global_env = target_vm->global_env();

    if (!global_env) {
        return std::nullopt;
    }

    const auto* binding = global_env->find_binding(expression);

    if (binding == nullptr) {
        return std::nullopt;
    }

    return make_variable_from_value(expression, binding->value);
}

Variable ExpressionEvaluator::evaluate(VM* target_vm, int frame_index,
                                       const std::string& expression,
                                       EvaluationContext context) const {
    Variable result;
    result.name = expression;
    result.value = std::string{messages::expression::evaluation_failed};
    result.type = "unknown";

    if (target_vm == nullptr) {
        return result;
    }

    if (expression.size() > config::expression::k_max_code_size) {
        result.value = std::string{messages::expression::too_long};
        return result;
    }

    const VMIntrospector intro(*target_vm);

    if (intro.frame_count() == 0) {
        return result;
    }

    const std::size_t target_frame =
        (frame_index >= 0 && static_cast<std::size_t>(frame_index) < intro.frame_count())
            ? static_cast<std::size_t>(frame_index)
            : static_cast<std::size_t>(top_frame_index(intro.frame_count()));

    // Strategy 1: Local variable lookup.
    if (auto local = try_local_lookup(intro, target_frame, expression)) {
        return *local;
    }

    // Strategy 2: Closure upvalue lookup.
    if (auto upvalue = try_upvalue_lookup(intro, target_frame, expression)) {
        return *upvalue;
    }

    // Strategy 3: Global environment lookup.
    if (auto global = try_global_lookup(target_vm, expression)) {
        return *global;
    }

    // Strategy 4: Compile and evaluate on scratch VM.
    // Skip in Hover and Variables contexts to avoid side effects — these
    // should only display existing values, never execute user code.
    if (context != EvaluationContext::Hover && context != EvaluationContext::Variables &&
        compiled_functions_ && compiled_top_level_) {
        return evaluate_on_scratch_vm(target_vm, static_cast<int>(target_frame), expression);
    }

    return result;
}

std::optional<CompiledFunction>
ExpressionEvaluator::lookup_or_compile(const std::string& expression) const {
    // Check the cache first.
    {
        const std::scoped_lock lock(cache_mutex_);
        auto* cached = expression_cache_.get(expression);

        if (cached != nullptr) {
            cache_hits_.fetch_add(1, std::memory_order_relaxed);
            return *cached;
        }
    }

    cache_misses_.fetch_add(1, std::memory_order_relaxed);

    // Compile the expression into a standalone `function __bp_eval__() { return
    // <expr> }` using the direct compiler (no type checker), so the expression
    // may reference program locals that are injected as globals into the scratch
    // environment at evaluation time.
    std::string compile_error;
    auto compiled = compile_expression_direct(expression, compile_error);

    if (!compiled.has_value()) {
        return std::nullopt;
    }

    // Cache the compiled function for reuse.
    {
        const std::scoped_lock lock(cache_mutex_);
        auto& entry = expression_cache_.put(expression, std::move(*compiled));
        return entry;
    }
}

std::shared_ptr<Environment> ExpressionEvaluator::build_scratch_environment(VM* target_vm,
                                                                            int frame_index) const {
    auto scratch_env = Environment::create();
    // Register the stdlib in SANDBOX mode: debug-expression evaluation (watches,
    // conditional breakpoints, logpoints, hover/REPL eval) must be free of OS
    // side effects — it can run automatically and repeatedly while stepping, so a
    // watch like `FileSystem.delete("x")` or `Process.run(...)` must never touch
    // the machine.  Sandbox mode skips the os_only modules (Console, Csv,
    // FileSystem, Http, KeyValueStore, Process, Socket, Xml) and makes their
    // look-ups report "not available in sandbox mode" instead of executing.
    register_all(scratch_env, /*sandbox=*/true);

    // Copy current locals into the scratch environment as globals.  Locals are
    // defined first so they shadow any program global of the same name in the
    // global-copy step below.
    const VMIntrospector intro(*target_vm);

    if (static_cast<std::size_t>(frame_index) < intro.frame_count()) {
        auto locals = intro.locals(static_cast<std::size_t>(frame_index));

        for (const auto& local : locals) {
            if (!local.name.empty() && local.name != "_") {
                scratch_env->define(local.name, local.value, false);
            }
        }
    }

    // Copy the program's top-level globals (user-defined functions, records,
    // choices, and namespaces) so compound expressions can reference them.  The
    // target VM's global environment also holds the stdlib bindings, so skip any
    // name already present — this preserves the stdlib registration above and
    // lets the copied locals shadow globals of the same name.
    if (auto program_globals = target_vm->global_env()) {
        program_globals->for_each_binding([&](const std::string& name, const Value& value) {
            if (!scratch_env->has_local(name)) {
                scratch_env->define(name, value, false);
            }
        });
    }

    return scratch_env;
}

Variable ExpressionEvaluator::evaluate_on_scratch_vm(VM* target_vm, int frame_index,
                                                     const std::string& expression) const {
    Variable result;
    result.name = expression;
    result.value = std::string{messages::expression::evaluation_failed};
    result.type = "unknown";

    auto eval_function = lookup_or_compile(expression);

    if (!eval_function.has_value()) {
        result.value = std::string{messages::expression::invalid};
        return result;
    }

    auto scratch_env = build_scratch_environment(target_vm, frame_index);

    // Declared before the scratch VM so it outlives the debug hook that captures
    // it by reference (locals are destroyed in reverse declaration order).
    std::atomic<bool> timed_out{false};

    VM scratch_vm{scratch_env};
    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + evaluation_timeout_;

    // Enforce the timeout *during* execution rather than only measuring it
    // afterwards.  The debug hook fires on each source-line change once a pause
    // is armed; past the deadline it aborts the scratch VM through the pause
    // callback (returning false terminates the run).  Without this a
    // non-terminating expression — a watch/condition/logpoint that loops or
    // blocks forever — would hang the calling thread indefinitely, freezing DAP
    // request handling (watch/evaluate) or the debuggee (conditions/logpoints).
    //
    // Granularity caveat: the hook fires on source-line changes, so it bounds
    // looping expressions but cannot interrupt a single native call that blocks
    // without advancing a line.  The scratch environment is sandboxed (see
    // build_scratch_environment), so the OS blocking primitives (Socket, Process,
    // Http) are unavailable; the residual risk is an always-available blocking
    // primitive such as Channel.receive.  Debug expressions are expected to be
    // pure and non-blocking; the VM's own resource guards (checked below) remain
    // the backstop for the rest.
    scratch_vm.set_debug_hook([deadline, &timed_out](int, int, std::size_t) {
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out.store(true, std::memory_order_relaxed);
            return true; // request a pause so the pause callback can abort
        }

        return false;
    });
    scratch_vm.set_pause_callback([]() { return false; }); // false => terminate
    scratch_vm.request_pause_check();                      // arm the per-line hook

    Value evaluated;
    bool threw = false;
    bool runaway = false;

    try {
        // Run the compiled `function __bp_eval__() { return <expr> }` and read
        // its return value directly.  This is far more robust than capturing the
        // argument of a stubbed print(): execute_function returns the
        // expression's Value without relying on @main bootstrapping.
        evaluated = scratch_vm.execute_function(*eval_function);
    } catch (const std::exception& ex) {
        threw = true;
        // A non-terminating expression can also trip the VM's global
        // runaway-loop guard before the cooperative time deadline fires: on a
        // fast host the max-iteration cap is reached first and surfaces as a
        // RuntimeError, whereas on a slow host the deadline wins.  Both mean the
        // expression does not terminate, so treat the guard the same as a
        // deadline abort rather than reporting a generic evaluation error — the
        // shared message generator keeps this in step with the VM.
        const std::string runaway_message =
            vm_errors::loop_exceeded_max_iterations(ResourceLimits::max_while_iterations);
        if (std::string_view{ex.what()}.find(runaway_message) != std::string_view::npos) {
            runaway = true;
        }
    } catch (...) {
        // A deadline abort can also surface as a non-std::exception while
        // unwinding the scratch VM's active frames, so defer the decision to the
        // timeout check below rather than reporting a generic error here.
        threw = true;
    }

    const auto elapsed = std::chrono::steady_clock::now() - start_time;

    if (runaway || timed_out.load(std::memory_order_relaxed) || elapsed > evaluation_timeout_) {
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        result.value = std::format("<timeout: {}ms>", elapsed_ms);
        result.type = "error";
        return result;
    }

    if (threw) {
        result.value = std::string{messages::expression::error};
        return result;
    }

    result.value = evaluated.to_string();
    result.type = evaluated.display_type_name();

    if (is_structured(evaluated)) {
        result.variables_reference = alloc_ref_(std::make_shared<Value>(std::move(evaluated)));
    }

    return result;
}

} // namespace luma::dap
