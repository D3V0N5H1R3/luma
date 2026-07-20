#ifndef LUMA_LSP_SCOPE_STACK_HPP
#define LUMA_LSP_SCOPE_STACK_HPP

#include <optional>
#include <string>
#include <vector>

#include "lsp_analysis_result.hpp"
#include "lsp_symbol_resolver.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// ScopeStack — reusable scope-chain abstraction for LSP handlers.
//
// Multiple LSP features (completion, hover, definition, references)
// need to walk up the scope hierarchy from a cursor position,
// searching for symbols at each level. This class encapsulates
// that common pattern.
//
// Scope levels (innermost to outermost):
//   1. Block scope — variables visible in the current block within a function
//   2. Function scope — parameters and all locals of the enclosing function
//   3. Module scope — top-level definitions, user functions, records, choices
//
// Usage:
//   ScopeStack scopes{result, line};
//   for (auto it = scopes.begin(); it != scopes.end(); ++it) {
//       // inspect *it (a ScopeLevel)
//   }
//   auto sym = scopes.find_symbol("x");  // searches outward
// ═══════════════════════════════════════════════════════════

// The kind of scope a level represents.
enum class ScopeKind {
    Block,    // Block-level scope within a function body
    Function, // Function parameter + local scope
    Module    // Top-level / document scope
};

// A resolved symbol found during scope traversal.
struct ScopeSymbol {
    std::string name;
    std::string type_name;
    ScopeKind origin; // Which scope level it was found in
    bool is_mutable{false};
    bool is_parameter{false};
};

// Represents a single level in the scope hierarchy.
// Provides methods to query symbols visible at that level only.
class ScopeLevel {
public:
    ScopeLevel(ScopeKind kind, const AnalysisResult& result, int line_1based,
               const std::optional<std::string>& enclosing_function)
        : kind_{kind},
          result_{result},
          line_{line_1based},
          enclosing_function_{enclosing_function} {}

    [[nodiscard]] ScopeKind kind() const {
        return kind_;
    }

    // Find a symbol by name at this scope level only.
    // Returns nullopt if the symbol is not visible at this level.
    [[nodiscard]] std::optional<ScopeSymbol> find(const std::string& name) const;

    // Collect all symbols visible at this scope level.
    [[nodiscard]] std::vector<ScopeSymbol> collect_all() const;

private:
    ScopeKind kind_;
    const AnalysisResult& result_;
    int line_;
    std::optional<std::string> enclosing_function_;

    [[nodiscard]] std::optional<ScopeSymbol> find_in_block(const std::string& name) const;
    [[nodiscard]] std::vector<ScopeSymbol> collect_block() const;
    [[nodiscard]] std::optional<ScopeSymbol> find_in_function(const std::string& name) const;
    [[nodiscard]] std::vector<ScopeSymbol> collect_function() const;
    [[nodiscard]] std::optional<ScopeSymbol> find_in_module(const std::string& name) const;
    [[nodiscard]] std::vector<ScopeSymbol> collect_module() const;
};

// ═══════════════════════════════════════════════════════════
// ScopeStack — iterable scope chain from a document position.
// ═══════════════════════════════════════════════════════════

class ScopeStack {
public:
    // Construct a scope stack for a 1-based source line within an analysis result.
    ScopeStack(const AnalysisResult& result, int line_1based);

    // ─── Iterator interface ───

    using const_iterator = std::vector<ScopeLevel>::const_iterator;

    [[nodiscard]] const_iterator begin() const {
        return levels_.begin();
    }

    [[nodiscard]] const_iterator end() const {
        return levels_.end();
    }

    [[nodiscard]] std::size_t size() const {
        return levels_.size();
    }

    [[nodiscard]] bool empty() const {
        return levels_.empty();
    }

    // Access a specific level by index (0 = innermost).
    [[nodiscard]] const ScopeLevel& operator[](std::size_t index) const {
        return levels_[index];
    }

    // ─── Symbol search (outward) ───

    // Find a symbol by name, searching from innermost scope outward.
    // Returns the first match found (closest scope wins).
    [[nodiscard]] std::optional<ScopeSymbol> find_symbol(const std::string& name) const;

    // Collect all visible symbols at all scope levels.
    // Inner scopes shadow outer scopes (first occurrence wins).
    [[nodiscard]] std::vector<ScopeSymbol> collect_visible_symbols() const;

    // ─── Context accessors ───

    // The name of the enclosing function (nullopt if at module level).
    [[nodiscard]] const std::optional<std::string>& enclosing_function() const {
        return enclosing_function_;
    }

    // Whether the position is inside a function body.
    [[nodiscard]] bool inside_function() const {
        return enclosing_function_.has_value();
    }

    // The innermost scope kind at the cursor position.
    [[nodiscard]] ScopeKind innermost_kind() const {
        if (levels_.empty()) {
            return ScopeKind::Module;
        }
        return levels_.front().kind();
    }

private:
    const AnalysisResult& result_;
    int line_;
    std::optional<std::string> enclosing_function_;
    std::vector<ScopeLevel> levels_;

    void build_levels();
};

} // namespace luma::lsp

#endif // LUMA_LSP_SCOPE_STACK_HPP
