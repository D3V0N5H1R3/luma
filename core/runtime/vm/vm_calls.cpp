#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "runtime/cli/terminal.hpp"
#include "runtime/concurrency/thread_pool.hpp"
#include "runtime/interpreter/runtime_exceptions.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

// ─────────── Shared top-level entry ───────────

Value VM::run_top_level(const CompiledFunction& fn) {
    auto func = std::make_shared<FunctionValue>();
    func->name = fn.name;
    func->compiled = &fn;

    // Size the runtime upvalue storage to match the function's descriptor count
    // so the size==upvalue_count invariant holds on this entry path too (it is
    // otherwise established only by handle_make_closure).  Top-level/@main/@test
    // functions normally have no upvalues; this keeps a crafted .lumc claiming
    // otherwise from driving an out-of-bounds upvalue access.
    func->upvalues.resize(fn.upvalues.size());
    func->upvalue_cells.resize(fn.upvalues.size());

    push(Value{func});
    (void)call_closure(func.get(), 0);
    return run();
}

// ─────────── Execution entry points ───────────

void VM::execute(const std::vector<CompiledFunction>& functions,
                 const CompiledFunction& top_level) {
    install_compiled_functions(&functions);
    const NativeCallableScope scope{vm_native_callable_};

    (void)run_top_level(top_level);

    // Auto-call the @main function if one exists.
    for (const auto& fn : functions) {
        if (fn.is_main) {
            (void)run_top_level(fn);
            break;
        }
    }
}

Value VM::execute_function(const CompiledFunction& func) {
    const NativeCallableScope scope{vm_native_callable_};

    // RAII guard ensures VM state is cleaned up if push(), call_closure(),
    // or run() throws, so the VM can be reused (e.g. in REPL mode).
    VMStack::ResetGuard guard{stack_};

    auto result = run_top_level(func);

    guard.dismissed = true;
    return result;
}

Value VM::execute_function(const CompiledFunction& func,
                           const std::vector<CompiledFunction>& functions) {
    install_compiled_functions(&functions);
    return execute_function(func);
}

// ─────────── Test execution ───────────

bool VM::execute_tests(const std::vector<CompiledFunction>& functions,
                       const CompiledFunction& top_level) {
    install_compiled_functions(&functions);
    const NativeCallableScope scope{vm_native_callable_};

    // Execute top-level code first (defines functions, sets up globals).
    (void)run_top_level(top_level);

    // Collect @test functions.
    std::vector<const CompiledFunction*> tests;

    for (const auto& fn : functions) {
        if (fn.is_test) {
            tests.push_back(&fn);
        }
    }

    if (tests.empty()) {
        std::cout << "no tests found\n";
        return true;
    }

    int passed = 0;
    int failed = 0;

    for (const auto* test : tests) {
        try {
            (void)run_top_level(*test);

            ++passed;
            std::cout << term::out_green() << "  pass" << term::out_reset() << "  " << test->name
                      << "\n";
        } catch (const std::exception& error) {
            ++failed;
            std::cout << term::out_red() << "  FAIL" << term::out_reset() << "  " << test->name
                      << ": " << error.what() << "\n";

            // Reset VM state so the next test starts cleanly.
            stack_.top = stack_.base;
            stack_.frames.clear();
            exceptions_.clear();
        }
    }

    const int total = passed + failed;

    std::cout << "\n"
              << total << " test(s): " << term::out_green() << passed << " passed"
              << term::out_reset();

    if (failed > 0) {
        std::cout << ", " << term::out_red() << failed << " failed" << term::out_reset();
    }

    std::cout << "\n";

    return failed == 0;
}

// ─────────── Native callable initialisation ───────────

void VM::init_call_fn() {
    vm_native_callable_ = [this](const Value& callee, std::vector<Value>& args,
                                 const SourceLocation& /*loc*/) -> Value {
        auto saved_stack_size = stack_size();

        push(callee);
        for (auto& arg : args) {
            push(std::move(arg));
        }

        auto prev_base = base_depth_;
        base_depth_ = stack_.frames.size();

        try {
            call_value(callee, static_cast<int>(args.size()));
            auto result = run();
            base_depth_ = prev_base;
            return result;
        } catch (...) {
            // Clean up frames pushed by call_value / the callback and
            // any stack values pushed during execution so that the caller
            // (e.g. map_with_error_handling) sees a consistent VM state.
            while (stack_.frames.size() > base_depth_) {
                stack_.frames.pop_back();
            }

            stack_.top = stack_.base + saved_stack_size;
            base_depth_ = prev_base;
            throw;
        }
    };
}

// ─────────── Thread pool access ───────────

ThreadPool& VM::pool() {
    return task_manager_.pool();
}

// ─────────── Task VM constructor ───────────

VM::VM(EnvPtr global_env, ThreadPool& pool, const std::vector<CompiledFunction>* compiled_fns)
    : global_env_{std::move(global_env)}, compiled_functions_{compiled_fns} {
    task_manager_.shared_pool = &pool;
    stack_.frames.reserve(
        std::max(VMStack::k_frame_max, static_cast<std::size_t>(ResourceLimits::max_call_depth)));
    init_call_fn();
}

} // namespace luma
