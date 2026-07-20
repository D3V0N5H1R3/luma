#ifndef LUMA_AST_DISPATCHER_HPP
#define LUMA_AST_DISPATCHER_HPP

// ─────────────────────────────────────────────────────────────────────────────
// CRTP-Based AST Dispatcher — Enables File Splitting for Large Visitors
// ─────────────────────────────────────────────────────────────────────────────
//
// @file  ast_dispatcher.hpp
// @brief CRTP dispatcher templates that route AST nodes to named handler
//        methods on a derived class.  Builds on top of ast_dispatch.hpp.
//
// Motivation:
//   The expression and statement type checkers are 2000+ line files because
//   every handler lives in the same translation unit.  This template allows
//   each handler method (e.g., visit_binary, visit_call) to be defined in
//   a separate .cpp file while keeping the dispatch routing centralised.
//
// Architecture:
//   - ExpressionDispatcher<Derived, Result>
//       Derived must implement visit_X(const XExpression&) -> Result
//       for each expression kind it cares about.  Unimplemented handlers
//       fall through to Derived::visit_expression_unhandled(const Expression&).
//
//   - StatementDispatcher<Derived>
//       Derived must implement visit_X(const XStatement&) -> void
//       for each statement kind.  Unimplemented handlers fall through to
//       Derived::visit_statement_unhandled(const Statement&).
//
//   - DeclarationDispatcher<Derived>
//       Same pattern for declarations.
//
// Usage (expression checker split across files):
//
//   class MyChecker : public ExpressionDispatcher<MyChecker, TypeInfo> { ... };
//   TypeInfo result = checker.dispatch_expr(some_expression);
//
//   // expression_type_checker.hpp
//   class ExpressionTypeChecker
//       : public ExpressionDispatcher<ExpressionTypeChecker, TypeInfo> {
//   public:
//       TypeInfo visit_literal(const LiteralExpression&);
//       TypeInfo visit_binary(const BinaryExpression&);
//       TypeInfo visit_call(const CallExpression&);
//       // ... one per expression kind
//       TypeInfo visit_expression_unhandled(const Expression&);
//   };
//
//   // check_binary_expr.cpp
//   TypeInfo ExpressionTypeChecker::visit_binary(const BinaryExpression& e) {
//       // ... implementation
//   }
//
//   // check_call_expr.cpp
//   TypeInfo ExpressionTypeChecker::visit_call(const CallExpression& e) {
//       // ... implementation
//   }
//
// How it works:
//   dispatch_expr() / dispatch_stmt() / dispatch_decl() delegate to the
//   dispatch_expression() / dispatch_statement() / dispatch_declaration() free
//   functions in ast_dispatch.hpp, which switch on the node's kind enum and
//   static_cast to the concrete type.  The supplied handler then routes each
//   concrete type to the named visit_X method on the derived class through an
//   if-constexpr chain.  Exhaustiveness is enforced at compile time with zero
//   runtime overhead: the free function's switch has no default case (so
//   -Wswitch fires when a new kind is added), and the if-constexpr chain ends
//   in a static_assert(always_false_v<...>) that fails to compile until the new
//   type is handled here as well.
//
// Current users:
//   - ExpressionTypeChecker  (expression_type_checker.hpp)
//       ExpressionDispatcher<ExpressionTypeChecker, TypeInfo>
//   - StatementTypeChecker   (statement_type_checker.hpp)
//       StatementDispatcher<StatementTypeChecker>
//   - Linter                 (linter.hpp)
//       ExpressionDispatcher<Linter, void>,
//       StatementDispatcher<Linter>,
//       DeclarationDispatcher<Linter>
//
// ─── TreeWalker Assessment ──────────────────────────────────────────────
//
// A generic TreeWalker<Derived> base (providing automatic child traversal
// with pre/post-visit hooks) was considered but is NOT warranted because:
//
//   1. The existing dispatchers already eliminate the boilerplate of
//      kind-switching.  Each derived class only overrides the visit_X
//      methods it cares about; unhandled kinds fall through to default
//      stubs that do nothing (or return a default value).
//
//   2. Each pass requires fundamentally different traversal logic:
//      - The type checker infers types bottom-up (children first, then
//        combine), threads scope/ownership state, and returns TypeInfo.
//      - The linter walks top-down, emitting warnings inline and tracking
//        variable usage via LinterTracker — it needs control over when
//        to recurse into children (e.g., suppressing warnings in tail
//        position of value blocks).
//      - The resolver walks the AST to assign stack-slot indices,
//        creating shared_ptr-linked scope chains that are unrelated to
//        the traversal order.
//      A generic child-traversal method would either be too rigid (forcing
//      a fixed pre/post order) or so configurable that it recreates the
//      complexity it was meant to eliminate.
//
//   3. Adding another CRTP layer on top of the dispatchers would increase
//      template instantiation depth and compile times without reducing
//      the amount of code each pass must write — the per-node logic is
//      irreducible.
//
// If a future pass needs a simple "walk everything and collect data"
// pattern, a standalone utility function that recursively dispatches
// without CRTP inheritance would be a lighter-weight solution than a
// TreeWalker base class.
//
// ─────────────────────────────────────────────────────────────────────────────

