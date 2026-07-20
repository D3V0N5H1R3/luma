#ifndef LUMA_LSP_ANALYSIS_VIEW_HPP
#define LUMA_LSP_ANALYSIS_VIEW_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/lexer/token.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_optional_ref.hpp"
#include "lsp_symbol_lookup.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// AnalysisResultView — read-only facade over AnalysisResult.
//
// Hides the SemanticAnalysis/AnalysisMetadata split behind a
// single query interface. Handlers that use this class are
// insulated from changes to the internal AnalysisResult layout.
//
// This is a non-owning view; the referenced AnalysisResult must
// outlive the view.
//
// Design notes:
// - Thin convenience layer — each method is a one-liner that
//   delegates to the underlying data or to existing free
//   functions from lsp_symbol_resolver.hpp.
// - Symbol lookup methods (find_definition, find_function,
//   find_record, etc.) delegate to an internal SymbolLookup
//   instance, eliminating duplicated map-lookup boilerplate.
// - Complements SymbolLookup (focused on map-based symbol
//   queries) by also exposing token access, identifier index
//   queries, scope-aware resolution, and cached AST access.
// - Supersedes the unused AnalysisQuery class with broader
//   coverage of actual handler access patterns.
// ═══════════════════════════════════════════════════════════

class AnalysisResultView {
public:
    explicit AnalysisResultView(const AnalysisResult& result)
        : result_{result}, lookup_{result.semantic} {}

    // ─── Token access ───

    [[nodiscard]] const std::vector<Token>& tokens() const {
        return result_.semantic.tokens;
    }

    [[nodiscard]] const Token& token(std::size_t index) const {
        return result_.semantic.tokens[index];
    }

    [[nodiscard]] std::size_t token_count() const {
        return result_.semantic.tokens.size();
    }

    // Find the token at a 0-based (line, character) position.
    // Returns the index into the token vector.
    [[nodiscard]] std::optional<std::size_t> find_token_at(int line, int character) const {
        return luma::lsp::find_token_at(result_, line, character);
    }

    // ─── Diagnostics ───

    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const {
        return result_.semantic.diagnostics;
    }

    [[nodiscard]] bool has_diagnostics() const {
        return !result_.semantic.diagnostics.empty();
    }

    // ─── Symbol lookups (delegated to SymbolLookup) ───

    [[nodiscard]] optional_ref<const SymbolDefinition>
    find_definition(const std::string& name) const {
        return lookup_.find_definition(name);
    }

    [[nodiscard]] optional_ref<const UserFunctionInfo>
    find_function(const std::string& name) const {
        return lookup_.find_function(name);
    }

    [[nodiscard]] optional_ref<const RecordInfo> find_record(const std::string& name) const {
        return lookup_.find_record(name);
    }

    [[nodiscard]] optional_ref<const std::string> find_doc_comment(const std::string& name) const {
        return lookup_.find_doc_comment(name);
    }

    [[nodiscard]] optional_ref<const std::vector<std::string>>
    find_choice_variants(const std::string& name) const {
        return lookup_.find_choice_variants(name);
    }

    [[nodiscard]] optional_ref<const std::vector<std::string>>
    find_interface_implementations(const std::string& name) const {
        return lookup_.find_interface_implementations(name);
    }

    // ─── Identifier index queries ───

    // Look up all occurrences of an identifier by name.
    // Returns indices into the token vector.
    [[nodiscard]] optional_ref<const std::vector<std::size_t>>
    find_identifier_occurrences(const std::string& name) const {
        auto it = result_.metadata.identifier_index.find(name);
        if (it != result_.metadata.identifier_index.end()) {
            return it->second;
        }
        return {};
    }

    // ─── Function scope queries ───

    [[nodiscard]] optional_ref<const std::pair<int, int>>
    find_function_body_range(const std::string& name) const {
        return lookup_.find_function_body_range(name);
    }

    // Find the enclosing function for a 1-based line number.
    [[nodiscard]] std::optional<std::string> find_enclosing_function(int line_1based) const {
        return luma::lsp::find_enclosing_function(result_, line_1based);
    }

    // ─── Variable resolution ───

    // Scope-aware variable type resolution at a 1-based line number.
    [[nodiscard]] std::optional<ResolvedVariableType> resolve_variable_type(const std::string& name,
                                                                            int line_1based) const {
        return luma::lsp::resolve_variable_type(result_, name, line_1based);
    }

    // Check whether a name is a local variable (not a top-level definition or function).
    [[nodiscard]] bool is_local_variable(const std::string& name) const {
        return luma::lsp::is_local_variable(result_, name);
    }

    // Check whether a local variable is in scope at a 1-based line.
    [[nodiscard]] bool is_in_scope(const std::string& name, int line_1based) const {
        return luma::lsp::is_in_scope(result_, name, line_1based);
    }

    // ─── Namespace queries ───

    // Count user-defined functions whose qualified name starts with "prefix.".
    [[nodiscard]] std::size_t count_namespace_members(const std::string& ns_prefix) const {
        const std::string prefix = ns_prefix + ".";
        std::size_t count{0};
        for (const auto& [qname, _] : result_.semantic.symbols.user_functions) {
            if (qname.starts_with(prefix)) {
                ++count;
            }
        }
        return count;
    }

    // ─── AST access ───

    // Access the cached AST (populated after a successful parse).
    [[nodiscard]] optional_ref<const Program> cached_program() const {
        if (result_.metadata.cached_program.has_value()) {
            return *result_.metadata.cached_program;
        }
        return {};
    }

    // ─── Escape hatch ───

    // Direct access to the underlying AnalysisResult for uncommon queries
    // not yet covered by the facade. Prefer adding a method over using this.
    [[nodiscard]] const AnalysisResult& raw() const {
        return result_;
    }

private:
    const AnalysisResult& result_;
    SymbolLookup lookup_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_ANALYSIS_VIEW_HPP
