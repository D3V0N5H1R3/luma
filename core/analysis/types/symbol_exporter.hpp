// ─────────────────────────────────────────────────────────────────────────────
// Symbol Exporter
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Build the exported symbol table from the TypeChecker's
//                 internal registries.
//
// The LSP uses this to provide hover, completions, and go-to-definition
// with accurate inferred types.
//
// Dependencies:
//   - analysis/types/type_info.hpp: For SymbolTable and TypeInfo.
//   - analysis/types/type_checking_context.hpp: Accessed via TypeCheckingServices (in .cpp only).
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "analysis/types/type_info.hpp"

namespace luma {

class TypeCheckingServices;

class SymbolExporter {
public:
    [[nodiscard]] SymbolTable build(const TypeCheckingServices& services) const;
};

} // namespace luma
