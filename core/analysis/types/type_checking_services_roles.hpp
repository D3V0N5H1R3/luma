// ─────────────────────────────────────────────────────────────────────────────
// Type Checking Service Roles
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Split the cross-cutting TypeCheckingServices interface into
//   focused, single-responsibility role interfaces (Interface Segregation
//   Principle).
//
// Background: Sub-checkers (ExpressionTypeChecker, StatementTypeChecker,
//   GenericResolver, MatchExhaustiveness, SymbolExporter) historically depend
//   on the whole TypeCheckingServices surface via a back-reference, even
//   though each uses only a slice of it.  These role interfaces capture the
//   documented concern groups so that:
//
//     1. Each responsibility is independently documented and mockable.
//     2. A sub-checker can be refactored to depend on only the role(s) it
//        actually consumes, narrowing its coupling surface.
//
// TypeCheckingServices (type_checking_context.hpp) inherits from all of these,
//   so existing code that depends on the aggregate is unaffected.  TypeChecker
//   remains the sole production implementation.
//
// Each interface mirrors one "── …" section of the original
//   TypeCheckingServices definition.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/types/type_info.hpp"
#include "common/string_hash.hpp"

namespace luma {

// Forward declarations — avoid pulling in full AST headers.
struct Expression;
struct Statement;
struct TypeAnnotation;
struct FunctionDeclaration;
struct RecordDeclaration;
struct ChoiceDeclaration;
struct InterfaceDeclaration;
struct MatchArm;
struct MatchStatement;
struct TypeCheckingContext;
class GenericResolver;
class StdlibTypeHandler;

// ── Per-pass context ────────────────────────────────────────────────────────
// Access the transient state bundle that tracks scope, function context,
// nesting depths, and namespace during a single check() call.
class IPassContextService {
public:
    virtual ~IPassContextService() = default;

    [[nodiscard]] virtual TypeCheckingContext& context() = 0;
    [[nodiscard]] virtual const TypeCheckingContext& context() const = 0;

protected:
    IPassContextService() = default;
    IPassContextService(const IPassContextService&) = default;
    IPassContextService& operator=(const IPassContextService&) = default;
    IPassContextService(IPassContextService&&) = default;
    IPassContextService& operator=(IPassContextService&&) = default;
};

// ── Diagnostics ───────────────────────────────────────────────────────────────
// Report type errors and warnings with optional source locations, hints,
// diagnostic codes, and attached fix suggestions; offer "did you mean?"
// name suggestions.
class IDiagnosticsService {
public:
    virtual ~IDiagnosticsService() = default;

    virtual void error(std::string_view message, const SourceLocation& loc,
                       std::string_view hint = {}, DiagnosticCode code = DiagnosticCode::None,
                       std::optional<Fix> fix = std::nullopt) = 0;

    virtual void warn(std::string_view message, const SourceLocation& loc,
                      std::string_view hint = {}, DiagnosticCode code = DiagnosticCode::None,
                      std::optional<Fix> fix = std::nullopt) = 0;

    [[nodiscard]] virtual std::string suggest_type_name(std::string_view unknown) const = 0;
    [[nodiscard]] virtual std::string suggest_variable_name(std::string_view unknown) const = 0;

protected:
    IDiagnosticsService() = default;
    IDiagnosticsService(const IDiagnosticsService&) = default;
    IDiagnosticsService& operator=(const IDiagnosticsService&) = default;
    IDiagnosticsService(IDiagnosticsService&&) = default;
    IDiagnosticsService& operator=(IDiagnosticsService&&) = default;
};

// ── Scope management ──────────────────────────────────────────────────────────
// Push and pop lexical scopes.  pop_scope() also emits warnings for unused
// variables in the scope being closed.
class IScopeService {
public:
    virtual ~IScopeService() = default;

    virtual void push_scope() = 0;
    virtual void pop_scope() = 0;

protected:
    IScopeService() = default;
    IScopeService(const IScopeService&) = default;
    IScopeService& operator=(const IScopeService&) = default;
    IScopeService(IScopeService&&) = default;
    IScopeService& operator=(IScopeService&&) = default;
};

// ── Type resolution and compatibility ────────────────────────────────────────
// Resolve type annotations to TypeInfo, check assignability, and verify
// structural interface satisfaction.
class ITypeResolutionService {
public:
    virtual ~ITypeResolutionService() = default;

