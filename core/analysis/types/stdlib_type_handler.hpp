// ─────────────────────────────────────────────────────────────────────────────
// Stdlib Type Handler
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Consolidate all stdlib type metadata used by the type checker
// into a single handler.  This includes return-type signatures, arity info,
// parameter types, namespace membership, and return-type refinement logic.
//
// Arities and return types are derived from the shared stdlib catalog
// (shared/stdlib/stdlib_catalog.hpp) at initialization time.  The catalog is
// the single source of truth — adding a new stdlib function there is enough
// for the type checker to pick it up.  Manual entries are only needed for
// return-type refinement (generic type relationships the catalog cannot
// express) and parameter-level type information.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/types/type_info.hpp"
#include "common/string_hash.hpp"

namespace luma {

class StdlibTypeHandler {
public:
    // Populate all registries from the shared stdlib catalog.
    void initialize();

    // ─── Namespace queries ───

    [[nodiscard]] bool is_stdlib_namespace(std::string_view name) const;
    [[nodiscard]] const StringSet& namespaces() const;

    // ─── Function metadata ───

    // Arity metadata for a stdlib function.
    struct ArityInfo {
        int min_arity{0};
        bool is_variadic{false};
    };

    // Consolidated metadata for a single stdlib function.
    struct FunctionInfo {
        TypeInfo return_type;
        std::optional<ArityInfo> arity;
        std::vector<TypeInfo> param_types;
    };

    // ─── Function queries ───

    [[nodiscard]] bool has_function(std::string_view name) const;
    [[nodiscard]] const TypeInfo* get_return_type(std::string_view name) const;
    [[nodiscard]] const ArityInfo* get_arity(std::string_view name) const;
    [[nodiscard]] const std::vector<TypeInfo>* get_param_types(std::string_view name) const;

    // ─── Bulk access (for symbol export) ───

    [[nodiscard]] StringMap<TypeInfo> build_signature_map() const;

    // ─── Return-type refinement ───

    // Refine a stdlib return type by substituting StdlibAny with the
    // concrete element type inferred from call-site arguments.  Delegates to a
    // single name-keyed refiner registry in stdlib_type_signatures.cpp.
    [[nodiscard]] TypeInfo refine_return_type(std::string_view name, const TypeInfo& static_type,
                                              const std::vector<TypeInfo>& arg_types) const;

private:
    void init_signatures();
    void init_arities();
    void init_param_types();

    StringMap<FunctionInfo> functions_;
};

} // namespace luma
