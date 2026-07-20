#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/ast/ast_dispatcher.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/types/type_info.hpp"
#include "analysis/types/type_refinement_stack.hpp"

namespace luma {

class TypeCheckingServices;

// ─────────────────────────────────────────────────────────────────────────────
// ExpressionTypeChecker — infers types for all expression AST nodes.
//
// ─── Error Reporting Convention ─────────────────────────────────────────
//
// All expression type-checking files (expression_type_checker_*.cpp) emit
// diagnostics through tc_.error() / tc_.warn(), using the
// TypeCheckingServices interface.  Two patterns coexist:
//
//   1. tc_.error(std::format(...), loc, hint)
//      Inline formatting — used for one-off messages where a dedicated
//      builder would add complexity without reuse benefit.
//
//   2. auto diag = diag_builders::type_mismatch(...);
//      tc_.error(diag.message, loc, diag.hint);
//      Builder helpers from <analysis/diagnostics/diagnostic_builders.hpp>
//      — preferred for recurring patterns (type mismatch, arity mismatch,
//      field mismatch, undefined symbol) to ensure consistent wording.
//
// Preferred convention: use diag_builders:: helpers when one exists for the
// error pattern; fall back to inline std::format() for unique messages.
// Always emit through tc_.error()/tc_.warn() — never construct Diagnostic
// objects directly in expression type checker code.
//
// ─── Coupling to TypeCheckingServices ────────────────────────────────────
//
// This class holds a mutable reference to TypeCheckingServices (tc_), the
// abstract interface defined in type_checking_context.hpp.  It does NOT
// depend on the concrete TypeChecker class directly.  The services consumed
// through tc_ fall into these categories:
//
//   - Diagnostic emission:  tc_.error(), tc_.warn()
//   - Type resolution:      tc_.resolve_type(), tc_.is_assignable(),
//                            tc_.satisfies_interface()
//   - Scope management:     tc_.push_scope(), tc_.pop_scope(),
//                            tc_.make_scope_guard()
//   - Symbol lookups:       tc_.find_record(), tc_.find_choice(),
//                            tc_.find_function(), tc_.find_interface(),
//                            tc_.is_stdlib_namespace(), tc_.functions(),
//                            tc_.namespace_functions()
//   - Sub-component access: tc_.generics(), tc_.stdlib_handler()
//   - Per-pass context:     tc_.context() (loop depth, pipe state, etc.)
//   - Statement delegation: tc_.check_statement() (for block results)
//
// The TypeCheckingServices interface already decouples ExpressionTypeChecker
// from the concrete TypeChecker implementation.  No further interface
// extraction is needed — the existing design supports stub-based testing
// and keeps the dependency surface explicit and auditable.
// ─────────────────────────────────────────────────────────────────────────────

class ExpressionTypeChecker : public ExpressionDispatcher<ExpressionTypeChecker, TypeInfo> {
public:
    // Mutable: expression type checking may emit diagnostics and register
    // symbols via the parent checker.
    explicit ExpressionTypeChecker(TypeCheckingServices& tc);

    [[nodiscard]] TypeInfo infer_expression_type(const Expression& expr);
    void push_refinement(const std::string& var, TypeInfo narrowed);
    void pop_refinements(std::size_t mark);
    [[nodiscard]] std::size_t refinement_mark() const;
    [[nodiscard]] const TypeInfo* find_refinement(const std::string& var) const;
    [[nodiscard]] bool try_extract_is_refinement(const Expression& condition, std::string& var_name,
                                                 TypeInfo& narrowed_type);
    [[nodiscard]] TypeInfo infer_assignment_target(const Expression& expr);
    [[nodiscard]] TypeInfo infer_block_result(const std::vector<std::unique_ptr<Statement>>& body);
    [[nodiscard]] static std::string type_mismatch_hint(const TypeInfo& expected,
                                                        const TypeInfo& actual);

    // ─── Expression dispatch handlers (called by ExpressionDispatcher) ───

