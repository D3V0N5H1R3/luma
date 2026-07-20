#ifndef LUMA_RESOLVER_RESOLVER_HPP
#define LUMA_RESOLVER_RESOLVER_HPP

#include <concepts>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/common/scope_manager.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_emitter.hpp"
#include "common/string_hash.hpp"

namespace luma {

struct Program;
struct Declaration;
struct ChoiceDeclaration;
struct NamespaceDeclaration;
struct UseDeclaration;

// Resolved variable reference — produced by the name resolver and
// consumed by the bytecode compiler for O(1) variable access.
struct ResolvedVar {
    std::uint16_t frame_depth{0}; // 0 = local, 1 = enclosing, etc.
    std::uint16_t slot_index{0};  // Index in that frame's slot array.
    bool is_mutable{false};
};

// Compile-time scope for name resolution.
//
// ResolveScope does NOT use ScopeStack<T>: it relies on a shared_ptr-linked
// parent chain so lookup() can traverse enclosing scopes and adjust
// frame_depth at each hop.  See common/scope_stack.hpp §Component Usage for
// the full rationale.
class ResolveScope {
public:
    // The symbol type stored by this scope — a boolean indicating mutability.
    // The scope internally allocates slot indices, but externally the define()
    // API accepts this flag to record whether the variable is mutable.
    using SymbolType = bool;

    explicit ResolveScope(std::shared_ptr<ResolveScope> parent = nullptr)
        : parent_{std::move(parent)} {}

    // Define a variable in this scope and return its slot index.
    [[nodiscard]] std::uint16_t define(std::string_view name, bool is_mutable);

    // Define a variable only if it is not already reachable from this scope.
    // Returns true if a new definition was created, false if it already exists.
    [[nodiscard]] bool define_if_absent(std::string_view name, bool is_mutable);

    // Look up a variable by name, searching parent scopes.
    // Returns std::nullopt if not found.
    [[nodiscard]] std::optional<ResolvedVar> lookup(std::string_view name) const;

    // Check if a variable is defined in this scope only (not cached parent lookups).
    [[nodiscard]] bool has_local(std::string_view name) const;

    [[nodiscard]] std::uint16_t local_count() const noexcept {
        return next_slot_;
    }

    [[nodiscard]] std::shared_ptr<ResolveScope> parent() const noexcept {
        return parent_;
    }

