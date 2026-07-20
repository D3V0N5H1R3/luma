// ─────────────────────────────────────────────────────────────────────────────
// Type Checker Module
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Perform static type checking on the AST.
//
// Key Types:
//   - TypeChecker: Main class that traverses the AST and reports type errors.
//   - TypeInfo: Represents a resolved type within the type system (type_info.hpp).
//   - TypeScope: Manages type and variable bindings for a lexical scope.
//   - GenericResolver: Manages generic type parameter bindings and inference.
//   - SymbolExporter: Builds the exported symbol table for the LSP
//                    (symbol_exporter.hpp).
//
// Dependencies:
//   - analysis/ast: For traversing the AST.
//   - analysis/diagnostics: For reporting type errors.
//
// Include audit (R-A19):
//   expression.hpp is included directly because TypeAnnotation (stored by
//   value in type_aliases_) and MatchArm (used by value in method parameters)
//   are defined there.  Statement is only forward-declared in expression.hpp
//   (via StatementPtr), so statement.hpp is not needed here.
//
//   ExpressionTypeChecker, StatementTypeChecker, and MatchExhaustivenessChecker
//   are stored as std::unique_ptr so their full definitions — which pull in
//   ast_dispatcher.hpp and the full AST — remain in the .cpp file only.
//   Callers of this header are no longer exposed to declaration.hpp or the
//   CRTP dispatcher templates.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/ast/expression.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_emitter.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/types/generic_resolver.hpp"
#include "analysis/types/stdlib_type_handler.hpp"
#include "analysis/types/symbol_exporter.hpp"
#include "analysis/types/symbol_registry.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "analysis/types/type_info.hpp"
#include "common/scope_guard.hpp"
#include "common/string_hash.hpp"

namespace luma {

// Sub-checker types stored as unique_ptr — full definitions live in .cpp only.
class ExpressionTypeChecker;
class StatementTypeChecker;
class MatchExhaustivenessChecker;

struct Program;
struct Declaration;
struct FunctionDeclaration;
struct RecordDeclaration;
struct InterfaceDeclaration;
struct ChoiceDeclaration;
struct NamespaceDeclaration;
struct TypeAliasDeclaration;
struct UseDeclaration;
struct TypeParam;
struct RecordField;

// ─────────────────────── Type Checker ───────────────────────
//
// Central orchestrator for static type analysis.  TypeChecker is the
// facade that owns all type-checking state and drives the two-pass
// algorithm (register declarations, then check them).  It delegates
// the bulk of the work to focused sub-components listed below.
//
// ── Delegation Architecture ──────────────────────────────────
//
// Each sub-component owns a single responsibility and holds a
// back-reference to TypeChecker for shared state access.  The sub-
// components are composed by value (not inheritance) so they can be
// tested or replaced independently.
//
//   Sub-component              Header / File                   Responsibility
//   ────────────────────────   ──────────────────────────────   ─────────────────────────────────
//   ExpressionTypeChecker      expression_type_checker.hpp/cpp  Infer types for all expression
//                                                               nodes (literals, calls, binary
//                                                               ops, member access, etc.).
//   StatementTypeChecker       statement_type_checker.hpp/cpp   Check type correctness of all
//                                                               statement nodes (let, assign,
//                                                               if, while, for, return, etc.).
//   GenericResolver            type_checker.hpp (above) /       Manage generic type parameter
//                              generic_resolver.cpp             bindings: push/pop/infer/
//                                                               validate bounds.
//   StdlibTypeHandler          stdlib_type_handler.hpp/cpp      Look up stdlib return types,
//                                                               arities, and parameter types.
//                                                               Handles type refinement for
//                                                               stdlib calls (e.g. Array.map).
//   SymbolExporter             symbol_exporter.hpp/cpp          Build the exported SymbolTable
//                                                               from internal registries for
//                                                               the LSP (hover, completions,
//                                                               go-to-definition).
//   TypeCheckingContext        type_checking_context.hpp        Per-pass transient state: the
//                                                               current scope, function return
//                                                               type, namespace, loop depth,
//                                                               resolve depth counter.
//   SymbolRegistry             symbol_registry.hpp              Record symbol locations during
//                                                               checking for LSP export.
//
// ── Responsibilities Retained by TypeChecker ─────────────────
//
// The following cross-cutting concerns remain in TypeChecker because
// they access multiple registries and are called by every sub-component:
//
//   Concern                      Methods                           Impl File
//   ──────────────────────────   ─────────────────────────────     ──────────────────────
//   Scope management             push_scope(), pop_scope(),        type_checker.cpp
//                                make_scope_guard()
//   Declaration passes           register_declarations(),          type_checker_decl.cpp
//                                check_declaration(),
//                                check_function()
//   Type resolution              resolve_type()                    type_checker_resolve.cpp
//   Type alias expansion         (within resolve_type)             type_checker_resolve.cpp
//   Assignability checking       is_assignable(),                  type_checker_resolve.cpp
//                                satisfies_interface(),
//                                satisfies_interface_interface()
//   Diagnostic emission          error(), warn(),                  type_checker.cpp
//                                type_mismatch_hint()
//   Name suggestions             suggest_type_name(),              type_checker.cpp
//                                suggest_variable_name()
//                                (delegates to suggest_name()
//                                in common/string_utils.hpp)
//   Flow-sensitive narrowing     ExpressionTypeChecker owns        expression_type_checker.hpp/cpp
//                                push_refinement(), pop_*(),
//                                find_refinement(),
//                                try_extract_is_refinement()
//   Symbol registries            records_, choices_,               type_checker.hpp (data)
//                                interfaces_, functions_,
//                                type_aliases_, namespace_functions_
//
// ── Why Type Resolution Is Not a Separate Class ──────────────
//
// resolve_type(), is_assignable(), and satisfies_interface() form a
// coherent "type resolution" responsibility and already live in their
// own file (type_checker_resolve.cpp).  However, they access ~10
// TypeChecker members (all symbol registries, generics_, ctx_,
// internal_members_, resolved_type_cache_, resolving_aliases_, and
// diagnostic methods).  Extracting them into a TypeResolver class
// would require forwarding references to all of these, yielding a
// class that is tightly coupled to TypeChecker with no real
// encapsulation benefit.  The file-level separation already provides
// the organisational clarity of a separate module.
//
// ── Future Extraction Candidates ─────────────────────────────
//
// If this class grows further, consider extracting:
//   - DiagnosticHelper: error(), warn(), type_mismatch_hint() —
//     only viable if diagnostics gain state beyond DiagnosticEmitter.

class TypeChecker : public DiagnosticEmitter, public TypeCheckingServices {
public:
    TypeChecker();