#include <type_traits>

#include "analysis/ast/ast_dispatch.hpp"
#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════════════════════════
// ExpressionDispatcher — CRTP base for expression visitors
// ═══════════════════════════════════════════════════════════════════════════════
//
// Template parameters:
//   Derived — The concrete visitor class (CRTP pattern, must be a class type)
//   Result  — Return type of each handler (e.g., TypeInfo, void)

template <typename Derived, typename Result>
    requires std::is_class_v<Derived>
class ExpressionDispatcher {
public:
    /// Dispatch an expression node to the appropriate visit_X method.
    /// Delegates to dispatch_expression() from ast_dispatch.hpp to avoid
    /// duplicating the kind→type switch.
    Result dispatch_expr(const Expression& expr) {
        auto& self = static_cast<Derived&>(*this);

        return dispatch_expression(expr, [&self](const auto& e) -> Result {
            using E = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<E, ArrayLiteralExpression>) {
                return self.visit_array_literal(e);
            } else if constexpr (std::is_same_v<E, AwaitExpression>) {
                return self.visit_await(e);
            } else if constexpr (std::is_same_v<E, BinaryExpression>) {
                return self.visit_binary(e);
            } else if constexpr (std::is_same_v<E, CallExpression>) {
                return self.visit_call(e);
            } else if constexpr (std::is_same_v<E, DictionaryLiteralExpression>) {
                return self.visit_dictionary_literal(e);
            } else if constexpr (std::is_same_v<E, DowncastExpression>) {
                return self.visit_downcast(e);
            } else if constexpr (std::is_same_v<E, ErrorPipeExpression>) {
                return self.visit_error_pipe(e);
            } else if constexpr (std::is_same_v<E, FailureExpression>) {
                return self.visit_failure(e);
            } else if constexpr (std::is_same_v<E, FieldAccessExpression>) {
                return self.visit_field_access(e);
            } else if constexpr (std::is_same_v<E, IfExpression>) {
                return self.visit_if(e);
            } else if constexpr (std::is_same_v<E, IndexAccessExpression>) {
                return self.visit_index_access(e);
            } else if constexpr (std::is_same_v<E, IsExpression>) {
                return self.visit_is(e);
            } else if constexpr (std::is_same_v<E, LambdaExpression>) {
                return self.visit_lambda(e);
            } else if constexpr (std::is_same_v<E, LiteralExpression>) {
                return self.visit_literal(e);
            } else if constexpr (std::is_same_v<E, MatchExpression>) {
                return self.visit_match(e);
            } else if constexpr (std::is_same_v<E, PipeExpression>) {
                return self.visit_pipe(e);
            } else if constexpr (std::is_same_v<E, RangeExpression>) {
                return self.visit_range(e);
            } else if constexpr (std::is_same_v<E, RecordCreationExpression>) {
                return self.visit_record_creation(e);
            } else if constexpr (std::is_same_v<E, RecordWithExpression>) {
                return self.visit_record_with(e);
            } else if constexpr (std::is_same_v<E, SomeExpression>) {
                return self.visit_some(e);
            } else if constexpr (std::is_same_v<E, SpawnExpression>) {
                return self.visit_spawn(e);
            } else if constexpr (std::is_same_v<E, StringInterpolationExpression>) {
                return self.visit_string_interpolation(e);
            } else if constexpr (std::is_same_v<E, SuccessExpression>) {
                return self.visit_success(e);
            } else if constexpr (std::is_same_v<E, TaskScopeExpression>) {
                return self.visit_task_scope(e);
            } else if constexpr (std::is_same_v<E, TupleLiteralExpression>) {
                return self.visit_tuple_literal(e);
            } else if constexpr (std::is_same_v<E, UnaryExpression>) {
                return self.visit_unary(e);
            } else if constexpr (std::is_same_v<E, VariableExpression>) {
                return self.visit_variable(e);
            } else {
                static_assert(always_false_v<E>, "unhandled expression type");
            }
        });
    }

    /// Default handler for unimplemented expression kinds.
    /// Override in derived class to provide custom fallback behaviour.
    /// The default implementation returns a value-initialised Result (or
    /// does nothing when Result is void).
    Result visit_expression_unhandled([[maybe_unused]] const Expression& expr) {
        if constexpr (!std::is_void_v<Result>) {
            return Result{};
        }
    }

    // ─── Default handler stubs ───
    // Each calls visit_expression_unhandled so the derived class only needs
    // to override the handlers it actually implements.

    Result visit_array_literal(const ArrayLiteralExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_await(const AwaitExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_binary(const BinaryExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_call(const CallExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_dictionary_literal(const DictionaryLiteralExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_downcast(const DowncastExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_error_pipe(const ErrorPipeExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_failure(const FailureExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_field_access(const FieldAccessExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_if(const IfExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_index_access(const IndexAccessExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_is(const IsExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_lambda(const LambdaExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_literal(const LiteralExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_match(const MatchExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_pipe(const PipeExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_range(const RangeExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_record_creation(const RecordCreationExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_record_with(const RecordWithExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_some(const SomeExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_spawn(const SpawnExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_string_interpolation(const StringInterpolationExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_success(const SuccessExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_task_scope(const TaskScopeExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_tuple_literal(const TupleLiteralExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_unary(const UnaryExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

    Result visit_variable(const VariableExpression& e) {
        return static_cast<Derived*>(this)->visit_expression_unhandled(e);
    }

protected:
    ~ExpressionDispatcher() = default;
};

// ═══════════════════════════════════════════════════════════════════════════════
// StatementDispatcher — CRTP base for statement visitors
// ═══════════════════════════════════════════════════════════════════════════════
//
// Template parameters:
//   Derived — The concrete visitor class (CRTP pattern, must be a class type)
//
// Statement handlers return void: a statement is visited for its side effects
// (emitting diagnostics, updating traversal state), not to produce a value.  A
// pass that must compute a per-statement result should accumulate it in the
// derived visitor's own member state.

template <typename Derived>
    requires std::is_class_v<Derived>
class StatementDispatcher {
public:
    /// Dispatch a statement node to the appropriate visit_X method.
    /// Delegates to dispatch_statement() from ast_dispatch.hpp to avoid
    /// duplicating the kind→type switch.
    void dispatch_stmt(const Statement& stmt) {
        auto& self = static_cast<Derived&>(*this);

        dispatch_statement(stmt, [&self](const auto& s) {
            using S = std::decay_t<decltype(s)>;

            if constexpr (std::is_same_v<S, AssignmentStatement>) {
                self.visit_assignment(s);
            } else if constexpr (std::is_same_v<S, BlockStatement>) {
                self.visit_block(s);
            } else if constexpr (std::is_same_v<S, BreakStatement>) {
                self.visit_break(s);
            } else if constexpr (std::is_same_v<S, ContinueStatement>) {
                self.visit_continue(s);
            } else if constexpr (std::is_same_v<S, CompoundAssignmentStatement>) {
                self.visit_compound_assignment(s);
            } else if constexpr (std::is_same_v<S, DecrementStatement>) {
                self.visit_decrement(s);
            } else if constexpr (std::is_same_v<S, ExpressionStatement>) {
                self.visit_expression_statement(s);
            } else if constexpr (std::is_same_v<S, ForStatement>) {
                self.visit_for(s);
            } else if constexpr (std::is_same_v<S, IfStatement>) {
                self.visit_if_statement(s);
            } else if constexpr (std::is_same_v<S, IncrementStatement>) {
                self.visit_increment(s);
            } else if constexpr (std::is_same_v<S, MatchStatement>) {
                self.visit_match_statement(s);
            } else if constexpr (std::is_same_v<S, ReturnStatement>) {
                self.visit_return(s);
            } else if constexpr (std::is_same_v<S, TryStatement>) {
                self.visit_try(s);
            } else if constexpr (std::is_same_v<S, TupleDestructuringStatement>) {
                self.visit_tuple_destructuring(s);
            } else if constexpr (std::is_same_v<S, VariableDeclStatement>) {
                self.visit_variable_declaration(s);
            } else if constexpr (std::is_same_v<S, WhileStatement>) {
                self.visit_while(s);
            } else {
                static_assert(always_false_v<S>, "unhandled statement type");
            }
        });
    }

    /// Default handler for unimplemented statement kinds.
    void visit_statement_unhandled([[maybe_unused]] const Statement& stmt) {}

    // ─── Default handler stubs ───

    void visit_assignment(const AssignmentStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_block(const BlockStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_break(const BreakStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_continue(const ContinueStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_compound_assignment(const CompoundAssignmentStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_decrement(const DecrementStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_expression_statement(const ExpressionStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_for(const ForStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_if_statement(const IfStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_increment(const IncrementStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_match_statement(const MatchStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_return(const ReturnStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_try(const TryStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_tuple_destructuring(const TupleDestructuringStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_variable_declaration(const VariableDeclStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

    void visit_while(const WhileStatement& s) {
        static_cast<Derived*>(this)->visit_statement_unhandled(s);
    }

protected:
    ~StatementDispatcher() = default;
};

// ═══════════════════════════════════════════════════════════════════════════════
// DeclarationDispatcher — CRTP base for declaration visitors
// ═══════════════════════════════════════════════════════════════════════════════

template <typename Derived>
    requires std::is_class_v<Derived>
class DeclarationDispatcher {
public:
    /// Dispatch a declaration node to the appropriate visit_X method.
    /// Delegates to dispatch_declaration() from ast_dispatch.hpp to avoid
    /// duplicating the kind→type switch.
    void dispatch_decl(const Declaration& decl) {
        auto& self = static_cast<Derived&>(*this);

        dispatch_declaration(decl, [&self](const auto& d) {
            using D = std::decay_t<decltype(d)>;

            if constexpr (std::is_same_v<D, ChoiceDeclaration>) {
                self.visit_choice(d);
            } else if constexpr (std::is_same_v<D, FunctionDeclaration>) {
                self.visit_function(d);
            } else if constexpr (std::is_same_v<D, IncludeDeclaration>) {
                self.visit_include(d);
            } else if constexpr (std::is_same_v<D, InterfaceDeclaration>) {
                self.visit_interface(d);
            } else if constexpr (std::is_same_v<D, NamespaceDeclaration>) {
                self.visit_namespace(d);
            } else if constexpr (std::is_same_v<D, RecordDeclaration>) {
                self.visit_record(d);
            } else if constexpr (std::is_same_v<D, TypeAliasDeclaration>) {
                self.visit_type_alias(d);
            } else if constexpr (std::is_same_v<D, UseDeclaration>) {
                self.visit_use(d);
            } else {
                static_assert(always_false_v<D>, "unhandled declaration type");
            }
        });
    }

    /// Default handler for unimplemented declaration kinds.
    void visit_declaration_unhandled([[maybe_unused]] const Declaration& decl) {}

    // ─── Default handler stubs ───

    void visit_choice(const ChoiceDeclaration& d) {
        static_cast<Derived*>(this)->visit_declaration_unhandled(d);
    }

    void visit_function(const FunctionDeclaration& d) {
        static_cast<Derived*>(this)->visit_declaration_unhandled(d);
    }

    void visit_include(const IncludeDeclaration& d) {
        static_cast<Derived*>(this)->visit_declaration_unhandled(d);
    }

    void visit_interface(const InterfaceDeclaration& d) {
        static_cast<Derived*>(this)->visit_declaration_unhandled(d);
    }

    void visit_namespace(const NamespaceDeclaration& d) {
        static_cast<Derived*>(this)->visit_declaration_unhandled(d);
    }

    void visit_record(const RecordDeclaration& d) {
        static_cast<Derived*>(this)->visit_declaration_unhandled(d);
    }

    void visit_type_alias(const TypeAliasDeclaration& d) {
        static_cast<Derived*>(this)->visit_declaration_unhandled(d);
    }

    void visit_use(const UseDeclaration& d) {
        static_cast<Derived*>(this)->visit_declaration_unhandled(d);
    }

protected:
    ~DeclarationDispatcher() = default;
};

} // namespace luma

#endif // LUMA_AST_DISPATCHER_HPP
