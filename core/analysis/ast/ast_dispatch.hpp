#ifndef LUMA_AST_DISPATCH_HPP
#define LUMA_AST_DISPATCH_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Centralised AST Dispatch — Canonical Visitor Mechanism
// ─────────────────────────────────────────────────────────────────────────────
//
// @file  ast_dispatch.hpp
// @brief Canonical AST dispatch mechanism for all analysis and compilation
//        passes.  Provides dispatch_expression(), dispatch_statement(), and
//        dispatch_declaration() free-function templates.
//
// All analysis and compilation passes should route AST traversal through
// these functions rather than reimplementing visitor patterns or
// kind-switches inline.  Callers consume them either directly (e.g. the
// name resolver, the type checker's declaration pass, and the language
// server's symbol collector) or indirectly through the CRTP visitor bases
// in ast_dispatcher.hpp (ExpressionDispatcher / StatementDispatcher /
// DeclarationDispatcher), on which the linter, type checker, and compiler
// build.
//
// Usage:
//   dispatch_expression(expr, [this](const auto& e) { return visit(e); });
//   dispatch_statement(stmt, [this](const auto& s) { visit(s); });
//   dispatch_declaration(decl, [this](const auto& d) { visit(d); });
//
// How it works:
//   Each function switches on the node's `kind` enum, static_casts to the
//   concrete derived type, and forwards to the caller-supplied handler.
//   The kind→type mapping is written once here; adding a new AST node kind
//   requires updating only one switch (and the corresponding concept).
//
// Return type:
//   Deduced from the handler via decltype(auto), so both void passes
//   (linter, compiler) and value-returning passes (type checker) work.
//
// Handler requirements:
//   - Callable with (const ConcreteNode&) for every node kind.
//   - All overloads must return the same type.
//
// ─── Adding a New AST Node Kind ─────────────────────────────────────────
//
// The AST node set is finite and changes infrequently.  When you do add a
// new node kind, the following files must be updated (in order):
//
//   1. expression.hpp / statement.hpp / declaration.hpp
//      — Add the enum value to ExpressionKind / StatementKind /
//        DeclarationKind and define the new derived struct.
//
//   2. THIS FILE (ast_dispatch.hpp)
//      — Add a case to the relevant dispatch_expression / dispatch_statement
//        / dispatch_declaration switch.  Also add the new type to the
//        corresponding ExpressionHandler / StatementHandler /
//        DeclarationHandler concept so callers get a compile error if they
//        forget to handle it.
//
//   3. ast_dispatcher.hpp
//      — Add an `else if constexpr` branch to the CRTP dispatcher's
//        dispatch_expr / dispatch_stmt / dispatch_decl chain (the
//        always_false_v static_assert fails to compile until you do), and
//        add a matching default handler stub (visit_X) in the CRTP base
//        class.
//
//   4. Consumers (linter.cpp, compiler.cpp, type checker, etc.)
//      — Implement the new visit_X handler.  If the CRTP base provides a
//        default stub that calls visit_expression_unhandled / etc., passes
//        that don't care about the new node will compile without changes.
//
// The switch-based design was chosen over a vtable or registration table
// because it is fully resolved at compile time (zero dispatch overhead),
// the compiler warns about unhandled enum values (with -Wswitch), and the
// node set is stable enough that the maintenance cost is low.
// ─────────────────────────────────────────────────────────────────────────────

#include <concepts>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "common/unreachable.hpp"