    // Non-copyable and non-movable: generics_ and the sub-checkers hold
    // back-references to *this, so copying or moving would leave them pointing
    // at the wrong (moved-from) TypeChecker.
    TypeChecker(const TypeChecker&) = delete;
    TypeChecker(TypeChecker&&) = delete;
    TypeChecker& operator=(const TypeChecker&) = delete;
    TypeChecker& operator=(TypeChecker&&) = delete;

    ~TypeChecker();

    // Analyse the program for type correctness.
    // Returns a list of type errors (empty if type-correct).
    // Set require_main = false when type-checking @test files that do not
    // need a @main entry point (e.g. luma --test).
    [[nodiscard]] std::vector<Diagnostic> check(const Program& program, bool require_main = true);

    // Returns warnings emitted during the last check() call.
    [[nodiscard]] const std::vector<Diagnostic>& get_warnings() const;

    // Returns the stdlib return-type registry (populated after check()).
    [[nodiscard]] const StringMap<TypeInfo>& stdlib_signatures() const;

    // Export the full resolved symbol table (populated after check()).
    // The LSP uses this to provide hover, completions, and go-to-definition
    // with accurate inferred types.
    [[nodiscard]] SymbolTable export_symbols();

private:
    // ====================================================================
    // State Reset                               (impl: type_checker.cpp)
    // ====================================================================

    void reset_state();

    // ====================================================================
    // Scope Management                          (impl: type_checker.cpp)
    // ====================================================================
    // Manages the lexical scope stack for variable and type bindings.
    // push_scope() / pop_scope() bracket every block that introduces a new
    // scope (functions, loops, if-branches).  pop_scope() also emits
    // unused-variable warnings.

    void push_scope() override;
    void pop_scope() override;

    // RAII scope guard for push_scope / pop_scope.
    [[nodiscard]] auto make_scope_guard() {
        push_scope();
        return ScopeGuard{[this] {
            pop_scope();
        }};
    }

