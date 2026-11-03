#ifndef LUMA_SYMBOLS_SYMBOL_KIND_HPP
#define LUMA_SYMBOLS_SYMBOL_KIND_HPP

#include <cstdint>
#include <ostream>
#include <string_view>

namespace luma::symbols {

// ═══════════════════════════════════════════════════════════════════
// SymbolKind — classification of named symbols
// ═══════════════════════════════════════════════════════════════════
//
// Shared between the type checker (export tables), the compiler
// (qualified name resolution), and the language server (document
// symbols and completion items).
//
// Values are chosen to align with the LSP SymbolKind specification
// (https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#symbolKind)
// so the language server can use them directly without mapping.

// Enumerators are listed in ascending value order.
enum class SymbolKind : uint8_t {
    Namespace = 3,
    Field = 8,
    Enum = 10, // Choice type
    Interface = 11,
    Function = 12,
    Variable = 13,
    Constant = 14,
    TypeAlias = 19, // Object in LSP terms
    Struct = 23,    // Record type
};

// Convert SymbolKind to its integer value for LSP protocol.
[[nodiscard]] constexpr int to_lsp_symbol_kind(SymbolKind kind) noexcept {
    return static_cast<int>(kind);
}

// Return a human-readable name for a SymbolKind (useful for logging and debugging).
[[nodiscard]] constexpr std::string_view symbol_kind_name(SymbolKind kind) noexcept {
    switch (kind) {
        case SymbolKind::Namespace:
            return "Namespace";
        case SymbolKind::Field:
            return "Field";
        case SymbolKind::Enum:
            return "Enum";
        case SymbolKind::Interface:
            return "Interface";
        case SymbolKind::Function:
            return "Function";
        case SymbolKind::Variable:
            return "Variable";
        case SymbolKind::Constant:
            return "Constant";
        case SymbolKind::TypeAlias:
            return "TypeAlias";
        case SymbolKind::Struct:
            return "Struct";
    }

    // No default case above: -Wswitch then flags any newly added enumerator.
    // Reached only for an out-of-range value (e.g. cast from an invalid int).
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, SymbolKind kind) {
    return os << symbol_kind_name(kind);
}

} // namespace luma::symbols

#endif // LUMA_SYMBOLS_SYMBOL_KIND_HPP