    [[nodiscard]] virtual TypeInfo resolve_type(const TypeAnnotation& ann) = 0;
    [[nodiscard]] virtual bool is_assignable(const TypeInfo& target, const TypeInfo& source) = 0;
    [[nodiscard]] virtual bool
    satisfies_interface(std::string_view record_name, std::string_view iface_name,
                        const std::vector<TypeInfo>& source_type_args = {},
                        const std::vector<TypeInfo>& target_type_args = {}) = 0;
    [[nodiscard]] virtual bool
    satisfies_interface_interface(std::string_view source_iface_name,
                                  std::string_view target_iface_name,
                                  const std::vector<TypeInfo>& source_type_args = {},
                                  const std::vector<TypeInfo>& target_type_args = {}) = 0;

protected:
    ITypeResolutionService() = default;
    ITypeResolutionService(const ITypeResolutionService&) = default;
    ITypeResolutionService& operator=(const ITypeResolutionService&) = default;
    ITypeResolutionService(ITypeResolutionService&&) = default;
    ITypeResolutionService& operator=(ITypeResolutionService&&) = default;
};

// ── Expression type inference ─────────────────────────────────────────────────
// Infer the type of an expression, an assignment target, or the result of a
// block (the last expression in a block body).
class IExpressionInferenceService {
public:
    virtual ~IExpressionInferenceService() = default;

    [[nodiscard]] virtual TypeInfo infer_expression_type(const Expression& expr) = 0;
    [[nodiscard]] virtual TypeInfo infer_assignment_target(const Expression& expr) = 0;
    [[nodiscard]] virtual TypeInfo
    infer_block_result(const std::vector<std::unique_ptr<Statement>>& body) = 0;

protected:
    IExpressionInferenceService() = default;
    IExpressionInferenceService(const IExpressionInferenceService&) = default;
    IExpressionInferenceService& operator=(const IExpressionInferenceService&) = default;
    IExpressionInferenceService(IExpressionInferenceService&&) = default;
    IExpressionInferenceService& operator=(IExpressionInferenceService&&) = default;
};

// ── Statement checking ────────────────────────────────────────────────────────
// Check a single statement for type correctness.
class IStatementCheckingService {
public:
    virtual ~IStatementCheckingService() = default;

    virtual void check_statement(const Statement& stmt) = 0;

protected:
    IStatementCheckingService() = default;
    IStatementCheckingService(const IStatementCheckingService&) = default;
    IStatementCheckingService& operator=(const IStatementCheckingService&) = default;
    IStatementCheckingService(IStatementCheckingService&&) = default;
    IStatementCheckingService& operator=(IStatementCheckingService&&) = default;
};

// ── Symbol registry ───────────────────────────────────────────────────────────
// Query the declaration registries populated during the registration pass
// (find-by-name and registry iteration), and track call/internal status.
// The lookup_variable() convenience wrappers remain on the aggregate
// TypeCheckingServices because they compose context() with these registries.
class ISymbolRegistryService {
public:
    virtual ~ISymbolRegistryService() = default;

    [[nodiscard]] virtual const RecordDeclaration* find_record(std::string_view name) const = 0;
    [[nodiscard]] virtual const ChoiceDeclaration* find_choice(std::string_view name) const = 0;
    [[nodiscard]] virtual const InterfaceDeclaration*
    find_interface(std::string_view name) const = 0;
    [[nodiscard]] virtual const FunctionDeclaration* find_function(std::string_view name) const = 0;

    [[nodiscard]] virtual bool is_stdlib_namespace(std::string_view name) const = 0;
    [[nodiscard]] virtual bool is_internal_member(std::string_view name) const = 0;
    virtual void mark_function_called(std::string_view name) = 0;