    // Convenience accessor for the current scope.
    [[nodiscard]] std::shared_ptr<TypeScope>& current_scope() {
        return ctx_.current_scope;
    }

    // ====================================================================
    // Statement List Helper
    // ====================================================================

    // Checks a list of statements and warns about unreachable code
    // after return, break, or continue.
    void check_statement_list(const std::vector<StatementPtr>& stmts);

    // ====================================================================
    // Registration Pass                    (impl: type_checker_decl.cpp)
    // ====================================================================
    // First pass: walk top-level declarations and populate the symbol
    // registries (records_, choices_, interfaces_, functions_, type_aliases_,
    // namespace_functions_) so that forward references resolve correctly.

    void register_declarations(const std::vector<std::unique_ptr<Declaration>>& decls);
    void register_declaration(const Declaration& decl);
    void register_use_declaration(const UseDeclaration& use_decl);
    void register_namespace(const NamespaceDeclaration& ns, std::string_view prefix);

    // ── register_use_declaration import helpers ──────────────────────────
    // Resolve `func`'s parameter and return types and bind it as a function
    // value under `name` in the current scope.
    void define_function_in_scope(const FunctionDeclaration& func, const std::string& name);

    // Wildcard import (`use Namespace`): bind every non-internal, non-nested
    // member of `map` whose qualified name begins with `prefix` under its bare
    // name, and re-register it in `map` so resolve_type() finds the bare name.
    // `kind` selects the TypeInfo::Kind (Record or Choice) of the bound value.
    template <typename T>
    void import_all_namespace_types(StringMap<const T*>& map, TypeInfo::Kind kind,
                                    const std::string& prefix);

    // Specific import (`use Namespace.member`): if `qname` names an entry in
    // `map`, bind it under `bare` and re-register it under `bare`.  Returns
    // true when an entry was imported.
    template <typename T>
    [[nodiscard]] bool import_one_namespace_type(StringMap<const T*>& map, TypeInfo::Kind kind,
                                                 const std::string& qname, const std::string& bare);

    // Warn if a named declaration already exists in the given registry map.
    // Eliminates the repeated "is already declared — previous declaration will
    // be shadowed" pattern for Record, Choice, Interface, and Function branches.
    template <typename MapT>
    void warn_if_duplicate(const MapT& map, const std::string& name, std::string_view kind,
                           const SourceLocation& loc) {
        if (map.contains(name)) {
            warn(std::format("{} '{}' is already declared — previous declaration will be shadowed",
                             kind, name),
                 loc, std::format("rename one of the duplicate {}s", kind));
        }
    }

    // ====================================================================
    // Checking Pass                        (impl: type_checker_decl.cpp)
    // ====================================================================
    // Second pass: type-check each declaration body.  Delegates expression
    // checking to ExpressionTypeChecker and statement checking to
    // StatementTypeChecker.

    void check_declaration(const Declaration& decl);
    void check_function(const FunctionDeclaration& func);
    [[nodiscard]] bool definitely_returns(const std::vector<StatementPtr>& stmts) const;
    // NOTE: check_statement() delegates to StatementTypeChecker::check().
    void check_statement(const Statement& stmt) override;

    // ====================================================================
    // Match Exhaustiveness                  (impl: type_checker.cpp)
    // ====================================================================

    void check_match_exhaustiveness(const std::vector<MatchArm>& arms, const TypeInfo& subject_type,
                                    const SourceLocation& loc) override;

    // ====================================================================
    // Expression Type Inference     (delegates to ExpressionTypeChecker)
    // ====================================================================
    // NOTE: see expression_type_checker.hpp/cpp for all expression inference
    // logic.  These thin wrappers forward to expr_checker_.
    //
    // infer_expression_type() derives a type from the expression's AST
    // structure (literals, operators, calls, etc.).  This is the dual of
    // resolve_type() which translates a written type annotation.

    [[nodiscard]] TypeInfo infer_expression_type(const Expression& expr) override;
    [[nodiscard]] TypeInfo infer_assignment_target(const Expression& expr) override;
    [[nodiscard]] TypeInfo
    infer_block_result(const std::vector<std::unique_ptr<Statement>>& body) override;

