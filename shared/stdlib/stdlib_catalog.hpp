#ifndef LUMA_STDLIB_CATALOG_HPP
#define LUMA_STDLIB_CATALOG_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Shared Stdlib Catalog — Single Source of Truth
// ─────────────────────────────────────────────────────────────────────────────
// This catalog is the authoritative source for stdlib function metadata
// (arity, parameter lists, return types, capabilities).  Both the type
// checker (core/analysis/types/stdlib_type_handler.hpp) and the language
// server (language-server/source/lsp_stdlib_registry.hpp) derive their
// data from this catalog at initialization time.
//
// To add a new stdlib function: add it here.  The type checker and
// language server will pick it up automatically.  Manual entries in
// StdlibTypeRegistry::init_signatures() are only needed for return-type
// refinement (generic type relationships the ReturnTypeDesc cannot express)
// and parameter-level TypeInfo objects.
//
// Parameter types are stored in FunctionSpec::param_types and are auto-derived
// from the catalog by StdlibTypeHandler::init_param_types().
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/string_hash.hpp"
#include "stdlib/stdlib_return_type.hpp"

namespace luma::stdlib {

// ─── Capabilities ───
// Each stdlib function requires a single OS capability, or Capability::None
// when it is always safe.  In sandbox mode only Capability::None functions
// are available; is_safe() performs that check.
enum class Capability : uint16_t {
    None = 0,
    FileSystem = 1 << 0,
    Network = 1 << 1,
    Process = 1 << 2,
    Console = 1 << 3,
};

// Metadata for a single stdlib function.
// Shared between the type checker (arity checking, return types) and
// the language server (completion parameter hints).
struct FunctionSpec {
    std::string qualified_name; // "Module.function"
    int arity{0};               // Exact arity, or minimum arity when is_variadic_fn is true.
    std::string params;         // Human-readable param list: "(arr: array<T>, f: func(T) -> U)"
    bool is_constant{false};    // True for named constants (Math.pi, etc.).
    ReturnTypeDesc return_type; // Return type descriptor.  When Unspecified, the type
                                // checker falls back to its manual signature table.
    Capability capabilities{Capability::None}; // Required OS capabilities.
    std::vector<ReturnTypeDesc>
        param_types; // Structured parameter types for the type checker (empty = unchecked).
    bool is_variadic_fn{false}; // True if this function accepts a variable number of arguments.

    // Returns true if this function requires no OS capabilities.
    [[nodiscard]] bool is_safe() const noexcept {
        return capabilities == Capability::None;
    }

    // Returns true if this function is variadic (accepts a variable number of arguments).
    [[nodiscard]] bool is_variadic() const noexcept {
        return is_variadic_fn;
    }

    // Returns the minimum number of arguments this function requires.
    // Arity is always non-negative.
    [[nodiscard]] int get_min_arity() const noexcept {
        return arity;
    }
};

// Convenience alias for the stdlib catalog map with transparent lookup.
using CatalogMap = luma::StringMap<FunctionSpec>;

// Returns the full stdlib function catalog, keyed by qualified name.
// The catalog is constructed once and cached internally.
[[nodiscard]] const CatalogMap& catalog();

// Returns the set of constant names (functions that are values, not callable).
[[nodiscard]] const luma::StringSet& constants();

// Returns the set of module prefixes that require the given capability.
[[nodiscard]] const luma::StringSet& sandbox_blocked_modules();

// Returns the set of stdlib function names that return result<T>.
// Useful for detecting discarded result values in the linter.
[[nodiscard]] const luma::StringSet& result_returning_functions();

} // namespace luma::stdlib

#endif // LUMA_STDLIB_CATALOG_HPP
