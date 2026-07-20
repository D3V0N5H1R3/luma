// ─────────────────────────────────────────────────────────────────────────────
// Compilation Context — mutable state for a single compilation pass
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Group all mutable state that accumulates during a single
//   compile() call so that helper classes can access shared state through
//   a well-defined struct.
//
// Extracted from compiler.hpp to reduce the size of the central header and
// allow forward declarations of these types in other headers.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_COMPILATION_CONTEXT_HPP
#define LUMA_COMPILER_COMPILATION_CONTEXT_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "common/string_hash.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/compiled_function.hpp"
#include "runtime/compiler/exception_context.hpp"
#include "runtime/compiler/loop_context.hpp"
#include "runtime/compiler/string_interner.hpp"

namespace luma {

// Compile-time local variable.
// `name` is an InternedString handle into the Compiler's StringInterner,
// enabling O(1) name comparison during local variable resolution.
struct Local {
    InternedString name;
    int depth{0};
    bool is_mutable{false};
    bool is_captured{false};    // Will be captured as an upvalue.
    bool is_number_type{false}; // Declared with `number` type annotation.
};

// Compiler scope — one per function / lambda / top-level.
struct CompilerScope {
    CompiledFunction function;
    std::vector<Local> locals;
    int scope_depth{0};

    // Centralises break/continue state for the innermost active loop.
    // Push on loop entry (begin_loop), pop on loop exit (end_loop).
    // break  → add_break(jump_offset);  end_loop patches all collected offsets.
    // continue → emit_loop(loop_context.current_start(), loc);
    LoopContext loop_context;

    // Active try/catch/finally blocks — used to unwind exception handlers
    // on break, continue, and return.
    ExceptionContext exception_context;
};

/// Tracks compiled function artifacts during compilation.
/// Functions are registered here as lambdas and named functions are compiled,
/// and indices into this registry are emitted as MakeClosure operands.
struct CompiledFunctionRegistry {
    std::vector<CompiledFunction> functions;

    void clear() {
        functions.clear();
    }
};

/// Tracks diagnostic messages emitted during compilation.
/// Accumulates errors and warnings; the `has_error` flag is a fast check
/// for whether any error-level diagnostic was reported.
struct CompilationDiagnostics {
    std::vector<Diagnostic> diagnostics;
    bool has_error{false};

    /// Append an error-level diagnostic and set the error flag.
    void add_error(Diagnostic diagnostic) {
        has_error = true;
        diagnostics.push_back(std::move(diagnostic));
    }

    /// Append a warning-level diagnostic (does not set the error flag).
    void add_warning(Diagnostic diagnostic) {
        diagnostics.push_back(std::move(diagnostic));
    }

    void clear() {
        diagnostics.clear();
        has_error = false;
    }
};

/// Non-owning lookup table of AST RecordDeclaration nodes by name.
/// Populated during the forward-declaration pass so that record creation
/// expressions can resolve field orderings at compile time.
/// Valid only during a single compile() call — the AST must outlive the table.
struct RecordDeclarationTable {
    StringMap<std::reference_wrapper<const RecordDeclaration>> declarations;

    void clear() {
        declarations.clear();
    }
};

// Compilation state — groups the mutable state that accumulates during a
// single compile() call.  Extracted from Compiler so that helper classes
// (VariableResolver, etc.) can access shared state through a well-defined
// struct rather than reaching into Compiler's private members.
//
// Sub-objects group related fields into cohesive units:
//   - compiled_functions: registry of compiled function artifacts
//   - diagnostics:        error/warning messages and error flag
//   - record_declarations: lookup table of record type declarations
struct CompilationContext {
    // Non-owning — the AST (Program) must outlive the compilation pass.
    // Set at the start of compile() and read (never written through)
    // during the traversal.
    const Program* program{nullptr};

    std::vector<CompilerScope> scope_stack;

    /// Registry of compiled function artifacts (lambdas and named functions).
    CompiledFunctionRegistry compiled_functions;

    /// Diagnostic messages and error state accumulated during compilation.
    CompilationDiagnostics diagnostics;

    /// Tracks whether the last emitted opcode was a Return, enabling
    /// dead-code elimination of redundant implicit returns.
    bool last_was_return{false};

    /// Lookup table of record type declarations by name.
    RecordDeclarationTable record_declarations;

    // Interner for local variable names.  Enables O(1) name comparison during
    // local variable resolution (resolve_local, resolve_upvalue_in).
    //
    // Mutable because intern() logically doesn't modify the compilation
    // output, but needs to update the deduplication cache.  resolve_local
    // (a const method) interns the search key so it can compare interned
    // handles in O(1).  This is an acceptable use of mutable for caching.
    mutable StringInterner interner;
};

/// State for a single for-loop iteration compilation.
/// Groups the many slot/offset parameters that compile_for_iteration needs,
/// replacing a 6-parameter function signature with a single struct.
struct ForIterationState {
    std::uint16_t iterator_slot;
    std::uint16_t loop_variable_slot;
    std::uint16_t index_slot;
    std::vector<std::uint16_t> destructure_slots;
    std::size_t loop_start;
    bool use_key_value;
};

} // namespace luma

#endif // LUMA_COMPILER_COMPILATION_CONTEXT_HPP