    // ====================================================================
    // Type Resolution and Compatibility (impl: type_checker_resolve.cpp)
    // ====================================================================
    // Resolves TypeAnnotation AST nodes to TypeInfo values, expands type
    // aliases, and determines type assignability (including structural
    // interface satisfaction).  These methods are the most cross-cutting —
    // they access symbol registries, generics, context, and diagnostics.
    // They live in their own .cpp file for organisational clarity but
    // remain TypeChecker methods (see class comment for rationale).
    //
    // resolve_type() performs *static resolution* from a type annotation
    // (e.g. `integer`, `array<string>`, a record name).  It expands type
    // aliases and binds generic parameters but does not examine expressions.
    // Contrast with infer_expression_type() which *infers* a type from an
    // expression's structure.

    [[nodiscard]] TypeInfo resolve_type(const TypeAnnotation& ann) override;
    [[nodiscard]] bool is_assignable(const TypeInfo& target, const TypeInfo& source) override;

    // ─── resolve_type helpers ───────────────────────────────────────────
    // Structured sub-cases of resolve_type.  The depth guard and result
    // cache remain in resolve_type; resolve_alias_type returns nullopt when
    // the annotation does not name a type alias.
    [[nodiscard]] TypeInfo resolve_tuple_type(const TypeAnnotation& ann);
    [[nodiscard]] TypeInfo resolve_function_type(const TypeAnnotation& ann);
    [[nodiscard]] std::optional<TypeInfo> resolve_alias_type(const TypeAnnotation& ann);
    [[nodiscard]] TypeInfo resolve_named_type(const TypeAnnotation& ann, bool cacheable);

    // Resolve a user-defined named type (record, choice, or interface) held in
    // `map`: rejects internal access, builds a generic instantiation when type
    // arguments are present, otherwise a plain named type (cached when
    // eligible).  `kind` is the resulting TypeInfo::Kind.  Returns nullopt when
    // `ann` does not name an entry in `map`, so the caller can try the next
    // registry.
    template <typename T>
    [[nodiscard]] std::optional<TypeInfo>
    resolve_user_named(const StringMap<const T*>& map, TypeInfo::Kind kind,
                       const TypeAnnotation& ann, bool cacheable);

    // Emits a TypeError and returns true when `qualified_name` names a member
    // marked `internal` that is being accessed from outside its owning
    // namespace.  Shared by record, choice, interface, and type-alias
    // resolution so every namespace member kind enforces internal access
    // control consistently.
    [[nodiscard]] bool reject_internal_access(const std::string& qualified_name);

    // ─── is_assignable helpers ──────────────────────────────────────────
    // Returns std::nullopt when the kind is not a container handled here,
    // allowing the caller to fall through to other checks.
    [[nodiscard]] std::optional<bool> is_container_assignable(const TypeInfo& target,
                                                              const TypeInfo& source);
    [[nodiscard]] bool is_function_assignable(const TypeInfo& target, const TypeInfo& source);
    [[nodiscard]] bool is_tuple_assignable(const TypeInfo& target, const TypeInfo& source);
    // Choice type-name aliasing (qualified vs bare names referring to the same
    // declaration); nullopt when the two choices are unrelated.
    [[nodiscard]] std::optional<bool> is_choice_alias_assignable(const TypeInfo& target,
                                                                 const TypeInfo& source);
    // Element-wise assignability of two types' inner_types (generic args).
    [[nodiscard]] bool inner_types_assignable(const TypeInfo& target, const TypeInfo& source);

    [[nodiscard]] bool
    satisfies_interface(std::string_view record_name, std::string_view iface_name,
                        const std::vector<TypeInfo>& source_type_args = {},
                        const std::vector<TypeInfo>& target_type_args = {}) override;
    [[nodiscard]] bool
    satisfies_interface_interface(std::string_view source_iface_name,
                                  std::string_view target_iface_name,
                                  const std::vector<TypeInfo>& source_type_args = {},
                                  const std::vector<TypeInfo>& target_type_args = {}) override;

    [[nodiscard]] bool check_structural_satisfaction(const std::vector<TypeParam>& source_params,
                                                     const std::vector<TypeInfo>& source_type_args,
                                                     const std::vector<RecordField>& source_fields,
                                                     const std::vector<TypeParam>& target_params,
                                                     const std::vector<TypeInfo>& target_type_args,
                                                     const std::vector<RecordField>& target_fields,
                                                     std::string_view source_prefix,
                                                     std::string_view target_prefix);