    // Maximum slot index that fits in a uint16_t frame offset.
    static constexpr std::uint16_t k_max_slot_index = std::numeric_limits<std::uint16_t>::max();

private:
    mutable StringMap<ResolvedVar> variables_;
    StringSet locals_;
    std::shared_ptr<ResolveScope> parent_;
    std::uint16_t next_slot_{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// ScopeType Concept
// ─────────────────────────────────────────────────────────────────────────────
// Common interface shared by the analysis scopes (ResolveScope, TypeScope,
// LinterTracker::ScopeData): a nested SymbolType plus define(), lookup(), and
// has_local().  These scopes have fundamentally different symbol types and
// lookup semantics, so they are NOT interchangeable through this concept; it is
// enforced on individual scope types via static_assert (see below) rather than
// used as a generic constraint on ScopeStack<T>.
//
// See common/scope_stack.hpp §Component Usage for the full rationale on how the
// scopes relate and why they are not unified into a single base.
// ─────────────────────────────────────────────────────────────────────────────
template <typename S>
concept ScopeType = requires(S& scope) {
    // Each scope type must expose a nested SymbolType alias.
    typename S::SymbolType;
} && requires(S& scope, std::string_view name, typename S::SymbolType sym) {
    // Define a symbol in the current scope.
    scope.define(name, sym);
    // Look up a symbol by name (returning optional or pointer).
    { scope.lookup(name) };
    // Check if a symbol is defined locally (not in parent scopes).
    { scope.has_local(name) } -> std::convertible_to<bool>;
};

static_assert(ScopeType<ResolveScope>, "ResolveScope must satisfy the ScopeType concept");

// Name resolution pass — resolves all variable references to slot
// indices, detects undeclared variables, and annotates the AST with
// ResolvedVar information.
class NameResolver : public DiagnosticEmitter, public ScopeManager<NameResolver> {
public:
    NameResolver() : DiagnosticEmitter(DiagnosticCategory::Compile, DiagnosticSource::Name) {}

    // Resolve all names in the program.
    // Returns a list of diagnostics (errors for undeclared variables, etc.).
    [[nodiscard]] std::vector<Diagnostic> resolve(Program& program);

private:
    // The ScopeManager<NameResolver> mixin supplies make_scope_guard(), which
    // calls back into push_scope / pop_scope, so it must be a friend.  These
    // maintain the shared_ptr-linked scope chain: structurally parallel to the
    // Linter's flat-stack push/pop but with a distinct storage layout, which is
    // exactly why the shared seam unifies only the guard lifecycle and not the
    // storage.  See common/scope_stack.hpp §Component Usage (TODO(refactor/A10)).
    friend class ScopeManager<NameResolver>;

    void push_scope();
    void pop_scope();

    // ─── Core traversal ────────────────────────────────────────────────
    void resolve_declaration(const Declaration& decl);
    void resolve_statement(const Statement& stmt);
    void resolve_expression(const Expression& expr);

    // ─── Shared helpers ────────────────────────────────────────────────
    void resolve_body(const std::vector<std::unique_ptr<Statement>>& body);
    // Open a fresh lexical scope, resolve `body` within it, then close it.
    void resolve_scoped_body(const std::vector<std::unique_ptr<Statement>>& body);
    void resolve_parameters(const std::vector<Parameter>& params);
    void resolve_match_arm(const MatchArm& arm);

    // ─── First-pass registration helpers ────────────────────────────────
    // Called during the first pass of resolve() to register top-level
    // declaration names so that forward references resolve correctly.
    void register_named_declaration(std::string_view name);
    void register_choice(const ChoiceDeclaration& choice);
    void register_namespace(const NamespaceDeclaration& ns);

    // Define a namespace member's exported name(s) in the current scope, each
    // qualified by `namespace_name` (empty for a bare `use` import): functions
    // and records define "Namespace.name", choices define
    // "Namespace.name.variant" per variant.  `absent_only` selects
    // define_if_absent (for `use` imports) over define (for registration).
    void define_namespace_member(const Declaration& member, std::string_view namespace_name,
                                 bool absent_only);

    // ─── Use-declaration processing ─────────────────────────────────────
    void resolve_use_declaration(const UseDeclaration& use_decl, const Program& program);
    void resolve_wildcard_import(const NamespaceDeclaration& ns);
    void resolve_specific_import(const NamespaceDeclaration& ns, std::string_view member_name);
    [[nodiscard]] static const NamespaceDeclaration* find_namespace(const Program& program,
                                                                    std::string_view name);

    // ─── resolve_statement helpers ──────────────────────────────────────
    void resolve_for_statement(const ForStatement& for_stmt);
    void resolve_if_statement(const IfStatement& if_stmt);
    void resolve_match_statement(const MatchStatement& match_stmt);
    void resolve_try_statement(const TryStatement& try_stmt);

    // ─── resolve_expression helpers ─────────────────────────────────────
    void resolve_lambda_expression(const LambdaExpression& lambda);
    void resolve_if_expression(const IfExpression& if_expr);
    void resolve_match_expression(const MatchExpression& match_expr);

    std::shared_ptr<ResolveScope> current_scope_;

    // Depth of the current resolve_expression recursion.  Guards against native
    // stack overflow on pathologically deep expression ASTs (e.g. a very long
    // parenthesised `a + b + c + ...` chain the parser builds iteratively).
    // Mirrors the Linter's expression_depth_ guard; resolution annotates the AST
    // rather than reporting, so it simply stops descending when the cap is hit.
    int expression_depth_{0};
};

} // namespace luma

#endif // LUMA_RESOLVER_RESOLVER_HPP