namespace luma {

// ─────────────────────── Exhaustiveness helper ───────────────────────
//
// Use in the final else-branch of an if-constexpr dispatch chain to turn
// an unhandled AST node type into a hard compile error:
//
//   } else {
//       static_assert(always_false_v<T>, "unhandled expression type");
//   }
//
// The type-dependency on T is required: a non-dependent static_assert(false)
// would be ill-formed even in discarded branches (C++20 §13.8.2).

template <typename> inline constexpr bool always_false_v = false;

// ─────────────────────── Concepts ───────────────────────
// Constrain handler types to catch missing overloads at the call site
// rather than deep inside the switch body.

// clang-format off

/// Helper concept: true when H is callable with (const NodeType&) for every
/// type in NodeTypes.  Used to build the per-category handler concepts below
/// without repeating the requires-clause boilerplate for each node type.
template <typename H, typename... NodeTypes>
concept HandlesAll = (requires(H&& h, const NodeTypes& node) {
    h(node);
} && ...);

/// A handler that can be called with every concrete expression node type.
template <typename H>
concept ExpressionHandler = HandlesAll<H,
    ArrayLiteralExpression, AwaitExpression, BinaryExpression,
    CallExpression, DictionaryLiteralExpression, DowncastExpression,
    ErrorPipeExpression, FailureExpression, FieldAccessExpression,
    IfExpression, IndexAccessExpression, IsExpression,
    LambdaExpression, LiteralExpression, MatchExpression,
    PipeExpression, RangeExpression, RecordCreationExpression,
    RecordWithExpression, SomeExpression, SpawnExpression,
    StringInterpolationExpression, SuccessExpression, TaskScopeExpression,
    TupleLiteralExpression, UnaryExpression, VariableExpression>;

/// A handler for statement dispatch — must accept every concrete statement type.
template <typename H>
concept StatementHandler = HandlesAll<H,
    AssignmentStatement, BlockStatement, BreakStatement,
    CompoundAssignmentStatement, ContinueStatement, DecrementStatement,
    ExpressionStatement, ForStatement, IfStatement, IncrementStatement,
    MatchStatement, RecordDestructuringStatement, ReturnStatement, TryStatement,
    TupleDestructuringStatement, VariableDeclStatement, WhileStatement>;

/// A handler for declaration dispatch — must accept every concrete
/// declaration type.
template <typename H>
concept DeclarationHandler = HandlesAll<H,
    ChoiceDeclaration, FunctionDeclaration, IncludeDeclaration,
    InterfaceDeclaration, NamespaceDeclaration, RecordDeclaration,
    TypeAliasDeclaration, UseDeclaration>;

// clang-format on

// ─────────────────────── Visitor Concepts ───────────────────────
//
// Higher-level concepts that constrain a visitor type V to be usable
// with the dispatch functions.  Unlike the Handler concepts above
// (which constrain the callable object passed to dispatch_*), these
// constrain a full visitor class whose operator() or visit method
// covers every AST node kind.
//
// Usage:
//   template <ExpressionVisitor V>
//   void analyse(const Expression& expr, V& visitor) {
//       dispatch_expression(expr, visitor);
//   }

/// Concept for a visitor whose operator() handles all expression types.
template <typename V>
concept ExpressionVisitor = ExpressionHandler<V>;

/// Concept for a visitor whose operator() handles all statement types.
template <typename V>
concept StatementVisitor = StatementHandler<V>;

/// Concept for a visitor whose operator() handles all declaration types.
template <typename V>
concept DeclarationVisitor = DeclarationHandler<V>;

// ─────────────────────── Expression Dispatch ───────────────────────

template <ExpressionHandler Handler>
decltype(auto) dispatch_expression(const Expression& expr, Handler&& handler) {
    switch (expr.kind) {
        case ExpressionKind::ArrayLiteral:
            return handler(static_cast<const ArrayLiteralExpression&>(expr));
        case ExpressionKind::Await:
            return handler(static_cast<const AwaitExpression&>(expr));
        case ExpressionKind::Binary:
            return handler(static_cast<const BinaryExpression&>(expr));
        case ExpressionKind::Call:
            return handler(static_cast<const CallExpression&>(expr));
        case ExpressionKind::DictionaryLiteral:
            return handler(static_cast<const DictionaryLiteralExpression&>(expr));
        case ExpressionKind::Downcast:
            return handler(static_cast<const DowncastExpression&>(expr));
        case ExpressionKind::ErrorPipe:
            return handler(static_cast<const ErrorPipeExpression&>(expr));
        case ExpressionKind::Failure:
            return handler(static_cast<const FailureExpression&>(expr));
        case ExpressionKind::FieldAccess:
            return handler(static_cast<const FieldAccessExpression&>(expr));
        case ExpressionKind::If:
            return handler(static_cast<const IfExpression&>(expr));
        case ExpressionKind::IndexAccess:
            return handler(static_cast<const IndexAccessExpression&>(expr));
        case ExpressionKind::Is:
            return handler(static_cast<const IsExpression&>(expr));
        case ExpressionKind::Lambda:
            return handler(static_cast<const LambdaExpression&>(expr));
        case ExpressionKind::Literal:
            return handler(static_cast<const LiteralExpression&>(expr));
        case ExpressionKind::Match:
            return handler(static_cast<const MatchExpression&>(expr));
        case ExpressionKind::Pipe:
            return handler(static_cast<const PipeExpression&>(expr));
        case ExpressionKind::Range:
            return handler(static_cast<const RangeExpression&>(expr));
        case ExpressionKind::RecordCreation:
            return handler(static_cast<const RecordCreationExpression&>(expr));
        case ExpressionKind::RecordWith:
            return handler(static_cast<const RecordWithExpression&>(expr));
        case ExpressionKind::Some:
            return handler(static_cast<const SomeExpression&>(expr));
        case ExpressionKind::Spawn:
            return handler(static_cast<const SpawnExpression&>(expr));
        case ExpressionKind::StringInterpolation:
            return handler(static_cast<const StringInterpolationExpression&>(expr));
        case ExpressionKind::Success:
            return handler(static_cast<const SuccessExpression&>(expr));
        case ExpressionKind::TaskScope:
            return handler(static_cast<const TaskScopeExpression&>(expr));
        case ExpressionKind::TupleLiteral:
            return handler(static_cast<const TupleLiteralExpression&>(expr));
        case ExpressionKind::Unary:
            return handler(static_cast<const UnaryExpression&>(expr));
        case ExpressionKind::Variable:
            return handler(static_cast<const VariableExpression&>(expr));
    }

    LUMA_UNREACHABLE();
}

// ─────────────────────── Statement Dispatch ───────────────────────

template <StatementHandler Handler>
decltype(auto) dispatch_statement(const Statement& stmt, Handler&& handler) {
    switch (stmt.kind) {
        case StatementKind::Assignment:
            return handler(static_cast<const AssignmentStatement&>(stmt));
        case StatementKind::Block:
            return handler(static_cast<const BlockStatement&>(stmt));
        case StatementKind::Break:
            return handler(static_cast<const BreakStatement&>(stmt));
        case StatementKind::CompoundAssignment:
            return handler(static_cast<const CompoundAssignmentStatement&>(stmt));
        case StatementKind::Continue:
            return handler(static_cast<const ContinueStatement&>(stmt));
        case StatementKind::Decrement:
            return handler(static_cast<const DecrementStatement&>(stmt));
        case StatementKind::Expression:
            return handler(static_cast<const ExpressionStatement&>(stmt));
        case StatementKind::For:
            return handler(static_cast<const ForStatement&>(stmt));
        case StatementKind::If:
            return handler(static_cast<const IfStatement&>(stmt));
        case StatementKind::Increment:
            return handler(static_cast<const IncrementStatement&>(stmt));
        case StatementKind::Match:
            return handler(static_cast<const MatchStatement&>(stmt));
        case StatementKind::RecordDestructuring:
            return handler(static_cast<const RecordDestructuringStatement&>(stmt));
        case StatementKind::Return:
            return handler(static_cast<const ReturnStatement&>(stmt));
        case StatementKind::Try:
            return handler(static_cast<const TryStatement&>(stmt));
        case StatementKind::TupleDestructuring:
            return handler(static_cast<const TupleDestructuringStatement&>(stmt));
        case StatementKind::VariableDeclaration:
            return handler(static_cast<const VariableDeclStatement&>(stmt));
        case StatementKind::While:
            return handler(static_cast<const WhileStatement&>(stmt));
    }

    LUMA_UNREACHABLE();
}

// ─────────────────────── Declaration Dispatch ───────────────────────

template <DeclarationHandler Handler>
decltype(auto) dispatch_declaration(const Declaration& decl, Handler&& handler) {
    switch (decl.kind) {
        case DeclarationKind::Choice:
            return handler(static_cast<const ChoiceDeclaration&>(decl));
        case DeclarationKind::Function:
            return handler(static_cast<const FunctionDeclaration&>(decl));
        case DeclarationKind::Include:
            return handler(static_cast<const IncludeDeclaration&>(decl));
        case DeclarationKind::Interface:
            return handler(static_cast<const InterfaceDeclaration&>(decl));
        case DeclarationKind::Namespace:
            return handler(static_cast<const NamespaceDeclaration&>(decl));
        case DeclarationKind::Record:
            return handler(static_cast<const RecordDeclaration&>(decl));
        case DeclarationKind::TypeAlias:
            return handler(static_cast<const TypeAliasDeclaration&>(decl));
        case DeclarationKind::Use:
            return handler(static_cast<const UseDeclaration&>(decl));
    }

    LUMA_UNREACHABLE();
}

} // namespace luma

#endif // LUMA_AST_DISPATCH_HPP