    // ====================================================================
    // Diagnostic Helpers                        (impl: type_checker.cpp)
    // ====================================================================
    // Error and warning emission, plus "did you mean?" name suggestions.
    // The suggest_*() methods gather domain-specific candidate lists and
    // delegate fuzzy matching to suggest_name() in common/string_utils.hpp
    // (Levenshtein distance with adaptive threshold).

    void error(std::string_view message, const SourceLocation& loc, std::string_view hint = {},
               DiagnosticCode code = DiagnosticCode::None,
               std::optional<Fix> fix = std::nullopt) override;
    void warn(std::string_view message, const SourceLocation& loc, std::string_view hint = {},
              DiagnosticCode code = DiagnosticCode::None,
              std::optional<Fix> fix = std::nullopt) override;
    [[nodiscard]] static std::string type_mismatch_hint(const TypeInfo& expected,
                                                        const TypeInfo& actual);
    // Suggest a close type name for "did you mean?" hints.
    // Candidates: built-in types + registry symbols + active generic params.
    [[nodiscard]] std::string suggest_type_name(std::string_view unknown) const override;
    // Suggest a close variable or function name for "did you mean?" hints.
    // Candidates: all visible scope variables + registry symbols.
    [[nodiscard]] std::string suggest_variable_name(std::string_view unknown) const override;
    [[nodiscard]] bool is_stdlib_namespace(std::string_view name) const override;

    // ====================================================================
    // Per-Pass State                        (see type_checking_context.hpp)
    // ====================================================================

    // Transient state that changes during a single type-check pass.
    // Includes current scope, function return type, namespace, loop depth,
    // and type-resolve recursion depth counter.
    TypeCheckingContext ctx_;

    // ====================================================================
    // Symbol Registries                     (populated by register_declarations)
    // ====================================================================

    // Non-owning pointers to AST nodes owned by the Program.
    // LIFETIME INVARIANT: Valid only during a single type-check pass.
    // The Program must outlive the TypeChecker (guaranteed by Pipeline::run()).
    // These registries are read by resolve_type(), is_assignable(), and
    // the sub-checkers (ExpressionTypeChecker, StatementTypeChecker).
    StringMap<const RecordDeclaration*> records_;
    StringMap<const ChoiceDeclaration*> choices_;
    StringMap<const InterfaceDeclaration*> interfaces_;
    StringMap<TypeAnnotation> type_aliases_;
    StringMap<const FunctionDeclaration*> functions_;
    StringMap<StringMap<const FunctionDeclaration*>> namespace_functions_;

    // NOTE: see stdlib_type_handler.hpp/cpp for stdlib return types, arities,
    // and parameter type checking.
    StdlibTypeHandler stdlib_handler_;
    StringMap<TypeInfo> stdlib_signatures_cache_;

    std::vector<Diagnostic> warnings_;
    // Cycle-detection set for recursive type alias expansion in resolve_type().
    StringSet resolving_aliases_;

    // Cycle-detection set for structural interface satisfaction.  Mutually- or
    // self-recursive interfaces (e.g. `interface A { B x }` / `interface B { A x }`)
    // would otherwise recurse forever through is_assignable → satisfies_interface*
    // → check_structural_satisfaction → is_assignable and overflow the stack.  A
    // (source, target) pair already under evaluation is treated as satisfied —
    // the standard coinductive rule for structural subtyping of recursive types
    // — which terminates the recursion without ever wrongly rejecting.
    //
    // The key is the pair of type *names*, deliberately excluding generic type
    // arguments: the set of name pairs is finite, so recursion is bounded for
    // every input.  Keying on instantiated type arguments instead would restore
    // unbounded recursion (hence the original crash) for polymorphic recursion
    // such as `interface S<T> { S<array<T>> next }`, so the bounded name key is
    // preferred even though it can over-accept exotic heterogeneous-generic
    // recursive types.
    std::set<std::pair<std::string, std::string>> interface_satisfaction_in_progress_;

    // Tracks user-function names that were called (for unused-function detection).
    StringSet called_functions_;

    // ====================================================================
    // Composed Helpers and Sub-Checkers
    // ====================================================================
    // These hold back-references to this TypeChecker and are accessed via
    // the TypeCheckingServices interface.  See the class comment above for
    // the full delegation architecture.

    // NOTE: see generic_resolver.cpp for generic type parameter inference.
    GenericResolver generics_{*this};
    SymbolExporter symbol_exporter_;
    // NOTE: see symbol_registry.hpp for symbol recording during checking.
    SymbolRegistry registry_;