    [[nodiscard]] TypeInfo visit_literal(const LiteralExpression& expr);
    [[nodiscard]] TypeInfo visit_variable(const VariableExpression& expr);
    [[nodiscard]] TypeInfo visit_binary(const BinaryExpression& expr);
    [[nodiscard]] TypeInfo visit_unary(const UnaryExpression& expr);
    [[nodiscard]] TypeInfo visit_call(const CallExpression& expr);
    [[nodiscard]] TypeInfo visit_field_access(const FieldAccessExpression& expr);
    [[nodiscard]] TypeInfo visit_index_access(const IndexAccessExpression& expr);
    [[nodiscard]] TypeInfo visit_lambda(const LambdaExpression& expr);
    [[nodiscard]] TypeInfo visit_if(const IfExpression& expr);
    [[nodiscard]] TypeInfo visit_match(const MatchExpression& expr);
    [[nodiscard]] TypeInfo visit_pipe(const PipeExpression& expr);
    [[nodiscard]] TypeInfo visit_error_pipe(const ErrorPipeExpression& expr);
    [[nodiscard]] TypeInfo visit_record_creation(const RecordCreationExpression& expr);
    [[nodiscard]] TypeInfo visit_record_with(const RecordWithExpression& expr);
    [[nodiscard]] TypeInfo visit_array_literal(const ArrayLiteralExpression& expr);
    [[nodiscard]] TypeInfo visit_dictionary_literal(const DictionaryLiteralExpression& expr);
    [[nodiscard]] TypeInfo visit_tuple_literal(const TupleLiteralExpression& expr);
    [[nodiscard]] TypeInfo visit_string_interpolation(const StringInterpolationExpression& expr);
    [[nodiscard]] TypeInfo visit_downcast(const DowncastExpression& expr);
    [[nodiscard]] TypeInfo visit_is(const IsExpression& expr);
    [[nodiscard]] TypeInfo visit_spawn(const SpawnExpression& expr);
    [[nodiscard]] TypeInfo visit_task_scope(const TaskScopeExpression& expr);
    [[nodiscard]] TypeInfo visit_await(const AwaitExpression& expr);
    [[nodiscard]] TypeInfo visit_success(const SuccessExpression& expr);
    [[nodiscard]] TypeInfo visit_some(const SomeExpression& expr);
    [[nodiscard]] TypeInfo visit_failure(const FailureExpression& expr);
    [[nodiscard]] TypeInfo visit_range(const RangeExpression& expr);
    [[nodiscard]] TypeInfo visit_expression_unhandled(const Expression& expr);

private:
    // Internal helper for field access without optional wrapping.
    // Used by infer_assignment_target which needs the raw type.
    [[nodiscard]] TypeInfo infer_field_access_inner(const FieldAccessExpression& expr);

    // Per-receiver field-access helpers used by infer_field_access_inner.
    // Each returns nullopt when the receiver does not match, so the
    // orchestrator can fall through to the StdlibAny default.
    [[nodiscard]] std::optional<TypeInfo> infer_namespace_member(const FieldAccessExpression& expr);
    [[nodiscard]] TypeInfo infer_choice_member(const FieldAccessExpression& expr,
                                               const TypeInfo& object_type);
    [[nodiscard]] std::optional<TypeInfo> infer_interface_member(const FieldAccessExpression& expr,
                                                                 const TypeInfo& object_type);
    [[nodiscard]] std::optional<TypeInfo> infer_optional_member(const FieldAccessExpression& expr,
                                                                const TypeInfo& object_type);
    [[nodiscard]] std::optional<TypeInfo> infer_tuple_member(const FieldAccessExpression& expr,
                                                             const TypeInfo& object_type);
    [[nodiscard]] std::optional<TypeInfo> infer_record_member(const FieldAccessExpression& expr,
                                                              const TypeInfo& object_type);

    // Resolve ChoiceName.Variant to its type: the choice type itself for unit
    // variants, or a constructor Func for data variants.  Owns the choice
    // lookup (via find_choice), the RAII generic-param scope, and the
    // unit-vs-data type build.  Returns nullopt when choice_name does not name
    // a choice; on an unknown variant it reports an error and returns the
    // choice type for recovery.
    [[nodiscard]] std::optional<TypeInfo> resolve_choice_variant(std::string_view choice_name,
                                                                 std::string_view variant_name,
                                                                 const SourceLocation& loc);