    [[nodiscard]] virtual const StringMap<const RecordDeclaration*>& records() const = 0;
    [[nodiscard]] virtual const StringMap<const ChoiceDeclaration*>& choices() const = 0;
    [[nodiscard]] virtual const StringMap<const InterfaceDeclaration*>& interfaces() const = 0;
    [[nodiscard]] virtual const StringMap<const FunctionDeclaration*>& functions() const = 0;
    [[nodiscard]] virtual const StringMap<StringMap<const FunctionDeclaration*>>&
    namespace_functions() const = 0;

protected:
    ISymbolRegistryService() = default;
    ISymbolRegistryService(const ISymbolRegistryService&) = default;
    ISymbolRegistryService& operator=(const ISymbolRegistryService&) = default;
    ISymbolRegistryService(ISymbolRegistryService&&) = default;
    ISymbolRegistryService& operator=(ISymbolRegistryService&&) = default;
};

// ── Match exhaustiveness ──────────────────────────────────────────────────────
class IMatchAnalysisService {
public:
    virtual ~IMatchAnalysisService() = default;

    virtual void check_match_exhaustiveness(const std::vector<MatchArm>& arms,
                                            const TypeInfo& subject_type,
                                            const SourceLocation& loc) = 0;

    [[nodiscard]] virtual bool is_match_exhaustive(const MatchStatement& match_stmt) const = 0;

protected:
    IMatchAnalysisService() = default;
    IMatchAnalysisService(const IMatchAnalysisService&) = default;
    IMatchAnalysisService& operator=(const IMatchAnalysisService&) = default;
    IMatchAnalysisService(IMatchAnalysisService&&) = default;
    IMatchAnalysisService& operator=(IMatchAnalysisService&&) = default;
};

// ── Generic inference ─────────────────────────────────────────────────────────
// Access the generic type parameter resolver for binding, inferring, and
// validating type parameter bindings during type checking.
class IGenericsService {
public:
    virtual ~IGenericsService() = default;

    [[nodiscard]] virtual GenericResolver& generics() = 0;

protected:
    IGenericsService() = default;
    IGenericsService(const IGenericsService&) = default;
    IGenericsService& operator=(const IGenericsService&) = default;
    IGenericsService(IGenericsService&&) = default;
    IGenericsService& operator=(IGenericsService&&) = default;
};

// ── Standard library ──────────────────────────────────────────────────────────
// Access the stdlib type metadata handler for return types, arities,
// parameter types, and return-type refinement.
class IStdlibTypeService {
public:
    virtual ~IStdlibTypeService() = default;

    [[nodiscard]] virtual StdlibTypeHandler& stdlib_handler() = 0;
    [[nodiscard]] virtual const StdlibTypeHandler& stdlib_handler() const = 0;

protected:
    IStdlibTypeService() = default;
    IStdlibTypeService(const IStdlibTypeService&) = default;
    IStdlibTypeService& operator=(const IStdlibTypeService&) = default;
    IStdlibTypeService(IStdlibTypeService&&) = default;
    IStdlibTypeService& operator=(IStdlibTypeService&&) = default;
};

// ── Flow-sensitive type refinements ──────────────────────────────────────────
// Push and pop type refinements for is<T> narrowing in if-branches.
// Refinements are owned by ExpressionTypeChecker; these methods forward to it
// so StatementTypeChecker can drive narrowing without depending on the
// concrete ExpressionTypeChecker type.
class IRefinementService {
public:
    virtual ~IRefinementService() = default;

    virtual void push_refinement(const std::string& var, TypeInfo narrowed) = 0;
    virtual void pop_refinements(std::size_t mark) = 0;
    [[nodiscard]] virtual std::size_t refinement_mark() const = 0;
    [[nodiscard]] virtual bool try_extract_is_refinement(const Expression& condition,
                                                         std::string& var_name,
                                                         TypeInfo& narrowed_type) = 0;

protected:
    IRefinementService() = default;
    IRefinementService(const IRefinementService&) = default;
    IRefinementService& operator=(const IRefinementService&) = default;
    IRefinementService(IRefinementService&&) = default;
    IRefinementService& operator=(IRefinementService&&) = default;
};

} // namespace luma
