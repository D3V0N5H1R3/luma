#ifndef LUMA_DAP_EXPRESSION_EVALUATOR_HPP
#define LUMA_DAP_EXPRESSION_EVALUATOR_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "common/lru_cache.hpp"
#include "dap_types.hpp"
#include "debugger_config.hpp"
#include "runtime/compiler/chunk.hpp"

namespace luma {
class Environment;
class VM;
class VMIntrospector;
class Value;
} // namespace luma

namespace luma::dap {

// ═══════════════════════════════════════════════════════════
// Expression evaluator — evaluates expressions in the context
// of a paused VM frame, with compiled expression caching.
//
// Strategies (in order):
//   1. Direct local variable lookup via VMIntrospector
//   2. Closure upvalue lookup via VMIntrospector
//   3. Global environment lookup
//   4. Scratch VM compilation and execution (cached)
//
// Design note: symbol resolution here is *runtime/VM-based*,
// querying live stack frames, upvalues, and the global
// environment via VMIntrospector. This is fundamentally
// different from the LSP's *static/AST-based* resolution
// (lsp_symbol_resolver.hpp), which queries AnalysisResult
// maps from the type checker. Unifying these approaches is
// not practical — they operate on disjoint data domains.
// Shared vocabulary types live in shared/symbols/.
// ═══════════════════════════════════════════════════════════

// DAP evaluation context — determines which evaluation strategies
// are safe and whether results can be cached.
enum class EvaluationContext {
    Watch,     // "watch"     — cacheable, allows scratch VM execution
    Hover,     // "hover"     — read-only, no side effects
    Repl,      // "repl"      — not cacheable, allows scratch VM execution
    Clipboard, // "clipboard" — cacheable, allows scratch VM execution
    Variables, // "variables" — read-only, no side effects
    Default    // ""          — unspecified context
};

// Convert a DAP context string to the typed enum.
[[nodiscard]] inline EvaluationContext parse_evaluation_context(std::string_view ctx) {
    if (ctx == "watch") {
        return EvaluationContext::Watch;
    }
    if (ctx == "hover") {
        return EvaluationContext::Hover;
    }
    if (ctx == "repl") {
        return EvaluationContext::Repl;
    }
    if (ctx == "clipboard") {
        return EvaluationContext::Clipboard;
    }
    if (ctx == "variables") {
        return EvaluationContext::Variables;
    }
    return EvaluationContext::Default;
}

// Callback to allocate a variable reference for a structured Value,
// decoupling from VariableInspector.  Takes a shared pointer to the
// Value and returns a variablesReference ID.
using RefAllocator = std::function<int(std::shared_ptr<Value>)>;

class ExpressionEvaluator {
public:
    explicit ExpressionEvaluator(RefAllocator alloc_ref);

    // Set the compiled program (for scratch VM compilation).
    void set_compiled_program(std::shared_ptr<std::vector<CompiledFunction>> fns,
                              std::shared_ptr<CompiledFunction> top_level);

    // Set the evaluation timeout for scratch VM execution.
    void set_evaluation_timeout(std::chrono::milliseconds timeout);

    // Evaluate an expression in the given VM at the given frame.
    // Returns a Variable with the result.
    [[nodiscard]] Variable evaluate(VM* target_vm, int frame_index, const std::string& expression,
                                    EvaluationContext context = EvaluationContext::Default) const;

    // Clear the expression compilation cache (e.g., on restart).
    void clear_cache();

    // ─── Observability ───

    // Number of cache hits since construction or last clear_cache().
    [[nodiscard]] std::size_t cache_hit_count() const noexcept {
        return cache_hits_.load(std::memory_order_relaxed);
    }

    // Number of cache misses since construction or last clear_cache().
    [[nodiscard]] std::size_t cache_miss_count() const noexcept {
        return cache_misses_.load(std::memory_order_relaxed);
    }

private:
    // Shared helper: build a Variable from a name and Value.
    [[nodiscard]] std::optional<Variable> make_variable_from_value(const std::string& name,
                                                                   const Value& value) const;

    // Per-strategy evaluation helpers.
    [[nodiscard]] std::optional<Variable> try_local_lookup(const VMIntrospector& intro,
                                                           std::size_t target_frame,
                                                           const std::string& expression) const;
    [[nodiscard]] std::optional<Variable> try_upvalue_lookup(const VMIntrospector& intro,
                                                             std::size_t target_frame,
                                                             const std::string& expression) const;
    [[nodiscard]] std::optional<Variable> try_global_lookup(VM* target_vm,
                                                            const std::string& expression) const;

    // Try to evaluate by compiling on a scratch VM.
    [[nodiscard]] Variable evaluate_on_scratch_vm(VM* target_vm, int frame_index,
                                                  const std::string& expression) const;

    // Expression compilation cache: expression source → compiled eval
    // function.  Avoids re-compiling for repeated evaluations.  Each entry is a
    // standalone `function __bp_eval__() { return <expr> }` produced by the
    // direct compiler; it is executed via VM::execute_function to read the
    // expression's value back.
    [[nodiscard]] std::optional<CompiledFunction>
    lookup_or_compile(const std::string& expression) const;
    [[nodiscard]] std::shared_ptr<Environment> build_scratch_environment(VM* target_vm,
                                                                         int frame_index) const;

    RefAllocator alloc_ref_;

    // Configurable timeout for scratch VM evaluation.
    std::chrono::milliseconds evaluation_timeout_{config::expression::k_default_evaluation_timeout};

    // Compiled program references.
    std::shared_ptr<std::vector<CompiledFunction>> compiled_functions_;
    std::shared_ptr<CompiledFunction> compiled_top_level_;

    // Leaf-level lock — never held while acquiring any other mutex.
    mutable std::mutex cache_mutex_;
    mutable LruCache<std::string, CompiledFunction> expression_cache_{
        config::expression::k_max_cache_entries}; // GUARDED_BY(cache_mutex_)

    // Cache performance counters (relaxed atomics — no synchronisation overhead).
    mutable std::atomic<std::size_t> cache_hits_{0};
    mutable std::atomic<std::size_t> cache_misses_{0};
};

} // namespace luma::dap

#endif // LUMA_DAP_EXPRESSION_EVALUATOR_HPP
