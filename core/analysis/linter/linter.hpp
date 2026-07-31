#ifndef LUMA_LINTER_LINTER_HPP
#define LUMA_LINTER_LINTER_HPP

#include <memory>
#include <string_view>
#include <vector>

#include "analysis/ast/ast_dispatcher.hpp"
#include "analysis/common/scope_manager.hpp"
#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_emitter.hpp"
#include "analysis/linter/lint_plugin.hpp"
#include "analysis/linter/lint_rule.hpp"
#include "analysis/linter/linter_tracker.hpp"
#include "analysis/source/source_location.hpp"

namespace luma {

struct MatchArm;
struct Program;

// Lint pass — checks for code quality issues after type checking.
// Runs independently from the type checker and produces warnings only.
//
// Checks performed:
// - Unused variables
// - Unused parameters
// - Unused functions
// - Unused includes
// - Mutable variables that are never mutated
// - Self-assignment (x = x)
// - Variable shadowing
// - Unreachable code after return/break/continue
// - Discarded result values
// - Always-false / always-true conditions
// - Incompatible comparisons
// - Floating-point equality
// - Empty blocks
// - Redundant else after return
// - Empty catch blocks (swallowed exceptions)
// - Division by literal zero
//
// ─── Architecture ────────────────────────────────────────────────────────
//
// The Linter's implementation is split across several .cpp files:
//
//   linter.cpp               — public API, AST dispatch, scope lifecycle,
//                               diagnostics, and unused-symbol reporting
//   linter_variables.cpp     — declaration and assignment visit handlers
//   linter_control_flow.cpp  — control flow statement visit handlers
//   linter_expressions.cpp   — expression visit handlers and check helpers
//
// Variable and include usage tracking is delegated to LinterTracker
// (linter_tracker.hpp/cpp), which owns the scope stack and include list
// but does not emit diagnostics.  The Linter queries the tracker for
// unused symbols and handles warning emission itself.
//
// ─── Traversal and Emission Flow ─────────────────────────────────────────
//
// The lint pass combines AST traversal with diagnostic emission in a
// single class rather than separating them into distinct phases.  This
// is intentional: each visit_* handler inspects a single AST node,
// optionally emits a warning via warn(), then recurses into child nodes.
// Splitting traversal from emission would require either (a) an
// intermediate list of "lint findings" that is post-processed into
// diagnostics, or (b) a separate visitor that re-walks the AST to
// collect results — both would add complexity without benefit since
// every finding maps 1:1 to a diagnostic.
//
// The overall flow is:
//
//   lint(program)
//     → for each declaration: lint_declaration(decl)
//       → dispatch_decl(decl)              [DeclarationDispatcher CRTP]
//         → visit_function / visit_namespace / visit_include
//           → lint_block(body)
//             → for each statement: lint_statement(stmt)
//               → dispatch_stmt(stmt)      [StatementDispatcher CRTP]
//                 → visit_assignment / visit_for / visit_if_statement / …
//                   → lint_expression(expr)
//                     → dispatch_expr(expr) [ExpressionDispatcher CRTP]
//                       → visit_binary / visit_call / visit_pipe / …
//                         → warn() → emit_warning() [DiagnosticEmitter]
//     → report_unused_in_current_scope()   [end-of-scope unused checks]
//     → report_unused_includes()
//
// DiagnosticEmitter (the fourth base class) provides emit_warning() and
// manages the accumulated diagnostic list.  The Linter's warn() helper
// is a thin wrapper that checks whether the relevant lint rule is
// enabled before delegating to emit_warning().
//
// ─── CRTP Dispatcher Inheritance ─────────────────────────────────────────
//
// Linter inherits from three CRTP dispatcher base classes defined in
// ast_dispatcher.hpp.  Each base provides:
//
//   ExpressionDispatcher<Linter, void>
//     — dispatch_expr(): routes an Expression to the appropriate visit_X
//       handler (visit_binary, visit_call, …).  Provides default stubs so
//       only the handlers the Linter cares about need to be overridden.
//
//   StatementDispatcher<Linter>
//     — dispatch_stmt(): routes a Statement to visit_assignment,
//       visit_for, visit_return, etc.  Same default-stub pattern.
//
//   DeclarationDispatcher<Linter>
//     — dispatch_decl(): routes a Declaration to visit_function,
//       visit_namespace, visit_include, etc.
//
// Why CRTP instead of composition:
//   CRTP gives the Linter zero-overhead static dispatch (no virtual calls)
//   and lets each visit_X method access Linter's private state directly
//   through the friend declarations below.  A composition approach would
//   require forwarding every handler through a wrapper object, adding
//   boilerplate without reducing coupling — the dispatcher templates are
//   header-only utilities with no independent state.
class Linter : public DiagnosticEmitter,
               public ExpressionDispatcher<Linter, void>,
               public StatementDispatcher<Linter>,
               public DeclarationDispatcher<Linter>,
               public ScopeManager<Linter> {
public:
    // Constructs a Linter that uses the given plugin registry.
    // Falls back to the global lint_plugin_registry() if none is provided.
    explicit Linter(const LintPluginRegistry& plugins = lint_plugin_registry())
        : DiagnosticEmitter(DiagnosticCategory::Warning, DiagnosticSource::Lint),
          plugins_(plugins) {}

    // Run all lint checks on the program.
    // Returns warnings (never errors — lint issues don't prevent execution).
    [[nodiscard]] std::vector<Diagnostic> lint(const Program& program);

private:
    // Allow CRTP base classes to access private handlers.
    friend class ExpressionDispatcher<Linter, void>;
    friend class StatementDispatcher<Linter>;
    friend class DeclarationDispatcher<Linter>;

    // ─── AST traversal dispatch ─────────────────────────────────────────
    //
    // These entry points delegate to the CRTP dispatchers, which route
    // each AST node to the appropriate visit_X method below.
    void lint_declaration(const Declaration& decl);
    void lint_statement(const Statement& stmt, bool is_tail_position = false);
    void lint_expression(const Expression& expr);
    // Check a sequence of statements for unreachable code after return/break/continue.
    // When last_is_value is true the final expression-statement is treated as the
    // block's result and W0010 (discarded-result) is suppressed for it.
    void lint_block(const std::vector<std::unique_ptr<Statement>>& stmts,
                    bool last_is_value = false);
    // Opens a fresh lexical scope (via make_scope_guard) and lints the block
    // within it.  Use for scoped blocks that register no scope-local variables;
    // sites that must register bindings first (function params, loop/catch/match
    // variables, lambda params) open the guard explicitly instead.
    void lint_scoped_block(const std::vector<std::unique_ptr<Statement>>& stmts,
                           bool last_is_value = false);
    // Lint the arms of a match expression or statement.
    void lint_match_arms(const std::vector<MatchArm>& arms, SourceLocation match_loc);

    // ─── Declaration handlers (via DeclarationDispatcher CRTP) ──────────
    void visit_function(const FunctionDeclaration& func);
    void visit_namespace(const NamespaceDeclaration& ns);
    void visit_include(const IncludeDeclaration& inc);

    // ─── Statement handlers (via StatementDispatcher CRTP) ──────────────
    //
    // Variable & assignment checks
    void visit_variable_declaration(const VariableDeclStatement& var);
    void visit_assignment(const AssignmentStatement& assign);
    void visit_compound_assignment(const CompoundAssignmentStatement& assign);
    void visit_tuple_destructuring(const TupleDestructuringStatement& td);
    void visit_record_destructuring(const RecordDestructuringStatement& rd);
    void visit_increment(const IncrementStatement& inc);
    void visit_decrement(const DecrementStatement& dec);
    //
    // Control flow & structural checks
    void visit_expression_statement(const ExpressionStatement& expr_stmt);
    void visit_return(const ReturnStatement& ret);
    void visit_for(const ForStatement& for_stmt);
    void visit_if_statement(const IfStatement& if_stmt);
    void visit_while(const WhileStatement& while_stmt);
    void visit_match_statement(const MatchStatement& match_stmt);
    void visit_try(const TryStatement& try_stmt);
    void visit_block(const BlockStatement& block);

    // ─── Expression handlers (via ExpressionDispatcher CRTP) ────────────
    //
    // Arithmetic & logic checks
    void visit_binary(const BinaryExpression& bin);
    void visit_unary(const UnaryExpression& unary);
    //
    // Call & pipe expressions
    void visit_call(const CallExpression& call);
    void visit_pipe(const PipeExpression& pipe);
    void visit_error_pipe(const ErrorPipeExpression& error_pipe);
    //
    // Control flow expressions
    void visit_if(const IfExpression& if_expr);
    void visit_match(const MatchExpression& match);
    void visit_lambda(const LambdaExpression& lambda);
    //
    // Data construction expressions
    void visit_record_creation(const RecordCreationExpression& record);
    void visit_record_with(const RecordWithExpression& record_with);
    void visit_array_literal(const ArrayLiteralExpression& arr);
    void visit_dictionary_literal(const DictionaryLiteralExpression& dict);
    void visit_tuple_literal(const TupleLiteralExpression& tuple);
    void visit_string_interpolation(const StringInterpolationExpression& interp);
    //
    // Type & wrapping expressions
    void visit_downcast(const DowncastExpression& downcast);
    void visit_is(const IsExpression& is);
    void visit_success(const SuccessExpression& success);
    void visit_failure(const FailureExpression& failure);
    void visit_some(const SomeExpression& some);
    //
    // Concurrency expressions
    void visit_spawn(const SpawnExpression& spawn);
    void visit_await(const AwaitExpression& await_expr);
    void visit_task_scope(const TaskScopeExpression& task_scope);
    //
    // Leaf expressions (traversal only)
    void visit_variable(const VariableExpression& var);
    void visit_range(const RangeExpression& range);
    void visit_field_access(const FieldAccessExpression& field);
    void visit_index_access(const IndexAccessExpression& idx);

    // ─── Assignment helpers ─────────────────────────────────────────────
    // Marks the root variable of an assignment target as mutated, walking
    // through field and index accesses (so 'p.x', 'a[i]', and 'p.arr[i]' all
    // mark their root binding). No-op when the target is not rooted in a
    // variable (e.g. 'get_point().x').
    void mark_target_mutated(const Expression& target);

    // ─── Diagnostics ────────────────────────────────────────────────────
    void warn(std::string_view message, SourceLocation loc, std::string_view hint = "",
              DiagnosticCode code = DiagnosticCode::None);

    // ─── Plugin registry ────────────────────────────────────────────────
    // Shared plugin dispatch: iterates enabled plugins and emits findings.
    template <typename Node, typename CheckFn> void run_plugins(const Node& node, CheckFn check_fn);
    // Runs all enabled plugins for a given expression/statement/declaration node.
    void run_expression_plugins(const Expression& expr);
    void run_statement_plugins(const Statement& stmt);
    void run_declaration_plugins(const Declaration& decl);

    // ─── Scope lifecycle ────────────────────────────────────────────────
    // make_scope_guard() is inherited from ScopeManager<Linter>, which calls
    // back into push_scope / pop_scope (hence the friend declaration).  Entry
    // pushes a fresh tracker scope; exit reports unused variables before popping
    // it — the asymmetry that keeps report-then-pop atomic under the RAII guard.
    friend class ScopeManager<Linter>;

    // Opens a fresh tracker scope (no usage reporting — that happens on pop).
    void push_scope();

    // Reports unused variables in the current scope, then pops it.
    void pop_scope();

    // ─── Usage reporting ────────────────────────────────────────────────
    // Emit warnings for unused variables/parameters in the current scope.
    void report_unused_in_current_scope();
    // Emit warnings for unused includes.
    void report_unused_includes();

    // ─── State ──────────────────────────────────────────────────────────
    LinterTracker tracker_;
    const LintRuleRegistry& registry_ = lint_rule_registry();
    const LintPluginRegistry& plugins_;

    // Recursion-depth counter for lint_expression().  Guards against native
    // stack overflow on pathologically deep expression ASTs.  See
    // ResourceLimits::max_expression_depth.
    int expression_depth_{0};
};

} // namespace luma

#endif // LUMA_LINTER_LINTER_HPP