    // ─── visit_match helper ─────────────────────────────────────────────
    // Infer the result type of a match's arms.  Shared by visit_match (match
    // expression) and infer_block_result (a match statement in value position).
    [[nodiscard]] TypeInfo infer_match_result(const Expression& subject,
                                              const std::vector<MatchArm>& arms,
                                              const SourceLocation& location);

    // ─── visit_record_creation helpers ──────────────────────────────────
    // Reject construction of a namespace-internal record from outside its
    // namespace.  Returns true (after emitting a diagnostic) when access is
    // blocked; the caller should abandon inference.
    [[nodiscard]] bool check_internal_record_access(const RecordCreationExpression& expr);

    // Verify every declared field is provided (unless it has a default) and that
    // each provided initializer's type matches the field's declared type.
    void check_record_provided_fields(const RecordDeclaration& record,
                                      const RecordCreationExpression& expr);

    // Report initializer fields the record does not declare, suggesting the
    // closest declared field name.
    void check_record_unknown_fields(const RecordDeclaration& record,
                                     const RecordCreationExpression& expr);

    // ─── visit_if / if-statement-in-value-position helpers ──────────────
    // Shared conditional inference: type-checks the condition, computes each
    // branch type via the supplied callables (under their own scope, with
    // flow-sensitive narrowing on the then-branch), merges branch ownership,
    // and unifies the two branch types.
    [[nodiscard]] TypeInfo infer_conditional_result(const Expression& condition,
                                                    const std::function<TypeInfo()>& compute_then,
                                                    const std::function<TypeInfo()>& compute_else,
                                                    const SourceLocation& location);

    // Unify two if/conditional branch types into the expression's result type.
    [[nodiscard]] TypeInfo merge_if_branch_types(const TypeInfo& then_type,
                                                 const TypeInfo& else_type,
                                                 const SourceLocation& location);

    // Infer the result type of an if *statement* used in value position (e.g.
    // the tail of a match arm or if-expression branch).
    [[nodiscard]] TypeInfo infer_if_statement_result(const IfStatement& stmt);

    // ─── visit_binary helpers ───────────────────────────────────────────

    // Constant-folding diagnostics (division by zero, overflow, shift range,
    // string repeat limits).  Called before dispatch.
    void check_binary_constant_folding(const BinaryExpression& expr, const TypeInfo& left_type);

    // Per-operator-family type checking.
    [[nodiscard]] TypeInfo check_arithmetic_binary(const BinaryExpression& expr,
                                                   const TypeInfo& left, const TypeInfo& right);
    [[nodiscard]] TypeInfo check_bitwise_binary(const BinaryExpression& expr, const TypeInfo& left,
                                                const TypeInfo& right);
    [[nodiscard]] TypeInfo check_equality_binary(const BinaryExpression& expr, const TypeInfo& left,
                                                 const TypeInfo& right);
    [[nodiscard]] TypeInfo check_comparison_binary(const BinaryExpression& expr,
                                                   const TypeInfo& left, const TypeInfo& right);
    [[nodiscard]] TypeInfo check_logical_binary(const BinaryExpression& expr, const TypeInfo& left,
                                                const TypeInfo& right);
    [[nodiscard]] TypeInfo check_null_coalescing_binary(const BinaryExpression& expr,
                                                        const TypeInfo& left,
                                                        const TypeInfo& right);
    [[nodiscard]] TypeInfo check_containment_binary(const BinaryExpression& expr,
                                                    const TypeInfo& left, const TypeInfo& right);

    // ─── visit_call helpers ─────────────────────────────────────────────

    // Resolve the callee expression to a qualified function name (e.g.
    // "foo" or "Module.bar").  Returns an empty string when the callee
    // is not a simple variable or field-access on a variable.
    [[nodiscard]] std::string resolve_callee_name(const CallExpression& expr,
                                                  const TypeInfo& callee_type) const;