    // Qualified names (e.g. "Geometry.helper") that are declared 'internal'.
    // External callers are not allowed to access these.
    StringSet internal_members_;

    // ====================================================================
    // Caches                          (used by type_checker_resolve.cpp)
    // ====================================================================

    // Caches resolved TypeInfo for simple type annotations (no type params,
    // not tuple/func) when no generic bindings are active.  Avoids repeated
    // string comparisons and hash lookups for the same annotation name.
    StringMap<TypeInfo> resolved_type_cache_;

    // ====================================================================
    // TypeCheckingServices Overrides
    // ====================================================================
    // New methods required by the TypeCheckingServices interface that
    // expose internal state through the abstract service contract.

    [[nodiscard]] TypeCheckingContext& context() override {
        return ctx_;
    }

    [[nodiscard]] const TypeCheckingContext& context() const override {
        return ctx_;
    }

    [[nodiscard]] GenericResolver& generics() override {
        return generics_;
    }

    [[nodiscard]] StdlibTypeHandler& stdlib_handler() override {
        return stdlib_handler_;
    }

    [[nodiscard]] const StdlibTypeHandler& stdlib_handler() const override {
        return stdlib_handler_;
    }

    [[nodiscard]] const RecordDeclaration* find_record(std::string_view name) const override {
        const auto it = records_.find(name);
        return it != records_.end() ? it->second : nullptr;
    }

    [[nodiscard]] const ChoiceDeclaration* find_choice(std::string_view name) const override {
        const auto it = choices_.find(name);
        return it != choices_.end() ? it->second : nullptr;
    }

    [[nodiscard]] const InterfaceDeclaration* find_interface(std::string_view name) const override {
        const auto it = interfaces_.find(name);
        return it != interfaces_.end() ? it->second : nullptr;
    }

    [[nodiscard]] const FunctionDeclaration* find_function(std::string_view name) const override {
        const auto it = functions_.find(name);
        return it != functions_.end() ? it->second : nullptr;
    }

    [[nodiscard]] const StringMap<const RecordDeclaration*>& records() const override {
        return records_;
    }

    [[nodiscard]] const StringMap<const ChoiceDeclaration*>& choices() const override {
        return choices_;
    }

    [[nodiscard]] const StringMap<const InterfaceDeclaration*>& interfaces() const override {
        return interfaces_;
    }

    [[nodiscard]] const StringMap<const FunctionDeclaration*>& functions() const override {
        return functions_;
    }

    [[nodiscard]] const StringMap<StringMap<const FunctionDeclaration*>>&
    namespace_functions() const override {
        return namespace_functions_;
    }

    [[nodiscard]] bool is_internal_member(std::string_view name) const override {
        return internal_members_.contains(name);
    }

    void mark_function_called(std::string_view name) override {
        called_functions_.emplace(name);
    }

    // ====================================================================
    // Flow-Sensitive Type Refinements     (forwarded to ExpressionTypeChecker)
    // ====================================================================

    void push_refinement(const std::string& var, TypeInfo narrowed) override;
    void pop_refinements(std::size_t mark) override;
    [[nodiscard]] std::size_t refinement_mark() const override;
    [[nodiscard]] bool try_extract_is_refinement(const Expression& condition, std::string& var_name,
                                                 TypeInfo& narrowed_type) override;

    // ====================================================================
    // Match Exhaustiveness Query        (forwarded to MatchExhaustivenessChecker)
    // ====================================================================

    [[nodiscard]] bool is_match_exhaustive(const MatchStatement& match_stmt) const override;

    // ====================================================================
    // Sub-Checkers                    (expression and statement type checking)
    // ====================================================================
    // NOTE: see expression_type_checker.hpp/cpp for all expression type
    // inference (literals, calls, binary ops, member access, closures, etc.).
    // NOTE: see statement_type_checker.hpp/cpp for all statement checking
    // (let, assign, if, while, for, return, yield, etc.).
    // NOTE: see match_exhaustiveness.hpp/cpp for match pattern coverage
    // analysis (boolean, choice, result, optional exhaustiveness).

    std::unique_ptr<ExpressionTypeChecker> expr_checker_;
    std::unique_ptr<StatementTypeChecker> stmt_checker_;
    std::unique_ptr<MatchExhaustivenessChecker> exhaustiveness_checker_;
};

} // namespace luma
