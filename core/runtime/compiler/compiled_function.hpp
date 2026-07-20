// ─────────────────────────────────────────────────────────────────────────────
// CompiledFunction — compile-time function representation
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Store the bytecode chunk, parameter info, upvalue
//   descriptors, and debug metadata for a single compiled function.
//   This is the compiler's output; it does not hold runtime state.
//
// Extracted from chunk.hpp to separate the bytecode container (Chunk) from
// the function-level metadata that wraps it.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_COMPILED_FUNCTION_HPP
#define LUMA_COMPILER_COMPILED_FUNCTION_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "common/string_hash.hpp"
#include "runtime/compiler/chunk.hpp"

namespace luma {

// Debug metadata for a compiled function — maps slot indices to variable
// names and mutability.  Separated from CompiledFunction so debug info
// can be stripped or handled independently.
struct FunctionDebugInfo {
    // Maps slot index → variable name.
    // Slot 0 is the function itself (unnamed); slots 1..arity are params.
    // Entries beyond arity are local variables declared in the function body.
    std::vector<std::string> local_names;

    // Parallel to local_names: true if the local is declared mutable.
    std::vector<bool> local_mutable;
};

// Compile-time function representation — bytecode chunk, parameter info,
// upvalue descriptors, and debug metadata.  This is the compiler's output;
// it does not hold runtime state.  At runtime the VM wraps a pointer to
// this struct inside a FunctionValue (value_collections.hpp), which pairs
// it with captured upvalue values to form a closure.
//
// Member layout note: chunk_data and verified_ keep the same declaration
// positions as the original public fields (chunk and is_verified) so that
// pre-compiled translation units continue to work correctly during
// incremental builds before a full library rebuild.
struct CompiledFunction {
    std::string name;
    // Raw bytecode chunk — access via chunk() / mutable_chunk() accessors.
    Chunk chunk_data;
    int arity{0};          // Total number of parameters (required + optional).
    int required_arity{0}; // Number of required parameters (no default value).
    int upvalue_count{0};
    std::vector<std::string> param_names; // Parameter names for named-arg reordering.

    // Hash map from parameter name to positional index.
    // Uses std::string keys (not string_view) because CompiledFunction
    // objects are moved after compilation, which would invalidate
    // string_view pointers into param_names (SSO strings change address
    // on move).
    // Built eagerly via build_param_name_index() after param_names is
    // finalised; immutable at runtime — safe for concurrent reads.
    StringMap<int> param_name_index_;

    /// Builds the parameter name index. Must be called once after
    /// param_names is finalised (typically at the end of compilation).
    void build_param_name_index() {
        param_name_index_.clear();
        for (int i = 0; i < static_cast<int>(param_names.size()); ++i) {
            param_name_index_.emplace(param_names[static_cast<std::size_t>(i)], i);
        }
    }

    /// Returns the parameter name → index map.
    [[nodiscard]] const StringMap<int>& param_name_index() const {
        return param_name_index_;
    }

    // Debug metadata (variable names and mutability).
    FunctionDebugInfo debug_info;

    // Upvalue descriptor — describes how to capture a variable from
    // an enclosing scope.
    struct Upvalue {
        std::uint16_t index{0}; // Slot index in the enclosing frame.
        bool is_local{true};    // true = captured from immediate parent;
                                // false = captured from grandparent (forwarded).
        bool is_mutable{false}; // true = the captured variable is mutable;
                                // mutable upvalues use shared cells so
                                // multiple closures can observe mutations.
    };

    std::vector<Upvalue> upvalues;
    bool is_main{false};
    bool is_test{false};
    // Verification flag — access via is_verified() / mark_verified() accessors.
    bool verified_{false};

    // ─── Chunk accessors ───
    // Read-only access to the bytecode chunk (VM, verifier, debugger).
    [[nodiscard]] const Chunk& chunk() const noexcept {
        return chunk_data;
    }

    // Mutable access to the bytecode chunk (compiler, optimizer, serializer).
    [[nodiscard]] Chunk& mutable_chunk() noexcept {
        return chunk_data;
    }

    // ─── Verification state ───
    // True if the bytecode verifier has validated this function's bytecode.
    // When true, the VM may skip redundant bounds checks in the hot path.
    [[nodiscard]] bool is_verified() const noexcept {
        return verified_;
    }

    // Mark the function as verified (called by VerifierPass after a clean run).
    void mark_verified() noexcept {
        verified_ = true;
    }

    // Set verification state explicitly (used by the bytecode deserializer).
    void set_is_verified(bool v) noexcept {
        verified_ = v;
    }
};

} // namespace luma

#endif // LUMA_COMPILER_COMPILED_FUNCTION_HPP