    // Validate that the number of (positional + named + piped) arguments
    // matches the function signature.  Emits a diagnostic on mismatch.
    void check_call_arity(const CallExpression& expr, const TypeInfo& callee_type,
                          const std::string& fn_name);

    // Handle construction of a generic choice variant: infer type params
    // from call arguments and return the specialised choice type.
    // Returns std::nullopt when the callee is not a generic choice
    // constructor, allowing visit_call to fall through.
    [[nodiscard]] std::optional<TypeInfo>
    infer_generic_choice_call(const CallExpression& expr, const TypeInfo& callee_type,
                              const std::vector<TypeInfo>& arg_types);

    // ─── visit_call helpers (argument / ownership / stdlib) ───────────

    // Type-check positional and named arguments against a user-defined
    // function's declared parameter types.
    void
    check_user_function_args(const CallExpression& expr, const TypeInfo& callee_type,
                             const std::string& fn_name, const std::vector<TypeInfo>& arg_types,
                             const std::vector<std::pair<std::string, TypeInfo>>& named_arg_types);

    // Check ownership compatibility (unique/borrow) at the call site.
    void check_call_ownership(const CallExpression& expr, const std::string& fn_name);

    // Type- and ownership-check the value piped into a function's first
    // parameter.  Shared by the variable-callee and namespace-callee pipe paths
    // so both report the same argument-type-mismatch diagnostic and apply the
    // same borrow/unique ownership rules.  `fn_name` names the callee (e.g.
    // "f" or "Module.f") for the ownership diagnostic.
    void check_pipe_first_parameter(const Parameter& first_param, std::string_view fn_name,
                                    const TypeInfo& left_type, const Expression& piped_value,
                                    const SourceLocation& loc);

    // Handle calls on stdlib namespace functions: arity, param types,
    // and return type resolution.  Returns the resolved return type,
    // or std::nullopt if the callee is not a stdlib namespace call.
    [[nodiscard]] std::optional<TypeInfo>
    check_stdlib_function_call(const CallExpression& expr, const std::vector<TypeInfo>& arg_types);

    // Convenience wrapper: build and emit a Type-category error diagnostic
    // via tc_.error(), which delegates to DiagnosticEmitter::emit_error().
    void emit_err(std::string_view message, const SourceLocation& loc, std::string_view hint = {},
                  DiagnosticCode code = DiagnosticCode::None);

    // ─── visit_index_access helpers ────────────────────────────────────

    [[nodiscard]] TypeInfo check_array_index(const IndexAccessExpression& expr,
                                             const TypeInfo& object_type,
                                             const TypeInfo& index_type);
    [[nodiscard]] TypeInfo check_dict_index(const IndexAccessExpression& expr,
                                            const TypeInfo& object_type,
                                            const TypeInfo& index_type);
    [[nodiscard]] TypeInfo check_string_index(const IndexAccessExpression& expr,
                                              const TypeInfo& index_type);
    [[nodiscard]] TypeInfo check_tuple_index(const IndexAccessExpression& expr,
                                             const TypeInfo& object_type);
    [[nodiscard]] TypeInfo check_optional_index(const TypeInfo& object_type);

    TypeCheckingServices& tc_;

    // Recursion-depth counter for infer_expression_type().  Guards against
    // native stack overflow on pathologically deep expression ASTs (e.g. a
    // very long flat `a + b + c + ...` chain that the parser builds without
    // tripping its own max_parse_depth guard).  See ResourceLimits::
    // max_expression_depth.
    int expression_depth_{0};

    // Flow-sensitive type refinements (e.g. is<T> narrowing in if-branches).
    // Owned here because all push/pop/find operations are driven from this
    // class; other components (e.g. StatementTypeChecker) reach them through
    // the TypeCheckingServices interface (tc_.push_refinement(), etc.), which
    // the concrete TypeChecker forwards to this ExpressionTypeChecker.  The
    // stack bookkeeping itself lives in TypeRefinementStack (SRP extraction).
    TypeRefinementStack refinements_;
};

} // namespace luma
